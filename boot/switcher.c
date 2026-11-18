/* switcher.c — boot-persistens daemon til MerakiTech Gen 1 (Sanbon SAT62091M).
 *
 * FORMÅL: Observér touch READ-ONLY og udløs runtime-skift A→B (stock-UI → ourui)
 * ved en hemmelig gestus. v6.12 (GESTUS-FIX.md): ÉN sammenhængende hold ≥5000 ms
 * i skærmens CENTER (inden for radius R af (240,240)) — ingen multi-press.
 * Hjørne-gestus'en (3× long-press ≥1,5 s i x<90,y<90 inden for 6 s) bevares som
 * fallback (SWITCHER_GESTURE=corner).
 *
 * GRAB-BYPASS (FINDINGS 'Gestus-debug': stock demo_v9 griber event0 EKSKLUSIVT
 * med EVIOCGRAB → en read-only switcher ser ingen events):
 *  (a) DUPLIKAT-NODE: åbner ALLE touch-noder (event0..9 med ABS_MT_POSITION_X,
 *      navn TECHWIN/tw31xx eller samme abs-range) read-only samtidigt og læser
 *      fra dem alle. Findes en duplikat-node som stock ikke grabber, leverer den
 *      events → gestus virker uden indgreb. (Stock's grab påvirker kun evdev-
 *      klienter på den grabbede node.)
 *  (b) SYSFS-FALLBACK: tw31xx-driveren eksponerer lu_coordinate_x/lu_coordinate_y/
 *      point_num i sysfs (grab påvirker ikke sysfs). Hvis evdev er blind (0 events
 *      mens sysfs ser touch) skiftes automatisk til sysfs-polling (50 ms).
 *  (c) LD_PRELOAD-SHIM (grab_shim.so): no-op'er ioctl(EVIOCGRAB) for stock-processen
 *      (/customer/demo_v9) via demo.sh's guard-linje-env. Forberedt lokalt; anvendes
 *      af boot/ondevice/verify_gesture_v612.py hvis (a)+(b) fejler på enheden.
 *  Kilde-vælg: SWITCHER_SOURCE=auto|evdev|sysfs (default auto).
 *
 * DESIGN (se BOOT.md for fuld dokumentation):
 *  - Åbner input-noder O_RDONLY|O_NONBLOCK. ALDRIG EVIOCGRAB, ALDRIG writes til
 *    input/UART/fb/config. Stock-UI'ets input-kø påvirkes ikke.
 *  - I B-mode griber ourui touch eksklusivt (EVIOCGRAB) → switcher får ingen events
 *    og idler naturligt. Skift B→A håndteres af ourui's egen knap (BOOT.md §UI-krav).
 *  - Mode verificeres via readlink(/proc/<pid>/exe) for hver pidof demo_v9:
 *      /customer/demo_v9     → A-mode (stock)  → gestus må skifte til B
 *      /new_customer/demo_v9 → B-mode (ourui)  → gør ingenting
 *      ingen demo_v9         → gør ingenting
 *  - Watchdog-race undgås med SIGSTOP-først: test.sh checker kun ps-grep "demo_v9"
 *    (1 s-loop). Kill→vent→launch ville efterlade et vindue hvor watchdog'en
 *    genstarter A oven i B. Derfor: SIGSTOP A (PID findes stadig) → launch B →
 *    verifikér B's exe-link → SIGKILL A → vent exit. Fejler B → SIGCONT A (fallback).
 *  - Robusthed: genåbner input-node ved fejl, poll-loop 50 ms, flock-single-instance,
 *    logrotation ved 64 KB, ingen reboot, ingen writes uden for /tmp.
 *
 * RAW-RANGE: auto-detekteres via EVIOCGABS ved åbning (virker også på en grab'et
 * node — grab blokerer ikke ioctl). Enhedens faktiske range er 0..480 (FINDINGS
 * 'Gestus-debug'); fallback-konstanten er derfor 480 (ikke 4095). Samme formel som
 * tinj.c/ourui: screen = (raw−min)·480/(max−min+1). SIM-beholder sit deklarerede
 * 0..4095 (sim/proto.py) og passer SWITCHER_RAW_MAX=4095 i fil-mode.
 *
 * BUILD (ARM, statisk, 0 warnings):
 *   zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 \
 *       -Wall -Wextra -Werror -o switcher switcher.c
 * SELF-TEST (host, deterministisk syntetisk input):
 *   zig cc -DSWITCHER_SELFTEST -O2 -Wall -Wextra -Werror -o switcher_selftest switcher.c
 * HOST-DAEMON (host, til fil-stream-test via test_switcher.py):
 *   zig cc -O2 -Wall -Wextra -Werror -o switcher_host switcher.c
 *
 * ENV (test/override):
 *   SWITCHER_GESTURE     center (default) | corner (fallback: 3×long-press i hjørne)
 *   SWITCHER_HOLD_MS     minimum hold-tid (default 5000 center / 1500 corner)
 *   SWITCHER_CENTER_X/Y  gestus-center (default 240/240)
 *   SWITCHER_CENTER_R    gestus-radius i px (default 40; stock-knappernes kant
 *                        ligger ~45 px fra center — 60 ville overlappe, se GESTUS-FIX.md)
 *   SWITCHER_INPUT       eksplicit input-node (ellers autodiscovery, alle touch-noder)
 *   SWITCHER_INPUT_FILE  læs rå input_event-stream fra fil/FIFO i stedet for node
 *                        (gestus-timing følger event-tidsstempler, ikke wall-clock)
 *   SWITCHER_RAW_MAX     raw-akse-max i fil-mode (default 480 = enhedens konstant;
 *                        sim-passede streams bruger 4095)
 *   SWITCHER_SOURCE      auto (default) | evdev | sysfs
 *   SWITCHER_SYSFS_DIR   sysfs-mappe med lu_coordinate_x/y + point_num (ellers auto-scan)
 *   SWITCHER_SYSFS_SCREEN=1  sysfs-koordinater er allerede skærmpixels (ellers raw)
 *   SWITCHER_LOG         logfil (default /tmp/switcher.log)
 *   SWITCHER_LOCK        lockfil (default /tmp/switcher.lock)
 *
 * Egne evdev-definitioner (ingen linux-headers → kan bygges på Mac til tests).
 * struct input_event er 16 byte på enhedens kernel 4.9/arm (32-bit timeval) —
 * bevist via strace: read(6</dev/input/event0>, ..., 16).
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* ---- evdev-konstanter (linux/input.h-værdier, asm-generic ioctl-kodning) ---- */
#define EV_SYN_        0x00
#define EV_KEY_        0x01
#define EV_ABS_        0x03
#define SYN_REPORT_    0x00
#define BTN_TOUCH_     0x14a
#define ABS_X_         0x00
#define ABS_Y_         0x01
#define ABS_MT_POSITION_X_ 0x35
#define ABS_MT_POSITION_Y_ 0x36
#define ABS_MT_TRACKING_ID_ 0x39

struct sw_input_event {
    int32_t  tv_sec;
    int32_t  tv_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};

