/* evprobe.c — READ-ONLY evdev diagnostics for GESTUS-FIX (MerakiTech Gen 1).
 *
 *   evprobe info                     name + ABS bitmap + absinfo for event0..3
 *   evprobe cap <node> <ms> <file>  capture raw input_events for <ms> into <file> (O_RDONLY)
 *
 * No writes to input, no grab. Build:
 *   zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 \
 *       -Wall -Wextra -Werror -o evprobe evprobe.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/input.h>

static void info_node(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) { printf("%s: open failed (%s)\n", path, strerror(errno)); return; }
    char name[128] = "";
    ioctl(fd, EVIOCGNAME(sizeof name), name);
    printf("%s: name '%s'\n", path, name);
    unsigned long absbits[4] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absbits), absbits) >= 0) {
        printf("  ABS-bitmap: %08lx %08lx\n", absbits[0], absbits[1]);
        for (int code = 0; code < ABS_CNT; code++) {
            if (!(absbits[code / (8 * sizeof(long))] & (1UL << (code % (8 * sizeof(long))))))
                continue;
            struct input_absinfo ai;
            if (ioctl(fd, EVIOCGABS(code), &ai) == 0)
                printf("  ABS 0x%02x: min=%d max=%d fuzz=%d flat=%d res=%d\n",
                       code, ai.minimum, ai.maximum, ai.fuzz, ai.flat, ai.resolution);
        }
    }
    /* Explicit MT/ST axes (even if the bitmap does not show them) */
    const int codes[] = { ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        struct input_absinfo ai;
        if (ioctl(fd, EVIOCGABS(codes[i]), &ai) == 0)
            printf("  EVIOCGABS(0x%02x) rc=0: min=%d max=%d\n", codes[i], ai.minimum, ai.maximum);
        else
            printf("  EVIOCGABS(0x%02x) rc<0 (%s)\n", codes[i], strerror(errno));
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "info") == 0) {
        for (int i = 0; i < 4; i++) {
            char p[64]; snprintf(p, sizeof p, "/dev/input/event%d", i);
            info_node(p);
        }
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "cap") == 0) {
        const char *node = argv[2];
        int ms = atoi(argv[3]);
        const char *out = argv[4];
        int fd = open(node, O_RDONLY | O_NONBLOCK);
        if (fd < 0) { perror("open node"); return 1; }
        int of = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (of < 0) { perror("open out"); close(fd); return 1; }
        struct input_event evs[64];
        long total = 0;
        int64_t t0 = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        t0 = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            int64_t now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            if (now - t0 >= ms) break;
            ssize_t n = read(fd, evs, sizeof evs);
            if (n > 0) {
                if (write(of, evs, (size_t)n) != n) { perror("write out"); break; }
                total += n / (ssize_t)sizeof(struct input_event);
            } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
                perror("read"); break;
            }
            usleep(5000);
        }
        close(of); close(fd);
        printf("cap %s: %ld events -> %s (%d ms)\n", node, total, out, ms);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "mem") == 0) {
        /* READ-ONLY: dump kernel memory via /dev/mem (diagnostics, no writes) */
        unsigned long addr = strtoul(argv[2], NULL, 16);
        int words = argc >= 4 ? atoi(argv[3]) : 4;
        int fd = open("/dev/mem", O_RDONLY | O_SYNC);
        if (fd < 0) { perror("open /dev/mem"); return 1; }
        unsigned long base = addr & ~0xFFFUL, off = addr & 0xFFF;
        void *m = mmap(NULL, 0x2000, PROT_READ, MAP_SHARED, fd, (off_t)base);
        if (m == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
        volatile uint32_t *p = (volatile uint32_t *)((char *)m + off);
        for (int i = 0; i < words; i++)
            printf("%08lx: %08x\n", addr + 4 * i, p[i]);
        munmap(m, 0x2000); close(fd);
        return 0;
    }
    fprintf(stderr, "usage: evprobe info | evprobe cap <node> <ms> <file> | evprobe mem <hexaddr> [words]\n");
    return 2;
}