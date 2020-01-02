#include "SSDCacheDictionary.h"

#include <Columns/ColumnsNumber.h>
#include <Common/typeid_cast.h>
#include <Common/ProfileEvents.h>
#include <Common/ProfilingScopedRWLock.h>
#include <DataStreams/IBlockInputStream.h>
#include <ext/chrono_io.h>
#include <ext/map.h>
#include <ext/range.h>
#include <ext/size.h>

namespace ProfileEvents
{
    extern const Event DictCacheKeysRequested;
    extern const Event DictCacheKeysRequestedMiss;
    extern const Event DictCacheKeysRequestedFound;
    extern const Event DictCacheKeysExpired;
    extern const Event DictCacheKeysNotFound;
    extern const Event DictCacheKeysHit;
    extern const Event DictCacheRequestTimeNs;
    extern const Event DictCacheRequests;
    extern const Event DictCacheLockWriteNs;
    extern const Event DictCacheLockReadNs;
}

namespace CurrentMetrics
{
    extern const Metric DictCacheRequests;
}

namespace DB
{

namespace ErrorCodes
{
    extern const int TYPE_MISMATCH;
    extern const int BAD_ARGUMENTS;
    extern const int UNSUPPORTED_METHOD;
    extern const int LOGICAL_ERROR;
    extern const int TOO_SMALL_BUFFER_SIZE;
}

CachePartition::CachePartition(CacheStorage & storage_, const size_t file_id_, const size_t max_size_, const size_t buffer_size_)
    : storage(storage_), file_id(file_id_), max_size(max_size_), buffer_size(buffer_size_)
{
}

void CachePartition::appendBlock(const Block & block)
{
    size_t bytes = 0;
    const auto new_columns = block.getColumns();
    if (new_columns.size() != header.columns())
    {
        throw Exception("Wrong size of block in BlockFile::appendBlock(). It's a bug.", ErrorCodes::TYPE_MISMATCH);
    }

    const auto id_column = typeid_cast<const ColumnUInt64 *>(new_columns.front().get());
    if (!id_column)
        throw Exception{"id column has type different from UInt64.", ErrorCodes::TYPE_MISMATCH};

    size_t start_size = buffer.front()->size();
    for (size_t i = 0; i < header.columns(); ++i)
    {
        buffer[i]->insertRangeFrom(*new_columns[i], 0, new_columns[i]->size());
        bytes += buffer[i]->byteSize();
    }

    const auto & ids = id_column->getData();
    for (size_t i = 0; i < new_columns.size(); ++i)
    {
        key_to_file_offset[ids[i]] = start_size + i;
    }

    if (bytes >= buffer_size)
    {
        flush();
    }
}

void CachePartition::flush()
{
    const auto id_column = typeid_cast<const ColumnUInt64 *>(buffer.front().get());
    if (!id_column)
        throw Exception{"id column has type different from UInt64.", ErrorCodes::TYPE_MISMATCH};
    const auto & ids = id_column->getData();

    key_to_file_offset[ids[0]] = out_file.getPositionInFile() + (1ULL << FILE_OFFSET_SIZE);
    size_t prev_size = 0;
    for (size_t row = 0; row < buffer.front()->size(); ++row)
    {
        key_to_file_offset[ids[row]] = key_to_file_offset[ids[row ? row - 1 : 0]] + prev_size;
        prev_size = 0;
        for (size_t col = 0; col < header.columns(); ++col)
        {
            const auto & column = buffer[col];
            const auto & type = header.getByPosition(col).type;
            type->serializeBinary(*column, row, out_file);
            if (type->getTypeId() != TypeIndex::String) {
                prev_size += column->sizeOfValueIfFixed();
            } else {
                prev_size += column->getDataAt(row).size + sizeof(UInt64);
            }
        }
    }

    if (out_file.hasPendingData()) {
        out_file.sync();
    }

    buffer = header.cloneEmptyColumns();
}

template <typename PresentIdHandler, typename AbsentIdHandler>
std::exception_ptr CacheStorage::update(DictionarySourcePtr & source_ptr, const std::vector<Key> & requested_ids,
        PresentIdHandler && on_updated, AbsentIdHandler && on_id_not_found)
{
    CurrentMetrics::Increment metric_increment{CurrentMetrics::DictCacheRequests};
    ProfileEvents::increment(ProfileEvents::DictCacheKeysRequested, requested_ids.size());

    std::unordered_map<Key, UInt8> remaining_ids{requested_ids.size()};
    for (const auto id : requested_ids)
        remaining_ids.insert({id, 0});

    const auto now = std::chrono::system_clock::now();

    const ProfilingScopedWriteRWLock write_lock{rw_lock, ProfileEvents::DictCacheLockWriteNs};

    if (now > backoff_end_time)
    {
        try
        {
            if (update_error_count)
            {
                /// Recover after error: we have to clone the source here because
                /// it could keep connections which should be reset after error.
                source_ptr = source_ptr->clone();
            }

            Stopwatch watch;
            auto stream = source_ptr->loadIds(requested_ids);
            stream->readPrefix();

            while (const auto block = stream->read())
            {
                const auto id_column = typeid_cast<const ColumnUInt64 *>(block.safeGetByPosition(0).column.get());
                if (!id_column)
                    throw Exception{"Id column has type different from UInt64.", ErrorCodes::TYPE_MISMATCH};

                const auto & ids = id_column->getData();

                /// cache column pointers
                const auto column_ptrs = ext::map<std::vector>(
                        ext::range(0, dictionary.getAttributes().size()),
                        [&block](size_t i) { return block.safeGetByPosition(i + 1).column.get(); });

                for (const auto i : ext::range(0, ids.size()))
                {
                    const auto id = ids[i];

                    on_updated(id, i, column_ptrs);
                    /// mark corresponding id as found
                    remaining_ids[id] = 1;
                }

                /// TODO: Add TTL to block
                partitions[0]->appendBlock(block);
            }

            stream->readSuffix();

            update_error_count = 0;
            last_update_exception = std::exception_ptr{};
            backoff_end_time = std::chrono::system_clock::time_point{};

            ProfileEvents::increment(ProfileEvents::DictCacheRequestTimeNs, watch.elapsed());
        }
        catch (...)
        {
            ++update_error_count;
            last_update_exception = std::current_exception();
            backoff_end_time = now + std::chrono::seconds(calculateDurationWithBackoff(rnd_engine, update_error_count));

            tryLogException(last_update_exception, log, "Could not update cache dictionary '" + dictionary.getName() +
                                                 "', next update is scheduled at " + ext::to_string(backoff_end_time));
        }
    }

    size_t not_found_num = 0, found_num = 0;

    /// Check which ids have not been found and require setting null_value
    auto mutable_columns = header.cloneEmptyColumns();
    for (const auto & id_found_pair : remaining_ids)
    {
        if (id_found_pair.second)
        {
            ++found_num;
            continue;
        }
        ++not_found_num;

        const auto id = id_found_pair.first;

        if (update_error_count)
        {
            /// TODO: юзать старые значения.

            /// We don't have expired data for that `id` so all we can do is to rethrow `last_exception`.
            std::rethrow_exception(last_update_exception);
        }

        /// TODO: Add TTL

        /// Set null_value for each attribute
        const auto & attributes = dictionary.getAttributes();
        for (size_t i = 0; i < attributes.size(); ++i)
        {
            const auto & attribute = attributes[i];
            mutable_columns[i].insert(attribute.null_value);
        }

        /// inform caller that the cell has not been found
        on_id_not_found(id);
    }
    partitions[0]->appendBlock(header.cloneWithColumns(std::move(mutable_columns)));

    ProfileEvents::increment(ProfileEvents::DictCacheKeysRequestedMiss, not_found_num);
    ProfileEvents::increment(ProfileEvents::DictCacheKeysRequestedFound, found_num);
    ProfileEvents::increment(ProfileEvents::DictCacheRequests);
}

SSDCacheDictionary::SSDCacheDictionary(
    const std::string & name_,
    const DictionaryStructure & dict_struct_,
    DictionarySourcePtr source_ptr_,
    const DictionaryLifetime dict_lifetime_,
    const std::string & path,
    const size_t partition_max_size)
    : name(name_)
    , dict_struct(dict_struct_)
    , source_ptr(std::move(source_ptr_))
    , dict_lifetime(dict_lifetime_)
    , storage(*this, path, 1, partition_max_size)
{
    if (!this->source_ptr->supportsSelectiveLoad())
        throw Exception{name + ": source cannot be used with CacheDictionary", ErrorCodes::UNSUPPORTED_METHOD};

    createAttributes();
}

#define DECLARE(TYPE) \
    void SSDCacheDictionary::get##TYPE( \
        const std::string & attribute_name, const PaddedPODArray<Key> & ids, ResultArrayType<TYPE> & out) const \
    { \
        const auto index = getAttributeIndex(attribute_name); \
        checkAttributeType(name, attribute_name, dict_struct.attributes[index].underlying_type, AttributeUnderlyingType::ut##TYPE); \
\
        const auto null_value = std::get<TYPE>(attributes[index].null_value); \
\
        getItemsNumberImpl<TYPE, TYPE>( \
            attribute_name, \
            ids, \
            out, \
            [&](const size_t) { return null_value; }); \
    }
    DECLARE(UInt8)
    DECLARE(UInt16)
    DECLARE(UInt32)
    DECLARE(UInt64)
    DECLARE(UInt128)
    DECLARE(Int8)
    DECLARE(Int16)
    DECLARE(Int32)
    DECLARE(Int64)
    DECLARE(Float32)
    DECLARE(Float64)
    DECLARE(Decimal32)
    DECLARE(Decimal64)
    DECLARE(Decimal128)
#undef DECLARE

#define DECLARE(TYPE) \
    void SSDCacheDictionary::get##TYPE( \
        const std::string & attribute_name, \
        const PaddedPODArray<Key> & ids, \
        const PaddedPODArray<TYPE> & def, \
        ResultArrayType<TYPE> & out) const \
    { \
        const auto index = getAttributeIndex(attribute_name); \
        checkAttributeType(name, attribute_name, dict_struct.attributes[index].underlying_type, AttributeUnderlyingType::ut##TYPE); \
\
        getItemsNumberImpl<TYPE, TYPE>( \
            attribute_name, \
            ids, \
            out, \
            [&](const size_t row) { return def[row]; }); \
    }
    DECLARE(UInt8)
    DECLARE(UInt16)
    DECLARE(UInt32)
    DECLARE(UInt64)
    DECLARE(UInt128)
    DECLARE(Int8)
    DECLARE(Int16)
    DECLARE(Int32)
    DECLARE(Int64)
    DECLARE(Float32)
    DECLARE(Float64)
    DECLARE(Decimal32)
    DECLARE(Decimal64)
    DECLARE(Decimal128)
#undef DECLARE

#define DECLARE(TYPE) \
    void SSDCacheDictionary::get##TYPE( \
        const std::string & attribute_name, \
        const PaddedPODArray<Key> & ids, \
        const TYPE def, \
        ResultArrayType<TYPE> & out) const \
    { \
        const auto index = getAttributeIndex(attribute_name); \
        checkAttributeType(name, attribute_name, dict_struct.attributes[index].underlying_type, AttributeUnderlyingType::ut##TYPE); \
\
        getItemsNumberImpl<TYPE, TYPE>( \
            attribute_name, \
            ids, \
            out, \
            [&](const size_t) { return def; }); \
    }
    DECLARE(UInt8)
    DECLARE(UInt16)
    DECLARE(UInt32)
    DECLARE(UInt64)
    DECLARE(UInt128)
    DECLARE(Int8)
    DECLARE(Int16)
    DECLARE(Int32)
    DECLARE(Int64)
    DECLARE(Float32)
    DECLARE(Float64)
    DECLARE(Decimal32)
    DECLARE(Decimal64)
    DECLARE(Decimal128)
#undef DECLARE

template <typename AttributeType, typename OutputType, typename DefaultGetter>
void SSDCacheDictionary::getItemsNumberImpl(
        const std::string & attribute_name, const PaddedPODArray<Key> & ids, ResultArrayType<OutputType> & out, DefaultGetter && get_default) const
{
    const auto attribute_index = getAttributeIndex(attribute_index);

    std::unordered_map<Key, std::vector<size_t>> not_found_ids;
    storage.getValue(attribute_name, ids, out, not_found_ids);
    if (not_found_ids.empty())
        return;

    std::vector<Key> required_ids(not_found_ids.size());
    std::transform(std::begin(not_found_ids), std::end(not_found_ids), std::begin(required_ids), [](auto & pair) { return pair.first; });

    storage.update(
            source_ptr,
            required_ids,
            [&](const auto id, const auto row, const auto & attributes)
            {
                for (const size_t row : not_found_ids[id])
                    out[row] = static_cast<OutputType>(attributes[attribute_index][row]);
            },
            [&](const auto id)
            {
                for (const size_t row : not_found_ids[id])
                    out[row] = get_default(row);
            });
}

void SSDCacheDictionary::getString(const std::string & attribute_name, const PaddedPODArray<Key> & ids, ColumnString * out) const
{
    auto & attribute = getAttribute(attribute_name);
    checkAttributeType(name, attribute_name, attribute.type, AttributeUnderlyingType::utString);

    const auto null_value = StringRef{std::get<String>(attribute.null_value)};

    getItemsString(attribute_name, ids, out, [&](const size_t) { return null_value; });
}

void SSDCacheDictionary::getString(
        const std::string & attribute_name, const PaddedPODArray<Key> & ids, const ColumnString * const def, ColumnString * const out) const
{
    auto & attribute = getAttribute(attribute_name);
    checkAttributeType(name, attribute_name, attribute.type, AttributeUnderlyingType::utString);

    getItemsString(attribute_name, ids, out, [&](const size_t row) { return def->getDataAt(row); });
}

void SSDCacheDictionary::getString(
        const std::string & attribute_name, const PaddedPODArray<Key> & ids, const String & def, ColumnString * const out) const
{
    auto & attribute = getAttribute(attribute_name);
    checkAttributeType(name, attribute_name, attribute.type, AttributeUnderlyingType::utString);

    getItemsString(attribute_name, ids, out, [&](const size_t) { return StringRef{def}; });
}

template <typename DefaultGetter>
void SSDCacheDictionary::getItemsString(const std::string & attribute_name, const PaddedPODArray<Key> & ids,
        ColumnString * out, DefaultGetter && get_default) const
{
    UNUSED(attribute_name);
    UNUSED(ids);
    UNUSED(out);
    UNUSED(get_default);
}

size_t SSDCacheDictionary::getAttributeIndex(const std::string & attr_name) const
{
    auto it = attribute_index_by_name.find(attr_name);
    if (it == std::end(attribute_index_by_name))
        throw  Exception{"Attribute `" + name + "` does not exist.", ErrorCodes::BAD_ARGUMENTS};
    return it->second;
}

SSDCacheDictionary::Attribute & SSDCacheDictionary::getAttribute(const std::string & attr_name)
{
    return attributes[getAttributeIndex(attr_name)];
}

const SSDCacheDictionary::Attribute & SSDCacheDictionary::getAttribute(const std::string & attr_name) const
{
    return attributes[getAttributeIndex(attr_name)];
}

const SSDCacheDictionary::Attributes & SSDCacheDictionary::getAttributes() const
{
    return attributes;
}

template <typename T>
SSDCacheDictionary::Attribute SSDCacheDictionary::createAttributeWithTypeImpl(const AttributeUnderlyingType type, const Field & null_value)
{
    Attribute attr{type, {}};
    attr.null_value = static_cast<T>(null_value.get<NearestFieldType<T>>());
    bytes_allocated += sizeof(T);
    return attr;
}

template <>
SSDCacheDictionary::Attribute SSDCacheDictionary::createAttributeWithTypeImpl<String>(const AttributeUnderlyingType type, const Field & null_value)
{
    Attribute attr{type, {}};
    attr.null_value = null_value.get<String>();
    bytes_allocated += sizeof(StringRef);
    //if (!string_arena)
    //    string_arena = std::make_unique<ArenaWithFreeLists>();
    return attr;
}

SSDCacheDictionary::Attribute SSDCacheDictionary::createAttributeWithType(const AttributeUnderlyingType type, const Field & null_value)
{
    switch (type)
    {
#define DISPATCH(TYPE) \
case AttributeUnderlyingType::ut##TYPE: \
    return createAttributeWithTypeImpl<TYPE>(type, null_value);

        DISPATCH(UInt8)
        DISPATCH(UInt16)
        DISPATCH(UInt32)
        DISPATCH(UInt64)
        DISPATCH(UInt128)
        DISPATCH(Int8)
        DISPATCH(Int16)
        DISPATCH(Int32)
        DISPATCH(Int64)
        DISPATCH(Decimal32)
        DISPATCH(Decimal64)
        DISPATCH(Decimal128)
        DISPATCH(Float32)
        DISPATCH(Float64)
        DISPATCH(String)
#undef DISPATCH
    }
}

void SSDCacheDictionary::createAttributes()
{
    attributes.resize(dict_struct.attributes.size());
    for (size_t i = 0; i < dict_struct.attributes.size(); ++i)
    {
        const auto & attribute = dict_struct.attributes[i];

        attribute_index_by_name.emplace(attribute.name, i);
        attributes.push_back(createAttributeWithType(attribute.type, attribute.null_value));

        if (attribute.hierarchical)
            throw Exception{name + ": hierarchical attributes not supported for dictionary of type " + getTypeName(),
                            ErrorCodes::TYPE_MISMATCH};
    }
}

}