#define SW_IOC_READ  2u
#define SW_IOC(dir,type,nr,size) (((dir)<<30)|((size)<<16)|((type)<<8)|(nr))
#define SW_EVIOCGNAME(len) SW_IOC(SW_IOC_READ,'E',0x06,(len))
#define SW_EVIOCGABS(abs)  SW_IOC(SW_IOC_READ,'E',0x40+(abs),24)
struct sw_input_absinfo { int32_t value, minimum, maximum, fuzz, flat, resolution; };

/* ---- gestus-parametre (BOOT.md §2 + GESTUS-FIX.md) ---- */
#define SCREEN_W      480
#define SCREEN_H      480
#define CORNER_X      90     /* fallback: øverste venstre hjørne x<90 && y<90 */
#define CORNER_Y      90
#define CORNER_HOLD_MS 1500  /* fallback: hver press ≥1,5 s */
#define WINDOW_MS     6000   /* fallback: 3 presses inden for 6 s */
#define COOLDOWN_MS   10000  /* efter trigger: ignorér gestus i 10 s */
#define GEST_CENTER   0
#define GEST_CORNER   1
#define CENTER_X_D    240    /* default center-hold punkt */
#define CENTER_Y_D    240
#define CENTER_R_D    40     /* sikker radius: stock-knapkant ~45 px fra center */
#define HOLD_MS_D     5000   /* center: ét sammenhængende hold ≥5 s */
#define RAW_MAX_D     480    /* enhedens faktiske ABS_MT-max (FINDINGS Gestus-debug) */

/* Debug-hook: NULL i self-test, logline i daemon (deles af gestus-maskinen). */
static void (*sw_dbg)(const char *fmt, ...) = NULL;

typedef struct {
    int raw_min_x, raw_max_x, raw_min_y, raw_max_y;
    int cur_x, cur_y;          /* skærmpixels */
    int down;                  /* finger nede */
    int pending;               /* down-position endnu ikke latch'et (afventer SYN) */
    int64_t down_ms;
    int down_in_zone;
    int stayed_in_zone;
    int count;                 /* corner: antal gyldige long-presses i vinduet */
    int64_t window_start_ms;   /* corner: første gyldige press' down-tid */
    /* konfiguration */
    int mode;                  /* GEST_CENTER | GEST_CORNER */
    int cx, cy, r, r2;         /* center-zone: radius + radius i anden */
    int hold_ms;               /* minimum hold/press-tid */
} Gesture;

static void gesture_reset_state(Gesture *g)
{
    g->cur_x = -1; g->cur_y = -1;
    g->down = 0; g->pending = 0; g->down_ms = 0;
    g->down_in_zone = 0; g->stayed_in_zone = 0;
    g->count = 0; g->window_start_ms = 0;
}

static void gesture_init(Gesture *g, int rminx, int rmaxx, int rminy, int rmaxy)
{
    memset(g, 0, sizeof *g);
    g->raw_min_x = rminx; g->raw_max_x = rmaxx;
    g->raw_min_y = rminy; g->raw_max_y = rmaxy;
    g->mode = GEST_CENTER;
    g->cx = CENTER_X_D; g->cy = CENTER_Y_D;
    g->r = CENTER_R_D; g->r2 = CENTER_R_D * CENTER_R_D;
    g->hold_ms = HOLD_MS_D;
    gesture_reset_state(g);
}

static void gesture_set_center(Gesture *g, int cx, int cy, int r, int hold_ms)
{
    g->mode = GEST_CENTER;
    g->cx = cx; g->cy = cy; g->r = r; g->r2 = r * r;
    g->hold_ms = hold_ms > 0 ? hold_ms : HOLD_MS_D;
    gesture_reset_state(g);
}

static void gesture_set_corner(Gesture *g, int hold_ms)
{
    g->mode = GEST_CORNER;
    g->hold_ms = hold_ms > 0 ? hold_ms : CORNER_HOLD_MS;
    gesture_reset_state(g);
}

/* ENV-konfiguration (daemon + fil-mode; self-test bruger setterne direkte). */
static void gesture_config_env(Gesture *g)
{
    const char *e;
    int hold = 0;
    if ((e = getenv("SWITCHER_HOLD_MS")) && *e) hold = atoi(e);
    if ((e = getenv("SWITCHER_GESTURE")) && *e && strcmp(e, "corner") == 0) {
        gesture_set_corner(g, hold);
        return;
    }
    int cx = CENTER_X_D, cy = CENTER_Y_D, r = CENTER_R_D;
    if ((e = getenv("SWITCHER_CENTER_X")) && *e) cx = atoi(e);
    if ((e = getenv("SWITCHER_CENTER_Y")) && *e) cy = atoi(e);
    if ((e = getenv("SWITCHER_CENTER_R")) && *e) r = atoi(e);
    if (r < 1) r = 1;
    if (r > 240) r = 240;
    gesture_set_center(g, cx, cy, r, hold);
}

static int scale_axis(int raw, int rmin, int rmax, int screen)
{
    if (rmax <= rmin) return 0;
    long v = (long)(raw - rmin) * screen / (rmax - rmin + 1);
    if (v < 0) v = 0;
    if (v >= screen) v = screen - 1;
    return (int)v;
}

/* Raw-range-valg (ren logik, unit-testes): EVIOCGABS-resultater hvis til stede,
 * ellers enhedens konstant 0..480. MT-akser har forrang over ST-akser. */
typedef struct {
    int have_x, have_y, have_mx, have_my;
    struct sw_input_absinfo x, y, mx, my;
} AbsProbe;

static void pick_ranges(const AbsProbe *p, int *rminx, int *rmaxx, int *rminy, int *rmaxy)
{
    *rminx = 0; *rmaxx = RAW_MAX_D; *rminy = 0; *rmaxy = RAW_MAX_D;
    if (p->have_x)  { *rminx = p->x.minimum;  *rmaxx = p->x.maximum; }
    if (p->have_y)  { *rminy = p->y.minimum;  *rmaxy = p->y.maximum; }
    if (p->have_mx) { *rminx = p->mx.minimum; *rmaxx = p->mx.maximum; }
    if (p->have_my) { *rminy = p->my.minimum; *rmaxy = p->my.maximum; }
}

static int in_zone(const Gesture *g)
{
    if (g->cur_x < 0 || g->cur_y < 0) return 0;
    if (g->mode == GEST_CORNER)
        return g->cur_x < CORNER_X && g->cur_y < CORNER_Y;
    long dx = g->cur_x - g->cx, dy = g->cur_y - g->cy;
    return dx * dx + dy * dy <= g->r2;
}

static void pos_update(Gesture *g, int x, int y)
{
    g->cur_x = x; g->cur_y = y;
    if (g->down && !g->pending && !in_zone(g)) g->stayed_in_zone = 0;
}

static void press_begin(Gesture *g, int64_t now_ms)
{
    if (g->down) return;
    g->down = 1;
    g->down_ms = now_ms;
    /* positionen latch'es ved første SYN_REPORT efter down: tracking-id/BTN_TOUCH
     * ankommer FØR positions-events i samme batch (jf. tinj.c's rækkefølge). */
    g->pending = 1;
    g->down_in_zone = 0;
    g->stayed_in_zone = 0;
}

