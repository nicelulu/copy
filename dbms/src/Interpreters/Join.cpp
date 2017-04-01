#include <common/logger_useful.h>

#include <Columns/ColumnString.h>
#include <Columns/ColumnFixedString.h>

#include <Interpreters/Join.h>
#include <Interpreters/NullableUtils.h>

#include <DataStreams/IProfilingBlockInputStream.h>
#include <Core/ColumnNumbers.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int UNKNOWN_SET_DATA_VARIANT;
    extern const int LOGICAL_ERROR;
    extern const int SET_SIZE_LIMIT_EXCEEDED;
    extern const int TYPE_MISMATCH;
    extern const int ILLEGAL_COLUMN;
}


Join::Join(const Names & key_names_left_, const Names & key_names_right_,
    const Limits & limits, ASTTableJoin::Kind kind_, ASTTableJoin::Strictness strictness_)
    : kind(kind_), strictness(strictness_),
    key_names_left(key_names_left_),
    key_names_right(key_names_right_),
    log(&Logger::get("Join")),
    max_rows(limits.max_rows_in_join),
    max_bytes(limits.max_bytes_in_join),
    overflow_mode(limits.join_overflow_mode)
{
}


Join::Type Join::chooseMethod(const ConstColumnPlainPtrs & key_columns, Sizes & key_sizes)
{
    size_t keys_size = key_columns.size();

    if (keys_size == 0)
        return Type::CROSS;

    bool all_fixed = true;
    size_t keys_bytes = 0;
    key_sizes.resize(keys_size);
    for (size_t j = 0; j < keys_size; ++j)
    {
        if (!key_columns[j]->isFixed())
        {
            all_fixed = false;
            break;
        }
        key_sizes[j] = key_columns[j]->sizeOfField();
        keys_bytes += key_sizes[j];
    }

    /// Если есть один числовой ключ, который помещается в 64 бита
    if (keys_size == 1 && key_columns[0]->isNumericNotNullable())
    {
        size_t size_of_field = key_columns[0]->sizeOfField();
        if (size_of_field == 1)
            return Type::key8;
        if (size_of_field == 2)
            return Type::key16;
        if (size_of_field == 4)
            return Type::key32;
        if (size_of_field == 8)
            return Type::key64;
        throw Exception("Logical error: numeric column has sizeOfField not in 1, 2, 4, 8.", ErrorCodes::LOGICAL_ERROR);
    }

    /// Если ключи помещаются в N бит, будем использовать хэш-таблицу по упакованным в N-бит ключам
    if (all_fixed && keys_bytes <= 16)
        return Type::keys128;
    if (all_fixed && keys_bytes <= 32)
        return Type::keys256;

    /// If there is single string key, use hash table of it's values.
    if (keys_size == 1 && (typeid_cast<const ColumnString *>(key_columns[0]) || typeid_cast<const ColumnConstString *>(key_columns[0])))
        return Type::key_string;

    if (keys_size == 1 && typeid_cast<const ColumnFixedString *>(key_columns[0]))
        return Type::key_fixed_string;

    /// Otherwise, will use set of cryptographic hashes of unambiguously serialized values.
    return Type::hashed;
}


template <typename Maps>
static void initImpl(Maps & maps, Join::Type type)
{
    switch (type)
    {
        case Join::Type::EMPTY:            break;
        case Join::Type::CROSS:            break;

    #define M(TYPE) \
        case Join::Type::TYPE: maps.TYPE = std::make_unique<typename decltype(maps.TYPE)::element_type>(); break;
        APPLY_FOR_JOIN_VARIANTS(M)
    #undef M

        default:
            throw Exception("Unknown JOIN keys variant.", ErrorCodes::UNKNOWN_SET_DATA_VARIANT);
    }
}

template <typename Maps>
static size_t getTotalRowCountImpl(const Maps & maps, Join::Type type)
{
    switch (type)
    {
        case Join::Type::EMPTY:            return 0;
        case Join::Type::CROSS:            return 0;

    #define M(NAME) \
        case Join::Type::NAME: return maps.NAME ? maps.NAME->size() : 0;
        APPLY_FOR_JOIN_VARIANTS(M)
    #undef M

        default:
            throw Exception("Unknown JOIN keys variant.", ErrorCodes::UNKNOWN_SET_DATA_VARIANT);
    }
}

template <typename Maps>
static size_t getTotalByteCountImpl(const Maps & maps, Join::Type type)
{
    switch (type)
    {
        case Join::Type::EMPTY:            return 0;
        case Join::Type::CROSS:            return 0;

    #define M(NAME) \
        case Join::Type::NAME: return maps.NAME ? maps.NAME->getBufferSizeInBytes() : 0;
        APPLY_FOR_JOIN_VARIANTS(M)
    #undef M

        default:
            throw Exception("Unknown JOIN keys variant.", ErrorCodes::UNKNOWN_SET_DATA_VARIANT);
    }
}


template <Join::Type type>
struct KeyGetterForType;

