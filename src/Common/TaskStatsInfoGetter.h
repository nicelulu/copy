#pragma once

#include <sys/types.h>
#include <common/types.h>
#include <boost/noncopyable.hpp>

struct taskstats;

namespace RK
{

/// Get taskstat info from OS kernel via Netlink protocol.
class TaskStatsInfoGetter : private boost::noncopyable
{
public:
    TaskStatsInfoGetter();
    ~TaskStatsInfoGetter();

    void getStat(::taskstats & out_stats, pid_t tid) const;

    /// Whether the current process has permissions (sudo or cap_net_admin capabilities) to get taskstats info
    static bool checkPermissions();

#if defined(OS_LINUX)
private:
    int netlink_socket_fd = -1;
    UInt16 taskstats_family_id = 0;
#endif
};

}