static void latch_zone(Gesture *g)
{
    g->down_in_zone = in_zone(g);
    g->stayed_in_zone = g->down_in_zone;
    g->pending = 0;
}

/* Returnerer 1 præcis når gestus'en udløses:
 *  center: ét gyldigt hold ≥hold_ms der blev i zonen (ved release).
 *  corner: tredje gyldige release inden for vinduet (legacy). */
static int press_end(Gesture *g, int64_t now_ms)
{
    if (!g->down) return 0;
    if (g->pending) latch_zone(g);   /* ingen SYN set → best-effort med aktuel position */
    int64_t dur = now_ms - g->down_ms;
    int valid = g->down_in_zone && g->stayed_in_zone && dur >= g->hold_ms;
    g->down = 0;
    if (g->mode == GEST_CENTER) {
        if (!valid && g->down_in_zone && dur >= 1000 && sw_dbg)
            sw_dbg("gestus: center-hold %d ms ugyldig (for kort eller drev ud af zonen)",
                   (int)dur);
        return valid;
    }
    if (!valid) return 0;
    if (g->count == 0) g->window_start_ms = g->down_ms;
    g->count++;
    if (g->count >= 3) {
        int ok = (now_ms - g->window_start_ms) <= WINDOW_MS;
        g->count = 0;
        return ok;
    }
    return 0;
}

/* Feed ét evdev-event. now_ms = event-tidsstempel (ms). Returnerer 1 ved trigger. */
static int gesture_feed(Gesture *g, uint16_t type, uint16_t code, int32_t value,
                        int64_t now_ms)
{
    /* vindues-udløb (corner): nulstil tælling (press-sporing bevares) */
    if (g->mode == GEST_CORNER && g->count > 0 &&
        now_ms - g->window_start_ms > WINDOW_MS)
        g->count = 0;

    if (type == EV_ABS_) {
        if (code == ABS_X_ || code == ABS_MT_POSITION_X_) {
            pos_update(g, scale_axis(value, g->raw_min_x, g->raw_max_x, SCREEN_W), g->cur_y);
        } else if (code == ABS_Y_ || code == ABS_MT_POSITION_Y_) {
            pos_update(g, g->cur_x, scale_axis(value, g->raw_min_y, g->raw_max_y, SCREEN_H));
        } else if (code == ABS_MT_TRACKING_ID_) {
            if (value >= 0) press_begin(g, now_ms);
            else            return press_end(g, now_ms);
        }
    } else if (type == EV_KEY_ && code == BTN_TOUCH_) {
        if (value) press_begin(g, now_ms);
        else       return press_end(g, now_ms);
    } else if (type == EV_SYN_ && code == SYN_REPORT_) {
        if (g->down && g->pending) latch_zone(g);
    }
    return 0;
}

/* Sysfs-sample (bypass b): point_num > 0 = finger nede i (x,y) SKÆRM-pixels.
 * Driveren opdaterer kun ved touch-ændringer → polling caller med mono-tid.
 * Returnerer 1 ved trigger (release efter gyldigt hold). */
static int gesture_sysfs_sample(Gesture *g, int x, int y, int points, int64_t now_ms)
{
    if (points > 0) {
        if (!g->down) {
            press_begin(g, now_ms);
            pos_update(g, x, y);
            latch_zone(g);
        } else {
            pos_update(g, x, y);
        }
    } else if (g->down) {
        return press_end(g, now_ms);
    }
    return 0;
}

/* ---- mode-klassifikation (ren logik, unit-testes) ---- */
typedef enum { MODE_NONE = 0, MODE_A, MODE_B } Mode;
typedef enum { ACT_SWITCH_A = 0, ACT_IDLE_B, ACT_IDLE_NONE, ACT_IDLE_MIXED } Action;

static Mode classify_exe(const char *exe)
{
    if (!exe) return MODE_NONE;
    if (strcmp(exe, "/customer/demo_v9") == 0)     return MODE_A;
    if (strcmp(exe, "/new_customer/demo_v9") == 0) return MODE_B;
    return MODE_NONE;
}

/* Beslutning ud fra exe-links for alle demo_v9-PIDs.
 * Blandet A+B (må ikke ske) → ingen handling (sikrest). */
static Action decide_action(const Mode *modes, int n)
{
    int na = 0, nb = 0;
    for (int i = 0; i < n; i++) {
        if (modes[i] == MODE_A) na++;
        else if (modes[i] == MODE_B) nb++;
    }
    if (nb > 0 && na > 0) return ACT_IDLE_MIXED;
    if (nb > 0)           return ACT_IDLE_B;
    if (na == 1)          return ACT_SWITCH_A;
    return ACT_IDLE_NONE;
}

#ifndef SWITCHER_SELFTEST
/* ============================ DAEMON (device/host) ============================ */
#include <sys/ioctl.h>

#define MAX_NODES 4
typedef struct {
    int fd;
    char path[40];
    long ev_count;
    int logged_first;
} Node;

typedef struct {
    int ok;
    char dir[160];
    int fx, fy, fn;            /* fds: lu_coordinate_x, lu_coordinate_y, point_num */
    int rminx, rmaxx, rminy, rmaxy;
    int last_x, last_y, last_points;
    int64_t touch_ms;          /* mono-tid for sidste sete touch-aktivitet */
} Sysfs;