template <> struct KeyGetterForType<Join::Type::key8> { using Type = JoinKeyGetterOneNumber<UInt8>; };
template <> struct KeyGetterForType<Join::Type::key16> { using Type = JoinKeyGetterOneNumber<UInt16>; };
template <> struct KeyGetterForType<Join::Type::key32> { using Type = JoinKeyGetterOneNumber<UInt32>; };
template <> struct KeyGetterForType<Join::Type::key64> { using Type = JoinKeyGetterOneNumber<UInt64>; };
template <> struct KeyGetterForType<Join::Type::key_string> { using Type = JoinKeyGetterString; };
template <> struct KeyGetterForType<Join::Type::key_fixed_string> { using Type = JoinKeyGetterFixedString; };
template <> struct KeyGetterForType<Join::Type::keys128> { using Type = JoinKeyGetterFixed<UInt128>; };
template <> struct KeyGetterForType<Join::Type::keys256> { using Type = JoinKeyGetterFixed<UInt256>; };
template <> struct KeyGetterForType<Join::Type::hashed> { using Type = JoinKeyGetterHashed; };


/// Нужно ли использовать хэш-таблицы maps_*_full, в которых запоминается, была ли строчка присоединена.
static bool getFullness(ASTTableJoin::Kind kind)
{
    return kind == ASTTableJoin::Kind::Right || kind == ASTTableJoin::Kind::Full;
}


void Join::init(Type type_)
{
    type = type_;

    if (kind == ASTTableJoin::Kind::Cross)
        return;

    if (!getFullness(kind))
    {
        if (strictness == ASTTableJoin::Strictness::Any)
            initImpl(maps_any, type);
        else
            initImpl(maps_all, type);
    }
    else
    {
        if (strictness == ASTTableJoin::Strictness::Any)
            initImpl(maps_any_full, type);
        else
            initImpl(maps_all_full, type);
    }
}

size_t Join::getTotalRowCount() const
{
    size_t res = 0;

    if (type == Type::CROSS)
    {
        for (const auto & block : blocks)
            res += block.rows();
    }
    else
    {
        res += getTotalRowCountImpl(maps_any, type);
        res += getTotalRowCountImpl(maps_all, type);
        res += getTotalRowCountImpl(maps_any_full, type);
        res += getTotalRowCountImpl(maps_all_full, type);
    }

    return res;
}

size_t Join::getTotalByteCount() const
{
    size_t res = 0;

    if (type == Type::CROSS)
    {
        for (const auto & block : blocks)
            res += block.bytes();
    }
    else
    {
        res += getTotalByteCountImpl(maps_any, type);
        res += getTotalByteCountImpl(maps_all, type);
        res += getTotalByteCountImpl(maps_any_full, type);
        res += getTotalByteCountImpl(maps_all_full, type);
        res += pool.size();
    }

    return res;
}


bool Join::checkSizeLimits() const
{
    if (max_rows && getTotalRowCount() > max_rows)
        return false;
    if (max_bytes && getTotalByteCount() > max_bytes)
        return false;
    return true;
}


void Join::setSampleBlock(const Block & block)
{
    Poco::ScopedWriteRWLock lock(rwlock);

    if (!empty())
        return;

    size_t keys_size = key_names_right.size();
    ConstColumnPlainPtrs key_columns(keys_size);

    for (size_t i = 0; i < keys_size; ++i)
        key_columns[i] = block.getByName(key_names_right[i]).column.get();

    /// Choose data structure to use for JOIN.
    init(chooseMethod(key_columns, key_sizes));

    sample_block_with_columns_to_add = block;

    /// Переносим из sample_block_with_columns_to_add ключевые столбцы в sample_block_with_keys, сохраняя порядок.
    size_t pos = 0;
    while (pos < sample_block_with_columns_to_add.columns())
    {
        const auto & name = sample_block_with_columns_to_add.getByPosition(pos).name;
        if (key_names_right.end() != std::find(key_names_right.begin(), key_names_right.end(), name))
        {
            sample_block_with_keys.insert(sample_block_with_columns_to_add.getByPosition(pos));
            sample_block_with_columns_to_add.erase(pos);
        }
        else
            ++pos;
    }

    for (size_t i = 0, size = sample_block_with_columns_to_add.columns(); i < size; ++i)
    {
        auto & column = sample_block_with_columns_to_add.getByPosition(i);
        if (!column.column)
            column.column = column.type->createColumn();
    }
}


namespace
{
    /// Вставка элемента в хэш-таблицу вида ключ -> ссылка на строку, которая затем будет использоваться при JOIN-е.
    template <ASTTableJoin::Strictness STRICTNESS, typename Map, typename KeyGetter>
    struct Inserter
    {
        static void insert(Map & map, const typename Map::key_type & key, Block * stored_block, size_t i, Arena & pool);
    };

    template <typename Map, typename KeyGetter>
    struct Inserter<ASTTableJoin::Strictness::Any, Map, KeyGetter>
    {
        static void insert(Map & map, const typename Map::key_type & key, Block * stored_block, size_t i, Arena & pool)
        {
            typename Map::iterator it;
            bool inserted;
            map.emplace(key, it, inserted);

            if (inserted)
            {
                KeyGetter::onNewKey(it->first, pool);
                new (&it->second) typename Map::mapped_type(stored_block, i);
            }
        }
    };

