#include <Processors/Merges/SummingSortedAlgorithm.h>
#include <Common/FieldVisitors.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/NestedUtils.h>
#include <Common/StringUtils/StringUtils.h>
#include <Columns/ColumnTuple.h>
#include <IO/WriteHelpers.h>
#include <Columns/ColumnAggregateFunction.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
    extern const int CORRUPTED_DATA;
}

static bool isInPrimaryKey(const SortDescription & description, const std::string & name, const size_t number)
{
    for (auto & desc : description)
        if (desc.column_name == name || (desc.column_name.empty() && desc.column_number == number))
            return true;

    return false;
}

/// Returns true if merge result is not empty
static bool mergeMap(const SummingSortedAlgorithm::MapDescription & desc, Row & row, SortCursor & cursor)
{
    /// Strongly non-optimal.

    Row & left = row;
    Row right(left.size());

    for (size_t col_num : desc.key_col_nums)
        right[col_num] = (*cursor->all_columns[col_num])[cursor->pos].template get<Array>();

    for (size_t col_num : desc.val_col_nums)
        right[col_num] = (*cursor->all_columns[col_num])[cursor->pos].template get<Array>();

    auto at_ith_column_jth_row = [&](const Row & matrix, size_t i, size_t j) -> const Field &
    {
        return matrix[i].get<Array>()[j];
    };

    auto tuple_of_nth_columns_at_jth_row = [&](const Row & matrix, const ColumnNumbers & col_nums, size_t j) -> Array
    {
        size_t size = col_nums.size();
        Array res(size);
        for (size_t col_num_index = 0; col_num_index < size; ++col_num_index)
            res[col_num_index] = at_ith_column_jth_row(matrix, col_nums[col_num_index], j);
        return res;
    };

    std::map<Array, Array> merged;

    auto accumulate = [](Array & dst, const Array & src)
    {
        bool has_non_zero = false;
        size_t size = dst.size();
        for (size_t i = 0; i < size; ++i)
            if (applyVisitor(FieldVisitorSum(src[i]), dst[i]))
                has_non_zero = true;
        return has_non_zero;
    };

    auto merge = [&](const Row & matrix)
    {
        size_t rows = matrix[desc.key_col_nums[0]].get<Array>().size();

        for (size_t j = 0; j < rows; ++j)
        {
            Array key = tuple_of_nth_columns_at_jth_row(matrix, desc.key_col_nums, j);
            Array value = tuple_of_nth_columns_at_jth_row(matrix, desc.val_col_nums, j);

            auto it = merged.find(key);
            if (merged.end() == it)
                merged.emplace(std::move(key), std::move(value));
            else
            {
                if (!accumulate(it->second, value))
                    merged.erase(it);
            }
        }
    };

    merge(left);
    merge(right);

    for (size_t col_num : desc.key_col_nums)
        row[col_num] = Array(merged.size());
    for (size_t col_num : desc.val_col_nums)
        row[col_num] = Array(merged.size());

    size_t row_num = 0;
    for (const auto & key_value : merged)
    {
        for (size_t col_num_index = 0, size = desc.key_col_nums.size(); col_num_index < size; ++col_num_index)
            row[desc.key_col_nums[col_num_index]].get<Array>()[row_num] = key_value.first[col_num_index];

        for (size_t col_num_index = 0, size = desc.val_col_nums.size(); col_num_index < size; ++col_num_index)
            row[desc.val_col_nums[col_num_index]].get<Array>()[row_num] = key_value.second[col_num_index];

        ++row_num;
    }

    return row_num != 0;
}

