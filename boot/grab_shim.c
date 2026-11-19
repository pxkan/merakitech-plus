/* grab_shim.c — LD_PRELOAD shim that no-ops ioctl(EVIOCGRAB) for the STOCK process.
 *
 * BACKGROUND (FINDINGS 'Gestus-debug'): stock demo_v9 grabs /dev/input/event0
 * EXCLUSIVELY (EVIOCGRAB) → the read-only switcher sees no events → gesture A→B
 * impossible while stock is running. The shim (bypass c) loads into stock via LD_PRELOAD from
 * demo.sh's guard line and returns 0 for EVIOCGRAB WITHOUT calling the kernel:
 * stock THINKS it grabs, but it does not → the switcher (read-only) sees events.
 *
 * SCOPE: only ioctl(EVIOCGRAB) in processes whose /proc/self/exe == /customer/demo_v9
 * (stock, incl. watchdog restart and runtime B→A launch). merakiplus (/new_customer/
 * demo_v9) and all other processes get ALL ioctls passed through unchanged — merakiplus's
 * own EVIOCGRAB in B mode is thus preserved (no phantom presses in the SIGSTOP'ed stock).
 * Risk: stock only loses exclusivity; evdev still delivers to stock as
 * before (a grab only takes events away from OTHER readers). No writes, no other behavior.
 *
 * BUILD (ARM .so, FREESTANDING: no libc dependency → loads into glibc/armhf
 * processes without DT_NEEDED; raw syscalls only):
 *   zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -shared -fPIC -nostdlib \
 *       -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
 *       -O2 -Wall -Wextra -Werror -o grab_shim.so grab_shim.c
 * (unwind flags: otherwise UND __aeabi_unwind_cpp_pr1 in the .so → glibc preload error)
 * HOST-SELF-TEST (decision-matrix + live ioctl no-op/passthrough via dlsym):
 *   zig cc -DGRAB_SHIM_SELFTEST -O2 -Wall -Wextra -Werror -o grab_shim_selftest grab_shim.c
 *
 * ENV (host build only; the ARM build has no libc/getenv):
 *   GRAB_SHIM_ALL=1  no-op EVIOCGRAB in ALL processes (tests only)
 *   GRAB_SHIM_OFF=1  disable the shim entirely (pass-through)
 *
 * REVERT: remove export LD_PRELOAD=… from demo.sh's guard line (or rm
 * grab_shim.so — the guard line tests -f) and restart stock. See GESTUS-FIX.md.
 */
#define SHIM_EVIOCGRAB 0x40044590UL   /* _IOW('E', 0x90, int) — linux/input.h */
#ifndef SHIM_TARGET
#define SHIM_TARGET "/customer/demo_v9"
#endif

#ifdef SHIM_HOST
/* ---- host-build: normal libc + dlsym(RTLD_NEXT) ---- */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/ioctl.h>