    template <typename Map, typename KeyGetter>
    struct Inserter<ASTTableJoin::Strictness::All, Map, KeyGetter>
    {
        static void insert(Map & map, const typename Map::key_type & key, Block * stored_block, size_t i, Arena & pool)
        {
            typename Map::iterator it;
            bool inserted;
            map.emplace(key, it, inserted);

            if (inserted)
            {
                KeyGetter::onNewKey(it->first, pool);
                new (&it->second) typename Map::mapped_type(stored_block, i);
            }
            else
            {
                /** Первый элемент списка хранится в значении хэш-таблицы, остальные - в pool-е.
                * Мы будем вставлять каждый раз элемент на место второго.
                * То есть, бывший второй элемент, если он был, станет третьим, и т. п.
                */
                auto elem = reinterpret_cast<typename Map::mapped_type *>(pool.alloc(sizeof(typename Map::mapped_type)));

                elem->next = it->second.next;
                it->second.next = elem;
                elem->block = stored_block;
                elem->row_num = i;
            }
        }
    };


    template <ASTTableJoin::Strictness STRICTNESS, typename KeyGetter, typename Map, bool has_null_map>
    void NO_INLINE insertFromBlockImplTypeCase(
        Map & map, size_t rows, const ConstColumnPlainPtrs & key_columns,
        size_t keys_size, const Sizes & key_sizes, Block * stored_block, ConstNullMapPtr null_map, Arena & pool)
    {
        KeyGetter key_getter(key_columns);

        for (size_t i = 0; i < rows; ++i)
        {
            if (has_null_map && (*null_map)[i])
                continue;

            auto key = key_getter.getKey(key_columns, keys_size, i, key_sizes);
            Inserter<STRICTNESS, Map, KeyGetter>::insert(map, key, stored_block, i, pool);
        }
    }


    template <ASTTableJoin::Strictness STRICTNESS, typename KeyGetter, typename Map>
    void insertFromBlockImplType(
        Map & map, size_t rows, const ConstColumnPlainPtrs & key_columns,
        size_t keys_size, const Sizes & key_sizes, Block * stored_block, ConstNullMapPtr null_map, Arena & pool)
    {
        if (null_map)
            insertFromBlockImplTypeCase<STRICTNESS, KeyGetter, Map, true>(map, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool);
        else
            insertFromBlockImplTypeCase<STRICTNESS, KeyGetter, Map, false>(map, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool);
    }


    template <ASTTableJoin::Strictness STRICTNESS, typename Maps>
    void insertFromBlockImpl(
        Join::Type type, Maps & maps, size_t rows, const ConstColumnPlainPtrs & key_columns,
        size_t keys_size, const Sizes & key_sizes, Block * stored_block, ConstNullMapPtr null_map, Arena & pool)
    {
        switch (type)
        {
            case Join::Type::EMPTY:            break;
            case Join::Type::CROSS:            break;    /// Do nothing. We have already saved block, and it is enough.

        #define M(TYPE) \
            case Join::Type::TYPE: \
                insertFromBlockImplType<STRICTNESS, typename KeyGetterForType<Join::Type::TYPE>::Type>(\
                    *maps.TYPE, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool); \
                    break;
            APPLY_FOR_JOIN_VARIANTS(M)
        #undef M

            default:
                throw Exception("Unknown JOIN keys variant.", ErrorCodes::UNKNOWN_SET_DATA_VARIANT);
        }
    }
}


bool Join::insertFromBlock(const Block & block)
{
    Poco::ScopedWriteRWLock lock(rwlock);

    if (empty())
        throw Exception("Logical error: Join was not initialized", ErrorCodes::LOGICAL_ERROR);

    size_t keys_size = key_names_right.size();
    ConstColumnPlainPtrs key_columns(keys_size);

    /// Rare case, when keys are constant. To avoid code bloat, simply materialize them.
    Columns materialized_columns;

    /// Memoize key columns to work.
    for (size_t i = 0; i < keys_size; ++i)
    {
        key_columns[i] = block.getByName(key_names_right[i]).column.get();

        if (auto converted = key_columns[i]->convertToFullColumnIfConst())
        {
            materialized_columns.emplace_back(converted);
            key_columns[i] = materialized_columns.back().get();
        }
    }

    /// We will insert to the map only keys, where all components are not NULL.
    ColumnPtr null_map_holder;
    ConstNullMapPtr null_map{};
    extractNestedColumnsAndNullMap(key_columns, null_map_holder, null_map);

    size_t rows = block.rows();

    blocks.push_back(block);
    Block * stored_block = &blocks.back();

    if (getFullness(kind))
    {
        /** Переносим ключевые столбцы в начало блока.
          * Именно там их будет ожидать NonJoinedBlockInputStream.
          */
        size_t key_num = 0;
        for (const auto & name : key_names_right)
        {
            size_t pos = stored_block->getPositionByName(name);
            ColumnWithTypeAndName col = stored_block->safeGetByPosition(pos);
            stored_block->erase(pos);
            stored_block->insert(key_num, std::move(col));
            ++key_num;
        }
    }
    else
    {
        /// Удаляем из stored_block ключевые столбцы, так как они не нужны.
        for (const auto & name : key_names_right)
            stored_block->erase(stored_block->getPositionByName(name));
    }

    /// Rare case, when joined columns are constant. To avoid code bloat, simply materialize them.
    for (size_t i = 0, size = stored_block->columns(); i < size; ++i)
    {
        ColumnPtr col = stored_block->safeGetByPosition(i).column;
        if (auto converted = col->convertToFullColumnIfConst())
            stored_block->safeGetByPosition(i).column = converted;
    }

    if (kind != ASTTableJoin::Kind::Cross)
    {
        /// Fill the hash table.
        if (!getFullness(kind))
        {
            if (strictness == ASTTableJoin::Strictness::Any)
                insertFromBlockImpl<ASTTableJoin::Strictness::Any>(type, maps_any, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool);
            else
                insertFromBlockImpl<ASTTableJoin::Strictness::All>(type, maps_all, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool);
        }
        else
        {
            if (strictness == ASTTableJoin::Strictness::Any)
                insertFromBlockImpl<ASTTableJoin::Strictness::Any>(type, maps_any_full, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool);
            else
                insertFromBlockImpl<ASTTableJoin::Strictness::All>(type, maps_all_full, rows, key_columns, keys_size, key_sizes, stored_block, null_map, pool);
        }
    }

    if (!checkSizeLimits())
    {
        if (overflow_mode == OverflowMode::THROW)
            throw Exception("Join size limit exceeded."
                " Rows: " + toString(getTotalRowCount()) +
                ", limit: " + toString(max_rows) +
                ". Bytes: " + toString(getTotalByteCount()) +
                ", limit: " + toString(max_bytes) + ".",
                ErrorCodes::SET_SIZE_LIMIT_EXCEEDED);

        if (overflow_mode == OverflowMode::BREAK)
            return false;

        throw Exception("Logical error: unknown overflow mode", ErrorCodes::LOGICAL_ERROR);
    }

    return true;
}


