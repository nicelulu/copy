#include "VolumeRAID1.h"

#include <Common/StringUtils/StringUtils.h>
#include <Common/quoteString.h>

namespace DB
{

ReservationPtr VolumeRAID1::reserve(UInt64 bytes)
{
    /// This volume can not store files which size greater than max_data_part_size

    if (max_data_part_size != 0 && bytes > max_data_part_size)
        return {};

    Reservations res(disks.size());
    for (size_t i = 0; i < disks.size(); ++i)
    {
        res[i] = disks[i]->reserve(bytes);

        if (!res[i])
            return {};
    }
    return std::make_unique<MultiDiskReservation>(res, bytes);
}

}
