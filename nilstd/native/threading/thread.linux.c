#include <unistd.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;

void ml_thread_sleep(i64 ms) {
    if (ms >= 1000) {
        sleep(ms / 1000);
        ms %= 1000;
    }
    usleep((useconds_t)(ms * 1000));
}

void ml_thread_yield() {
    sched_yield();
}

i32 ml_thread_cpu_count() {
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    if (nprocs < 1) return 1;
    return (i32)nprocs;
}
