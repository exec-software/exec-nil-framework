// Link the actual freestanding archive into an owned hosted test. The default
// hook must resolve, and an application-supplied hook must replace it.
#include <cstdint>

extern "C" std::uint32_t nyrt_abi_version() noexcept;
extern "C" void __ny_host_fail(const char *) noexcept;

#if defined(NIL_TEST_HOST_OVERRIDE)
static const char *observed = nullptr;
extern "C" void __ny_host_fail(const char *message) noexcept { observed = message; }
#endif

int main() {
    // Require the runtime object even in the override case, so a duplicate
    // strong definition cannot escape the link check through archive laziness.
    if (nyrt_abi_version() != 1) return 1;
    static const char message[] = "owned failure hook probe";
    __ny_host_fail(message);
#if defined(NIL_TEST_HOST_OVERRIDE)
    if (observed != message) return 2;
#endif
    return 0;
}
