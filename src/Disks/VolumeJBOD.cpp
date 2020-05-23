#include "VolumeJBOD.h"

#include <Common/StringUtils/StringUtils.h>
#include <Common/formatReadable.h>
#include <Common/quoteString.h>
#include <common/logger_useful.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int EXCESSIVE_ELEMENT_IN_CONFIG;
}

VolumeJBOD::VolumeJBOD(
    String name_,
    const Poco::Util::AbstractConfiguration & config,
    const String & config_prefix,
    DiskSelectorPtr disk_selector
) : IVolume(name_, config, config_prefix, disk_selector)
{
    Logger * logger = &Logger::get("StorageConfiguration");

    auto has_max_bytes = config.has(config_prefix + ".max_data_part_size_bytes");
    auto has_max_ratio = config.has(config_prefix + ".max_data_part_size_ratio");
    if (has_max_bytes && has_max_ratio)
        throw Exception(
            "Only one of 'max_data_part_size_bytes' and 'max_data_part_size_ratio' should be specified.",
            ErrorCodes::EXCESSIVE_ELEMENT_IN_CONFIG);

    if (has_max_bytes)
    {
        max_data_part_size = config.getUInt64(config_prefix + ".max_data_part_size_bytes", 0);
    }
    else if (has_max_ratio)
    {
        auto ratio = config.getDouble(config_prefix + ".max_data_part_size_ratio");
        if (ratio < 0)
            throw Exception("'max_data_part_size_ratio' have to be not less then 0.", ErrorCodes::EXCESSIVE_ELEMENT_IN_CONFIG);
        UInt64 sum_size = 0;
        std::vector<UInt64> sizes;
        for (const auto & disk : disks)
        {
            sizes.push_back(disk->getTotalSpace());
            sum_size += sizes.back();
        }
        max_data_part_size = static_cast<decltype(max_data_part_size)>(sum_size * ratio / disks.size());
        for (size_t i = 0; i < disks.size(); ++i)
            if (sizes[i] < max_data_part_size)
                LOG_WARNING(                     logger,                     "Disk " << backQuote(disks[i]->getName()) << " on volume " << backQuote(config_prefix) << " have not enough space ("                             << formatReadableSizeWithBinarySuffix(sizes[i]) << ") for containing part the size of max_data_part_size ("                             << formatReadableSizeWithBinarySuffix(max_data_part_size) << ")");
    }
    static constexpr UInt64 MIN_PART_SIZE = 8u * 1024u * 1024u;
    if (max_data_part_size != 0 && max_data_part_size < MIN_PART_SIZE)
        LOG_WARNING(             logger,             "Volume " << backQuote(name) << " max_data_part_size is too low (" << formatReadableSizeWithBinarySuffix(max_data_part_size)                       << " < " << formatReadableSizeWithBinarySuffix(MIN_PART_SIZE) << ")");
}

DiskPtr VolumeJBOD::getNextDisk()
{
    size_t start_from = last_used.fetch_add(1u, std::memory_order_relaxed);
    size_t index = start_from % disks.size();
    return disks[index];
}

ReservationPtr VolumeJBOD::reserve(UInt64 bytes)
{
    /// This volume can not store files which size greater than max_data_part_size

    if (max_data_part_size != 0 && bytes > max_data_part_size)
        return {};

    size_t start_from = last_used.fetch_add(1u, std::memory_order_relaxed);
    size_t disks_num = disks.size();
    for (size_t i = 0; i < disks_num; ++i)
    {
        size_t index = (start_from + i) % disks_num;

        auto reservation = disks[index]->reserve(bytes);

        if (reservation)
            return reservation;
    }
    return {};
}

}