namespace
{
    template <ASTTableJoin::Kind KIND, ASTTableJoin::Strictness STRICTNESS, typename Map>
    struct Adder;

    template <typename Map>
    struct Adder<ASTTableJoin::Kind::Left, ASTTableJoin::Strictness::Any, Map>
    {
        static void addFound(const typename Map::const_iterator & it, size_t num_columns_to_add, ColumnPlainPtrs & added_columns,
            size_t i, IColumn::Filter * filter, IColumn::Offset_t & current_offset, IColumn::Offsets_t * offsets,
            size_t num_columns_to_skip)
        {
            for (size_t j = 0; j < num_columns_to_add; ++j)
                added_columns[j]->insertFrom(*it->second.block->getByPosition(num_columns_to_skip + j).column.get(), it->second.row_num);
        }

        static void addNotFound(size_t num_columns_to_add, ColumnPlainPtrs & added_columns,
            size_t i, IColumn::Filter * filter, IColumn::Offset_t & current_offset, IColumn::Offsets_t * offsets)
        {
            for (size_t j = 0; j < num_columns_to_add; ++j)
                added_columns[j]->insertDefault();
        }
    };

    template <typename Map>
    struct Adder<ASTTableJoin::Kind::Inner, ASTTableJoin::Strictness::Any, Map>
    {
        static void addFound(const typename Map::const_iterator & it, size_t num_columns_to_add, ColumnPlainPtrs & added_columns,
            size_t i, IColumn::Filter * filter, IColumn::Offset_t & current_offset, IColumn::Offsets_t * offsets,
            size_t num_columns_to_skip)
        {
            (*filter)[i] = 1;

            for (size_t j = 0; j < num_columns_to_add; ++j)
                added_columns[j]->insertFrom(*it->second.block->getByPosition(num_columns_to_skip + j).column.get(), it->second.row_num);
        }

        static void addNotFound(size_t num_columns_to_add, ColumnPlainPtrs & added_columns,
            size_t i, IColumn::Filter * filter, IColumn::Offset_t & current_offset, IColumn::Offsets_t * offsets)
        {
            (*filter)[i] = 0;
        }
    };

    template <ASTTableJoin::Kind KIND, typename Map>
    struct Adder<KIND, ASTTableJoin::Strictness::All, Map>
    {
        static void addFound(const typename Map::const_iterator & it, size_t num_columns_to_add, ColumnPlainPtrs & added_columns,
            size_t i, IColumn::Filter * filter, IColumn::Offset_t & current_offset, IColumn::Offsets_t * offsets,
            size_t num_columns_to_skip)
        {
            size_t rows_joined = 0;
            for (auto current = &static_cast<const typename Map::mapped_type::Base_t &>(it->second); current != nullptr; current = current->next)
            {
                for (size_t j = 0; j < num_columns_to_add; ++j)
                    added_columns[j]->insertFrom(*current->block->getByPosition(num_columns_to_skip + j).column.get(), current->row_num);

                ++rows_joined;
            }

            current_offset += rows_joined;
            (*offsets)[i] = current_offset;
        }

        static void addNotFound(size_t num_columns_to_add, ColumnPlainPtrs & added_columns,
            size_t i, IColumn::Filter * filter, IColumn::Offset_t & current_offset, IColumn::Offsets_t * offsets)
        {
            if (KIND == ASTTableJoin::Kind::Inner)
            {
                (*offsets)[i] = current_offset;
            }
            else
            {
                ++current_offset;
                (*offsets)[i] = current_offset;

                for (size_t j = 0; j < num_columns_to_add; ++j)
                    added_columns[j]->insertDefault();
            }
        }
    };