static const char *LOG_PATH  = "/tmp/switcher.log";
static const char *LOCK_PATH = "/tmp/switcher.lock";
static FILE *logf = NULL;
static volatile sig_atomic_t stopflag = 0;

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void logline(const char *fmt, ...)
{
    if (!logf) return;
    if (ftell(logf) > 65536) {           /* simpel rotation: truncate */
        fclose(logf);
        logf = fopen(LOG_PATH, "w");
        if (!logf) return;
    }
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    char ts[32];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(logf, "[%s UTC] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(logf, fmt, ap);
    va_end(ap);
    fputc('\n', logf);
    fflush(logf);
}

static void on_signal(int sig) { (void)sig; stopflag = 1; }

/* Find demo_v9-PIDs via pidof (busybox). Returnerer antal; pids[i] + exe[i]. */
static int find_demos(int *pids, char exes[][256], int max)
{
    int n = 0;
    FILE *p = popen("pidof demo_v9 2>/dev/null", "r");
    if (!p) return 0;
    int pid;
    while (n < max && fscanf(p, "%d", &pid) == 1) {
        char path[64], buf[256];
        snprintf(path, sizeof path, "/proc/%d/exe", pid);
        ssize_t r = readlink(path, buf, sizeof buf - 1);
        if (r > 0) { buf[r] = 0; pids[n] = pid; snprintf(exes[n], 256, "%s", buf); n++; }
    }
    pclose(p);
    return n;
}

static int pid_alive(int pid) { return kill(pid, 0) == 0 || errno != ESRCH; }

/* Vent til /proc/<pid>/exe matcher expected (ms-budget). 1=ok. */
static int wait_exe(int pid, const char *expected, int budget_ms)
{
    int64_t deadline = mono_ms() + budget_ms;
    while (mono_ms() < deadline) {
        char path[64], buf[256];
        snprintf(path, sizeof path, "/proc/%d/exe", pid);
        ssize_t r = readlink(path, buf, sizeof buf - 1);
        if (r > 0) {
            buf[r] = 0;
            if (strcmp(buf, expected) == 0 && pid_alive(pid)) return 1;
        }
        if (!pid_alive(pid)) return 0;
        usleep(50000);
    }
    return 0;
}

/* SIGSTOP-først-skift A→B (se filhoved for watchdog-race-begrundelse). */
static void do_switch(int pid_a)
{
    logline("skift A→B: SIGSTOP pid %d (watchdog ser stadig PID)", pid_a);
    if (kill(pid_a, SIGSTOP) != 0) {
        logline("skift FEJL: SIGSTOP pid %d: %s", pid_a, strerror(errno));
        return;
    }
    pid_t child = fork();
    if (child < 0) {
        logline("skift FEJL: fork: %s — SIGCONT pid %d", strerror(errno), pid_a);
        kill(pid_a, SIGCONT);
        return;
    }
    if (child == 0) {
        /* barn: detach + start B's demo_v9 (ourui) med B-libs */
        setsid();
        if (chdir("/new_customer") != 0) _exit(126);
        setenv("LD_LIBRARY_PATH", "/new_customer/libs:/config/wifi", 1);
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) { dup2(nul, 0); close(nul); }
        int out = open("/tmp/ourui_boot.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); close(out); }
        execl("/new_customer/demo_v9", "./demo_v9", (char *)NULL);
        _exit(127);
    }
    logline("skift A→B: launchede B pid %d (/new_customer/demo_v9)", (int)child);
    if (wait_exe((int)child, "/new_customer/demo_v9", 2000)) {
        logline("skift A→B: B verificeret (exe-link) — SIGKILL pid %d", pid_a);
        kill(pid_a, SIGKILL);
        int64_t deadline = mono_ms() + 2000;
        while (pid_alive(pid_a) && mono_ms() < deadline) usleep(50000);
        if (pid_alive(pid_a))
            logline("skift ADVARSEL: pid %d lever stadig efter SIGKILL", pid_a);
        else
            logline("skift A→B FÆRDIG: ourui kører, stock-A exit'et, ingen reboot");
    } else {
        logline("skift FEJL: B (pid %d) kom ikke op — fallback: SIGCONT A, dræb B",
                (int)child);
        if (pid_alive((int)child)) kill((int)child, SIGKILL);
        kill(pid_a, SIGCONT);
    }
}

static void handle_trigger(void)
{
    int pids[16];
    char exes[16][256];
    Mode modes[16];
    int n = find_demos(pids, exes, 16);
    for (int i = 0; i < n; i++) modes[i] = classify_exe(exes[i]);
    Action a = decide_action(modes, n);
    for (int i = 0; i < n; i++)
        logline("mode-check: pid %d exe=%s → %s", pids[i], exes[i],
                modes[i] == MODE_A ? "A" : modes[i] == MODE_B ? "B" : "ukendt");
    switch (a) {
    case ACT_SWITCH_A:
        logline("GESTUS UDLØST + mode A → runtime-skift til ourui");
        for (int i = 0; i < n; i++)
            if (modes[i] == MODE_A) { do_switch(pids[i]); break; }
        break;
    case ACT_IDLE_B:
        logline("GESTUS UDLØST men mode B (ourui kører) → ingen handling "
                "(ourui har selv skift-knap)");
        break;
    case ACT_IDLE_MIXED:
        logline("GESTUS UDLØST men A+B kører samtidigt → ingen handling (sikrest)");
        break;
    default:
        logline("GESTUS UDLØST men ingen demo_v9 → ingen handling");
        break;
    }
}

/* Åbn ÉN node og probe abs-ranges + navn. Returnerer fd eller -1. */
static int probe_node(const char *path, AbsProbe *p, char *name, size_t namesz, int *has_mt)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    memset(p, 0, sizeof *p);
    p->have_x  = ioctl(fd, SW_EVIOCGABS(ABS_X_), &p->x) == 0;
    p->have_y  = ioctl(fd, SW_EVIOCGABS(ABS_Y_), &p->y) == 0;
    p->have_mx = ioctl(fd, SW_EVIOCGABS(ABS_MT_POSITION_X_), &p->mx) == 0;
    p->have_my = ioctl(fd, SW_EVIOCGABS(ABS_MT_POSITION_Y_), &p->my) == 0;
    *has_mt = p->have_mx;
    name[0] = 0;
    ioctl(fd, SW_EVIOCGNAME(namesz), name);
    return fd;
}

/* Input-discovery (bypass a): ALLE touch-noder åbnes read-only samtidigt.
 * event0 først (bevist touch-node), derefter event1..9; duplikater (samme navn
 * eller samme abs-range) beholdes — en duplikat stock ikke grabber leverer events.
 * Returnerer antal noder; gesture-ranges sættes fra den foretrukne node. */
static int open_nodes(Node *nodes, Gesture *g)
{
    const char *override = getenv("SWITCHER_INPUT");
    char path[64];
    int n = 0;
    int first = -1;
    for (int i = 0; i < 10 && n < MAX_NODES; i++) {
        if (override && *override) {
            if (i > 0) break;
            snprintf(path, sizeof path, "%s", override);
        } else {
            snprintf(path, sizeof path, "/dev/input/event%d", i);
        }
        AbsProbe p;
        char name[64];
        int has_mt = 0;
        int fd = probe_node(path, &p, name, sizeof name, &has_mt);
        if (fd < 0) continue;
        int name_ok = strcasestr(name, "TECHWIN") || strcasestr(name, "tw31xx");
        if (!has_mt && !name_ok) { close(fd); continue; }
        int rminx, rmaxx, rminy, rmaxy;
        pick_ranges(&p, &rminx, &rmaxx, &rminy, &rmaxy);
        logline("input: %s (navn '%s', raw x %d..%d y %d..%d) — READ-ONLY, ingen grab",
                path, name, rminx, rmaxx, rminy, rmaxy);
        if (first < 0 && (has_mt || name_ok)) {
            first = n;
            gesture_init(g, rminx, rmaxx, rminy, rmaxy);
        }
        nodes[n].fd = fd;
        snprintf(nodes[n].path, sizeof nodes[n].path, "%s", path);
        nodes[n].ev_count = 0;
        nodes[n].logged_first = 0;
        n++;
    }
    return n;
}

/* ---- sysfs-probe (bypass b): tw31xx's lu_coordinate_x/y + point_num ---- */
static int sysfs_open_dir(Sysfs *s, const char *dir)
{
    char p[200];
    snprintf(p, sizeof p, "%s/lu_coordinate_x", dir);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/lu_coordinate_y", dir);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/point_num", dir);
    if (access(p, R_OK) != 0) return 0;
    snprintf(p, sizeof p, "%s/lu_coordinate_x", dir);
    s->fx = open(p, O_RDONLY);
    snprintf(p, sizeof p, "%s/lu_coordinate_y", dir);
    s->fy = open(p, O_RDONLY);
    snprintf(p, sizeof p, "%s/point_num", dir);
    s->fn = open(p, O_RDONLY);
    if (s->fx < 0 || s->fy < 0 || s->fn < 0) {
        if (s->fx >= 0) close(s->fx);
        if (s->fy >= 0) close(s->fy);
        if (s->fn >= 0) close(s->fn);
        s->fx = s->fy = s->fn = -1;
        return 0;
    }
    snprintf(s->dir, sizeof s->dir, "%s", dir);
    s->last_x = s->last_y = -1; s->last_points = 0; s->touch_ms = 0;
    s->ok = 1;
    return 1;
}

