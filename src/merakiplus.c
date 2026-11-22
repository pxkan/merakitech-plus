/* merakiplus.c — Meraki+ (identifier: merakiplus): a custom display UI for the round
 * 480x480 screen, v6.16: feature parity (settings + activity control), its own visual
 * design system, boot-slot integration, product name + two languages — and from v6.4 a SIM build.
 *
 * v6.20 = v6.19 + the fixes of the 2026-11 code review (docs/dev/REVIEW-2026-11.md, CHANGELOG): stepper repeat after a failed save;
 *  SAVE refused while a start is pending; the existing STOP stays offered for a latched start word; queued STOPs survive a failing
 *  frame; a pre-arm/disarm cmd8 without its 0x88 echo is resent (max 2); an unusable Wi-Fi password is never "open"; a one-shot saved
 *  without a clock is armed once the clock is set; config.json.orig via tmp + rename; glyph_next stops at the NUL; NaN-safe SIM JSON,
 *  hardened ctrl parser and clamped display of absurd config values. The src/mp/ module split (unity build) came just before.
 *
 * v6.19 = v6.18.1 + (1) NO READY TONE AT A POWER-ON WITH HOT BOILERS (user: jingle + both ready tones back-to-back is redundant): at every
 *  power-on (cold boot of the display, OFF -> on, sleep -> wake) a boiler whose heat stage is already 100 over the first 1.5 s counts as
 *  announced — only the jingle plays; a tone needs a real cold -> hot transition during the session (re-arm after cooling unchanged).
 *  (2) nicer sounds: tools/gen_audio.py now synthesizes struck-instrument timbres (marimba / glockenspiel partials + a soft FM bell + a
 *  small room) instead of sine beeps; the three audio/<name>.wav files regenerated. Control logic, frames, the helper and the settings are unchanged.
 * v6.18.1 = v6.18 + a fix in the sound helper only (src/merakiplus_snd.c puts libpthread.so.0 into the global scope before
 *  libmi_common.so; docs/audio.md §3). In this file only MERAKIPLUS_VERSION changed.
 *
 * v6.18 = v6.17 + SOUND (UI_v6.md "CHANGES-v6.18", docs/audio.md). The stock UI plays a jingle at power-on (the audio track of
 *  video/power.mp4, page_logo 0x1001B8) and ONE "pre-heat complete" sound when BOTH boilers are hot (uartRxProcess 0x145BA0: words 62 AND 63
 *  > 99, once per power-on). Meraki+ plays its OWN short tones (audio/<name>.wav, made by tools/gen_audio.py): a start jingle once per
 *  power-on and a DIFFERENT ready tone per boiler — extract (word 62, the MCU's heat stage, reaches 100) and steam (word 63) — so you
 *  can hear which boiler is ready. Once per power-on, re-armed only after real cooling (stage <= 30 or 15 °C below the ready point);
 *  silent while off/asleep; dropped while an activity runs (like stock). SYSTEM › DISPLAY · SOUND: SOUND (off / on / stock sounds),
 *  READY TONE (off / brew / steam / both boilers) — own file /new_customer/merakiplus/sound — and VOLUME (config "volume", moved here
 *  from STOCK UI, capped at 70 like stock). Playback via the helper merakiplus_snd (glibc; the display's own libmi_ao.so, exactly the
 *  MI_AO calls of stock's player libhvplayer.so). DIAGNOSTICS › SOUND shows mode · volume and the last event. No UART frame changed.
 * v6.16 = v6.15 + TIME & SCHEDULING (UI_v6.md "CHANGES-v6.16", docs/time-scheduling.md). User bugs: (1) TIME FORMAT was inverted —
 *  stock update_time 0xD5944 XORs model[0x59] with 1 before the test (missed in v6.1): timemode 1 = 24 h, 0 = 12 h A.M./P.M.; (2) no clock —
 *  HOME's top line now shows the local time (SoC UTC + stock zone table 0x1A5890 + DST when sumtime is on), DIAGNOSE a TIME · SCHEDULE
 *  block; (3) no planned pre-heat — a schedule (on/off, hh:mm, once / every day / Mon-Fri / Sat-Sun) whose next occurrence goes out as
 *  the cmd8 alarm with the power-off frame exactly as stock (performSleepActions 0x148324 -> powerOffSend 0x14700C(day, hour, minute))
 *  and is pre-armed while on; the MCU's sleep task wakes on it (reset -> heating; the display cold-boots -> boot push). Plus optional
 *  AUTO OFF after 15/20/30/60 min without touch or activity (stock: fixed 30 min, SleepRefresh 0x149268). Schedule + auto off live in
 *  merakiplus's own file /new_customer/merakiplus/schedule; config.json gets only timemode/sumtime as before. The cmd8 date/time is now stock's
 *  local time (v6.14 sent the SoC's UTC).
 * v6.15 = v6.14 + the HOME graph RESETS WHEN AN ACTIVITY STARTS (UI_v6.md "CHANGES-v6.15" + §14.7). User bug: grind/brew/water start
 *  tares the weight to 0 (MCU), but the graph's 40 s window still held the pre-start history (e.g. a 200-300 g cup placed on the
 *  scale), so the autoscale was dominated by the cup step and the 0 -> 36 g curve was a flat line at the bottom. Now graph_push()
 *  clears the ring buffer (and restarts the chart filters) on the IDLE -> ACTIVE transition of the main phase (phase_main: grind,
 *  brew, water, or steam alone); the scale is recomputed from the fresh buffer, so the curve starts at 0 and fills the height.
 *  A concurrent secondary activity (steam/wand flush during a brew or grind) is NOT an idle -> active transition and keeps the
 *  curve. Within 1.5 s of the start the buffer is re-anchored once more if the weight source latches late (a physical grind
 *  start) or the tare lands late (weight drops > 2 g in one sample). One ring buffer holds pressure, weight and milk, so all
 *  series are action-relative (flow is word 52, a stat number without history). DISPLAY ONLY: regs, control, auto-stop and
 *  thresholds read the raw words; the DIAGNOSTICS RAW view still shows raw (unconditioned) values of the buffer.
 * v6.14 = v6.13 + the PHYSICAL POWER BUTTON (UI_v6.md "CHANGES-v6.14" + §19). User bug: the button did nothing in merakiplus —
 *  merakiplus only read the touch node (event0); the button is the k62091 node (event1, EV_KEY), which stock's app_powerKey_thread (0x140AB4)
 *  reads. The stock semantics are reversed (dealPowerState 0x14911C / powerOff 0x149074 / SleepRefresh 0x149268) and mirrored:
 *  HOLD the button (stock: two one-second ticks = 1-2 s; merakiplus 1.5 s) -> cmd8 time/alarm frame (alarm 0/0/0 = none) + 80 ms ->
 *  cmd6 sub0 len2 = [0, 0] (word 0 = power off, word 1 = awake) -> OFF view, backlight off, keep-awake paused.
 *  Blocked (like stock's isExtractionOrSteamSwitchOn 0x1481E0) while brew/steam/grind is running — stock NEVER stops an activity.
 *  The MCU (sleep task 0x2F52) deletes the status task itself (no UART replies), resets work_sta + outputs and wakes ONLY on a
 *  hold of the button (>= 0.8 s, its own GPIO PC.8 -> MCU reset 0x326) or an alarm. merakiplus: button again -> TURNING ON… -> when
 *  the controller replies with word 0 = 1 -> PowerOn (sub0=[1] + sub1=[0], as at boot) -> HOME. No SoC suspend
 *  (stock: echo mem > /sys/power/state + killall demo_v9) — net watchdog and telnet keep running; only the backlight goes off.
 * v6.13 = v6.12 + MANUAL STOP for WATER, BREW and GRIND (UI_v6.md "CHANGES-v6.13" + §18). User bug: hot water could not be
 *  stopped from the screen — only steam had a STOP button. The stop words are reversed from the MCU firmware and cross-checked against
 *  the stock UI's own stop calls: WATER cmd6 sub16 len1 word16=0 (+ sub19 len1 word19=0, if word 19 != 0), BREW cmd6 sub22 len1
 *  word22=0, GRIND cmd6 sub6 len1 word6=0 (addresses at act_push_stop()). HOME shows the STOP button (STOP WATER / STOP BREW /
 *  STOP GRIND) in the same place as STOP STEAM while the activity's bits are set; with latched steam at the same time = two buttons side by side.
 *  Press -> toast "STOPPING …" -> frame (CRC + ACK + one retransmit) -> "… STOPPED" once the MCU has cleared the bits; otherwise
 *  "STOP: NO REPLY" / "STOP NOT CONFIRMED" (5 s). Auto-stop is unchanged (merakiplus sends nothing when the MCU stops by itself).
 *  v6.12 = the v6.11 UI (source = merakiplus_v6_12.c, binary 725386cc…) + the boot agent's gesture fix (boot/GESTUS-FIX.md).
 *
 * v6.10 = v6.9 + PRESSURE ESTIMATE in bar (UI_v6.md "CHANGES-v6.10" + §16): "PRESSURE* UNCALIBRATED" is replaced by "≈BAR · EST.".
 *  One linear anchor: the pre-infusion plateau in the capture (BREW.md §6: word 51 = 358 raw at 6.06 s) = config
 *  preInfusionPressure (4 bar) -> k = preInfusionPressure / P_ANCHOR_RAW bar per raw unit (0.01117). The anchor is read from
 *  config (SET[S_PIPRESS]), so a changed config changes the scale. Above P_EST_MAX (20 bar) the linear value is not physical
 *  (deadhead/maintenance pulses up to 16670 raw = 186 bar): the BAR field shows "PULSE", DIAGNOSTICS "MAINTENANCE PULSE" — never
 *  a number. The basis is shown on DIAGNOSTICS (new block PRESSURE · ESTIMATE) and in a toast when tapping the chart/BAR field. The RAW view
 *  (DIAGNOSTICS → GRAPH) shows word 51 as raw numbers. The section "v6.10: pressure estimate" below has the constants and caveats.
 *  Control, writes, auto-stop and thresholds still read only raw values — v6.10 only changes what is displayed.
 *
 * v6.9 = v6.8 + DISPLAY CONDITIONING of the graph and the hero number (UI_v6.md "CHANGES-v6.9" + §14): the chart and
 *  hero/gauge/BAR show median-3 + EMA of the raw samples; the chart scale ignores short deadhead pulses (they are shown as
 *  small ticks along the top edge). The ring buffer and reg[] are still RAW — control, writes, auto-stop, thresholds and
 *  DIAGNOSTICS counters are unchanged. DIAGNOSTICS → GRAPH switches to the raw view (this session only). The section
 *  "v6.9: display conditioning" below has the constants and how to tune them.
 *
 * v6.8 = v6.7 without the REVERT button on HOME (UI_v6.md "CHANGES-v6.8"): it was a phase-2 safeguard.
 *  HOME = gauge + stats + activity icons + page dots; there is no touch zone where the pill used to be.
 *  The escape routes are unchanged: SYSTEM "SWITCH TO ORIGINAL NOW" (in run.sh mode = clean exit, run.sh SIGCONTs)
 *  + the BOOT UI row, the switcher gesture and run.sh's trap.
 *
 * v6.7 = v6.6 + two things (UI_v6.md "CHANGES-v6.7"):
 *  - NET WATCHDOG: wifi at boot + self-heal (the section "NET" below). On a slot B boot nothing started
 *    wifi (INSTALL-REPORT §3.1). merakiplus now does what stock's libmi_wlan does according to /config/wifi/wlan.json —
 *    init script, conf from config.json (byte-identical w. save_config), wpa_supplicant and udhcpc with
 *    stock's arguments — but ONLY when the stack is down. A live wpa_supplicant is never touched, and still
 *    no signal is sent to any process (0 kill calls). DIAGNOSTICS has a NET row; log in
 *    /tmp/merakiplus_net.log; `merakiplus --net-status` (read-only) and `--net-watch [s]`.
 *  - WORD 113 IN THE BOOT PUSH: the lo byte is sent as 0 (stock's captured boot push, GROUND-TRUTH.md);
 *    the SAVE path still packs breakTime.
 *
 * v6.6 = v6.5 + three things (UI_v6.md "CHANGES-v6.6"):
 *  - PHASE SEMANTICS per STEAMLIVE.md (proven live): two steam paths — LATCHED (word 24 = 1, UI start,
 *    auto-stop at word 48 >= word 25: STEAM·AUTO w. target and STOP button) and MOMENTARY wand/flush (bits 5+14
 *    without word 24 = 1: STEAM·WAND, no auto-stop expectation, no STOP button). Concurrent phases
 *    (brew bits + steam bits, 0x5327): HOME shows the main activity AND a steam marker, weight + milk temp.
 *    The state contract's phase (phase_raw: steam first) is unchanged; phase_main() is new.
 *  - 64-BIT CLOCK: all times are ms_t (int64_t). Up to and including v6.5 the clock was a 32-bit `long` on the device and
 *    wrapped after 24.86 days (poll/render could stall, keep-awake could run away). The main loop's
 *    timers live in `lt` and are driven by loop_poll()/loop_graph()/loop_render_due()/loop_rendered().
 *  - RE-PUSH AFTER MCU RESTART: if the controller's version request (0x89) comes again after the
 *    first handshake, or words 112/113 hold the MCU's start values (0 / 0), the boot push is sent
 *    again — same blocks, same frame policy, same activity guard. Counted on DIAGNOSTICS.
 *
 * v6.5 = v6.4 + two fixes in the DEVICE build (UI_v6.md "CHANGES-v6.5"):
 *  - BOOT PUSH on the device too: at boot cmd6 sub1 len30 + cmd6 sub112 len3 are sent from
 *    config.json — the same two blocks as the stock UI's fuart_thread — through the frame policy and
 *    the write queue (ACK wait + one retransmit). On a slot B boot the stock UI never starts, and
 *    the controller would otherwise sit on its own start values (brew 92.0 °C / group 50 °C).
 *    ACTIVITY GUARD: if an activity is already running the first time work_sta is known, the
 *    push is skipped (run.sh started in the middle of a brew: the block would reset the start words).
 *  - RX RESYNC WATCHDOG: a header candidate with an impossible length is discarded immediately, and an incomplete
 *    candidate that has waited RX_RESYNC_MS is discarded, so the parser finds the next AA 55. No bytes
 *    are consumed without a CRC verdict. New counter on DIAGNOSTICS (RX CRC / RESYNC) + boot push status.
 *
 * v6.4 = v6.3 + SIM HAL against the simulated test environment (sim/CONTRACT.md; UI_v6.md "CHANGES-v6.4"):
 *  - -DMERAKIPLUS_SIM (without MERAKIPLUS_PREVIEW) = REAL-TIME build without hardware: framebuffer -> memory
 *    + file, touch -> FIFO w. raw 16-byte evdev records, UART -> pty, all system paths under a
 *    sandbox root, state JSONL per frame and a ctrl socket w. test hooks. Same main loop,
 *    same protocol, save, activity and drawing code as the device.
 *  - WITHOUT -DMERAKIPLUS_SIM all of this is gone: the source after the preprocessor is the same as
 *    v6.3 except for the version string and one toast text (see below).
 *  - BOOT PUSH (sub1 len30 + sub112 len3, like the stock UI's fuart_thread) existed in v6.4 ONLY in
 *    the SIM and preview builds (contract requirement). From v6.5 the device build sends it too (see above).
 *  - Toast on SAVE without ACK: "SAVE FAILED · NO ACK · NOTHING SAVED" (before: "NO ACK · NOTHING SAVED").
 *
 * v6.3 = v6.2 + product name + internationalisation (see UI_v6.md "CHANGES-v6.3", REVERT.md):
 *  - PRODUCT NAME: the UI is called Meraki+ (splash, HOME title line w. "+" badge, DIAGNOSTICS/ABOUT,
 *    exit texts). No generation name in any UI string — the same software runs on several
 *    machine generations. Paths, binaries and logs use the identifier merakiplus (run.sh/boot).
 *  - I18N: ONE string table (STR_TABLE) w. English (default) + Danish for EVERY visible text;
 *    lookup per render (TR()), so a language switch takes effect live without a restart.
 *  - SYSTEM row UI LANGUAGE / UI-SPROG: saved in merakiplus's OWN file /new_customer/merakiplus/lang
 *    ("en"/"da", atomic; missing/unreadable/unknown -> English). The stock UI's config.language
 *    is not touched; its row (STOCK LANG. / STOCK-SPROG) is independent.
 *  - Numbers, decimal point and units (°C, g, s, bar) are the same in both languages; only words are translated.
 *  - Protocol, frame policy, keep-awake, EVIOCGRAB, circle clip, boot logic and run.sh: UNCHANGED.
 *
 * v6.2 = v6.1 + boot-slot integration (see UI_v6.md "CHANGES-v6.2", REVERT.md):
 *  - SYSTEM row BOOT UI: MERAKIPLUS/ORIGINAL for the NEXT reboot = the flag file /customer/new
 *    (touch/rm). Only when /new_customer is mounted and its demo_v9 is merakiplus; otherwise locked.
 *  - SYSTEM action SWITCH TO ORIGINAL NOW: runtime switch without reboot per boot/BOOT.md §9.2
 *    (close UART/fb/touch -> fork+setsid+execl /customer/demo_v9 -> wait 400 ms -> exit).
 *  - REVERT/exit: visible reminder when the stock UI continues on an old RAM mirror.
 *  - DIAGNOSTICS: boot flag, exe slot, switcher daemon, latest switch event (scroll).
 *  - Protocol, frame policy, keep-awake, EVIOCGRAB, circle clip and run.sh: UNCHANGED.
 *
 * v6.1 = v6 reconciled against PARITY.md / parity_spec.json (see UI_v6.md "CHANGES-v6.1"):
 *  - SET[] uses spec-proven limits/steps; estimated max values are marked SF_EST (visible hint).
 *  - brewMode/sleepMode/sleepModeSetting removed (no stock editor); breakTime locked
 *    (MCU 0x1E10 overwrites it); hot-water TEMPERATURE is shown as a sensor (word 40).
 *  - Central frame policy in wq_push_t(): trigger words 26/27/30 can NEVER be != 0,
 *    start words only w. explicit intent, and every word must lie within its spec interval.
 *
 * Phase 2 soft-replace (unchanged contract): run.sh SIGSTOPs demo_v9, merakiplus owns
 * /dev/fb0 + /dev/ttyS1 + /dev/input/event0, run.sh SIGCONTs demo_v9 on exit.
 *
 * v6 vs v4 (details in UI_v6.md):
 *  - KEEP-AWAKE FIX: word1 is written 0 (=awake). v4 wrote 1 (=sleep/drain) — bug.
 *  - SETTINGS: 24 rows on 3 pages (list -> stepper detail -> SAVE/CANCEL).
 *    Save = cmd6 write (where the word is known) + atomic config.json persist
 *    (tmp+rename, pristine+rolling backup, md5 log). The UI is the only range guard.
 *  - ACTIVITIES: GRIND/BREW/WATER/STEAM w. observed recipes (hold-to-start),
 *    STOP for steam (word24=0), auto-stop expectation in UI text.
 *  - PHASES: steam = work_sta bits 5+14 (0x4020); sleep = word1==1 / bit8 cleared.
 *  - GRAPHICS: anti-aliased stroke font (own, no dependencies), AA primitives,
 *    270-degree gauge w. LUT, retained compositor w. dirty rects, page slide,
 *    crossfade, touch ripple, toasts, boot splash. Global circle clip kept.
 *
 * Build (device):  zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -o merakiplus_v6 merakiplus.c
 * Build (SIM):     zig cc -DMERAKIPLUS_SIM -O2 -o merakiplus_v6_sim merakiplus.c -lm          (host; the one run_e2e.py starts)
 *                  zig cc -target arm-linux-musleabihf -mcpu=cortex_a7 -static -O2 -DMERAKIPLUS_SIM -o merakiplus_v6_sim_arm merakiplus.c
 *                  env: MERAKIPLUS_SIM_TTY, MERAKIPLUS_SIM_FB, MERAKIPLUS_SIM_STATE (required), MERAKIPLUS_SIM_ROOT (required),
 *                  MERAKIPLUS_SIM_TOUCH, MERAKIPLUS_SIM_CTRL, MERAKIPLUS_SIM_NOBOOTPUSH=1, MERAKIPLUS_SIM_NOPROVISION=1,
 *                  MERAKIPLUS_SIM_BOOTPUSH_GUARD=1 (the device's activity guard: skip instead of defer),
 *                  MERAKIPLUS_SIM_UPTIME_MS=<n> (v6.6: the clock starts at n ms — real-time wrap test; exists only in the SIM build)
 * Build (preview): zig cc -DMERAKIPLUS_PREVIEW -O2 -o /tmp/merakiplus_v6_preview merakiplus.c
 *                  /tmp/merakiplus_v6_preview [--lang en|da] <outdir>   (all pages/states -> .bin)
 *                  /tmp/merakiplus_v6_preview [--lang en|da] --selftest (frame vectors, config save, i18n)
 *                  /tmp/merakiplus_v6_preview --bench         (ms/frame)
 *                  /tmp/merakiplus_v6_preview --dump-table    (SET[] as TSV -> parity_check.py)
 *                  /tmp/merakiplus_v6_preview --dump-strings  (the string table as TSV: id, class, EN, DA)
 * Env (device):    MERAKIPLUS_CONFIG, MERAKIPLUS_BAKDIR, MERAKIPLUS_NOGRAB=1, MERAKIPLUS_NOBL=1, MERAKIPLUS_READONLY=1,
 *                  MERAKIPLUS_NOBOOTPUSH=1 (v6.5: no boot push at startup — v6.4 behaviour),
 *                  MERAKIPLUS_LANGFILE=<file> (the language file; default /new_customer/merakiplus/lang),
 *                  MERAKIPLUS_SWITCH_DRY=1 (SWITCH NOW only logs what it would do; merakiplus keeps running),
 *                  MERAKIPLUS_ROOT=<dir> (prefix for the boot paths /customer /new_customer /proc /tmp; test only)
 */
