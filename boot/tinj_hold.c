/* tinj_hold.c — touch injector with LONG-PRESS for the boot test's gesture verification.
 *
 * Based on artifacts/ourui/tinj.c (same raw scaling via EVIOCGABS, same
 * event sequence). tinj.c's tap only holds 80 ms — the switcher gesture requires
 * ≥1.5 s per press, hence this variant. Does NOT touch tinj.c (shared artifact).
 *
 * Usage:
 *   tinj_hold tap <x> <y>                 (80 ms, like tinj)
 *   tinj_hold hold <x> <y> <ms>           (long-press on one point)
 *   tinj_hold gesture                     (v6.12 DEFAULT: CENTER hold 5200 ms at
 *                                          (240,240) — background on stock home between
 *                                          the four buttons; the switcher's center gesture
 *                                          requires ≥5000 ms within radius 40)
 *   tinj_hold gesture center [x y] [ms]   (center hold, default (240,240) 5200 ms)
 *   tinj_hold gesture corner [x y]        (LEGACY fallback: 3×1600 ms hold + 300 ms
 *                                          gap, default (75,75) = top-left
 *                                          corner, background on stock home;
 *                                          span 5400 ms ≤ the switcher's 6 s window)
 *
 * ONLY for A mode (stock UI): in B mode ourui grabs event0 exclusively (EVIOCGRAB),
 * so injected events go to ourui, not to stock. The whole corner box x<90,y<90
 * is background on stock home (the Grinder circle centered ~(160,154) r≈72 does not
 * reach into it) → no actuator press. Center (240,240) is also background: the nearest
 * button edge is ~45 px away (measured: measure_stock_home.py) — keep the finger still.
 *
 * RAW RANGE: auto via EVIOCGABS; fallback = the device's constant 0..480 (FINDINGS
 * 'Gestus-debug'; not 4095). Same formula as switcher/ourui: raw = px*(max-min+1)/480.
 *
 * Build: zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 \
 *            -Wall -Wextra -Werror -o tinj_hold tinj_hold.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static int fd = -1;
static int xmin = 0, xmax = 480, ymin = 0, ymax = 480;   /* device-constant fallback */

static void send(__u16 type, __u16 code, __s32 val)
{
    struct input_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type; ev.code = code; ev.value = val;
    if (write(fd, &ev, sizeof ev) != sizeof ev) { perror("write ev"); exit(1); }
}
static void syn(void) { send(EV_SYN, SYN_REPORT, 0); }
static int rawx(int x) { return xmin + (long)x * (xmax - xmin + 1) / 480; }
static int rawy(int y) { return ymin + (long)y * (ymax - ymin + 1) / 480; }

static void down(int x, int y)
{
    send(EV_ABS, ABS_MT_TRACKING_ID, 1);
    send(EV_ABS, ABS_MT_POSITION_X, rawx(x));
    send(EV_ABS, ABS_MT_POSITION_Y, rawy(y));
    send(EV_ABS, ABS_X, rawx(x));
    send(EV_ABS, ABS_Y, rawy(y));
    send(EV_KEY, BTN_TOUCH, 1);
    syn();
}
static void up(void)
{
    send(EV_ABS, ABS_MT_TRACKING_ID, -1);
    send(EV_KEY, BTN_TOUCH, 0);
    syn();
}
static void hold_ms(int x, int y, int ms)
{
    down(x, y);
    usleep((useconds_t)ms * 1000);
    up();
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: tinj_hold tap x y | hold x y ms | "
                        "gesture [center [x y] [ms] | corner [x y]]\n");
        return 2;
    }
    fd = open("/dev/input/event0", O_WRONLY);
    if (fd < 0) { perror("open event0"); return 1; }
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) { xmin = ai.minimum; xmax = ai.maximum; }
    if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) { ymin = ai.minimum; ymax = ai.maximum; }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0) { xmin = ai.minimum; xmax = ai.maximum; }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0) { ymin = ai.minimum; ymax = ai.maximum; }

    if (strcmp(argv[1], "tap") == 0 && argc >= 4) {
        int x = atoi(argv[2]), y = atoi(argv[3]);
        hold_ms(x, y, 80);
        printf("tap %d %d\n", x, y);
    } else if (strcmp(argv[1], "hold") == 0 && argc >= 5) {
        int x = atoi(argv[2]), y = atoi(argv[3]), ms = atoi(argv[4]);
        if (ms < 0 || ms > 60000) { fprintf(stderr, "ms must be 0..60000\n"); return 2; }
        hold_ms(x, y, ms);
        printf("hold %d %d %d ms\n", x, y, ms);
    } else if (strcmp(argv[1], "gesture") == 0) {
        const char *mode = argc >= 3 ? argv[2] : "center";
        if (strcmp(mode, "corner") == 0) {
            int x = argc >= 5 ? atoi(argv[3]) : 75;
            int y = argc >= 5 ? atoi(argv[4]) : 75;
            /* 3×1600 ms + 2×300 ms gap = 5400 ms from first down to last up */
            for (int i = 0; i < 3; i++) {
                hold_ms(x, y, 1600);
                printf("gesture corner: hold %d/3 (%d,%d) 1600 ms\n", i + 1, x, y);
                fflush(stdout);
                if (i < 2) usleep(300000);
            }
            printf("gesture corner done (span 5400 ms)\n");
        } else {
            /* center (default): one continuous hold ≥5000 ms */
            int off = strcmp(mode, "center") == 0 ? 3 : 2;
            int x = argc >= off + 2 ? atoi(argv[off]) : 240;
            int y = argc >= off + 2 ? atoi(argv[off + 1]) : 240;
            int ms = argc >= off + 3 ? atoi(argv[off + 2]) : 5200;
            if (ms < 0 || ms > 60000) { fprintf(stderr, "ms must be 0..60000\n"); return 2; }
            hold_ms(x, y, ms);
            printf("gesture center: hold (%d,%d) %d ms\n", x, y, ms);
        }
    } else {
        fprintf(stderr, "unknown mode\n");
        return 2;
    }
    close(fd);
    return 0;
}
