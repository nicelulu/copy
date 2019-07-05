#include "common/Sleep.h"

#include <time.h>
#include <errno.h>

/**
  * Sleep with nanoseconds precision
  *
  * In case query profiler is turned on, all threads spawned for
  * query execution are repeatedly interrupted by signals from timer.
  * Functions for relative sleep (sleep(3), nanosleep(2), etc.) have
  * problems in this setup and man page for nanosleep(2) suggests
  * using absolute deadlines, for instance clock_nanosleep(2).
  */
void SleepForNanoseconds(uint64_t nanoseconds)
{
    const auto clock_type = CLOCK_REALTIME;

    struct timespec current_time;
    clock_gettime(clock_type, &current_time);

    const uint64_t resolution = 1'000'000'000;
    struct timespec finish_time = current_time;

    finish_time.tv_nsec += nanoseconds % resolution;
    const uint64_t extra_second = finish_time.tv_nsec / resolution;
    finish_time.tv_nsec %= resolution;

    finish_time.tv_sec += (nanoseconds / resolution) + extra_second;

    while (clock_nanosleep(clock_type, TIMER_ABSTIME, &finish_time, nullptr) == EINTR);
}

void SleepForMicroseconds(uint64_t microseconds)
{
    SleepForNanoseconds(microseconds * 1000);
}

void SleepForMilliseconds(uint64_t milliseconds)
{
    SleepForMicroseconds(milliseconds * 1000);
}

void SleepForSeconds(uint64_t seconds)
{
    SleepForMilliseconds(seconds * 1000);
}