    template <ASTTableJoin::Kind KIND, ASTTableJoin::Strictness STRICTNESS, typename KeyGetter, typename Map, bool has_null_map>
    void NO_INLINE joinBlockImplTypeCase(
        Block & block, const Map & map, size_t rows, const ConstColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes,
        size_t num_columns_to_add, size_t num_columns_to_skip, ColumnPlainPtrs & added_columns, ConstNullMapPtr null_map,
        std::unique_ptr<IColumn::Filter> & filter,
        IColumn::Offset_t & current_offset, std::unique_ptr<IColumn::Offsets_t> & offsets_to_replicate)
    {
        KeyGetter key_getter(key_columns);

        for (size_t i = 0; i < rows; ++i)
        {
            if (has_null_map && (*null_map)[i])
            {
                Adder<KIND, STRICTNESS, Map>::addNotFound(
                    num_columns_to_add, added_columns, i, filter.get(), current_offset, offsets_to_replicate.get());
            }
            else
            {
                auto key = key_getter.getKey(key_columns, keys_size, i, key_sizes);
                typename Map::const_iterator it = map.find(key);

                if (it != map.end())
                {
                    it->second.setUsed();
                    Adder<KIND, STRICTNESS, Map>::addFound(
                        it, num_columns_to_add, added_columns, i, filter.get(), current_offset, offsets_to_replicate.get(), num_columns_to_skip);
                }
                else
                    Adder<KIND, STRICTNESS, Map>::addNotFound(
                        num_columns_to_add, added_columns, i, filter.get(), current_offset, offsets_to_replicate.get());
            }
        }
    }

    template <ASTTableJoin::Kind KIND, ASTTableJoin::Strictness STRICTNESS, typename KeyGetter, typename Map>
    void joinBlockImplType(
        Block & block, const Map & map, size_t rows, const ConstColumnPlainPtrs & key_columns, size_t keys_size, const Sizes & key_sizes,
        size_t num_columns_to_add, size_t num_columns_to_skip, ColumnPlainPtrs & added_columns, ConstNullMapPtr null_map,
        std::unique_ptr<IColumn::Filter> & filter,
        IColumn::Offset_t & current_offset, std::unique_ptr<IColumn::Offsets_t> & offsets_to_replicate)
    {
        if (null_map)
            joinBlockImplTypeCase<KIND, STRICTNESS, KeyGetter, Map, true>(
                block, map, rows, key_columns, keys_size, key_sizes, num_columns_to_add, num_columns_to_skip,
                added_columns, null_map, filter, current_offset, offsets_to_replicate);
        else
            joinBlockImplTypeCase<KIND, STRICTNESS, KeyGetter, Map, false>(
                block, map, rows, key_columns, keys_size, key_sizes, num_columns_to_add, num_columns_to_skip,
                added_columns, null_map, filter, current_offset, offsets_to_replicate);
    }
}


template <ASTTableJoin::Kind KIND, ASTTableJoin::Strictness STRICTNESS, typename Maps>
void Join::joinBlockImpl(Block & block, const Maps & maps) const
{
    size_t keys_size = key_names_left.size();
    ConstColumnPlainPtrs key_columns(keys_size);

    /// Rare case, when keys are constant. To avoid code bloat, simply materialize them.
    Columns materialized_columns;

    /// Memoize key columns to work.
    for (size_t i = 0; i < keys_size; ++i)
    {
        key_columns[i] = block.getByName(key_names_left[i]).column.get();

        if (auto converted = key_columns[i]->convertToFullColumnIfConst())
        {
            materialized_columns.emplace_back(converted);
            key_columns[i] = materialized_columns.back().get();
        }
    }

    /// Keys with NULL value in any column won't join to anything.
    ColumnPtr null_map_holder;
    ConstNullMapPtr null_map{};
    extractNestedColumnsAndNullMap(key_columns, null_map_holder, null_map);

    size_t existing_columns = block.columns();

    /** Если используется FULL или RIGHT JOIN, то столбцы из "левой" части надо материализовать.
      * Потому что, если они константы, то в "неприсоединённых" строчках, у них могут быть другие значения
      *  - значения по-умолчанию, которые могут отличаться от значений этих констант.
      */
    if (getFullness(kind))
    {
        for (size_t i = 0; i < existing_columns; ++i)
        {
            auto & col = block.safeGetByPosition(i).column;

            if (auto converted = col->convertToFullColumnIfConst())
                col = converted;
        }
    }

    /// Добавляем в блок новые столбцы.
    size_t num_columns_to_add = sample_block_with_columns_to_add.columns();
    ColumnPlainPtrs added_columns(num_columns_to_add);

    for (size_t i = 0; i < num_columns_to_add; ++i)
    {
        const ColumnWithTypeAndName & src_column = sample_block_with_columns_to_add.safeGetByPosition(i);
        ColumnWithTypeAndName new_column = src_column.cloneEmpty();
        added_columns[i] = new_column.column.get();
        added_columns[i]->reserve(src_column.column->size());
        block.insert(std::move(new_column));
    }

    size_t rows = block.rows();

    /// Используется при ANY INNER JOIN
    std::unique_ptr<IColumn::Filter> filter;

    if ((kind == ASTTableJoin::Kind::Inner || kind == ASTTableJoin::Kind::Right) && strictness == ASTTableJoin::Strictness::Any)
        filter = std::make_unique<IColumn::Filter>(rows);

    /// Используется при ALL ... JOIN
    IColumn::Offset_t current_offset = 0;
    std::unique_ptr<IColumn::Offsets_t> offsets_to_replicate;

    if (strictness == ASTTableJoin::Strictness::All)
        offsets_to_replicate = std::make_unique<IColumn::Offsets_t>(rows);

    /** Для LEFT/INNER JOIN, сохранённые блоки не содержат ключи.
      * Для FULL/RIGHT JOIN, сохранённые блоки содержат ключи;
      *  но они не будут использоваться на этой стадии соединения (а будут в AdderNonJoined), и их нужно пропустить.
      */
    size_t num_columns_to_skip = 0;
    if (getFullness(kind))
        num_columns_to_skip = keys_size;

//    std::cerr << num_columns_to_skip << "\n" << block.dumpStructure() << "\n" << blocks.front().dumpStructure() << "\n";

    switch (type)
    {
    #define M(TYPE) \
        case Join::Type::TYPE: \
            joinBlockImplType<KIND, STRICTNESS, typename KeyGetterForType<Join::Type::TYPE>::Type>(\
                block, *maps.TYPE, rows, key_columns, keys_size, key_sizes, \
                num_columns_to_add, num_columns_to_skip, added_columns, null_map, \
                filter, current_offset, offsets_to_replicate); \
            break;
        APPLY_FOR_JOIN_VARIANTS(M)
    #undef M

        default:
            throw Exception("Unknown JOIN keys variant.", ErrorCodes::UNKNOWN_SET_DATA_VARIANT);
    }

    /// Если ANY INNER|RIGHT JOIN - фильтруем все столбцы кроме новых.
    if (filter)
        for (size_t i = 0; i < existing_columns; ++i)
            block.safeGetByPosition(i).column = block.safeGetByPosition(i).column->filter(*filter, -1);

    /// Если ALL ... JOIN - размножаем все столбцы кроме новых.
    if (offsets_to_replicate)
        for (size_t i = 0; i < existing_columns; ++i)
            block.safeGetByPosition(i).column = block.safeGetByPosition(i).column->replicate(*offsets_to_replicate);
}