static void sysfs_probe(Sysfs *s, const Gesture *g)
{
    memset(s, 0, sizeof *s);
    s->fx = s->fy = s->fn = -1;
    /* range: evdev-detekteret hvis tilgængelig (ioctl virker også på grab'et node),
     * ellers enhedens konstant 0..480. SCREEN=1: koordinater er allerede pixels. */
    if (getenv("SWITCHER_SYSFS_SCREEN")) {
        s->rminx = 0; s->rmaxx = SCREEN_W - 1;
        s->rminy = 0; s->rmaxy = SCREEN_H - 1;
    } else {
        s->rminx = g->raw_min_x; s->rmaxx = g->raw_max_x;
        s->rminy = g->raw_min_y; s->rmaxy = g->raw_max_y;
    }
    const char *env = getenv("SWITCHER_SYSFS_DIR");
    if (env && *env) {
        if (sysfs_open_dir(s, env))
            logline("input: sysfs %s (env) — grab-bypass (b) klar", s->dir);
        return;
    }
    DIR *d = opendir("/sys/bus/i2c/devices");
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char dir[200];
        snprintf(dir, sizeof dir, "/sys/bus/i2c/devices/%s", de->d_name);
        if (sysfs_open_dir(s, dir)) {
            logline("input: sysfs %s (auto-scan) — grab-bypass (b) klar", s->dir);
            break;
        }
    }
    closedir(d);
}

static int sysfs_read_int(int fd, int *out)
{
    char b[32];
    if (lseek(fd, 0, SEEK_SET) < 0) return 0;
    ssize_t n = read(fd, b, sizeof b - 1);
    if (n <= 0) return 0;
    b[n] = 0;
    *out = (int)strtol(b, NULL, 10);
    return 1;
}

/* Læs sysfs-sample; returnerer 1 hvis læst. x/y i skærmpixels. */
static int sysfs_sample(Sysfs *s, int *x, int *y, int *points)
{
    int rx = 0, ry = 0, pn = 0;
    if (!sysfs_read_int(s->fn, &pn)) return 0;
    if (!sysfs_read_int(s->fx, &rx)) return 0;
    if (!sysfs_read_int(s->fy, &ry)) return 0;
    *points = pn;
    *x = scale_axis(rx, s->rminx, s->rmaxx, SCREEN_W);
    *y = scale_axis(ry, s->rminy, s->rmaxy, SCREEN_H);
    return 1;
}

static void sysfs_close(Sysfs *s)
{
    if (s->fx >= 0) close(s->fx);
    if (s->fy >= 0) close(s->fy);
    if (s->fn >= 0) close(s->fn);
    s->fx = s->fy = s->fn = -1;
    s->ok = 0;
}

#define SRC_NONE 0
#define SRC_EV   1
#define SRC_SYS  2

