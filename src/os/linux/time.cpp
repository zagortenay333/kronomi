#include <time.h>
#include <unistd.h>

Void os_sleep_ms (U64 msec) {
    struct timespec ts_sleep = { static_cast<I64>(msec/1000), static_cast<I64>((msec % 1000) * 1000000) };
    nanosleep(&ts_sleep, NULL);
}

U64 os_time_ms () {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<U64>((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));
}