#define _GNU_SOURCE
#ifdef MERAKIPLUS_WASM
#include "mp/wasm_compat.h"         /* v6.20: the browser build (Sim Studio) = the SIM build on wasm32-wasi */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <math.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <dirent.h>
/* v6.6: the time base — monotonic milliseconds in 64 bits (rationale and now_ms(): see "TIME BASE" below) */
typedef int64_t ms_t;
/* v6.4: three build types.
 *   DEVICE_RT      (default)                        the device: /dev/fb0, /dev/ttyS1, /dev/input/event0
 *   MERAKIPLUS_PREVIEW  (-DMERAKIPLUS_PREVIEW)                offline previews + selftest (synthetic time)
 *   SIM_RT         (-DMERAKIPLUS_SIM without MERAKIPLUS_PREVIEW) real time against sim/mcu_sim.py (sim/CONTRACT.md)
 * Everything SIM-specific is in "#ifdef SIM_RT" blocks or in the macros below, which are EMPTY
 * (or equal to the v6.3 expression) in the device build. */
#if defined(MERAKIPLUS_SIM) && !defined(MERAKIPLUS_PREVIEW)
#define SIM_RT 1
#endif
#if !defined(MERAKIPLUS_PREVIEW) && !defined(SIM_RT)
#define DEVICE_RT 1
#endif
#ifdef DEVICE_RT
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#ifdef SIM_RT
#include <sys/socket.h>
#include <sys/un.h>
/* contract §4: raw 16-byte evdev record (= struct input_event on 32-bit ARM), little-endian */
struct input_event { int32_t sec, usec; uint16_t type, code; int32_t value; };
_Static_assert(sizeof(struct input_event)==16,"evdev record must be 16 bytes");
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03
#define SYN_REPORT 0
#define BTN_TOUCH 0x14a
#define ABS_X 0x00
#define ABS_Y 0x01
#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39
#endif
/* ---- HAL macros: the device branch is verbatim v6.3 ---- */
#ifdef SIM_RT
static char sim_tty[300]="";
static int  sim_rc=0;
static void sim_perr(const char*s);
static void sim_toast(const char*m,uint32_t col);
static int  sim_ev_next(struct input_event*ev,ms_t now);
static void sim_env(void);
static void sim_start(void);
static void sim_tick(ms_t now);
static void sim_frame(ms_t now);
static void sim_exit(void);
static int  sim_bye_begin(int mode,ms_t now);
static int  sim_ka_hold=0;
static int  sim_net_on=1;
#define UART_DEV sim_tty
#define UART_ERR_RC 3
#define UART_PERR(s) sim_perr(s)
#define INP_READ(ev) sim_ev_next(&(ev),now)
#define INP_UNGRAB
#define SYS_SYNC
#define SIM_SIG(s) if((s)==SIGINT) sim_rc=130;
#define SIM_TOAST(m,col) sim_toast(m,col);
#define SIM_ENV sim_env();
#define SIM_START sim_start();
#define SIM_TICK(now) sim_tick(now);
#define SIM_FRAME(now) sim_frame(now);
#define SIM_EXIT sim_exit();
#define SIM_DRY_BYE(mode,now) if(sim_bye_begin(mode,now)) return;
#define SIM_KA_HOLD if(sim_ka_hold) return;
#define MAIN_RC sim_rc
#else
#define UART_DEV UART
#define UART_ERR_RC 1
#define UART_PERR(s) perror(s)
#define INP_READ(ev) read(ifd,&ev,sizeof(ev))==sizeof(ev)
#define INP_UNGRAB ioctl(ifd,EVIOCGRAB,0);
#define SYS_SYNC sync();
#define SIM_SIG(s)
#define SIM_TOAST(m,col)
#define SIM_ENV
#define SIM_START
#define SIM_TICK(now)
#define SIM_FRAME(now)
#define SIM_EXIT
#define SIM_DRY_BYE(mode,now)
#define SIM_KA_HOLD
#define MAIN_RC 0
#endif