int main(void)
{
    const char *e;
    if ((e = getenv("SWITCHER_LOG")) && *e)  LOG_PATH = e;
    if ((e = getenv("SWITCHER_LOCK")) && *e) LOCK_PATH = e;

    logf = fopen(LOG_PATH, "a");
    sw_dbg = logline;
    /* single-instance: flock (ikke-blokerende) */
    int lockfd = open(LOCK_PATH, O_CREAT | O_RDWR, 0644);
    if (lockfd >= 0 && flock(lockfd, LOCK_EX | LOCK_NB) != 0) {
        logline("anden switcher-instans kører allerede — exit");
        if (logf) fclose(logf);
        return 0;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    Gesture g;
    gesture_init(&g, 0, RAW_MAX_D, 0, RAW_MAX_D);
    gesture_config_env(&g);
    if (g.mode == GEST_CENTER)
        logline("switcher startet (pid %d): gestus = CENTER-hold ≥%d ms i (%d,%d) "
                "radius %d px (én finger, bliv i zonen, slip efter ≥%d ms)",
                (int)getpid(), g.hold_ms, g.cx, g.cy, g.r, g.hold_ms);
    else
        logline("switcher startet (pid %d): gestus = CORNER-fallback 3×long-press "
                "≥%d ms i x<%d,y<%d inden for %d s", (int)getpid(), g.hold_ms,
                CORNER_X, CORNER_Y, WINDOW_MS / 1000);

    Node nodes[MAX_NODES];
    int nnodes = 0;
    Sysfs sys;
    memset(&sys, 0, sizeof sys);
    sys.fx = sys.fy = sys.fn = -1;
    int fd = -1;
    int filemode = (e = getenv("SWITCHER_INPUT_FILE")) && *e;
    int source = SRC_NONE;
    const char *src_env = getenv("SWITCHER_SOURCE");
    int force_src = SRC_NONE;
    if (src_env && strcmp(src_env, "evdev") == 0) force_src = SRC_EV;
    if (src_env && strcmp(src_env, "sysfs") == 0) force_src = SRC_SYS;
    int64_t last_trigger = -COOLDOWN_MS;
    int64_t start_ms = mono_ms();
    int64_t last_sysfs_probe = 0;
    long ev_total = 0, ev_at_switch = 0;
    struct sw_input_event evs[64];

    while (!stopflag) {
        if (filemode) {
            if (fd < 0) {
                fd = open(getenv("SWITCHER_INPUT_FILE"), O_RDONLY | O_NONBLOCK);
                if (fd < 0) { usleep(500000); continue; }
                int rmax = RAW_MAX_D;
                if ((e = getenv("SWITCHER_RAW_MAX")) && *e) rmax = atoi(e);
                gesture_init(&g, 0, rmax, 0, rmax);
                gesture_config_env(&g);
                logline("input: FIL-mode %s (raw 0..%d)", getenv("SWITCHER_INPUT_FILE"), rmax);
                source = SRC_EV;
            }
        } else if (nnodes == 0 && fd < 0) {
            nnodes = open_nodes(nodes, &g);
            if (nnodes > 0) gesture_config_env(&g);   /* range fra node + env-gestus */
            if (!sys.ok) sysfs_probe(&sys, &g);
            if (nnodes == 0 && !sys.ok) {
                /* åbn-fejl (node væk/permissions) → idle-loop, ingen crash */
                usleep(500000);
                continue;
            }
            if (force_src == SRC_SYS && sys.ok) source = SRC_SYS;
            else if (force_src == SRC_EV) source = SRC_EV;
            else if (nnodes > 0) source = SRC_EV;
            else source = SRC_SYS;   /* ingen evdev-noder, men sysfs fundet */
            logline("input-kilder: %d evdev-noder, sysfs=%s → kilde=%s", nnodes,
                    sys.ok ? sys.dir : "ingen",
                    source == SRC_SYS ? "sysfs" : "evdev");
        }
        if (!filemode && !sys.ok && mono_ms() - last_sysfs_probe > 5000) {
            last_sysfs_probe = mono_ms();
            sysfs_probe(&sys, &g);
            if (sys.ok && (source == SRC_NONE || force_src == SRC_SYS)) source = SRC_SYS;
        }

        struct pollfd pfds[MAX_NODES];
        int npfd = 0;
        if (filemode) {
            if (fd >= 0) { pfds[npfd].fd = fd; pfds[npfd].events = POLLIN; pfds[npfd].revents = 0; npfd++; }
        } else {
            for (int i = 0; i < nnodes; i++) {
                pfds[npfd].fd = nodes[i].fd;
                pfds[npfd].events = POLLIN;
                pfds[npfd].revents = 0;
                npfd++;
            }
        }
        int pr = npfd ? poll(pfds, (nfds_t)npfd, 50) : (usleep(50000), 0);
        if (pr < 0) {
            if (errno == EINTR) continue;
            for (int i = 0; i < nnodes; i++) close(nodes[i].fd);
            nnodes = 0;
            if (fd >= 0) { close(fd); fd = -1; }
            usleep(500000);
            continue;
        }
        int64_t now = mono_ms();
        /* --- evdev: læs ALLE noder (bypass a); feed kun hvis kilde=evdev --- */
        for (int i = 0; i < npfd; i++) {
            if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (filemode) {
                    logline("input: POLLERR/HUP — genåbner");
                    close(fd); fd = -1; usleep(500000);
                } else {
                    logline("input: %s POLLERR/HUP — lukker node", nodes[i].path);
                    close(nodes[i].fd);
                    nodes[i].fd = -1;   /* komprimeres efter loopet (pfds-index bevares) */
                }
                continue;
            }
            if (!(pfds[i].revents & POLLIN)) continue;
            ssize_t n = read(pfds[i].fd, evs, sizeof evs);
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                logline("input: read-fejl %s — genåbner node", strerror(errno));
                if (filemode) { close(fd); fd = -1; }
                else { close(nodes[i].fd); nodes[i].fd = -1; }
                usleep(500000);
                continue;
            }
            if (n == 0) {
                if (filemode) { logline("input: EOF (fil-mode) — exit"); goto done; }
                logline("input: EOF — genåbner node");
                close(nodes[i].fd);
                nodes[i].fd = -1;
                usleep(500000);
                continue;
            }
            size_t cnt = (size_t)n / sizeof(struct sw_input_event);
            ev_total += (long)cnt;
            if (!filemode && !nodes[i].logged_first) {
                nodes[i].logged_first = 1;
                logline("input: %s leverer events (%d stk) — read-only bypass (a) aktiv",
                        nodes[i].path, (int)cnt);
            }
            if (source != SRC_EV) continue;
            for (size_t k = 0; k < cnt; k++) {
                int64_t ev_ms = (int64_t)evs[k].tv_sec * 1000 + evs[k].tv_usec / 1000;
                int before = g.count;
                if (gesture_feed(&g, evs[k].type, evs[k].code, evs[k].value, ev_ms)) {
                    if (now - last_trigger < COOLDOWN_MS) {
                        logline("gestus udløst men cooldown aktiv — ignoreret");
                    } else {
                        last_trigger = now;
                        handle_trigger();
                    }
                } else if (g.mode == GEST_CORNER && g.count > before) {
                    logline("gestus: gyldig long-press i hjørne talt (%d/3)", g.count);
                }
            }
        }
        if (!filemode) {   /* komprimér node-listen (lukkede fd'er markeret -1) */
            int w = 0;
            for (int i = 0; i < nnodes; i++)
                if (nodes[i].fd >= 0) nodes[w++] = nodes[i];
            nnodes = w;
        }
        /* --- sysfs (bypass b): poll + kilde-arbitrering --- */
        if (sys.ok) {
            int sx = 0, sy = 0, pn = 0;
            if (sysfs_sample(&sys, &sx, &sy, &pn)) {
                if (pn > 0 || sx != sys.last_x || sy != sys.last_y)
                    sys.touch_ms = now;
                sys.last_x = sx; sys.last_y = sy; sys.last_points = pn;
                if (source == SRC_SYS) {
                    if (gesture_sysfs_sample(&g, sx, sy, pn, now)) {
                        if (now - last_trigger < COOLDOWN_MS) {
                            logline("gestus udløst men cooldown aktiv — ignoreret");
                        } else {
                            last_trigger = now;
                            handle_trigger();
                        }
                    }
                }
            } else if (source == SRC_SYS) {
                logline("input: sysfs læs-fejl — tilbage til evdev");
                source = SRC_EV;
                gesture_init(&g, g.raw_min_x, g.raw_max_x, g.raw_min_y, g.raw_max_y);
                gesture_config_env(&g);
            }
            if (source == SRC_EV && force_src == SRC_NONE && ev_total == 0 &&
                now - start_ms > 3000 && now - sys.touch_ms < 2000) {
                logline("input: evdev BLIND (0 events mens sysfs ser touch) — "
                        "stock grabber sandsynligvis event0 → skifter til sysfs (bypass b)");
                source = SRC_SYS;
                ev_at_switch = ev_total;
                gesture_init(&g, g.raw_min_x, g.raw_max_x, g.raw_min_y, g.raw_max_y);
                gesture_config_env(&g);
            }
            if (source == SRC_SYS && force_src == SRC_NONE && ev_total > ev_at_switch) {
                logline("input: evdev leverer igen events (shim/duplikat?) — tilbage til evdev");
                source = SRC_EV;
                gesture_init(&g, g.raw_min_x, g.raw_max_x, g.raw_min_y, g.raw_max_y);
                gesture_config_env(&g);
            }
        }
        waitpid(-1, NULL, WNOHANG);      /* reap evt. B-barn */
    }
done:
    logline("switcher stopper (signal/EOF)");
    for (int i = 0; i < nnodes; i++) close(nodes[i].fd);
    if (fd >= 0) close(fd);
    sysfs_close(&sys);
    if (logf) fclose(logf);
    return 0;
}

#else
/* ============================ SELF-TEST (host) ============================ */
static int tests_run = 0, tests_fail = 0;
#define CHECK(cond, name) do { \
    tests_run++; \
    if (cond) { printf("  OK   %s\n", name); } \
    else { tests_fail++; printf("  FEJL %s\n", name); } \
} while (0)