void Join::joinBlockImplCross(Block & block) const
{
    Block res = block.cloneEmpty();

    /// Добавляем в блок новые столбцы.
    size_t num_existing_columns = res.columns();
    size_t num_columns_to_add = sample_block_with_columns_to_add.columns();

    ColumnPlainPtrs src_left_columns(num_existing_columns);
    ColumnPlainPtrs dst_left_columns(num_existing_columns);
    ColumnPlainPtrs dst_right_columns(num_columns_to_add);

    for (size_t i = 0; i < num_existing_columns; ++i)
    {
        src_left_columns[i] = block.getByPosition(i).column.get();
        dst_left_columns[i] = res.getByPosition(i).column.get();
    }

    for (size_t i = 0; i < num_columns_to_add; ++i)
    {
        const ColumnWithTypeAndName & src_column = sample_block_with_columns_to_add.getByPosition(i);
        ColumnWithTypeAndName new_column = src_column.cloneEmpty();
        dst_right_columns[i] = new_column.column.get();
        res.insert(std::move(new_column));
    }

    size_t rows_left = block.rows();

    /// NOTE Было бы оптимальнее использовать reserve, а также методы replicate для размножения значений левого блока.

    for (size_t i = 0; i < rows_left; ++i)
    {
        for (const Block & block_right : blocks)
        {
            size_t rows_right = block_right.rows();

            for (size_t col_num = 0; col_num < num_existing_columns; ++col_num)
                for (size_t j = 0; j < rows_right; ++j)
                    dst_left_columns[col_num]->insertFrom(*src_left_columns[col_num], i);

            for (size_t col_num = 0; col_num < num_columns_to_add; ++col_num)
            {
                const IColumn * column_right = block_right.getByPosition(col_num).column.get();

                for (size_t j = 0; j < rows_right; ++j)
                    dst_right_columns[col_num]->insertFrom(*column_right, j);
            }
        }
    }

    block = res;
}


void Join::checkTypesOfKeys(const Block & block_left, const Block & block_right) const
{
    size_t keys_size = key_names_left.size();

    for (size_t i = 0; i < keys_size; ++i)
        if (!block_left.getByName(key_names_left[i]).type->equals(*block_right.getByName(key_names_right[i]).type))
            throw Exception("Type mismatch of columns to JOIN by: "
                + key_names_left[i] + " " + block_left.getByName(key_names_left[i]).type->getName() + " at left, "
                + key_names_right[i] + " " + block_right.getByName(key_names_right[i]).type->getName() + " at right",
                ErrorCodes::TYPE_MISMATCH);
}