static long real_ioctl(int fd, unsigned long req, long a)
{
    static long (*fn)(int, unsigned long, ...);
    if (!fn) fn = (long (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    return fn(fd, req, a);
}
static long host_readlink(const char *p, char *b, long n) { return (long)readlink(p, b, (size_t)n); }
static int streq(const char *a, const char *b) { return strcmp(a, b) == 0; }
#else
/* ---- ARM device build: freestanding (-nostdlib), raw EABI syscalls ---- */
#include <stdarg.h>
#include <stddef.h>
#define NR_ioctl    54
#define NR_readlink 85
static long sys3(long n, long a, long b, long c)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r7 __asm__("r7") = n;
    __asm__ volatile("svc 0x0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}
static long real_ioctl(int fd, unsigned long req, long a)
{
    long r = sys3(NR_ioctl, (long)fd, (long)req, a);
    return r < 0 ? -1 : r;   /* libc convention: -1 on error (errno is not set) */
}
static long host_readlink(const char *p, char *b, long n)
{
    return sys3(NR_readlink, (long)p, (long)b, n);
}
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
#endif

/* Decision (pure logic, unit-tested): no-op ONLY EVIOCGRAB in the stock process.
 * Both grab (arg 1) and release (arg 0) are no-op'ed: stock thinks both succeeded. */
static int shim_noop(unsigned long req, const char *exe)
{
    if (req != SHIM_EVIOCGRAB) return 0;
#ifdef SHIM_HOST
    if (getenv("GRAB_SHIM_OFF")) return 0;
    if (getenv("GRAB_SHIM_ALL")) return 1;
#endif
    return exe && streq(exe, SHIM_TARGET);
}

/* int return type matches libc's ioctl prototype (glibc/macOS); the ARM build has
 * no prototype conflict (freestanding). */
int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    long a = va_arg(ap, long);   /* ARM32: long == 32 bit == int/pointer-register */
    va_end(ap);
    static char exe[96];
    static int have;             /* 0 = unknown (retry), 1 = cached */
    if (!have) {
        long n = host_readlink("/proc/self/exe", exe, (long)sizeof exe - 1);
        if (n > 0) { exe[n] = 0; have = 1; }
    }
    if (shim_noop(req, have == 1 ? exe : NULL)) return 0;
    return (int)real_ioctl(fd, req, a);
}

#ifdef GRAB_SHIM_SELFTEST
static int tests_run = 0, tests_fail = 0;
#define CHECK(cond, name) do { \
    tests_run++; \
    if (cond) { printf("  OK   %s\n", name); } \
    else { tests_fail++; printf("  FAIL %s\n", name); } \
} while (0)

int main(void)
{
    printf("grab_shim self-test (EVIOCGRAB no-op for stock + passthrough)\n");
    unsetenv("GRAB_SHIM_ALL");
    unsetenv("GRAB_SHIM_OFF");

    /* decision-matrix */
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/customer/demo_v9") == 1,
          "EVIOCGRAB + stock exe → no-op");
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/new_customer/demo_v9") == 0,
          "EVIOCGRAB + merakiplus exe → passthrough (merakiplus's grab preserved)");
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/bin/busybox") == 0,
          "EVIOCGRAB + other exe → passthrough");
    CHECK(shim_noop(SHIM_EVIOCGRAB, NULL) == 0, "EVIOCGRAB + unknown exe → passthrough");
    CHECK(shim_noop(0x12345678UL, "/customer/demo_v9") == 0,
          "other ioctl + stock exe → passthrough");
    setenv("GRAB_SHIM_ALL", "1", 1);
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/new_customer/demo_v9") == 1,
          "GRAB_SHIM_ALL=1 → no-op regardless of exe (test switch)");
    setenv("GRAB_SHIM_OFF", "1", 1);
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/customer/demo_v9") == 0,
          "GRAB_SHIM_OFF=1 → passthrough regardless of exe");
    unsetenv("GRAB_SHIM_OFF");

    /* live ioctl: the shim interposes ioctl in this entire test binary */
    int fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0, "open /dev/null for ioctl test");
    CHECK(ioctl(fd, SHIM_EVIOCGRAB, 1) == 0,
          "GRAB_SHIM_ALL: ioctl(EVIOCGRAB,1) returns 0 WITHOUT a kernel call (no-op)");
    CHECK(ioctl(fd, SHIM_EVIOCGRAB, 0) == 0,
          "GRAB_SHIM_ALL: ioctl(EVIOCGRAB,0) returns 0 (release no-op)");
    int pp[2];
    CHECK(pipe(pp) == 0, "pipe for passthrough test");
    int n = -1;
    CHECK(ioctl(pp[0], FIONREAD, &n) == 0 && n == 0,
          "passthrough: FIONREAD works normally with shim active");
    unsetenv("GRAB_SHIM_ALL");
    CHECK(ioctl(fd, SHIM_EVIOCGRAB, 1) == -1,
          "without ALL + non-stock exe: real ioctl is called (fails on /dev/null) → -1");
    close(pp[0]); close(pp[1]);
    close(fd);

    printf("\n%d tests, %d failed\n", tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
#endif
