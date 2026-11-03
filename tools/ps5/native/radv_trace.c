// Trace points in RADV's start for the PS5 title (linked into the program's link only, with
// --wrap for each name: tools/ps5/build_native.sh lists them). The first console runs of the
// Vulkan title closed with nothing in the log after RADV's "invalid pci bus info" line, not
// even a crash report, so each step RADV takes after it across its own files, and any exit or
// abort, writes a line to stderr (game.log) before and after.
//
// Each wrapper passes on six integer arguments and returns an integer: the functions traced
// take only pointers and integers (six or fewer) and return a pointer, an integer or nothing,
// so the registers are passed through as they came.
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void say(const char* a, const char* b)
{
    write(2, a, strlen(a));
    write(2, b, strlen(b));
    write(2, "\n", 1);
}

static void say_hex(const char* a, uint64_t v)
{
    char line[64];
    char* p = line;
    size_t n = strlen(a);
    memcpy(p, a, n);
    p += n;
    *p++ = '0';
    *p++ = 'x';
    for (int s = 60; s >= 0; s -= 4) *p++ = "0123456789abcdef"[(v >> s) & 15];
    *p++ = '\n';
    write(2, line, (size_t)(p - line));
}

#define SSR_TRACE(name)                                                                      \
    uint64_t __real_##name(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);      \
    uint64_t __wrap_##name(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e,       \
                           uint64_t f)                                                       \
    {                                                                                        \
        static int calls; /* (the first few calls only: some are made all the time later) */ \
        const int n = __atomic_fetch_add(&calls, 1, __ATOMIC_RELAXED);                       \
        if (n < 8) say("[radv] > ", #name);                                                  \
        const uint64_t r = __real_##name(a, b, c, d, e, f);                                  \
        if (n < 8) say("[radv] < ", #name);                                                  \
        return r;                                                                            \
    }

// (ac_compute_device_uuid is the one that prints the last line seen: its "<" line shows the
// trace points work. The fourth console run closed after it and before radv_probe_video_decode,
// where RADV hashes its cache UUID with BLAKE3, whose code picks SSE4.1, AVX2 or AVX-512 by
// CPUID and XGETBV.)
SSR_TRACE(ac_compute_device_uuid)
SSR_TRACE(blake3_hasher_init)
SSR_TRACE(blake3_hasher_update)
SSR_TRACE(blake3_hasher_finalize)
SSR_TRACE(radv_is_rt_wave64_enabled)
SSR_TRACE(radv_probe_video_decode)
SSR_TRACE(radv_probe_video_encode)
SSR_TRACE(disk_cache_create)
SSR_TRACE(disk_cache_create_custom)
SSR_TRACE(util_queue_init)
SSR_TRACE(radv_init_physical_device_decoder)
SSR_TRACE(radv_init_physical_device_encoder)
SSR_TRACE(ac_init_perfcounters)
SSR_TRACE(radv_init_wsi)
SSR_TRACE(ac_get_gs_table_depth)
SSR_TRACE(ac_get_task_info)

// An end on purpose, which no signal reports: where it was called from.
void __real_abort(void) __attribute__((noreturn));
void __wrap_abort(void)
{
    say_hex("[ps5] abort() called from ", (uint64_t)(uintptr_t)__builtin_return_address(0));
    __real_abort();
}

void __real_exit(int) __attribute__((noreturn));
void __wrap_exit(int code)
{
    say_hex("[ps5] exit() called from ", (uint64_t)(uintptr_t)__builtin_return_address(0));
    say_hex("[ps5] exit code ", (uint64_t)(unsigned)code);
    __real_exit(code);
}

void __real__Exit(int) __attribute__((noreturn));
void __wrap__Exit(int code)
{
    say_hex("[ps5] _Exit() called from ", (uint64_t)(uintptr_t)__builtin_return_address(0));
    __real__Exit(code);
}
