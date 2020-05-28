#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <Core/Block.h>
#include <Storages/StorageInMemoryMetadata.h>
#include <Storages/MergeTree/MergeTreeDataPartChecksum.h>
#include <Storages/SelectQueryInfo.h>
#include <Storages/MergeTree/MarkRange.h>
#include <Interpreters/ExpressionActions.h>
#include <Parsers/ASTIndexDeclaration.h>
#include <DataTypes/DataTypeLowCardinality.h>

constexpr auto INDEX_FILE_PREFIX = "skp_idx_";

namespace DB
{

class MergeTreeData;
class IMergeTreeIndex;

using MergeTreeIndexPtr = std::shared_ptr<const IMergeTreeIndex>;
using MutableMergeTreeIndexPtr = std::shared_ptr<IMergeTreeIndex>;


/// Stores some info about a single block of data.
struct IMergeTreeIndexGranule
{
    virtual ~IMergeTreeIndexGranule() = default;

    virtual void serializeBinary(WriteBuffer & ostr) const = 0;
    virtual void deserializeBinary(ReadBuffer & istr) = 0;

    virtual bool empty() const = 0;
};

using MergeTreeIndexGranulePtr = std::shared_ptr<IMergeTreeIndexGranule>;
using MergeTreeIndexGranules = std::vector<MergeTreeIndexGranulePtr>;


/// Aggregates info about a single block of data.
struct IMergeTreeIndexAggregator
{
    virtual ~IMergeTreeIndexAggregator() = default;

    virtual bool empty() const = 0;
    virtual MergeTreeIndexGranulePtr getGranuleAndReset() = 0;

    /// Updates the stored info using rows of the specified block.
    /// Reads no more than `limit` rows.
    /// After finishing updating `pos` will store the position of the first row which was not read.
    virtual void update(const Block & block, size_t * pos, size_t limit) = 0;
};

using MergeTreeIndexAggregatorPtr = std::shared_ptr<IMergeTreeIndexAggregator>;
using MergeTreeIndexAggregators = std::vector<MergeTreeIndexAggregatorPtr>;


/// Condition on the index.
class IMergeTreeIndexCondition
{
public:
    virtual ~IMergeTreeIndexCondition() = default;
    /// Checks if this index is useful for query.
    virtual bool alwaysUnknownOrTrue() const = 0;

    virtual bool mayBeTrueOnGranule(MergeTreeIndexGranulePtr granule) const = 0;
};

using MergeTreeIndexConditionPtr = std::shared_ptr<IMergeTreeIndexCondition>;


/// Structure for storing basic index info like columns, expression, arguments, ...
class IMergeTreeIndex
{
public:
    IMergeTreeIndex(const StorageMetadataSkipIndexField & index_)
        : index(index_)
    {
    }

    virtual ~IMergeTreeIndex() = default;

    /// gets filename without extension
    String getFileName() const { return INDEX_FILE_PREFIX + index.name; }

    /// Checks whether the column is in data skipping index.
    virtual bool mayBenefitFromIndexForIn(const ASTPtr & node) const = 0;

    virtual MergeTreeIndexGranulePtr createIndexGranule() const = 0;

    virtual MergeTreeIndexAggregatorPtr createIndexAggregator() const = 0;

    virtual MergeTreeIndexConditionPtr createIndexCondition(
            const SelectQueryInfo & query_info, const Context & context) const = 0;

    Names getColumnsRequiredForIndexCalc() const { return index.expression->getRequiredColumns(); }

    const StorageMetadataSkipIndexField & index;
};

using MergeTreeIndices = std::vector<MergeTreeIndexPtr>;


class MergeTreeIndexFactory : private boost::noncopyable
{
public:
    static MergeTreeIndexFactory & instance();

    using Creator = std::function<
            std::shared_ptr<IMergeTreeIndex>(
                    const StorageMetadataSkipIndexField & index)>;

    using Validator = std::function<void(const StorageMetadataSkipIndexField & index, bool attach)>;

    void validate(const StorageMetadataSkipIndexField & index, bool attach) const;

    std::shared_ptr<IMergeTreeIndex> get(const StorageMetadataSkipIndexField & index) const;

    MergeTreeIndices getMany(const std::vector<StorageMetadataSkipIndexField> & indices) const;

    void registerCreator(const std::string & index_type, Creator creator);
    void registerValidator(const std::string & index_type, Validator creator);

protected:
    MergeTreeIndexFactory();

private:
    using Creators = std::unordered_map<std::string, Creator>;
    using Validators = std::unordered_map<std::string, Validator>;
    Creators creators;
    Validators validators;
};

std::shared_ptr<IMergeTreeIndex> minmaxIndexCreator(
    const StorageMetadataSkipIndexField & index);
void minmaxIndexValidator(const StorageMetadataSkipIndexField & index, bool attach);


std::shared_ptr<IMergeTreeIndex> setIndexCreator(
    const StorageMetadataSkipIndexField & index);
void setIndexValidator(const StorageMetadataSkipIndexField & index, bool attach);

std::shared_ptr<IMergeTreeIndex> bloomFilterIndexCreator(
    const StorageMetadataSkipIndexField & index);

void bloomFilterIndexValidator(const StorageMetadataSkipIndexField & index, bool attach);


std::shared_ptr<IMergeTreeIndex> bloomFilterIndexCreatorNew(
    const StorageMetadataSkipIndexField & index);
void bloomFilterIndexValidatorNew(const StorageMetadataSkipIndexField & index, bool attach);
}
