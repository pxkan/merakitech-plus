/* grab_shim.c — LD_PRELOAD-shim der no-op'er ioctl(EVIOCGRAB) for STOCK-processen.
 *
 * BAGGRUND (FINDINGS 'Gestus-debug'): stock demo_v9 griber /dev/input/event0
 * EKSKLUSIVT (EVIOCGRAB) → den read-only switcher ser ingen events → gestus A→B
 * umulig mens stock kører. Shim'en (bypass c) loader i stock via LD_PRELOAD fra
 * demo.sh's guard-linje og returnerer 0 for EVIOCGRAB UDEN at kalde kernelen:
 * stock TROR den grabber, men gør det ikke → switcher (read-only) ser events.
 *
 * SCOPE: kun ioctl(EVIOCGRAB) i processer hvis /proc/self/exe == /customer/demo_v9
 * (stock, inkl. watchdog-genstart og runtime-B→A-launch). ourui (/new_customer/
 * demo_v9) og alle andre processer får ALLE ioctls pass-through uændret — ourui's
 * eget EVIOCGRAB i B-mode bevares dermed (ingen fantom-tryk i SIGSTOP'et stock).
 * Risiko: stock mister kun eksklusiviteten; evdev leverer stadig til stock som
 * før (grab fratager kun ANDRE læsere events). Ingen writes, ingen anden adfærd.
 *
 * BUILD (ARM .so, FREESTANDING: ingen libc-afhængighed → loader i glibc/armhf-
 * processer uden DT_NEEDED; kun rå syscalls):
 *   zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -shared -fPIC -nostdlib \
 *       -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-exceptions \
 *       -O2 -Wall -Wextra -Werror -o grab_shim.so grab_shim.c
 * (unwind-flags: ellers UND __aeabi_unwind_cpp_pr1 i .so'en → glibc preload-fejl)
 * HOST-SELF-TEST (decision-matrix + live ioctl no-op/passthrough via dlsym):
 *   zig cc -DGRAB_SHIM_SELFTEST -O2 -Wall -Wextra -Werror -o grab_shim_selftest grab_shim.c
 *
 * ENV (kun host-build; ARM-build har ingen libc/getenv):
 *   GRAB_SHIM_ALL=1  no-op EVIOCGRAB i ALLE processer (kun til tests)
 *   GRAB_SHIM_OFF=1  deaktivér shim'en helt (pass-through)
 *
 * REVERT: fjern export LD_PRELOAD=… fra demo.sh's guard-linje (eller rm
 * grab_shim.so — guard-linjen tester -f) og genstart stock. Se GESTUS-FIX.md.
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
/* ---- ARM device-build: freestanding (-nostdlib), rå EABI-syscalls ---- */
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
    return r < 0 ? -1 : r;   /* libc-konvention: -1 ved fejl (errno sættes ikke) */
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

/* Beslutning (ren logik, unit-testes): no-op KUN EVIOCGRAB i stock-processen.
 * Både grab (arg 1) og release (arg 0) no-op'es: stock tror begge lykkedes. */
static int shim_noop(unsigned long req, const char *exe)
{
    if (req != SHIM_EVIOCGRAB) return 0;
#ifdef SHIM_HOST
    if (getenv("GRAB_SHIM_OFF")) return 0;
    if (getenv("GRAB_SHIM_ALL")) return 1;
#endif
    return exe && streq(exe, SHIM_TARGET);
}

/* int-returtype matcher libc's ioctl-prototype (glibc/macOS); ARM-build har
 * ingen prototype-konflikt (freestanding). */
int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    long a = va_arg(ap, long);   /* ARM32: long == 32 bit == int/pointer-register */
    va_end(ap);
    static char exe[96];
    static int have;             /* 0 = ukendt (prøv igen), 1 = cachet */
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
    else { tests_fail++; printf("  FEJL %s\n", name); } \
} while (0)

int main(void)
{
    printf("grab_shim self-test (EVIOCGRAB no-op for stock + passthrough)\n");
    unsetenv("GRAB_SHIM_ALL");
    unsetenv("GRAB_SHIM_OFF");

    /* decision-matrix */
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/customer/demo_v9") == 1,
          "EVIOCGRAB + stock-exe → no-op");
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/new_customer/demo_v9") == 0,
          "EVIOCGRAB + ourui-exe → passthrough (ourui's grab bevares)");
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/bin/busybox") == 0,
          "EVIOCGRAB + anden exe → passthrough");
    CHECK(shim_noop(SHIM_EVIOCGRAB, NULL) == 0, "EVIOCGRAB + ukendt exe → passthrough");
    CHECK(shim_noop(0x12345678UL, "/customer/demo_v9") == 0,
          "anden ioctl + stock-exe → passthrough");
    setenv("GRAB_SHIM_ALL", "1", 1);
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/new_customer/demo_v9") == 1,
          "GRAB_SHIM_ALL=1 → no-op uanset exe (test-knap)");
    setenv("GRAB_SHIM_OFF", "1", 1);
    CHECK(shim_noop(SHIM_EVIOCGRAB, "/customer/demo_v9") == 0,
          "GRAB_SHIM_OFF=1 → passthrough uanset exe");
    unsetenv("GRAB_SHIM_OFF");

    /* live ioctl: shim'en interposer ioctl i hele denne test-binær */
    int fd = open("/dev/null", O_RDONLY);
    CHECK(fd >= 0, "open /dev/null til ioctl-test");
    CHECK(ioctl(fd, SHIM_EVIOCGRAB, 1) == 0,
          "GRAB_SHIM_ALL: ioctl(EVIOCGRAB,1) returnerer 0 UDEN kernel-kald (no-op)");
    CHECK(ioctl(fd, SHIM_EVIOCGRAB, 0) == 0,
          "GRAB_SHIM_ALL: ioctl(EVIOCGRAB,0) returnerer 0 (release no-op)");
    int pp[2];
    CHECK(pipe(pp) == 0, "pipe til passthrough-test");
    int n = -1;
    CHECK(ioctl(pp[0], FIONREAD, &n) == 0 && n == 0,
          "passthrough: FIONREAD virker normalt med shim aktiv");
    unsetenv("GRAB_SHIM_ALL");
    CHECK(ioctl(fd, SHIM_EVIOCGRAB, 1) == -1,
          "uden ALL + ikke-stock exe: rigtig ioctl kaldes (fejler på /dev/null) → -1");
    close(pp[0]); close(pp[1]);
    close(fd);

    printf("\n%d tests, %d fejl\n", tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
#endif