static int64_t T; /* syntetisk tidsakse (ms) */
static void ev(Gesture *g, uint16_t type, uint16_t code, int32_t v, int *trig)
{
    if (gesture_feed(g, type, code, v, T)) *trig = 1;
}
static void raw_xy(Gesture *g, int x, int y, int *trig)
{
    /* screen→raw som tinj.c: raw = px*(max-min+1)/480 */
    int rx = x * (g->raw_max_x - g->raw_min_x + 1) / SCREEN_W;
    int ry = y * (g->raw_max_y - g->raw_min_y + 1) / SCREEN_H;
    ev(g, EV_ABS_, ABS_MT_POSITION_X_, rx, trig);
    ev(g, EV_ABS_, ABS_MT_POSITION_Y_, ry, trig);
    ev(g, EV_ABS_, ABS_X_, rx, trig);
    ev(g, EV_ABS_, ABS_Y_, ry, trig);
}
static void down(Gesture *g, int x, int y, int *trig)
{
    ev(g, EV_ABS_, ABS_MT_TRACKING_ID_, 1, trig);
    raw_xy(g, x, y, trig);
    ev(g, EV_KEY_, BTN_TOUCH_, 1, trig);
    ev(g, EV_SYN_, SYN_REPORT_, 0, trig);
}
static void move(Gesture *g, int x, int y, int *trig)
{
    raw_xy(g, x, y, trig);
    ev(g, EV_SYN_, SYN_REPORT_, 0, trig);
}
static void up(Gesture *g, int *trig)
{
    ev(g, EV_ABS_, ABS_MT_TRACKING_ID_, -1, trig);
    ev(g, EV_KEY_, BTN_TOUCH_, 0, trig);
    ev(g, EV_SYN_, SYN_REPORT_, 0, trig);
}
/* hold i ms; returnerer 1 hvis gestus udløstes undervejs */
static int hold(Gesture *g, int x, int y, int ms, int gap_after)
{
    int trig = 0;
    down(g, x, y, &trig);
    T += ms;
    up(g, &trig);
    T += gap_after;
    return trig;
}