static SummingSortedAlgorithm::ColumnsDefinition defineColumns(
    const Block & header,
    const SortDescription & description,
    const Names & column_names_to_sum)
{
    size_t num_columns = header.columns();
    SummingSortedAlgorithm::ColumnsDefinition def;

    /// name of nested structure -> the column numbers that refer to it.
    std::unordered_map<std::string, std::vector<size_t>> discovered_maps;

    /** Fill in the column numbers, which must be summed.
        * This can only be numeric columns that are not part of the sort key.
        * If a non-empty column_names_to_sum is specified, then we only take these columns.
        * Some columns from column_names_to_sum may not be found. This is ignored.
        */
    for (size_t i = 0; i < num_columns; ++i)
    {
        const ColumnWithTypeAndName & column = header.safeGetByPosition(i);

        /// Discover nested Maps and find columns for summation
        if (typeid_cast<const DataTypeArray *>(column.type.get()))
        {
            const auto map_name = Nested::extractTableName(column.name);
            /// if nested table name ends with `Map` it is a possible candidate for special handling
            if (map_name == column.name || !endsWith(map_name, "Map"))
            {
                def.column_numbers_not_to_aggregate.push_back(i);
                continue;
            }

            discovered_maps[map_name].emplace_back(i);
        }
        else
        {
            bool is_agg_func = WhichDataType(column.type).isAggregateFunction();

            /// There are special const columns for example after prewhere sections.
            if ((!column.type->isSummable() && !is_agg_func) || isColumnConst(*column.column))
            {
                def.column_numbers_not_to_aggregate.push_back(i);
                continue;
            }

            /// Are they inside the PK?
            if (isInPrimaryKey(description, column.name, i))
            {
                def.column_numbers_not_to_aggregate.push_back(i);
                continue;
            }

            if (column_names_to_sum.empty()
                || column_names_to_sum.end() !=
                   std::find(column_names_to_sum.begin(), column_names_to_sum.end(), column.name))
            {
                // Create aggregator to sum this column
                SummingSortedAlgorithm::AggregateDescription desc;
                desc.is_agg_func_type = is_agg_func;
                desc.column_numbers = {i};

                if (!is_agg_func)
                {
                    desc.init("sumWithOverflow", {column.type});
                }

                def.columns_to_aggregate.emplace_back(std::move(desc));
            }
            else
            {
                // Column is not going to be summed, use last value
                def.column_numbers_not_to_aggregate.push_back(i);
            }
        }
    }

    /// select actual nested Maps from list of candidates
    for (const auto & map : discovered_maps)
    {
        /// map should contain at least two elements (key -> value)
        if (map.second.size() < 2)
        {
            for (auto col : map.second)
                def.column_numbers_not_to_aggregate.push_back(col);
            continue;
        }

        /// no elements of map could be in primary key
        auto column_num_it = map.second.begin();
        for (; column_num_it != map.second.end(); ++column_num_it)
            if (isInPrimaryKey(description, header.safeGetByPosition(*column_num_it).name, *column_num_it))
                break;
        if (column_num_it != map.second.end())
        {
            for (auto col : map.second)
                def.column_numbers_not_to_aggregate.push_back(col);
            continue;
        }

        DataTypes argument_types;
        SummingSortedAlgorithm::AggregateDescription desc;
        SummingSortedAlgorithm::MapDescription map_desc;

        column_num_it = map.second.begin();
        for (; column_num_it != map.second.end(); ++column_num_it)
        {
            const ColumnWithTypeAndName & key_col = header.safeGetByPosition(*column_num_it);
            const String & name = key_col.name;
            const IDataType & nested_type = *assert_cast<const DataTypeArray &>(*key_col.type).getNestedType();

            if (column_num_it == map.second.begin()
                || endsWith(name, "ID")
                || endsWith(name, "Key")
                || endsWith(name, "Type"))
            {
                if (!nested_type.isValueRepresentedByInteger() && !isStringOrFixedString(nested_type))
                    break;

                map_desc.key_col_nums.push_back(*column_num_it);
            }
            else
            {
                if (!nested_type.isSummable())
                    break;

                map_desc.val_col_nums.push_back(*column_num_it);
            }

            // Add column to function arguments
            desc.column_numbers.push_back(*column_num_it);
            argument_types.push_back(key_col.type);
        }

        if (column_num_it != map.second.end())
        {
            for (auto col : map.second)
                def.column_numbers_not_to_aggregate.push_back(col);
            continue;
        }

        if (map_desc.key_col_nums.size() == 1)
        {
            // Create summation for all value columns in the map
            desc.init("sumMapWithOverflow", argument_types);
            def.columns_to_aggregate.emplace_back(std::move(desc));
        }
        else
        {
            // Fall back to legacy mergeMaps for composite keys
            for (auto col : map.second)
                def.column_numbers_not_to_aggregate.push_back(col);
            def.maps_to_sum.emplace_back(std::move(map_desc));
        }
    }

    return def;
}

static MutableColumns getMergedDataColumns(
    const Block & header,
    const SummingSortedAlgorithm::ColumnsDefinition & columns_definition)
{
    MutableColumns columns;
    columns.reserve(columns_definition.getNumColumns());

    for (auto & desc : columns_definition.columns_to_aggregate)
    {
        // Wrap aggregated columns in a tuple to match function signature
        if (!desc.is_agg_func_type && isTuple(desc.function->getReturnType()))
        {
            size_t tuple_size = desc.column_numbers.size();
            MutableColumns tuple_columns(tuple_size);
            for (size_t i = 0; i < tuple_size; ++i)
                tuple_columns[i] = header.safeGetByPosition(desc.column_numbers[i]).column->cloneEmpty();

            columns.emplace_back(ColumnTuple::create(std::move(tuple_columns)));
        }
        else
            columns.emplace_back(header.safeGetByPosition(desc.column_numbers[0]).column->cloneEmpty());
    }

    for (auto & column_number : columns_definition.column_numbers_not_to_aggregate)
        columns.emplace_back(header.safeGetByPosition(column_number).type->createColumn());

    return columns;
}

