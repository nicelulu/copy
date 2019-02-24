#include <Storages/MergeTree/MergeTreeBloomFilterIndex.h>

#include <Common/UTF8Helpers.h>
#include <DataTypes/DataTypesNumber.h>
#include <IO/WriteHelpers.h>
#include <IO/ReadHelpers.h>
#include <Interpreters/ExpressionActions.h>
#include <Interpreters/ExpressionAnalyzer.h>
#include <Interpreters/SyntaxAnalyzer.h>
#include <Interpreters/QueryNormalizer.h>
#include <Storages/MergeTree/MergeTreeData.h>
#include <Parsers/ASTIdentifier.h>
#include <Parsers/ASTLiteral.h>
#include <Parsers/ASTSubquery.h>

#include <Poco/Logger.h>

#include <boost/algorithm/string.hpp>


namespace DB
{

namespace ErrorCodes
{
    extern const int INCORRECT_QUERY;
}


/// Adds all tokens from string to bloom filter.
static void stringToBloomFilter(
    const char * data, size_t size, const std::unique_ptr<ITokenExtractor> & token_extractor, StringBloomFilter & bloom_filter)
{
    size_t cur = 0;
    size_t token_start = 0;
    size_t token_len = 0;
    while (cur < size && token_extractor->next(data, size, &cur, &token_start, &token_len))
        bloom_filter.add(data + token_start, token_len);
}

/// Adds all tokens from like pattern string to bloom filter. (Because like pattern can contain `\%` and `\_`.)
static void likeStringToBloomFilter(
    const String & data, const std::unique_ptr<ITokenExtractor> & token_extractor, StringBloomFilter & bloom_filter)
{
    size_t cur = 0;
    String token;
    while (cur < data.size() && token_extractor->nextLike(data, &cur, token))
        bloom_filter.add(token.c_str(), token.size());
}


MergeTreeBloomFilterIndexGranule::MergeTreeBloomFilterIndexGranule(const MergeTreeBloomFilterIndex & index)
    : IMergeTreeIndexGranule()
    , index(index)
    , bloom_filters(
            index.columns.size(), StringBloomFilter(index.bloom_filter_size, index.bloom_filter_hashes, index.seed))
    , has_elems(false)
{
}

void MergeTreeBloomFilterIndexGranule::serializeBinary(WriteBuffer & ostr) const
{
    if (empty())
        throw Exception(
                "Attempt to write empty minmax index `" + index.name + "`", ErrorCodes::LOGICAL_ERROR);

    for (const auto & bloom_filter : bloom_filters)
    {
        /*auto *log = &Poco::Logger::get("bf");
        LOG_DEBUG(log, "writing fingerprint:" << bloom_filter.getFingerPrint() << " " << bloom_filter.getSum());*/
        ostr.write(reinterpret_cast<const char *>(bloom_filter.getFilter().data()), index.bloom_filter_size);
    }
}

void MergeTreeBloomFilterIndexGranule::deserializeBinary(ReadBuffer & istr)
{
    for (auto & bloom_filter : bloom_filters)
    {
        StringBloomFilter::Container filter_data(index.bloom_filter_size, 0);
        istr.read(reinterpret_cast<char *>(filter_data.data()), index.bloom_filter_size);
        bloom_filter.setFilter(std::move(filter_data));
        /*auto *log = &Poco::Logger::get("bf");
        LOG_DEBUG(log, "reading fingerprint:" << bloom_filter.getFingerPrint() << " " << bloom_filter.getSum());*/
    }
    has_elems = true;
}

void MergeTreeBloomFilterIndexGranule::update(const Block & block, size_t * pos, size_t limit)
{
    if (*pos >= block.rows())
        throw Exception(
                "The provided position is not less than the number of block rows. Position: "
                + toString(*pos) + ", Block rows: " + toString(block.rows()) + ".", ErrorCodes::LOGICAL_ERROR);

    size_t rows_read = std::min(limit, block.rows() - *pos);

    for (size_t col = 0; col < index.columns.size(); ++col)
    {
        const auto & column = block.getByName(index.columns[col]).column;
        for (size_t i = 0; i < rows_read; ++i)
        {
            auto ref = column->getDataAt(*pos + i);
            stringToBloomFilter(ref.data, ref.size, index.token_extractor_func, bloom_filters[col]);
        }
    }
    has_elems = true;
    *pos += rows_read;
}


const BloomFilterCondition::AtomMap BloomFilterCondition::atom_map
{
        {
                "notEquals",
                [] (RPNElement & out, const Field & value, const MergeTreeBloomFilterIndex & idx)
                {
                    out.function = RPNElement::FUNCTION_NOT_EQUALS;
                    out.bloom_filter = std::make_unique<StringBloomFilter>(
                            idx.bloom_filter_size, idx.bloom_filter_hashes, idx.seed);

                    const auto & str = value.get<String>();
                    stringToBloomFilter(str.c_str(), str.size(), idx.token_extractor_func, *out.bloom_filter);
                    return true;
                }
        },
        {
                "equals",
                [] (RPNElement & out, const Field & value, const MergeTreeBloomFilterIndex & idx)
                {
                    out.function = RPNElement::FUNCTION_EQUALS;
                    out.bloom_filter = std::make_unique<StringBloomFilter>(
                            idx.bloom_filter_size, idx.bloom_filter_hashes, idx.seed);

                    const auto & str = value.get<String>();
                    stringToBloomFilter(str.c_str(), str.size(), idx.token_extractor_func, *out.bloom_filter);
                    return true;
                }
        },
        {
                "like",
                [] (RPNElement & out, const Field & value, const MergeTreeBloomFilterIndex & idx)
                {
                    out.function = RPNElement::FUNCTION_LIKE;
                    out.bloom_filter = std::make_unique<StringBloomFilter>(
                            idx.bloom_filter_size, idx.bloom_filter_hashes, idx.seed);

                    const auto & str = value.get<String>();
                    likeStringToBloomFilter(str, idx.token_extractor_func, *out.bloom_filter);
                    return true;
                }
        },
        {
                "notIn",
                [] (RPNElement & out, const Field &, const MergeTreeBloomFilterIndex &)
                {
                    out.function = RPNElement::FUNCTION_NOT_IN;
                    return true;
                }
        },
        {
                "in",
                [] (RPNElement & out, const Field &, const MergeTreeBloomFilterIndex &)
                {
                    out.function = RPNElement::FUNCTION_IN;
                    return true;
                }
        },
};

BloomFilterCondition::BloomFilterCondition(
    const SelectQueryInfo & query_info,
    const Context & context,
    const MergeTreeBloomFilterIndex & index_) : index(index_), prepared_sets(query_info.sets)
{
    /// Do preparation similar to KeyCondition.
    Block block_with_constants = KeyCondition::getBlockWithConstants(
            query_info.query, query_info.syntax_analyzer_result, context);

    const ASTSelectQuery & select = typeid_cast<const ASTSelectQuery &>(*query_info.query);
    if (select.where_expression)
    {
        traverseAST(select.where_expression, context, block_with_constants);

        if (select.prewhere_expression)
        {
            traverseAST(select.prewhere_expression, context, block_with_constants);
            rpn.emplace_back(RPNElement::FUNCTION_AND);
        }
    }
    else if (select.prewhere_expression)
    {
        traverseAST(select.prewhere_expression, context, block_with_constants);
    }
    else
    {
        rpn.emplace_back(RPNElement::FUNCTION_UNKNOWN);
    }

    auto * log = &Poco::Logger::get("bf");
    for (size_t i = 0; i < rpn.size(); ++i) {
        if (rpn[i].bloom_filter)
            LOG_DEBUG(log, ": " << rpn[i].function << " " << rpn[i].key_column << " " << rpn[i].bloom_filter->getFingerPrint());
        else
            LOG_DEBUG(log, ": " << rpn[i].function << " " << rpn[i].key_column << " " << "empty");
    }
}

bool BloomFilterCondition::alwaysUnknownOrTrue() const
{
    /// Check like in KeyCondition.
    std::vector<bool> rpn_stack;

    for (const auto & element : rpn)
    {
        if (element.function == RPNElement::FUNCTION_UNKNOWN
            || element.function == RPNElement::ALWAYS_TRUE)
        {
            rpn_stack.push_back(true);
        }
        else if (element.function == RPNElement::FUNCTION_EQUALS
             || element.function == RPNElement::FUNCTION_NOT_EQUALS
             || element.function == RPNElement::FUNCTION_LIKE
             || element.function == RPNElement::FUNCTION_NOT_LIKE
             || element.function == RPNElement::FUNCTION_IN
             || element.function == RPNElement::FUNCTION_NOT_IN
             || element.function == RPNElement::ALWAYS_FALSE)
        {
            rpn_stack.push_back(false);
        }
        else if (element.function == RPNElement::FUNCTION_NOT)
        {
            // do nothing
        }
        else if (element.function == RPNElement::FUNCTION_AND)
        {
            auto arg1 = rpn_stack.back();
            rpn_stack.pop_back();
            auto arg2 = rpn_stack.back();
            rpn_stack.back() = arg1 && arg2;
        }
        else if (element.function == RPNElement::FUNCTION_OR)
        {
            auto arg1 = rpn_stack.back();
            rpn_stack.pop_back();
            auto arg2 = rpn_stack.back();
            rpn_stack.back() = arg1 || arg2;
        }
        else
            throw Exception("Unexpected function type in KeyCondition::RPNElement", ErrorCodes::LOGICAL_ERROR);
    }

    return rpn_stack[0];
}

bool BloomFilterCondition::mayBeTrueOnGranule(MergeTreeIndexGranulePtr idx_granule) const
{
    std::shared_ptr<MergeTreeBloomFilterIndexGranule> granule
            = std::dynamic_pointer_cast<MergeTreeBloomFilterIndexGranule>(idx_granule);
    if (!granule)
        throw Exception(
                "BloomFilter index condition got a granule with the wrong type.", ErrorCodes::LOGICAL_ERROR);

    /// Check like in KeyCondition.
    std::vector<BoolMask> rpn_stack;
    for (const auto & element : rpn)
    {
        if (element.function == RPNElement::FUNCTION_UNKNOWN)
        {
            rpn_stack.emplace_back(true, true);
        }
        else if (element.function == RPNElement::FUNCTION_EQUALS
             || element.function == RPNElement::FUNCTION_NOT_EQUALS)
        {
            rpn_stack.emplace_back(
                    granule->bloom_filters[element.key_column].contains(*element.bloom_filter), true);

            if (element.function == RPNElement::FUNCTION_NOT_EQUALS)
                rpn_stack.back() = !rpn_stack.back();
        }
        else if (element.function == RPNElement::FUNCTION_LIKE
             || element.function == RPNElement::FUNCTION_NOT_LIKE)
        {
            rpn_stack.emplace_back(
                    granule->bloom_filters[element.key_column].contains(*element.bloom_filter), true);

            if (element.function == RPNElement::FUNCTION_NOT_LIKE)
                rpn_stack.back() = !rpn_stack.back();
        }
        else if (element.function == RPNElement::FUNCTION_IN
                 || element.function == RPNElement::FUNCTION_NOT_IN)
        {
            rpn_stack.emplace_back(true, true);
            auto *log = &Poco::Logger::get("bf");
            LOG_DEBUG(log, "IN FUNC SIZE:" << element.set_mapping.size());

            for (size_t i = 0; i < element.set_mapping.size(); ++i)
            {
                size_t key_idx = element.set_mapping[i].key_index;
                size_t tuple_idx = element.set_mapping[i].tuple_index;

                LOG_DEBUG(log, "IN FUNC:" << element.set_bloom_filters[tuple_idx].getFingerPrint() << " " << element.set_bloom_filters[tuple_idx].getSum());

                if (!granule->bloom_filters[key_idx].contains(element.set_bloom_filters[tuple_idx]))
                {
                    rpn_stack.back().can_be_true = false;
                    break;
                }
            }

            if (element.function == RPNElement::FUNCTION_NOT_IN)
                rpn_stack.back() = !rpn_stack.back();
        }
        else if (element.function == RPNElement::FUNCTION_NOT)
        {
            rpn_stack.back() = !rpn_stack.back();
        }
        else if (element.function == RPNElement::FUNCTION_AND)
        {
            auto arg1 = rpn_stack.back();
            rpn_stack.pop_back();
            auto arg2 = rpn_stack.back();
            rpn_stack.back() = arg1 & arg2;
        }
        else if (element.function == RPNElement::FUNCTION_OR)
        {
            auto arg1 = rpn_stack.back();
            rpn_stack.pop_back();
            auto arg2 = rpn_stack.back();
            rpn_stack.back() = arg1 | arg2;
        }
        else if (element.function == RPNElement::ALWAYS_FALSE)
        {
            rpn_stack.emplace_back(false, true);
        }
        else if (element.function == RPNElement::ALWAYS_TRUE)
        {
            rpn_stack.emplace_back(true, false);
        }
        else
            throw Exception("Unexpected function type in KeyCondition::RPNElement", ErrorCodes::LOGICAL_ERROR);
    }

    if (rpn_stack.size() != 1)
        throw Exception("Unexpected stack size in KeyCondition::mayBeTrueInRange", ErrorCodes::LOGICAL_ERROR);

    return rpn_stack[0].can_be_true;
}

void BloomFilterCondition::traverseAST(
    const ASTPtr & node, const Context & context, Block & block_with_constants)
{
    /// The same as in KeyCondition.
    RPNElement element;

    if (auto * func = typeid_cast<ASTFunction *>(&*node))
    {
        if (operatorFromAST(func, element))
        {
            auto & args = typeid_cast<ASTExpressionList &>(*func->arguments).children;
            for (size_t i = 0, size = args.size(); i < size; ++i)
            {
                traverseAST(args[i], context, block_with_constants);

                if (i != 0 || element.function == RPNElement::FUNCTION_NOT)
                    rpn.emplace_back(std::move(element));
            }

            return;
        }
    }

    if (!atomFromAST(node, context, block_with_constants, element))
    {
        element.function = RPNElement::FUNCTION_UNKNOWN;
    }

    rpn.emplace_back(std::move(element));
}

bool BloomFilterCondition::getKey(const ASTPtr & node, size_t & key_column_num)
{
    auto it = std::find(index.columns.begin(), index.columns.end(), node->getColumnName());
    if (it == index.columns.end())
        return false;

    key_column_num = static_cast<size_t>(it - index.columns.begin());
    return true;
}

bool BloomFilterCondition::atomFromAST(
    const ASTPtr & node, const Context & context, Block & block_with_constants, RPNElement & out)
{
    Field const_value;
    DataTypePtr const_type;
    if (const auto * func = typeid_cast<const ASTFunction *>(node.get()))
    {
        const ASTs & args = typeid_cast<const ASTExpressionList &>(*func->arguments).children;

        if (args.size() != 2)
            return false;

        size_t key_arg_pos;           /// Position of argument with key column (non-const argument)
        size_t key_column_num = -1;   /// Number of a key column (inside key_column_names array)

        if (functionIsInOrGlobalInOperator(func->name) && tryPrepareSetBloomFilter(args, context, out))
        {
            auto *log = &Poco::Logger::get("atom");
            LOG_DEBUG(log, "bf: in0000000000000");
            key_arg_pos = 0;
        }
        else if (KeyCondition::getConstant(args[1], block_with_constants, const_value, const_type) && getKey(args[0], key_column_num))
        {
            key_arg_pos = 0;
        }
        else if (KeyCondition::getConstant(args[0], block_with_constants, const_value, const_type) && getKey(args[1], key_column_num))
        {
            key_arg_pos = 1;
        }
        else
            return false;

        if (const_type->getTypeId() != TypeIndex::String && const_type->getTypeId() != TypeIndex::FixedString)
            return false;

        if (key_arg_pos == 1 && (func->name != "equals" || func->name != "notEquals"))
            return false;
        else
            key_arg_pos = 0;

        auto *log = &Poco::Logger::get("atom");
        LOG_DEBUG(log, "bf: name:"<< func->name);
        const auto atom_it = atom_map.find(func->name);
        if (atom_it == std::end(atom_map))
            return false;

        out.key_column = key_column_num;
        return atom_it->second(out, const_value, index);
    }
    else if (KeyCondition::getConstant(node, block_with_constants, const_value, const_type))
    {
        /// Check constant like in KeyCondition
        if (const_value.getType() == Field::Types::UInt64
        || const_value.getType() == Field::Types::Int64
        || const_value.getType() == Field::Types::Float64)
        {
            /// Zero in all types is represented in memory the same way as in UInt64.
            out.function = const_value.get<UInt64>()
                           ? RPNElement::ALWAYS_TRUE
                           : RPNElement::ALWAYS_FALSE;

            return true;
        }
    }

    return false;
}

bool BloomFilterCondition::operatorFromAST(
    const ASTFunction * func, RPNElement & out)
{
    /// The same as in KeyCondition.
    const ASTs & args = typeid_cast<const ASTExpressionList &>(*func->arguments).children;

    if (func->name == "not")
    {
        if (args.size() != 1)
            return false;

        out.function = RPNElement::FUNCTION_NOT;
    }
    else
    {
        if (func->name == "and" || func->name == "indexHint")
            out.function = RPNElement::FUNCTION_AND;
        else if (func->name == "or")
            out.function = RPNElement::FUNCTION_OR;
        else
            return false;
    }

    return true;
}

bool BloomFilterCondition::tryPrepareSetBloomFilter(
    const ASTs & args,
    const Context &,
    RPNElement & out)
{
    auto *log = &Poco::Logger::get("atom");
    LOG_DEBUG(log, "bf: tryPrepareSetBloomFilter");
    const ASTPtr & left_arg = args[0];
    const ASTPtr & right_arg = args[1];

    std::vector<KeyTuplePositionMapping> key_tuple_mapping;
    DataTypes data_types;

    const auto * left_arg_tuple = typeid_cast<const ASTFunction *>(left_arg.get());
    if (left_arg_tuple && left_arg_tuple->name == "tuple")
    {
        const auto & tuple_elements = left_arg_tuple->arguments->children;
        for (size_t i = 0; i < tuple_elements.size(); ++i)
        {
            size_t key = 0;
            if (getKey(tuple_elements[i], key))
            {
                key_tuple_mapping.emplace_back(i, key);
                data_types.push_back(index.data_types[key]);
            }
        }
    }
    else
    {
        size_t key = 0;
        if (getKey(left_arg, key))
        {
            key_tuple_mapping.emplace_back(-1, key);
            data_types.push_back(index.data_types[key]);
        }
    }

    if (key_tuple_mapping.empty())
        return false;
    LOG_DEBUG(log, "bf: key_tuple_mapping");

    PreparedSetKey set_key;
    if (typeid_cast<const ASTSubquery *>(right_arg.get()) || typeid_cast<const ASTIdentifier *>(right_arg.get()))
        set_key = PreparedSetKey::forSubquery(*right_arg);
    else
        set_key = PreparedSetKey::forLiteral(*right_arg, data_types);

    auto set_it = prepared_sets.find(set_key);
    if (set_it == prepared_sets.end())
        return false;
    LOG_DEBUG(log, "bf: set_it");

    const SetPtr & prepared_set = set_it->second;
    if (!prepared_set->hasExplicitSetElements())
        return false;
    LOG_DEBUG(log, "bf: hasExplicitSetElements");

    std::vector<StringBloomFilter> bloom_filters;

    const auto & columns = prepared_set->getSetElements();
    for (size_t col = 0; col < key_tuple_mapping.size(); ++col)
    {
        bloom_filters.emplace_back(index.bloom_filter_size, index.bloom_filter_hashes, index.seed);
        size_t tuple_idx = key_tuple_mapping[col].tuple_index;
        const auto & column = columns[tuple_idx];
        for (size_t row = 0; row < prepared_set->getTotalRowCount(); ++row)
        {
            auto ref = column->getDataAt(row);
            stringToBloomFilter(ref.data, ref.size, index.token_extractor_func, bloom_filters.back());
        }
    }

    out.set_mapping = std::move(key_tuple_mapping);
    out.set_bloom_filters = std::move(bloom_filters);

    return true;
}


MergeTreeIndexGranulePtr MergeTreeBloomFilterIndex::createIndexGranule() const
{
    return std::make_shared<MergeTreeBloomFilterIndexGranule>(*this);
}

IndexConditionPtr MergeTreeBloomFilterIndex::createIndexCondition(
        const SelectQueryInfo & query, const Context & context) const
{
    return std::make_shared<BloomFilterCondition>(query, context, *this);
};


bool NgramTokenExtractor::next(const char * data, size_t len, size_t * pos, size_t * token_start, size_t * token_len) const
{
    *token_start = *pos;
    *token_len = 0;
    size_t code_points = 0;
    for (; code_points < n && *token_start + *token_len < len; ++code_points)
    {
        size_t sz = UTF8::seqLength(static_cast<UInt8>(data[*token_start + *token_len]));
        *token_len += sz;
    }
    *pos += UTF8::seqLength(static_cast<UInt8>(data[*pos]));
    return code_points == n;
}

bool NgramTokenExtractor::nextLike(const String & str, size_t * pos, String & token) const
{
    token.clear();

    size_t code_points = 0;
    bool escaped = false;
    for (size_t i = *pos; i < str.size();)
    {
        if (escaped && (str[i] == '%' || str[i] == '_' || str[i] == '\\'))
        {
            token += str[i];
            ++code_points;
            escaped = false;
            ++i;
        }
        else if (!escaped && (str[i] == '%' || str[i] == '_'))
        {
            /// This token is too small, go to the next.
            token.clear();
            code_points = 0;
            escaped = false;
            *pos = ++i;
        }
        else if (!escaped && str[i] == '\\')
        {
            escaped = true;
            ++i;
        }
        else
        {
            const size_t sz = UTF8::seqLength(static_cast<UInt8>(str[i]));
            for (size_t j = 0; j < sz; ++j)
                token += str[i + j];
            i += sz;
            ++code_points;
            escaped = false;
        }

        if (code_points == n) {
            *pos += UTF8::seqLength(static_cast<UInt8>(str[*pos]));
            return true;
        }
    }

    return false;
}

std::unique_ptr<IMergeTreeIndex> bloomFilterIndexCreator(
    const NamesAndTypesList & new_columns,
    std::shared_ptr<ASTIndexDeclaration> node,
    const MergeTreeData & /* data */,
    const Context & context)
{
    if (node->name.empty())
        throw Exception("Index must have unique name", ErrorCodes::INCORRECT_QUERY);

    ASTPtr expr_list = MergeTreeData::extractKeyExpressionList(node->expr->clone());

    auto syntax = SyntaxAnalyzer(context, {}).analyze(
            expr_list, new_columns);
    auto index_expr = ExpressionAnalyzer(expr_list, syntax, context).getActions(false);

    auto sample = ExpressionAnalyzer(expr_list, syntax, context)
            .getActions(true)->getSampleBlock();

    Names columns;
    DataTypes data_types;

    for (size_t i = 0; i < expr_list->children.size(); ++i)
    {
        const auto & column = sample.getByPosition(i);

        columns.emplace_back(column.name);
        data_types.emplace_back(column.type);

        if (data_types.back()->getTypeId() != TypeIndex::String
            && data_types.back()->getTypeId() != TypeIndex::FixedString)
            throw Exception("Bloom filter index can be used only with `String` or `FixedString` column.", ErrorCodes::INCORRECT_QUERY);
    }

    boost::algorithm::to_lower(node->type->name);
    if (node->type->name == NgramTokenExtractor::getName()) {
        if (!node->type->arguments || node->type->arguments->children.size() != 4)
            throw Exception("`ngrambf` index must have exactly 4 arguments.", ErrorCodes::INCORRECT_QUERY);

        size_t n = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[0]).value.get<size_t>();
        size_t bloom_filter_size = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[1]).value.get<size_t>();
        size_t bloom_filter_hashes = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[2]).value.get<size_t>();
        size_t seed = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[3]).value.get<size_t>();

        auto tokenizer = std::make_unique<NgramTokenExtractor>(n);

        return std::make_unique<MergeTreeBloomFilterIndex>(
                node->name, std::move(index_expr), columns, data_types, sample, node->granularity,
                bloom_filter_size, bloom_filter_hashes, seed, std::move(tokenizer));
    /*} else if (node->type->name == SplitTokenExtractor::getName()) {
        if (!node->type->arguments || node->type->arguments->children.size() != 2)
            throw Exception("`tokenbf` index must have exactly 2 arguments.", ErrorCodes::INCORRECT_QUERY);

        size_t bloom_filter_size = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[0]).value.get<size_t>();
        size_t seed = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[1]).value.get<size_t>();
        size_t bloom_filter_hashes = typeid_cast<const ASTLiteral &>(
                *node->type->arguments->children[2]).value.get<size_t>();

        auto tokenizer = std::make_unique<SplitTokenExtractor>();

        return std::make_unique<MergeTreeBloomFilterIndex>(
                node->name, std::move(index_expr), columns, data_types, sample, node->granularity,
                bloom_filter_size, bloom_filter_hashes, seed, std::move(tokenizer));*/
    } else {
        throw Exception("Unknown index type: `" + node->name + "`.", ErrorCodes::LOGICAL_ERROR);
    }
}

}
