#include <windows.h>
#include <stdint.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;

void ml_thread_sleep(i64 ms) {
    Sleep((DWORD)ms);
}

void ml_thread_yield() {
    SwitchToThread();
}

i32 ml_thread_cpu_count() {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (i32)sysinfo.dwNumberOfProcessors;
}