void Join::joinBlock(Block & block) const
{
//    std::cerr << "joinBlock: " << block.dumpStructure() << "\n";

    Poco::ScopedReadRWLock lock(rwlock);

    checkTypesOfKeys(block, sample_block_with_keys);

    if (kind == ASTTableJoin::Kind::Left && strictness == ASTTableJoin::Strictness::Any)
        joinBlockImpl<ASTTableJoin::Kind::Left, ASTTableJoin::Strictness::Any>(block, maps_any);
    else if (kind == ASTTableJoin::Kind::Inner && strictness == ASTTableJoin::Strictness::Any)
        joinBlockImpl<ASTTableJoin::Kind::Inner, ASTTableJoin::Strictness::Any>(block, maps_any);
    else if (kind == ASTTableJoin::Kind::Left && strictness == ASTTableJoin::Strictness::All)
        joinBlockImpl<ASTTableJoin::Kind::Left, ASTTableJoin::Strictness::All>(block, maps_all);
    else if (kind == ASTTableJoin::Kind::Inner && strictness == ASTTableJoin::Strictness::All)
        joinBlockImpl<ASTTableJoin::Kind::Inner, ASTTableJoin::Strictness::All>(block, maps_all);
    else if (kind == ASTTableJoin::Kind::Full && strictness == ASTTableJoin::Strictness::Any)
        joinBlockImpl<ASTTableJoin::Kind::Left, ASTTableJoin::Strictness::Any>(block, maps_any_full);
    else if (kind == ASTTableJoin::Kind::Right && strictness == ASTTableJoin::Strictness::Any)
        joinBlockImpl<ASTTableJoin::Kind::Inner, ASTTableJoin::Strictness::Any>(block, maps_any_full);
    else if (kind == ASTTableJoin::Kind::Full && strictness == ASTTableJoin::Strictness::All)
        joinBlockImpl<ASTTableJoin::Kind::Left, ASTTableJoin::Strictness::All>(block, maps_all_full);
    else if (kind == ASTTableJoin::Kind::Right && strictness == ASTTableJoin::Strictness::All)
        joinBlockImpl<ASTTableJoin::Kind::Inner, ASTTableJoin::Strictness::All>(block, maps_all_full);
    else if (kind == ASTTableJoin::Kind::Cross)
        joinBlockImplCross(block);
    else
        throw Exception("Logical error: unknown combination of JOIN", ErrorCodes::LOGICAL_ERROR);
}


void Join::joinTotals(Block & block) const
{
    Block totals_without_keys = totals;

    if (totals_without_keys)
    {
        for (const auto & name : key_names_right)
            totals_without_keys.erase(totals_without_keys.getPositionByName(name));

        for (size_t i = 0; i < totals_without_keys.columns(); ++i)
            block.insert(totals_without_keys.safeGetByPosition(i));
    }
    else
    {
        /// Будем присоединять пустые totals - из одной строчки со значениями по-умолчанию.
        totals_without_keys = sample_block_with_columns_to_add.cloneEmpty();

        for (size_t i = 0; i < totals_without_keys.columns(); ++i)
        {
            totals_without_keys.safeGetByPosition(i).column->insertDefault();
            block.insert(totals_without_keys.safeGetByPosition(i));
        }
    }
}


template <ASTTableJoin::Strictness STRICTNESS, typename Mapped>
struct AdderNonJoined;

template <typename Mapped>
struct AdderNonJoined<ASTTableJoin::Strictness::Any, Mapped>
{
    static void add(const Mapped & mapped,
        size_t num_columns_left, ColumnPlainPtrs & columns_left,
        size_t num_columns_right, ColumnPlainPtrs & columns_right)
    {
        for (size_t j = 0; j < num_columns_left; ++j)
            columns_left[j]->insertDefault();

        for (size_t j = 0; j < num_columns_right; ++j)
            columns_right[j]->insertFrom(*mapped.block->getByPosition(j).column.get(), mapped.row_num);
    }
};

template <typename Mapped>
struct AdderNonJoined<ASTTableJoin::Strictness::All, Mapped>
{
    static void add(const Mapped & mapped,
        size_t num_columns_left, ColumnPlainPtrs & columns_left,
        size_t num_columns_right, ColumnPlainPtrs & columns_right)
    {
        for (auto current = &static_cast<const typename Mapped::Base_t &>(mapped); current != nullptr; current = current->next)
        {
            for (size_t j = 0; j < num_columns_left; ++j)
                columns_left[j]->insertDefault();

            for (size_t j = 0; j < num_columns_right; ++j)
                columns_right[j]->insertFrom(*current->block->getByPosition(j).column.get(), current->row_num);
        }
    }
};