static void preprocessChunk(Chunk & chunk)
{
    auto num_rows = chunk.getNumRows();
    auto columns = chunk.detachColumns();

    for (auto & column : columns)
        column = column->convertToFullColumnIfConst();

    chunk.setColumns(std::move(columns), num_rows);
}

static void postprocessChunk(
    Chunk & chunk, size_t num_result_columns,
    const SummingSortedAlgorithm::ColumnsDefinition & def)
{
    size_t num_rows = chunk.getNumRows();
    auto columns = chunk.detachColumns();

    Columns res_columns(num_result_columns);
    size_t next_column = 0;

    for (auto & desc : def.columns_to_aggregate)
    {
        auto column = std::move(columns[next_column]);
        ++next_column;

        if (!desc.is_agg_func_type && isTuple(desc.function->getReturnType()))
        {
            /// Unpack tuple into block.
            size_t tuple_size = desc.column_numbers.size();
            for (size_t i = 0; i < tuple_size; ++i)
                res_columns[desc.column_numbers[i]] = assert_cast<const ColumnTuple &>(*column).getColumnPtr(i);
        }
        else
            res_columns[desc.column_numbers[0]] = std::move(column);
    }

    for (auto column_number : def.column_numbers_not_to_aggregate)
    {
        auto column = std::move(columns[next_column]);
        ++next_column;

        res_columns[column_number] = std::move(column);
    }

    chunk.setColumns(std::move(res_columns), num_rows);
}

static void setRow(Row & row, SortCursor & cursor, const Names & column_names)
{
    size_t num_columns = row.size();
    for (size_t i = 0; i < num_columns; ++i)
    {
        try
        {
            cursor->all_columns[i]->get(cursor->pos, row[i]);
        }
        catch (...)
        {
            tryLogCurrentException(__PRETTY_FUNCTION__);

            /// Find out the name of the column and throw more informative exception.

            String column_name;
            if (i < column_names.size())
                column_name = column_names[i];

            throw Exception("MergingSortedBlockInputStream failed to read row " + toString(cursor->pos)
                            + " of column " + toString(i) + (column_name.empty() ? "" : " (" + column_name + ")"),
                            ErrorCodes::CORRUPTED_DATA);
        }
    }
}


Chunk SummingSortedAlgorithm::SummingMergedData::pull(size_t num_result_columns)
{
    auto chunk = MergedData::pull();
    postprocessChunk(chunk, num_result_columns, def);

    initAggregateDescription(def.columns_to_aggregate);

    return chunk;
}

SummingSortedAlgorithm::SummingSortedAlgorithm(
    const Block & header, size_t num_inputs,
    SortDescription description_,
    const Names & column_names_to_sum,
    size_t max_block_size)
    : IMergingAlgorithmWithDelayedChunk(num_inputs, std::move(description_))
    , columns_definition(defineColumns(header, description, column_names_to_sum))
    , merged_data(getMergedDataColumns(header, columns_definition), max_block_size, columns_definition)
    , column_names(header.getNames())
{
    current_row.resize(header.columns());
    merged_data.initAggregateDescription(columns_definition.columns_to_aggregate);
}

void SummingSortedAlgorithm::initialize(Chunks chunks)
{
    for (auto & chunk : chunks)
        if (chunk)
            preprocessChunk(chunk);

    initializeQueue(std::move(chunks));
}

void SummingSortedAlgorithm::consume(Chunk chunk, size_t source_num)
{
    preprocessChunk(chunk);
    updateCursor(std::move(chunk), source_num);
}