#define W 480
#define H 480
#define CX 240
#define CY 240
#define RSAFE 234          /* safety radius (bezel) — all content clips to this */
#define REGN 129
#define UART "/dev/ttyS1"
#define FB   "/dev/fb0"
#define INP  "/dev/input/event0"
#define INP_KBD "/dev/input/event1"   /* v6.14: the power button (k62091, EV_KEY) — stock app_powerKey_thread 0x140AB4 opens exactly this one */
#define KBD_NAME "k62091"
#define BL_SYSFS "/sys/class/pwm/pwmchip0/pwm2/duty_cycle"
#define BL_EN_SYSFS "/sys/class/pwm/pwmchip0/pwm2/enable"   /* v6.14: stock uikit_os_back_light_off/on 0xC2278 = blpwm_set_enable(2,0/1) */
#define MERAKIPLUS_VERSION "V6.20"

/* ---- v6.10: pressure estimate (UI_v6.md §16) ----
 * Word 51 (SPI2 ADC channel 0xA, REGISTERMAP: pressure OR pump-drive candidate) -> bar with ONE linear anchor:
 *   P0 = P_ANCHOR_RAW = 358 raw: the pre-infusion plateau in the capture BREW.md §6 (UART capture 20:54:05-06.7, 6.06 s,
 *        median 356, span 332..366; STEAMLIVE.md confirms ~360 / 6.84 s), captured with config preInfusionPressure = 4.
 *   anchor pressure = config preInfusionPressure (SET[S_PIPRESS], default P_ANCHOR_BAR_DEF = 4; outside 1..15 -> default).
 *   k = anchor pressure / P0 bar per raw unit (4/358 = 0.011173); displayed bar = word51 x k (pre 358 -> 4.0; ext 1346 -> 15.0).
 * CAVEATS (also shown on DIAGNOSTICS and in UI_v6.md §16):
 *   (a) ext ~15 bar is plausible for a vibration pump WITHOUT an OPV, but it is pump-side pressure, not pressure at the puck.
 *   (b) deadhead/maintenance pulses (up to 16670 raw = 186 bar linear) are NOT physical under a linear scale: above
 *       P_EST_MAX (20 bar) no number is shown — the BAR field shows "PULSE", DIAGNOSTICS "MAINTENANCE PULSE" / "PULSE".
 *   (c) a two-point affine scale (rest 0 + pre 4 + ext 9) is inconsistent -> word 51 may be non-linear or a
 *       pump-drive level; hence an ESTIMATE, not a certified calibration. Raw is always available (DIAGNOSTICS → GRAPH).
 * Display only: control, writes, auto-stop and thresholds never use bar. Tuning: -DP_ANCHOR_RAW=… -DP_EST_MAX=… at build time. */