/// Поток из неприсоединённых ранее строк правой таблицы.
class NonJoinedBlockInputStream : public IProfilingBlockInputStream
{
public:
    NonJoinedBlockInputStream(const Join & parent_, Block & left_sample_block, size_t max_block_size_)
        : parent(parent_), max_block_size(max_block_size_)
    {
        /** left_sample_block содержит ключи и "левые" столбцы.
          * result_sample_block - ключи, "левые" столбцы и "правые" столбцы.
          */

        size_t num_keys = parent.key_names_left.size();
        size_t num_columns_left = left_sample_block.columns() - num_keys;
        size_t num_columns_right = parent.sample_block_with_columns_to_add.columns();

        result_sample_block = left_sample_block;

//        std::cerr << result_sample_block.dumpStructure() << "\n";

        /// Добавляем в блок новые столбцы.
        for (size_t i = 0; i < num_columns_right; ++i)
        {
            const ColumnWithTypeAndName & src_column = parent.sample_block_with_columns_to_add.safeGetByPosition(i);
            ColumnWithTypeAndName new_column = src_column.cloneEmpty();
            result_sample_block.insert(std::move(new_column));
        }

        column_numbers_left.reserve(num_columns_left);
        column_numbers_keys_and_right.reserve(num_keys + num_columns_right);

        for (size_t i = 0; i < num_keys + num_columns_left; ++i)
        {
            const String & name = left_sample_block.safeGetByPosition(i).name;

            auto found_key_column = std::find(parent.key_names_left.begin(), parent.key_names_left.end(), name);
            if (parent.key_names_left.end() == found_key_column)
                column_numbers_left.push_back(i);
            else
                column_numbers_keys_and_right.push_back(found_key_column - parent.key_names_left.begin());
        }

        for (size_t i = 0; i < num_columns_right; ++i)
            column_numbers_keys_and_right.push_back(num_keys + num_columns_left + i);

        columns_left.resize(num_columns_left);
        columns_keys_and_right.resize(num_keys + num_columns_right);
    }

    String getName() const override { return "NonJoined"; }

    String getID() const override
    {
        std::stringstream res;
        res << "NonJoined(" << &parent << ")";
        return res.str();
    }


protected:
    Block readImpl() override
    {
        if (parent.blocks.empty())
            return Block();

        if (parent.strictness == ASTTableJoin::Strictness::Any)
            return createBlock<ASTTableJoin::Strictness::Any>(parent.maps_any_full);
        else if (parent.strictness == ASTTableJoin::Strictness::All)
            return createBlock<ASTTableJoin::Strictness::All>(parent.maps_all_full);
        else
            throw Exception("Logical error: unknown JOIN strictness (must be ANY or ALL)", ErrorCodes::LOGICAL_ERROR);
    }

private:
    const Join & parent;
    size_t max_block_size;

    Block result_sample_block;
    ColumnNumbers column_numbers_left;
    ColumnNumbers column_numbers_keys_and_right;
    ColumnPlainPtrs columns_left;
    ColumnPlainPtrs columns_keys_and_right;

    std::unique_ptr<void, std::function<void(void *)>> position;    /// type erasure


    template <ASTTableJoin::Strictness STRICTNESS, typename Maps>
    Block createBlock(const Maps & maps)
    {
        Block block = result_sample_block.cloneEmpty();

        size_t num_columns_left = column_numbers_left.size();
        size_t num_columns_right = column_numbers_keys_and_right.size();

        for (size_t i = 0; i < num_columns_left; ++i)
        {
            auto & column_with_type_and_name = block.safeGetByPosition(column_numbers_left[i]);
            column_with_type_and_name.column = column_with_type_and_name.type->createColumn();
            columns_left[i] = column_with_type_and_name.column.get();
        }

        for (size_t i = 0; i < num_columns_right; ++i)
        {
            auto & column_with_type_and_name = block.safeGetByPosition(column_numbers_keys_and_right[i]);
            column_with_type_and_name.column = column_with_type_and_name.type->createColumn();
            columns_keys_and_right[i] = column_with_type_and_name.column.get();
            columns_keys_and_right[i]->reserve(column_with_type_and_name.column->size());
        }

        size_t rows_added = 0;

        switch (parent.type)
        {
        #define M(TYPE) \
            case Join::Type::TYPE: \
                rows_added = fillColumns<STRICTNESS>(*maps.TYPE, num_columns_left, columns_left, num_columns_right, columns_keys_and_right); \
                break;
            APPLY_FOR_JOIN_VARIANTS(M)
        #undef M

            default:
                throw Exception("Unknown JOIN keys variant.", ErrorCodes::UNKNOWN_SET_DATA_VARIANT);
        }

//        std::cerr << "rows added: " << rows_added << "\n";

        if (!rows_added)
            return Block();

/*        std::cerr << block.dumpStructure() << "\n";
        WriteBufferFromFileDescriptor wb(STDERR_FILENO);
        TabSeparatedBlockOutputStream out(wb);
        out.write(block);*/

        return block;
    }


    template <ASTTableJoin::Strictness STRICTNESS, typename Map>
    size_t fillColumns(const Map & map,
        size_t num_columns_left, ColumnPlainPtrs & columns_left,
        size_t num_columns_right, ColumnPlainPtrs & columns_right)
    {
        size_t rows_added = 0;

        if (!position)
            position = decltype(position)(
                static_cast<void *>(new typename Map::const_iterator(map.begin())),
                [](void * ptr) { delete reinterpret_cast<typename Map::const_iterator *>(ptr); });

        auto & it = *reinterpret_cast<typename Map::const_iterator *>(position.get());
        auto end = map.end();

        for (; it != end; ++it)
        {
            if (it->second.getUsed())
                continue;

            AdderNonJoined<STRICTNESS, typename Map::mapped_type>::add(it->second, num_columns_left, columns_left, num_columns_right, columns_right);

            ++rows_added;
            if (rows_added == max_block_size)
                break;
        }

        return rows_added;
    }
};


BlockInputStreamPtr Join::createStreamWithNonJoinedRows(Block & left_sample_block, size_t max_block_size) const
{
    return std::make_shared<NonJoinedBlockInputStream>(*this, left_sample_block, max_block_size);
}


}