void SummingSortedAlgorithm::insertCurrentRowIfNeeded()
{
    /// We have nothing to aggregate. It means that it could be non-zero, because we have columns_not_to_aggregate.
    if (columns_definition.columns_to_aggregate.empty())
        current_row_is_zero = false;

    for (auto & desc : columns_definition.columns_to_aggregate)
    {
        // Do not insert if the aggregation state hasn't been created
        if (desc.created)
        {
            if (desc.is_agg_func_type)
            {
                current_row_is_zero = false;
            }
            else
            {
                try
                {
                    desc.function->insertResultInto(desc.state.data(), *desc.merged_column);

                    /// Update zero status of current row
                    if (desc.column_numbers.size() == 1)
                    {
                        // Flag row as non-empty if at least one column number if non-zero
                        current_row_is_zero = current_row_is_zero && desc.merged_column->isDefaultAt(desc.merged_column->size() - 1);
                    }
                    else
                    {
                        /// It is sumMapWithOverflow aggregate function.
                        /// Assume that the row isn't empty in this case (just because it is compatible with previous version)
                        current_row_is_zero = false;
                    }
                }
                catch (...)
                {
                    desc.destroyState();
                    throw;
                }
            }
            desc.destroyState();
        }
        else
            desc.merged_column->insertDefault();
    }

    /// If it is "zero" row, then rollback the insertion
    /// (at this moment we need rollback only cols from columns_to_aggregate)
    if (current_row_is_zero)
    {
        for (auto & desc : columns_definition.columns_to_aggregate)
            desc.merged_column->popBack(1);

        return;
    }

    merged_data.insertRow(current_row, columns_definition.column_numbers_not_to_aggregate);
}

void SummingSortedAlgorithm::addRow(SortCursor & cursor)
{
    for (auto & desc : columns_definition.columns_to_aggregate)
    {
        if (!desc.created)
            throw Exception("Logical error in SummingSortedBlockInputStream, there are no description", ErrorCodes::LOGICAL_ERROR);

        if (desc.is_agg_func_type)
        {
            // desc.state is not used for AggregateFunction types
            auto & col = cursor->all_columns[desc.column_numbers[0]];
            assert_cast<ColumnAggregateFunction &>(*desc.merged_column).insertMergeFrom(*col, cursor->pos);
        }
        else
        {
            // Specialized case for unary functions
            if (desc.column_numbers.size() == 1)
            {
                auto & col = cursor->all_columns[desc.column_numbers[0]];
                desc.add_function(desc.function.get(), desc.state.data(), &col, cursor->pos, nullptr);
            }
            else
            {
                // Gather all source columns into a vector
                ColumnRawPtrs columns(desc.column_numbers.size());
                for (size_t i = 0; i < desc.column_numbers.size(); ++i)
                    columns[i] = cursor->all_columns[desc.column_numbers[i]];

                desc.add_function(desc.function.get(), desc.state.data(), columns.data(), cursor->pos, nullptr);
            }
        }
    }
}

IMergingAlgorithm::Status SummingSortedAlgorithm::merge()
{
    /// Take the rows in needed order and put them in `merged_columns` until rows no more than `max_block_size`
    while (queue.isValid())
    {
        bool key_differs;
        bool has_previous_group = !last_key.empty();

        SortCursor current = queue.current();

        {
            detail::RowRef current_key;
            current_key.set(current);

            if (!has_previous_group)    /// The first key encountered.
            {
                key_differs = true;
                current_row_is_zero = true;
            }
            else
                key_differs = !last_key.hasEqualSortColumnsWith(current_key);

            last_key = current_key;
            last_chunk_sort_columns.clear();
        }

        if (key_differs)
        {
            if (has_previous_group)
                /// Write the data for the previous group.
                insertCurrentRowIfNeeded();

            if (merged_data.hasEnoughRows())
            {
                /// The block is now full and the last row is calculated completely.
                last_key.reset();
                return Status(merged_data.pull(column_names.size()));
            }

            setRow(current_row, current, column_names);

            /// Reset aggregation states for next row
            for (auto & desc : columns_definition.columns_to_aggregate)
                desc.createState();

            // Start aggregations with current row
            addRow(current);

            if (columns_definition.maps_to_sum.empty())
            {
                /// We have only columns_to_aggregate. The status of current row will be determined
                /// in 'insertCurrentRowIfNeeded' method on the values of aggregate functions.
                current_row_is_zero = true; // NOLINT
            }
            else
            {
                /// We have complex maps that will be summed with 'mergeMap' method.
                /// The single row is considered non zero, and the status after merging with other rows
                /// will be determined in the branch below (when key_differs == false).
                current_row_is_zero = false; // NOLINT
            }
        }
        else
        {
            addRow(current);

            // Merge maps only for same rows
            for (const auto & desc : columns_definition.maps_to_sum)
                if (mergeMap(desc, current_row, current))
                    current_row_is_zero = false;
        }

        if (!current->isLast())
        {
            queue.next();
        }
        else
        {
            /// We get the next block from the corresponding source, if there is one.
            queue.removeTop();
            return Status(current.impl->order);
        }
    }

    /// We will write the data for the last group, if it is non-zero.
    /// If it is zero, and without it the output stream will be empty, we will write it anyway.
    insertCurrentRowIfNeeded();
    last_chunk_sort_columns.clear();
    return Status(merged_data.pull(column_names.size()), true);
}

}