#ifndef P_ANCHOR_RAW
#define P_ANCHOR_RAW 358
#endif
#ifndef P_ANCHOR_BAR_DEF
#define P_ANCHOR_BAR_DEF 4
#endif
#ifndef P_EST_MAX
#define P_EST_MAX 20.f
#endif

/* ---- TIME BASE (v6.6): monotonic milliseconds in 64 bits ----
 * Up to and including v6.5 the clock was a `long`. On the device (ARMv7, ILP32) `long` is 32 bits, so the clock wrapped
 * 2^31 ms = 24.86 days after boot: timers compared absolutely (poll, render, keep-awake)
 * could stall for 49.7 days, and any time difference over 24.86 days became negative ("DONE" chip,
 * CRC warning and toast that came back). ms_t is 64 bits in all build types: the clock does not wrap
 * (292 million years), and ALL time values — points in time, deadlines and differences — are ms_t.
 * The sum is formed in 64 bits (time_t is 64 bits on musl and on the host) and only assigned afterwards.
 * (The typedef is at the top of the file, before the HAL macros.) */
_Static_assert(sizeof(ms_t)==8,"the time base must be 64 bits");
#ifdef SIM_RT
static int64_t sim_clk_off=0;                              /* MERAKIPLUS_SIM_UPTIME_MS: the clock starts at a chosen value (real-time wrap test) */
#define CLK_OFF sim_clk_off
#else
#define CLK_OFF 0
#endif
static ms_t ts_ms(const struct timespec*ts){ int64_t v=(int64_t)ts->tv_sec*1000+ts->tv_nsec/1000000+CLK_OFF; return (ms_t)v; }
static ms_t now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts_ms(&ts); }

