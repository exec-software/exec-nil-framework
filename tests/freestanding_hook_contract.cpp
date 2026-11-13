// Link the actual freestanding archive into an owned hosted test. The default
// hook must resolve, and an application-supplied hook must replace it.
#include <cstdint>

extern "C" std::uint32_t nyrt_abi_version() noexcept;
extern "C" void __ny_host_fail(const char *) noexcept;
extern "C" void nyrt_heap_init(void *, std::uint64_t) noexcept;
extern "C" void *nyrt_alloc(std::uint64_t) noexcept;
extern "C" void *nyrt_mem_copy(void *, const void *, std::uint64_t) noexcept;
extern "C" std::uint64_t nyrt_str_length(const char *) noexcept;
extern "C" std::uint64_t nyrt_heap_used() noexcept;
extern "C" void nyrt_context_end() noexcept;

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
    alignas(16) unsigned char arena[128];
    for (auto &byte : arena) byte = 0xa5;
    nyrt_heap_init(arena, sizeof(arena));
    auto *first = static_cast<unsigned char *>(nyrt_alloc(33));
    if (first != arena || nyrt_heap_used() != 33) return 3;
    for (unsigned i = 0; i < 33; ++i) if (first[i] != 0) return 4;
    for (unsigned i = 33; i < sizeof(arena); ++i) if (arena[i] != 0xa5) return 5;
    const unsigned char source[] = {0, 0xff, 17, 0x80, 41};
    if (nyrt_mem_copy(first + 1, source, sizeof(source)) != first + 1) return 6;
    for (unsigned i = 0; i < sizeof(source); ++i) if (first[i + 1] != source[i]) return 7;
    if (first[0] || first[6]) return 8;
    if (nyrt_mem_copy(nullptr, nullptr, 0) != nullptr) return 9;
    if (nyrt_alloc(0) != arena + 48 || nyrt_heap_used() != 49 || arena[48]) return 10;
    if (nyrt_alloc(UINT64_MAX) != nullptr || nyrt_heap_used() != 49) return 11;
    if (nyrt_str_length(nullptr) || nyrt_str_length("") || nyrt_str_length("ab\0c") != 2) return 12;
    nyrt_context_end();
    if (nyrt_heap_used() || nyrt_alloc(128) != arena) return 13;
    for (auto byte : arena) if (byte) return 14;
    if (nyrt_alloc(1) != nullptr) return 15;
    nyrt_heap_init(nullptr, 128);
    if (nyrt_alloc(1) != nullptr) return 16;
    return 0;
}