int main(void)
{
    Gesture g;
    int trig;

    printf("switcher self-test (center-hold + corner-fallback + skalering + mode-logik)\n");

    /* 1: center happy path — ét hold 5200 ms i (240,240), raw 0..480 (device) */
    gesture_init(&g, 0, 480, 0, 480); T = 100000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: ét hold 5,2 s i (240,240) → trigger ved release");

    /* 2: center for kort (3000 ms < 5000) */
    gesture_init(&g, 0, 480, 0, 480); T = 200000; trig = 0;
    trig |= hold(&g, 240, 240, 3000, 0);
    CHECK(trig == 0, "center: hold 3,0 s (<5 s) → ingen trigger");

    /* 3: center præcis 5000 ms → trigger (grænse) */
    gesture_init(&g, 0, 480, 0, 480); T = 250000; trig = 0;
    trig |= hold(&g, 240, 240, 5000, 0);
    CHECK(trig == 1, "center: hold præcis 5000 ms → trigger");

    /* 4: off-center (300,240): afstand 60 > radius 40 */
    gesture_init(&g, 0, 480, 0, 480); T = 300000; trig = 0;
    trig |= hold(&g, 300, 240, 5200, 0);
    CHECK(trig == 0, "center: hold i (300,240) uden for radius → ingen trigger");

    /* 5: radius-grænse (raw 480: screen 279 = radius 39 gyldig; 281 = 41 ugyldig) */
    gesture_init(&g, 0, 480, 0, 480); T = 350000; trig = 0;
    trig |= hold(&g, 280, 240, 5200, 0);
    CHECK(trig == 1, "center: (280,240) → screen 279, radius 39 ≤ 40 → trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 360000; trig = 0;
    trig |= hold(&g, 282, 240, 5200, 0);
    CHECK(trig == 0, "center: (282,240) → screen 281, radius 41 > 40 → ingen trigger");

    /* 6: finger driver ud af zonen under hold → ugyldig */
    gesture_init(&g, 0, 480, 0, 480); T = 400000; trig = 0;
    down(&g, 240, 240, &trig); T += 2000;
    move(&g, 350, 350, &trig); T += 2000;
    move(&g, 240, 240, &trig); T += 1500;
    up(&g, &trig);
    CHECK(trig == 0, "center: hold der forlader zonen undervejs → ingen trigger");

    /* 7: center med raw 0..4095 (sim-deklareret range) — samme formel */
    gesture_init(&g, 0, 4095, 0, 4095); T = 500000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: raw 0..4095 stream → trigger (skalering konsistent)");

    /* 8: multi-press ødelægger ikke center (kun ét gyldigt hold tæller) */
    gesture_init(&g, 0, 480, 0, 480); T = 600000; trig = 0;
    trig |= hold(&g, 240, 240, 80, 200);     /* tap */
    trig |= hold(&g, 240, 240, 80, 200);     /* tap */
    CHECK(trig == 0, "center: to korte taps → ingen trigger");
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: taps efterfulgt af ét 5,2 s hold → trigger");

    /* 9: corner-fallback — 3×1600 ms i (45,45), 300 ms gap → span 5400 ≤6000 */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 700000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 1, "corner: 3×1,6 s hjørne-hold inden for 6 s → trigger");
    CHECK(g.count == 0, "corner: tælling nulstillet efter trigger");

    /* 10: corner kun 2 holds → ingen trigger */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 800000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 0 && g.count == 2, "corner: 2 holds → ingen trigger (2/3)");

    /* 11: corner for korte presses (1000 ms < 1500) */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 900000; trig = 0;
    trig |= hold(&g, 45, 45, 1000, 200);
    trig |= hold(&g, 45, 45, 1000, 200);
    trig |= hold(&g, 45, 45, 1000, 0);
    CHECK(trig == 0 && g.count == 0, "corner: 3×1,0 s (for kort) → ingen trigger");

    /* 12: center-hold tæller IKKE i corner-mode (og omvendt) */
    gesture_init(&g, 0, 480, 0, 480); gesture_set_corner(&g, 0); T = 1000000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 0 && g.count == 0, "corner: center-hold tæller ikke");
    gesture_init(&g, 0, 4095, 0, 4095); T = 1050000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 0, "center: 3×1,6 s hjørne-hold → ingen trigger (kun ét hold tæller)");

    /* 13: skalering raw→screen (tinj.c-formlen, begge ranges) */
    CHECK(scale_axis(0, 0, 4095, 480) == 0,        "raw 0 → screen 0 (4095-range)");
    CHECK(scale_axis(4095, 0, 4095, 480) == 479,   "raw 4095 → screen 479");
    CHECK(scale_axis(2048, 0, 4095, 480) == 240,   "raw 2048 → screen 240 (center)");
    CHECK(scale_axis(0, 0, 480, 480) == 0,         "raw 0 → screen 0 (480-range)");
    CHECK(scale_axis(480, 0, 480, 480) == 479,     "raw 480 → screen 479 (device-max)");
    CHECK(scale_axis(240, 0, 480, 480) == 239,     "raw 240 → screen 239 (±1 px, tinj-formel)");
    CHECK(scale_axis(767, 0, 4095, 480) == 89,     "raw 767 → screen 89 (hjørne)");
    CHECK(scale_axis(768, 0, 4095, 480) == 90,     "raw 768 → screen 90 (uden for)");

    /* 14: raw-range auto-valg (pick_ranges): fallback = device-konstant 480 */
    AbsProbe p;
    int a, b, c, d;
    memset(&p, 0, sizeof p);
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(a == 0 && b == 480 && c == 0 && d == 480, "pick_ranges: ingen ioctl-svar → 0..480");
    p.have_mx = 1; p.mx.minimum = 0; p.mx.maximum = 480;
    p.have_my = 1; p.my.minimum = 0; p.my.maximum = 480;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(a == 0 && b == 480 && c == 0 && d == 480, "pick_ranges: EVIOCGABS 0..480 → 0..480");
    p.mx.maximum = 4095; p.my.maximum = 4095;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(b == 4095 && d == 4095, "pick_ranges: EVIOCGABS 0..4095 → 0..4095 (auto)");
    p.have_x = 1; p.x.minimum = 5; p.x.maximum = 4000;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(b == 4095, "pick_ranges: MT-akser har forrang over ST-akser");
    memset(&p, 0, sizeof p);
    p.have_x = 1; p.x.minimum = 5; p.x.maximum = 4000;
    p.have_y = 1; p.y.minimum = 6; p.y.maximum = 4001;
    pick_ranges(&p, &a, &b, &c, &d);
    CHECK(a == 5 && b == 4000 && c == 6 && d == 4001, "pick_ranges: ST-only probe bruges");

    /* 15: sysfs-sample (bypass b): center-hold via polling */
    gesture_init(&g, 0, 480, 0, 480); T = 1100000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 2600; trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 2600; trig |= gesture_sysfs_sample(&g, 240, 240, 0, T);
    CHECK(trig == 1, "sysfs: center-hold 5,2 s via point_num → trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 1200000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 3000; trig |= gesture_sysfs_sample(&g, 240, 240, 0, T);
    CHECK(trig == 0, "sysfs: hold 3,0 s → ingen trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 1300000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 320, 240, 1, T);
    T += 5200; trig |= gesture_sysfs_sample(&g, 320, 240, 0, T);
    CHECK(trig == 0, "sysfs: off-center (320,240) → ingen trigger");
    gesture_init(&g, 0, 480, 0, 480); T = 1400000; trig = 0;
    trig |= gesture_sysfs_sample(&g, 240, 240, 1, T);
    T += 2000; trig |= gesture_sysfs_sample(&g, 350, 350, 1, T);
    T += 3200; trig |= gesture_sysfs_sample(&g, 240, 240, 0, T);
    CHECK(trig == 0, "sysfs: drift ud af zonen → ingen trigger");

    /* 16: env-konfiguration */
    setenv("SWITCHER_GESTURE", "corner", 1);
    setenv("SWITCHER_HOLD_MS", "1700", 1);
    gesture_init(&g, 0, 480, 0, 480); gesture_config_env(&g);
    CHECK(g.mode == GEST_CORNER && g.hold_ms == 1700, "env: SWITCHER_GESTURE=corner + HOLD_MS");
    setenv("SWITCHER_GESTURE", "center", 1);
    setenv("SWITCHER_CENTER_X", "100", 1);
    setenv("SWITCHER_CENTER_Y", "120", 1);
    setenv("SWITCHER_CENTER_R", "55", 1);
    unsetenv("SWITCHER_HOLD_MS");
    gesture_init(&g, 0, 480, 0, 480); gesture_config_env(&g);
    CHECK(g.mode == GEST_CENTER && g.cx == 100 && g.cy == 120 && g.r2 == 55 * 55 &&
          g.hold_ms == 5000, "env: center X/Y/R + default hold 5000");
    unsetenv("SWITCHER_GESTURE"); unsetenv("SWITCHER_CENTER_X");
    unsetenv("SWITCHER_CENTER_Y"); unsetenv("SWITCHER_CENTER_R");
    gesture_init(&g, 0, 480, 0, 480); gesture_config_env(&g);
    CHECK(g.mode == GEST_CENTER && g.cx == 240 && g.cy == 240 &&
          g.r2 == 40 * 40 && g.hold_ms == 5000, "env: defaults = center (240,240) r40 5000 ms");

    /* 17: mode-klassifikation */
    CHECK(classify_exe("/customer/demo_v9") == MODE_A,     "exe /customer/demo_v9 → A");
    CHECK(classify_exe("/new_customer/demo_v9") == MODE_B, "exe /new_customer/demo_v9 → B");
    CHECK(classify_exe("/tmp/andet") == MODE_NONE,         "ukendt exe → NONE");
    CHECK(classify_exe(NULL) == MODE_NONE,                 "NULL exe → NONE");

    /* 18: beslutningslogik */
    Mode mA[1] = { MODE_A };
    Mode mB[1] = { MODE_B };
    Mode mAB[2] = { MODE_A, MODE_B };
    Mode mAA[2] = { MODE_A, MODE_A };
    CHECK(decide_action(mA, 1) == ACT_SWITCH_A,    "[A] → SWITCH_A");
    CHECK(decide_action(mB, 1) == ACT_IDLE_B,      "[B] → IDLE_B (ourui har selv knap)");
    CHECK(decide_action(mAB, 2) == ACT_IDLE_MIXED, "[A,B] → IDLE_MIXED (sikrest)");
    CHECK(decide_action(mAA, 2) == ACT_IDLE_NONE,  "[A,A] (transient fork) → IDLE_NONE");
    CHECK(decide_action(NULL, 0) == ACT_IDLE_NONE, "[] → IDLE_NONE");

    /* 19: ny sekvens efter trigger virker igen (center) */
    gesture_init(&g, 0, 480, 0, 480); T = 1500000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: første trigger");
    T += 20000; trig = 0;
    trig |= hold(&g, 240, 240, 5200, 0);
    CHECK(trig == 1, "center: andet hold efter trigger → trigger igen");

    /* 20: MT-only stream (tracking-id uden BTN_TOUCH) driver center-detektion */
    gesture_init(&g, 0, 480, 0, 480); T = 1600000; trig = 0;
    ev(&g, EV_ABS_, ABS_MT_TRACKING_ID_, 7, &trig);
    raw_xy(&g, 240, 240, &trig);
    ev(&g, EV_SYN_, SYN_REPORT_, 0, &trig);
    T += 5200;
    ev(&g, EV_ABS_, ABS_MT_TRACKING_ID_, -1, &trig);
    ev(&g, EV_SYN_, SYN_REPORT_, 0, &trig);
    CHECK(trig == 1, "center: MT-only stream (tracking-id) → trigger");

    /* 21: corner vindues-udløb + taps mellem holds (legacy bevaret) */
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 1700000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 300);
    trig |= hold(&g, 45, 45, 1600, 0);
    T += 7000; /* >6 s siden første down */
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 0 && g.count == 1, "corner: udløbet vindue → tælling forfra (1/3)");
    gesture_init(&g, 0, 4095, 0, 4095); gesture_set_corner(&g, 0); T = 1800000; trig = 0;
    trig |= hold(&g, 45, 45, 1600, 100);
    trig |= hold(&g, 45, 45, 80, 100);
    trig |= hold(&g, 45, 45, 1600, 100);
    trig |= hold(&g, 45, 45, 80, 100);
    trig |= hold(&g, 45, 45, 1600, 0);
    CHECK(trig == 1, "corner: taps mellem gyldige holds → trigger (span 5,16 s)");

    printf("\n%d tests, %d fejl\n", tests_run, tests_fail);
    return tests_fail ? 1 : 0;
}
#endif