/* ====================================================================== */
/* The rest of the program lives in src/mp/, split along its old section headers and #included here in
 * the original order (a unity build: one translation unit, so build.sh and the binaries are unchanged).
 *   core (no drawing):  proto · config · net · i18n · bootslot · model
 *   UI:                 gfx (engine) · ui_state · services (power, schedule, sound) · ui_pages · runtime
 *   entry points:       input · sim_hal · main  (device + SIM)   |   preview · selftest  (PREVIEW)
 * Every fragment is part of this file's scope: statics are shared, and order matters. */
/* ====================================================================== */
#include "mp/proto.c"
#include "mp/config.c"
#include "mp/net.c"
#include "mp/i18n.c"
#include "mp/bootslot.c"
#include "mp/model.c"
#include "mp/gfx.c"
#include "mp/ui_state.c"
#include "mp/services.c"
#include "mp/ui_pages.c"
#include "mp/runtime.c"
#ifndef MERAKIPLUS_PREVIEW
#include "mp/input.c"
#include "mp/sim_hal.c"
#include "mp/main.c"
#ifdef MERAKIPLUS_WASM
#include "mp/wasm_hal.c"           /* v6.20: exports mp_init/mp_step/mp_touch/mp_fb/mp_ctrl/mp_state instead of main() */
#endif
#else
#include "mp/preview.c"
#include "mp/selftest.c"
#endif
