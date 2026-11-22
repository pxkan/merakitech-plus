/* src/mp/model.c — settings model, time + scheduling, own setting files, per-word policy, activity recipes, manual STOP, phases, display conditioning, history.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ================= settings model =================
 * ONE table = the single source of truth for label/field/word/limits. The controller has NO
 * range checks (PARITY §0.10) -> min/max/step are enforced here and in frame_policy().
 * All limits are reconciled against PARITY.md §2 / parity_spec.json (raw config units);
 * parity_check.py compares the table with the spec automatically (--dump-table).
 *   ptier = the spec's tier (1 core / 2 comfort / 3 system); tier = merakiplus page.
 *   SF_EST: min+step are proven, but max is an ESTIMATE (the stock picker's option count is
 *           not mapped, PARITY §6.6). The UI shows a visible hint, and a value that
 *           the stock UI itself has saved (<= hard) widens the interval.
 *   SF_LOCK: shown, but has no editor (reason in note).
 *   SF_SENSOR: read-only measured value from reg[def] — no config key, no write.
 *   SF_LOCAL (v6.3): merakiplus's OWN setting (UI language) — no config key, no MCU word,
 *           not a spec parameter (left out of --dump-table); saved in its own file.
 *   SF_OWN (v6.16): merakiplus's OWN time settings (pre-heat schedule + auto off) — like SF_LOCAL, saved in
 *           /new_customer/merakiplus/schedule (sched_save); SF_HHMM: the value is minutes after midnight, shown as a clock time.
 * label/unit/note/hint and enum names are string ids (TX_*), never text — see STR_TABLE. */
enum { K_NUM=0, K_ENUM };
enum { M_NONE=0, M_W4, M_W23, M_W11, M_W112, M_W113, M_W114, M_W25, M_W14, M_W17, M_W2 };
#define SF_TEMP10 1   /* value in 0.1 °C */
#define SF_TEMP1  2   /* value in whole °C */
#define SF_LOCK   4
#define SF_BL     8   /* live preview: backlight */
#define SF_EST    16
#define SF_SENSOR 32
#define SF_LOCAL  64
#define SF_OWN    128 /* v6.16: own file (schedule) */
#define SF_HHMM   256 /* v6.16: minutes after midnight, shown as hh:mm (12/24 h per timemode) */
#define SF_SND    512 /* v6.18: own file (sound) */
typedef struct {
    const char *key; short label, unit;
    int tier, ptier, min, max, step, dec, kind;
    const short *opts;
    int mcu, flags, def, hard;
    short note, hint;             /* hint 0 (TX_NONE) = no hint */
    int val, loaded, lo, hi;      /* runtime: value, loaded?, effective interval */
} Setting;
static const short OPT_OFFON[]={TX_E_OFF,TX_E_ON};
static const short OPT_GRINDM[]={TX_E_WEIGHT,TX_E_TIME};                /* 1=weight auto, 2=time [PARITY #12] */
static const short OPT_HOTM[]={TX_E_AUTO,TX_E_MANUAL};                  /* 1=auto, 2=manual     [PARITY #13] */
static const short OPT_GEAR[]={TX_E_GENTLE,TX_E_MILD,TX_E_MEDIUM,TX_E_STRONG};   /* Gentle/Mild/Medium/Strong = 1..4 [0xdd028] */
/* v6.16: FIXED (inverted since v6.1). Stock update_time 0xD5944: r3 = model[0x59] ^ 1 (0xD5970); r3 == 0 (timemode 1) -> "%02d:%02d"
 * (0x165D94), else (timemode 0) -> "%02d:%02d A.M./P.M." (0x165D84/0x165D74). model_data_init 0xCEF94: model[0x59] = timemode != 0;
 * the stock switch "24-Hour Format" (0x171F60) is ON when model[0x59] = 1 (onClick 0xFCD60 toggles it, setTimeMode 0xFCDEC). */
static const short OPT_TIME[]={TX_E_12H,TX_E_24H};                      /* 0 = 12-hour (A.M./P.M.), 1 = 24-hour */
static const short OPT_REPEAT[]={TX_E_ONCE,TX_E_DAILY,TX_E_WORKDAYS,TX_E_WEEKEND};   /* v6.16: stock repeat Never / Everyday / Workday / Weekend */
static const short OPT_AUTOOFF[]={TX_E_OFF,TX_E_MIN15,TX_E_MIN20,TX_E_MIN30,TX_E_MIN60};   /* v6.16: stock "%dmin" table 0x1A02D0 = 15/20/30/60 */
static const short OPT_TUNIT[]={TX_U_F,TX_U_C};                         /* 0=°F, 1=°C           [PARITY #17] */
static const short OPT_LANG[]={TX_LG_EN,TX_LG_DE,TX_LG_FR,TX_LG_IT,TX_LG_ES,TX_LG_PL,TX_LG_AR,TX_LG_ZHS,TX_LG_ZHT,TX_LG_KO,TX_LG_JA};   /* stock: index 0..10 [strings @0x166d84] */
static const short OPT_UILANG[]={TX_UL_EN,TX_UL_DA};
static const short OPT_SOUND[]={TX_E_OFF,TX_E_ON,TX_E_SND_STOCK};                  /* v6.18: 0 off, 1 Meraki+ tones, 2 the stock UI's own mp3 files (if present) */
static const short OPT_SNDRDY[]={TX_E_OFF,TX_E_RDY_BREW,TX_E_RDY_STEAM,TX_E_RDY_BOTH};   /* v6.18: bit 0 = brew (extract) boiler, bit 1 = steam boiler */                    /* v6.3: = LANG_EN, LANG_DA */
_Static_assert(TX_UL_DA==TX_UL_EN+LANG_DA,"TX_UL_* must follow LANG_*");
enum { S_GRINDW=0, S_GRINDMODE, S_BREWW, S_BREWT, S_PISW, S_PITIME, S_PIPRESS, S_BREAK, S_HEADT,
       S_STEAMT, S_STEAMG, S_HOTW, S_HOTMODE, S_HOTT,
       S_UILANG, S_BL, S_VOL, S_TUNIT, S_LANG, S_HOTSC, S_DST, S_CLEARTIP, S_TIMEMODE, S_AUTOTIME, S_TZ,
       S_PREHEAT, S_PHAT, S_PHREP, S_AUTOOFF, S_SOUND, S_SNDRDY, NSET };   /* v6.16: + the time rows (merakiplus's own file); v6.18: + sound (own file) */
static Setting SET[NSET]={
 /*              key                   label            unit    page,spec-tier  min   max step dec kind   opts        mcu    flags                    def hard  note, hint */
 /* v6.17: page = the list page the row belongs to (0 GRIND, 1 BREW, 2 WATER, 3 STEAM, 4 SYSTEM); the row ORDER on a page and the
  * SYSTEM sections are in FEAT_ROWS / SEC_ROWS (the row model). Before v6.17: 0 BREW, 1 STEAM · WATER, 2 SYSTEM (the spec's tiers). */
 /* ---- GRIND + BREW (spec tier 1) ---- */
 [S_GRINDW]   ={"autoGrindWeight",    TX_SL_GRINDW,    TX_U_G,   0,1,   10, 309,  1, 1,K_NUM, 0,         M_W4,  0,                      180, 309,TX_SN_GRINDW,0},
 [S_GRINDMODE]={"grindMode",          TX_SL_GRINDMODE, TX_NONE,  0,1,    1,   2,  1, 0,K_ENUM,OPT_GRINDM,M_W2,  0,                        1,   2,TX_SN_GRINDMODE,0},
 [S_BREWW]    ={"autobrewWeight",     TX_SL_BREWW,     TX_U_G,   1,1,  100, 990, 10,-1,K_NUM, 0,         M_W23, 0,                      360, 990,TX_SN_BREWW,0},
 [S_BREWT]    ={"brewTemper",         TX_SL_BREWT,     TX_U_C,   1,1,  850, 960, 10, 1,K_NUM, 0,         M_W11, SF_TEMP10,              870, 960,TX_SN_BREWT,0},
 [S_PISW]     ={"preInfusionSwitch",  TX_SL_PISW,      TX_NONE,  1,1,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_W112,0,                        0,   1,TX_SN_PISW,0},
 [S_PITIME]   ={"preInfusionTime",    TX_SL_PITIME,    TX_U_S,   1,1,    5,  15,  1, 0,K_NUM, 0,         M_W113,SF_EST,                  15,  60,TX_SN_PITIME,TX_SH_EST},
 [S_PIPRESS]  ={"preInfusionPressure",TX_SL_PIPRESS,   TX_U_BAR, 1,1,    3,   4,  1, 0,K_NUM, 0,         M_W112,0,                        4,   4,TX_SN_PIPRESS,0},
 [S_BREAK]    ={"breakTime",          TX_SL_BREAK,     TX_U_S,   4,1,    0, 255,  1, 0,K_NUM, 0,         M_W113,SF_LOCK,                  5, 255,TX_SN_BREAK,0},
 [S_HEADT]    ={"headTemper",         TX_SL_HEADT,     TX_U_C,   1,1,   30,  80, 10, 0,K_NUM, 0,         M_W114,SF_TEMP1|SF_EST,         50, 100,TX_SN_HEADT,TX_SH_EST},
 /* ---- STEAM + WATER (spec tier 1); the WATER SENSOR row is not listed: the WATER page shows it as its live line ---- */
 [S_STEAMT]   ={"AtuoSteamTarge",     TX_SL_STEAMT,    TX_U_C,   3,1,  450, 700, 10, 1,K_NUM, 0,         M_W25, SF_TEMP10,              550, 700,TX_SN_STEAMT,0},
 [S_STEAMG]   ={"steamGears",         TX_SL_STEAMG,    TX_NONE,  3,1,    1,   4,  1, 0,K_ENUM,OPT_GEAR,  M_W14, 0,                        4,   4,TX_SN_STEAMG,0},
 [S_HOTW]     ={"autoHotWaterTarget", TX_SL_HOTW,      TX_U_G,   2,1,  300,5000,100,-1,K_NUM, 0,         M_W17, 0,                     3600,5000,TX_SN_HOTW,0},
 [S_HOTMODE]  ={"hotWaterMode",       TX_SL_HOTMODE,   TX_NONE,  2,1,    1,   2,  1, 0,K_ENUM,OPT_HOTM,  M_NONE,0,                        1,   2,TX_SN_HOTMODE,0},
 [S_HOTT]     ={0,                    TX_SL_HOTT,      TX_U_C,   2,1,  200,1200,  1, 1,K_NUM, 0,         M_NONE,SF_SENSOR|SF_LOCK|SF_TEMP10,40,  0,TX_SN_HOTT,0},
 /* ---- SYSTEM: first merakiplus's own UI language (v6.3; not a spec parameter), then
  *      spec tier 2 and tier 3 — all config only, no MCU write ---- */
 [S_UILANG]   ={0,                    TX_SL_UILANG,    TX_NONE,  4,0,    0,   1,  1, 0,K_ENUM,OPT_UILANG,M_NONE,SF_LOCAL,                 0,   1,TX_SN_UILANG,0},
 [S_BL]       ={"backlight",          TX_SL_BL,        TX_NONE,  4,2,    1,  10,  1, 0,K_NUM, 0,         M_NONE,SF_BL,                    8,  10,TX_SN_BL,0},
 [S_VOL]      ={"volume",             TX_SL_VOL,       TX_NONE,  4,2,    0, 100,  1, 0,K_NUM, 0,         M_NONE,0,                       40, 100,TX_SN_VOL,TX_SH_VOL},
 [S_TUNIT]    ={"Temperature",        TX_SL_TUNIT,     TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_TUNIT, M_NONE,0,                        0,   1,TX_SN_TUNIT,0},
 [S_LANG]     ={"language",           TX_SL_LANG,      TX_NONE,  4,2,    0,  10,  1, 0,K_ENUM,OPT_LANG,  M_NONE,0,                        0,  10,TX_SN_LANG,0},
 [S_HOTSC]    ={"hotWaterShortcut",   TX_SL_HOTSC,     TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        0,   1,TX_SN_HOTSC,0},
 [S_DST]      ={"sumtime",            TX_SL_DST,       TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        0,   1,TX_SN_TIMEFMT,0},
 [S_CLEARTIP] ={"clearTip",           TX_SL_CLEARTIP,  TX_NONE,  4,2,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        0,   1,TX_SN_CLEARTIP,0},
 [S_TIMEMODE] ={"timemode",           TX_SL_TIMEMODE,  TX_NONE,  4,3,    0,   1,  1, 0,K_ENUM,OPT_TIME,  M_NONE,0,                        1,   1,TX_SN_TIMEFMT,0},
 [S_AUTOTIME] ={"Automatically",      TX_SL_AUTOTIME,  TX_NONE,  4,3,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,0,                        1,   1,TX_SN_AUTOTIME,0},
 [S_TZ]       ={"timeZone",           TX_SL_TZ,        TX_NONE,  4,3,    0,   0,  1, 0,K_NUM, 0,         M_NONE,SF_LOCK,                 44,   0,TX_SN_TZ,0},
 /* ---- v6.16: pre-heat schedule + auto off — merakiplus's OWN file (/new_customer/merakiplus/schedule), not config.json, no MCU word ---- */
 [S_PREHEAT]  ={0,                    TX_SL_PREHEAT,   TX_NONE,  4,0,    0,   1,  1, 0,K_ENUM,OPT_OFFON, M_NONE,SF_OWN,                   0,   1,TX_SN_PREHEAT,0},
 [S_PHAT]     ={0,                    TX_SL_PHAT,      TX_NONE,  4,0,    0,1435,  5, 0,K_NUM, 0,         M_NONE,SF_OWN|SF_HHMM,         390,1435,TX_SN_PHAT,0},
 [S_PHREP]    ={0,                    TX_SL_PHREP,     TX_NONE,  4,0,    0,   3,  1, 0,K_ENUM,OPT_REPEAT,M_NONE,SF_OWN,                   1,   3,TX_SN_PHREP,0},
 [S_AUTOOFF]  ={0,                    TX_SL_AUTOOFF,   TX_NONE,  4,0,    0,   4,  1, 0,K_ENUM,OPT_AUTOOFF,M_NONE,SF_OWN,                  0,   4,TX_SN_AUTOOFF,0},
 /* ---- v6.18: sound — merakiplus's OWN file (/new_customer/merakiplus/sound), not config.json, no MCU word. The volume is the stock
  *      config "volume" (S_VOL, shared with the stock UI, which caps it at 70 like merakiplus) ---- */
 [S_SOUND]    ={0,                    TX_SL_SOUND,     TX_NONE,  4,0,    0,   2,  1, 0,K_ENUM,OPT_SOUND, M_NONE,SF_SND,                   1,   2,TX_SN_SOUND,0},
 [S_SNDRDY]   ={0,                    TX_SL_SNDRDY,    TX_NONE,  4,0,    0,   3,  1, 0,K_ENUM,OPT_SNDRDY,M_NONE,SF_SND,                   3,   3,TX_SN_SNDRDY,0},
};
static const short TIER_NAME[5]={TX_A_GRIND,TX_A_BREW,TX_A_WATER,TX_A_STEAM,TX_PG_SYSTEM};   /* v6.17: the list pages' titles (page = SET[].tier) */
static int sv(int i){ return SET[i].val; }
/* v6.10: pressure estimate (the constants and caveats are at P_ANCHOR_RAW at the top). The anchor pressure is config preInfusionPressure,
 * so a changed config changes the scale; outside 1..15 bar (or no config: default 4) P_ANCHOR_BAR_DEF is used. */
static int p_anchor_bar(void){ int v=sv(S_PIPRESS); return (v>=1&&v<=15)?v:P_ANCHOR_BAR_DEF; }
static float p_k(void){ return (float)p_anchor_bar()/(float)P_ANCHOR_RAW; }
/* raw word 51 -> bar estimate; -1 = above P_EST_MAX: a deadhead/maintenance pulse, not a physical pressure — never shown as a number */
static float p_bar(float raw){ float b=raw*p_k(); return b>P_EST_MAX?-1.f:(b<0.f?0.f:b); }
static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int set_valid(const Setting*s,int v){ return v>=s->lo && v<=s->hi; }
/* loaded value outside the spec interval (only possible if config was changed behind the stock UI's back) */
static int set_oor(const Setting*s){ return s->loaded && !(s->flags&SF_SENSOR) && !set_valid(s,s->val); }

/* ================= v6.16: TIME & SCHEDULING (docs/time-scheduling.md + UI_v6.md "CHANGES-v6.16") =================
 * Reversed from the stock UI demo_v9 (symbols kept) and the MCU firmware:
 *  - TIME SOURCE: the SoC system clock, kept in UTC. Stock sets it only via an HTTP query (time_sync_thread 0x13FDEC, gated by
 *    config "Automatically") -> biz_set_system_time 0x1359B0 = `date -s <UTC>` + `hwclock -w`; no NTP, nothing from the MCU.
 *    merakiplus does NOT sync the clock (no network calls): it reads the same SoC clock. Before 2025 = "not set" -> "--:--", no alarm.
 *  - LOCAL TIME: _refresh_systime 0x135548 = UTC + the time zone table entry 0x1A5890[timeZone].offset (136 entries x 12 bytes:
 *    offset s, "UTC±hh:mm", name; copied below verbatim, incl. two stock quirks: #30 Newfoundland -2:30 under the label -03:30
 *    and #135 Kiritimati +14:00 under the label +13:00) + 3600 if sumtime and is_dst 0x1354D4. Stock's DST window is date-only,
 *    comes from the HTTP answer (model+0x15B8..0x15CC, RAM only) and is never computed offline; merakiplus computes the window from the
 *    region's rule (the rules stock's calc_dst_dates_for_timezone 0x13BB14 uses: EU last Sun Mar -> last Sun Oct at 01:00 UTC;
 *    US/CA 2nd Sun Mar -> 1st Sun Nov; Levant last Fri Mar -> last Fri Oct; AU 1st Sun Oct -> 1st Sun Apr; NZ last Sun Sep ->
 *    1st Sun Apr; others none) — config "sumtime" stays the user's on/off switch (deliberate, documented deviation).
 *  - DISPLAY: update_time 0xD5944 — timemode 1 = "%02d:%02d", timemode 0 = "%02d:%02d A.M./P.M." with 0 -> 12 A.M., 12 -> 12 P.M.,
 *    13..23 -> 1..11 P.M. (0xD5990..0xD5A6C).
 *  - SCHEDULED PRE-HEAT: stock keeps up to 30 entries of 20 bytes at model+0x74 (saved in the binary /customer/config.bin:
 *    data_save 0xD091C copies model+0x70, 0xF78 bytes): [0] exists, [1] enabled, [2] hour, [3] minute, [4..10] Sun..Sat repeat
 *    flags (judgeWeek 0x14955C / judgeWeekEqual 0x1495E0; all 0 = one-shot). Nothing is sent while the machine is on: only
 *    powerOff 0x149074 -> performSleepActions 0x148324 computes the NEXT enabled occurrence (minute resolution, strictly after
 *    now: today if the weekday matches and hh:mm > now, else the next matching weekday, one-shot -> today or tomorrow) and sends it
 *    as the cmd8 alarm via powerOffSend 0x14700C(mday of the target day, hour, minute) — day wrapped with biz_days_in_month
 *    0x135AE8; no entry -> 0/0/0. A one-shot entry is remembered (model+0x1758..0x176C) and switched off at the next start once
 *    its time has passed (deal_preheat_data 0xCF49C).
 *  - AUTO OFF: SleepRefresh 0x149268 counts a 1800 s idle counter down once per second (reset by every touch, touch_feedback_cb ->
 *    sleepTimeUpfdate 0x14818C, and every second while isExtractionOrSteamSwitchOn); at 0 updateSleepState 0x14827C calls powerOff
 *    (the same alarm frame as above). config sleepMode/sleepModeSetting are loaded (model+0x54/0x55) but never read in this
 *    firmware; the setting's own table (0x1A02D0) is 15/20/30/60 min.
 *  - MCU (MEM03): cmd8 handler 0x9C88 copies the 10 bytes to 0x20000E18, echoes 0x88 and sets its RTC from them (0x9DCE -> 0x7494);
 *    the sleep task 0x2F52 compares RTC day/hour/minute with the alarm once per second and, if day+hour+minute != 0 and they match,
 *    resets the CPU (0x326) = wake: word 0 starts as 1 (heating), and on this hardware the display gets power again = cold boot.
 * merakiplus: the clock on HOME + DIAGNOSE (SoC UTC + zone + DST, 12/24 h per timemode), a pre-heat schedule (one entry: on/off, time,
 * once / every day / Mon-Fri / Sat-Sun) and auto off (off / 15 / 20 / 30 / 60 min) in merakiplus's own file; the alarm goes out with
 * the power-off frame exactly as stock (pw_off_begin), and — a documented addition — is PRE-ARMED with the same frame while the
 * machine is on, so a power-off merakiplus does not see (the button's kernel cut, §19) still wakes it. Never while off/turning off. */
enum { DR_NONE=0, DR_US, DR_EU, DR_LEV, DR_AU, DR_NZ };
static void idle_kick(ms_t now);     /* auto off: every touch/button/SAVE restarts the idle timer (sched_step) */
#define TZ_N 136
static const struct { int off; const char*utc,*name; unsigned char rule; } TZT[TZ_N]={   /* stock 0x1A5890 (offset, label, name) + DST rule */
 {-43200,"UTC-12:00","DATELINE STANDARD TIME",DR_US},  /* 0 */
 {-39600,"UTC-11:00","UTC-11",DR_US},  /* 1 */
 {-36000,"UTC-10:00","DATELINE STANDARD TIME",DR_US},  /* 2 */
 {-36000,"UTC-11:00","UTC-10:00",DR_US},  /* 3 */
 {-34200," UTC-09:30","MARQUESAS ISLANDS",DR_US},  /* 4 */
 {-32400,"UTC-09:00","ALASKA",DR_US},  /* 5 */
 {-32400,"UTC-09:00","COORDINATED UNIVERSAL TIME-09",DR_US},  /* 6 */
 {-28800,"UTC-08:00","BAJA CALIFORNIA",DR_US},  /* 7 */
 {-28800,"UTC-08:00","COORDINATED UNIVERSAL TIME-08",DR_US},  /* 8 */
 {-28800,"UTC-08:00","PACIFIC TIME (US + CANADA)",DR_US},  /* 9 */
 {-25200,"UTC-07:00","ARIZONA",DR_US},  /* 10 */
 {-25200,"UTC-07:00","CHIHUAHUA, LA PAZ, MAZATLAN",DR_US},  /* 11 */
 {-21600,"UTC-06:00","CENTRAL AMERICA",DR_US},  /* 12 */
 {-21600,"UTC-06:00","CENTRAL TIME (US + CANADA)",DR_US},  /* 13 */
 {-21600,"UTC-06:00","EASTER ISLAND",DR_US},  /* 14 */
 {-21600,"UTC-06:00","GUADALAJARA, MEXICO CITY, MONTERREY",DR_US},  /* 15 */
 {-21600,"UTC-06:00","SASKATCHEWAN",DR_US},  /* 16 */
 {-18000,"UTC-05:00","BOGOTA, LIMA, QUITO, RIO BRANCO",DR_US},  /* 17 */
 {-18000,"UTC-05:00","CHETUMAL",DR_US},  /* 18 */
 {-18000,"UTC-05:00","EASTERN TIME (US + CANADA)",DR_US},  /* 19 */
 {-18000,"UTC-05:00","HAITI",DR_US},  /* 20 */
 {-18000,"UTC-05:00","HAVANA",DR_US},  /* 21 */
 {-18000,"UTC-05:00","INDIANA (EAST)",DR_US},  /* 22 */
 {-18000,"UTC-05:00","TURKS AND CAICOS",DR_US},  /* 23 */
 {-14400,"UTC-04:00","ASUNCION",DR_US},  /* 24 */
 {-14400,"UTC-04:00","ATLANTIC TIME (CANADA)",DR_US},  /* 25 */
 {-14400,"UTC-04:00","CARACAS",DR_US},  /* 26 */
 {-14400,"UTC-04:00","CUIABA",DR_US},  /* 27 */
 {-14400,"UTC-04:00","GEORGETOWN, LA PAZ, MANAUS, SAN JUAN",DR_US},  /* 28 */
 {-14400,"UTC-04:00","SANTIAGO",DR_NONE},  /* 29 */
 { -9000,"UTC-03:30","NEWFOUNDLAND",DR_US},  /* 30 */
 {-10800,"UTC-03:00","ARAGUAINA",DR_NONE},  /* 31 */
 {-10800,"UTC-03:00","BRASILIA",DR_NONE},  /* 32 */
 {-10800,"UTC-03:00","CAYENNE, FORTALEZA",DR_NONE},  /* 33 */
 {-10800,"UTC-03:00","CITY OF BUENOS AIRES",DR_NONE},  /* 34 */
 {-10800,"UTC-03:00","GREENLAND",DR_US},  /* 35 */
 {-10800,"UTC-03:00","MONTEVIDEO",DR_NONE},  /* 36 */
 {-10800,"UTC-03:00","PUNTA ARENAS",DR_NONE},  /* 37 */
 {-10800,"UTC-03:00","SAINT PIERRE AND MIQUELON",DR_US},  /* 38 */
 {-10800,"UTC-03:00","SALVADOR",DR_NONE},  /* 39 */
 { -7200,"UTC-02:00","COORDINATED UNIVERSAL TIME-02",DR_NONE},  /* 40 */
 { -3600,"UTC-01:00","AZORES",DR_EU},  /* 41 */
 { -3600,"UTC-01:00","CABO VERDE IS.",DR_EU},  /* 42 */
 {     0,"UTC","COORDINATED UNIVERSAL TIME",DR_NONE},  /* 43 */
 {     0,"UTC+00:00","DUBLIN, EDINBURGH, LISBON, LONDON",DR_EU},  /* 44 */
 {     0,"UTC+00:00","MONROVIA, REYKJAVIK",DR_EU},  /* 45 */
 {     0,"UTC+00:00","SAO TOME",DR_EU},  /* 46 */
 {  3600,"UTC+01:00","CASABLANCA",DR_EU},  /* 47 */
 {  3600,"UTC+01:00","AMSTERDAM, BERLIN, BERN, ROME, STOCKHOLM, VIENNA",DR_EU},  /* 48 */
 {  3600,"UTC+01:00","BELGRADE, BRATISLAVA, BUDAPEST, LJUBLJANA, PRAGUE",DR_EU},  /* 49 */
 {  3600,"UTC+01:00","BRUSSELS, COPENHAGEN, MADRID, PARIS",DR_EU},  /* 50 */
 {  3600,"UTC+01:00","SARAJEVO, SKOPJE, WARSAW, ZAGREB",DR_EU},  /* 51 */
 {  3600,"UTC+01:00","WEST CENTRAL AFRICA",DR_EU},  /* 52 */
 {  7200,"UTC+02:00","AMMAN",DR_LEV},  /* 53 */
 {  7200,"UTC+02:00","ATHENS, BUCHAREST",DR_EU},  /* 54 */
 {  7200,"UTC+02:00","BEIRUT",DR_LEV},  /* 55 */
 {  7200,"UTC+02:00","CAIRO",DR_EU},  /* 56 */
 {  7200,"UTC+02:00","CHISINAU",DR_EU},  /* 57 */
 {  7200,"UTC+02:00","DAMASCUS",DR_LEV},  /* 58 */
 {  7200,"UTC+02:00","GAZA, HEBRON",DR_LEV},  /* 59 */
 {  7200,"UTC+02:00","HARARE, PRETORIA",DR_EU},  /* 60 */
 {  7200,"UTC+02:00","HELSINKI, KYIV, RIGA, SOFIA, TALLINN, VILNIUS",DR_EU},  /* 61 */
 {  7200,"UTC+02:00","JERUSALEM",DR_LEV},  /* 62 */
 {  7200,"UTC+02:00","KALININGRAD",DR_EU},  /* 63 */
 {  7200,"UTC+02:00","KHARTOUM",DR_EU},  /* 64 */
 {  7200,"UTC+02:00","TRIPOLI",DR_EU},  /* 65 */
 {  7200,"UTC+02:00","WINDHOEK",DR_EU},  /* 66 */
 { 10800,"UTC+03:00","BAGHDAD",DR_EU},  /* 67 */
 { 10800,"UTC+03:00","ISTANBUL",DR_EU},  /* 68 */
 { 10800,"UTC+03:00","KUWAIT, RIYADH",DR_EU},  /* 69 */
 { 10800,"UTC+03:00","MINSK",DR_EU},  /* 70 */
 { 10800,"UTC+03:00","MOSCOW, ST. PETERSBURG",DR_EU},  /* 71 */
 { 10800,"UTC+03:00","NAIROBI",DR_NONE},  /* 72 */
 { 10800,"UTC+03:00","TEHRAN",DR_NONE},  /* 73 */
 { 14400,"UTC+04:00","ABU DHABI, MUSCAT",DR_NONE},  /* 74 */
 { 14400,"UTC+04:00","ASTRAKHAN, ULYANOVSK",DR_NONE},  /* 75 */
 { 14400,"UTC+04:00","BAKU",DR_NONE},  /* 76 */
 { 14400,"UTC+04:00","IZHEVSK, SAMARA",DR_NONE},  /* 77 */
 { 14400,"UTC+04:00","PORT LOUIS",DR_NONE},  /* 78 */
 { 14400,"UTC+04:00","SARATOV",DR_NONE},  /* 79 */
 { 14400,"UTC+04:00","TBILISI",DR_NONE},  /* 80 */
 { 14400,"UTC+04:00","VOLGOGRAD",DR_NONE},  /* 81 */
 { 14400,"UTC+04:00","YEREVAN",DR_NONE},  /* 82 */
 { 16200,"UTC+04:30","KABUL",DR_NONE},  /* 83 */
 { 18000,"UTC+05:00","ASHGABAT, TASHKENT",DR_NONE},  /* 84 */
 { 18000,"UTC+05:00","EKATERINBURG",DR_NONE},  /* 85 */
 { 18000,"UTC+05:00","ISLAMABAD, KARACHI",DR_NONE},  /* 86 */
 { 18000,"UTC+05:00","QYZYLORDA",DR_NONE},  /* 87 */
 { 19800,"UTC+05:30","CHENNAI, KOLKATA, MUMBAI, NEW DELHI",DR_NONE},  /* 88 */
 { 19800,"UTC+05:30","SRI JAYAWARDENEPURA",DR_NONE},  /* 89 */
 { 20700,"UTC+05:45","KATHMANDU",DR_NONE},  /* 90 */
 { 21600,"UTC+06:00","ASTANA",DR_NONE},  /* 91 */
 { 21600,"UTC+06:00","DHAKA",DR_NONE},  /* 92 */
 { 21600,"UTC+06:00","OMSK",DR_NONE},  /* 93 */
 { 23400,"UTC+06:30","YANGON (RANGOON)",DR_NONE},  /* 94 */
 { 25200,"UTC+07:00","BANGKOK, HANOI, JAKARTA",DR_NONE},  /* 95 */
 { 25200,"UTC+07:00","BARNAUL, GORNO-ALTAYSK",DR_NONE},  /* 96 */
 { 25200,"UTC+07:00","HOVD",DR_NONE},  /* 97 */
 { 25200,"UTC+07:00","KRASNOYARSK",DR_NONE},  /* 98 */
 { 25200,"UTC+07:00","NOVOSIBIRSK",DR_NONE},  /* 99 */
 { 25200,"UTC+07:00","TOMSK",DR_NONE},  /* 100 */
 { 28800,"UTC+08:00","BEIJING, CHONGQING, HONG KONG, URUMQI",DR_NONE},  /* 101 */
 { 28800,"UTC+08:00","IRKUTSK",DR_NONE},  /* 102 */
 { 28800,"UTC+08:00","KUALA LUMPUR, SINGAPORE",DR_NONE},  /* 103 */
 { 28800,"UTC+08:00","PERTH",DR_NONE},  /* 104 */
 { 28800,"UTC+08:00","TAIPEI",DR_NONE},  /* 105 */
 { 28800,"UTC+08:00","ULAANBAATAR",DR_NONE},  /* 106 */
 { 31500,"UTC+08:45","EUCLA",DR_NONE},  /* 107 */
 { 32400,"UTC+09:00","CHITA",DR_NONE},  /* 108 */
 { 32400,"UTC+09:00","OSAKA, SAPPORO, TOKYO",DR_NONE},  /* 109 */
 { 32400,"UTC+09:00","PYONGYANG",DR_NONE},  /* 110 */
 { 32400,"UTC+09:00","SEOUL",DR_NONE},  /* 111 */
 { 32400,"UTC+09:00","YAKUTSK",DR_NONE},  /* 112 */
 { 34200,"UTC+09:30","ADELAIDE",DR_AU},  /* 113 */
 { 34200,"UTC+09:30","DARWIN",DR_AU},  /* 114 */
 { 36000,"UTC+10:00","BRISBANE",DR_AU},  /* 115 */
 { 36000,"UTC+10:00","CANBERRA, MELBOURNE, SYDNEY",DR_AU},  /* 116 */
 { 36000,"UTC+10:00","GUAM, PORT MORESBY",DR_AU},  /* 117 */
 { 36000,"UTC+10:00","HOBART",DR_AU},  /* 118 */
 { 36000,"UTC+10:00","VLADIVOSTOK",DR_AU},  /* 119 */
 { 37800,"UTC+10:30","LORD HOWE ISLAND",DR_AU},  /* 120 */
 { 39600,"UTC+11:00","BOUGAINVILLE ISLAND",DR_NONE},  /* 121 */
 { 39600,"UTC+11:00","CHOKURDAKH",DR_NONE},  /* 122 */
 { 39600,"UTC+11:00","MAGADAN",DR_NONE},  /* 123 */
 { 39600,"UTC+11:00","NORFOLK ISLAND",DR_NONE},  /* 124 */
 { 39600,"UTC+11:00","SAKHALIN",DR_NONE},  /* 125 */
 { 39600,"UTC+11:00","SOLOMON IS., NEW CALEDONIA",DR_NONE},  /* 126 */
 { 43200,"UTC+12:00","ANADYR, PETROPAVLOVSK-KAMCHATSKY",DR_NZ},  /* 127 */
 { 43200,"UTC+12:00","AUCKLAND, WELLINGTON",DR_NZ},  /* 128 */
 { 43200,"UTC+12:00","COORDINATED UNIVERSAL TIME+12",DR_NZ},  /* 129 */
 { 43200,"UTC+12:00","FIJI",DR_NZ},  /* 130 */
 { 45900,"UTC+12:45","CHATHAM ISLANDS",DR_NZ},  /* 131 */
 { 46800,"UTC+13:00","COORDINATED UNIVERSAL TIME+13",DR_NONE},  /* 132 */
 { 46800,"UTC+13:00","NUKU'ALOFA",DR_NONE},  /* 133 */
 { 46800,"UTC+13:00","SAMOA",DR_NONE},  /* 134 */
 { 50400,"UTC+13:00","KIRITIMATI ISLAND",DR_NONE},  /* 135 */
};
_Static_assert(sizeof TZT/sizeof TZT[0]==TZ_N,"TZT must have the stock table's 136 entries");
#define CLK_MIN_VALID 1735689600LL          /* 2025-01-01 00:00 UTC: before this the SoC clock has never been set */
static int64_t clk_fake=-1;                 /* PREVIEW/selftest: a fixed UTC time */
static int64_t clk_off=0;                   /* SIM: offset to time(NULL) (MERAKIPLUS_SIM_CLOCK / ctrl "clock") */
static int64_t clk_utc(void){ return clk_fake>=0?clk_fake:(int64_t)time(NULL)+clk_off; }
static int clk_valid(int64_t u){ return u>=CLK_MIN_VALID; }
static int tz_idx(void){ int i=SET[S_TZ].val; return (i>=0&&i<TZ_N)?i:43; }      /* outside the table: #43 = UTC */
/* civil date <-> days since 1970-01-01 (proleptic Gregorian) */
static int64_t days_civil(int y,int m,int d){ y-=m<=2; int64_t era=(y>=0?y:y-399)/400; int yoe=(int)(y-era*400); int doy=(153*(m+(m>2?-3:9))+2)/5+d-1; int doe=yoe*365+yoe/4-yoe/100+doy; return era*146097+doe-719468; }
static int dow_civil(int y,int m,int d){ int64_t z=days_civil(y,m,d); return (int)((z%7+11)%7); }   /* 0 = Sunday (1970-01-01 = Thursday) */
static int dim_civil(int y,int m){ static const int D[12]={31,28,31,30,31,30,31,31,30,31,30,31}; return m==2&&((y%4==0&&y%100)||y%400==0)?29:D[m-1]; }
/* the n-th (n >= 1) or the last (n = -1) weekday `dow` of a month -> day of month */
static int nth_dow(int y,int m,int dow,int n){
    if(n>0){ int d1=dow_civil(y,m,1); return 1+(dow-d1+7)%7+(n-1)*7; }
    int dl=dim_civil(y,m), w=dow_civil(y,m,dl); return dl-(w-dow+7)%7;
}
static int64_t utc_of(int y,int m,int d,int h){ return days_civil(y,m,d)*86400+h*3600LL; }
/* is DST in effect at UTC time u in zone i (standard offset `off`)? Transitions: EU 01:00 UTC; the others 02:00 local standard time */
static int dst_on(int i,int64_t u){
    int off=TZT[i].off; time_t t=(time_t)(u+off); struct tm lt; gmtime_r(&t,&lt); int y=lt.tm_year+1900; int64_t a,b;
    switch(TZT[i].rule){
    case DR_EU:  a=utc_of(y,3,nth_dow(y,3,0,-1),1); b=utc_of(y,10,nth_dow(y,10,0,-1),1); return u>=a&&u<b;
    case DR_US:  a=utc_of(y,3,nth_dow(y,3,0,2),2)-off; b=utc_of(y,11,nth_dow(y,11,0,1),1)-off; return u>=a&&u<b;
    case DR_LEV: a=utc_of(y,3,nth_dow(y,3,5,-1),2)-off; b=utc_of(y,10,nth_dow(y,10,5,-1),1)-off; return u>=a&&u<b;
    case DR_AU:  a=utc_of(y,10,nth_dow(y,10,0,1),2)-off; b=utc_of(y,4,nth_dow(y,4,0,1),2)-off; return u>=a||u<b;   /* southern hemisphere */
    case DR_NZ:  a=utc_of(y,9,nth_dow(y,9,0,-1),2)-off; b=utc_of(y,4,nth_dow(y,4,0,1),2)-off; return u>=a||u<b;
    default: return 0;
    }
}
/* local time like stock's _refresh_systime: UTC + zone offset + (sumtime && DST ? 1 h : 0). Returns 1 if DST was applied. */
static int clk_local_at(int64_t u,struct tm*tm){
    int i=tz_idx(), d=SET[S_DST].val&&dst_on(i,u); time_t t=(time_t)(u+TZT[i].off+(d?3600:0)); gmtime_r(&t,tm); return d;
}
static int clk_now(struct tm*tm){ int64_t u=clk_utc(); clk_local_at(u,tm); return clk_valid(u); }
/* "HH:MM" (timemode 1) or "HH:MM A.M./P.M." (timemode 0) exactly as stock's update_time; ampm=0 drops the A.M./P.M. suffix */
static void fmt_hm(char*b,size_t n,int h,int m,int ampm){
    if(SET[S_TIMEMODE].val){ snprintf(b,n,"%02d:%02d",h,m); return; }
    int pm=h>=12, hh=h>12?h-12:(h==0?12:h);
    if(ampm) snprintf(b,n,"%02d:%02d %s",hh,m,TR(pm?TX_PM:TX_AM)); else snprintf(b,n,"%02d:%02d",hh,m);
}
static const short WD_TX[7]={TX_WD_SUN,TX_WD_MON,TX_WD_TUE,TX_WD_WED,TX_WD_THU,TX_WD_FRI,TX_WD_SAT};

/* ---- the schedule file: merakiplus's OWN settings (not config.json, not the stock UI's /customer/config.bin) ----
 * Text, one "key=value" per line: preheat=0|1, at=HH:MM, repeat=once|daily|mon-fri|sat-sun, autooff=0|15|20|30|60,
 * once_at=<UTC seconds of the one-shot occurrence, 0 = none>. Unknown lines are ignored, a bad value -> its default.
 * Written atomically like the language file (tmp + fsync + re-read/compare + rename + fsync of the directory). */
static char sched_path[256]="/new_customer/merakiplus/schedule";
static const char*const REP_KEY[4]={"once","daily","mon-fri","sat-sun"};
static const unsigned char REP_MASK[4]={0x00,0x7F,0x3E,0x41};   /* bit k = weekday k (0 = Sunday), stock's Sun..Sat flags; 0 = one-shot */
static const int AO_MIN[5]={0,15,20,30,60};
enum { SCS_DEFAULT=0, SCS_FILE, SCS_SESSION };
static struct { int on,hm,rep,ao,src; int64_t once_at; } sch={0,390,1,0,SCS_DEFAULT,0};
static int sched_val(int i){ return i==S_PREHEAT?sch.on:(i==S_PHAT?sch.hm:(i==S_PHREP?sch.rep:sch.ao)); }
static void sched_load(void){
    sch.on=0; sch.hm=390; sch.rep=1; sch.ao=0; sch.once_at=0; sch.src=SCS_DEFAULT;
    char b[512]; int fd=open(sched_path,O_RDONLY); if(fd<0) return;
    int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<=0) return; b[n]=0; sch.src=SCS_FILE;
    for(char*l=strtok(b,"\r\n");l;l=strtok(NULL,"\r\n")){
        char*e=strchr(l,'='); if(!e) continue; *e++=0;
        if(!strcmp(l,"preheat")) sch.on=atoi(e)==1;
        else if(!strcmp(l,"at")){ int h,m; if(sscanf(e,"%d:%d",&h,&m)==2&&h>=0&&h<24&&m>=0&&m<60&&m%5==0) sch.hm=h*60+m; }
        else if(!strcmp(l,"repeat")){ for(int k=0;k<4;k++) if(!strcmp(e,REP_KEY[k])) sch.rep=k; }
        else if(!strcmp(l,"autooff")){ int v=atoi(e); for(int k=0;k<5;k++) if(v==AO_MIN[k]) sch.ao=k; }
        else if(!strcmp(l,"once_at")) sch.once_at=strtoll(e,NULL,10);
    }
    if(sch.once_at<0) sch.once_at=0;
}
static int sched_save(void){
    char d[192],chk[192],tmp[280]; int n=snprintf(d,sizeof d,"preheat=%d\nat=%02d:%02d\nrepeat=%s\nautooff=%d\nonce_at=%lld\n",
        sch.on,sch.hm/60,sch.hm%60,REP_KEY[sch.rep],AO_MIN[sch.ao],(long long)sch.once_at);
    snprintf(tmp,sizeof tmp,"%s.tmp",sched_path);
    if(!write_all(tmp,d,n,0)){ unlink(tmp); sch.src=SCS_SESSION; return 0; }
    int fd=open(tmp,O_RDONLY); int cn=fd>=0?(int)read(fd,chk,sizeof chk):-1; if(fd>=0) close(fd);
    if(cn!=n||memcmp(chk,d,n)||rename(tmp,sched_path)!=0){ unlink(tmp); sch.src=SCS_SESSION; return 0; }
    fsync_dir(sched_path); sch.src=SCS_FILE; return 1;
}
/* ---- v6.18: the sound file — merakiplus's OWN setting (docs/audio.md) ----
 * Text, one "key=value" per line: sound=off|on|stock, ready=off|brew|steam|both. Missing file / unknown value -> the default
 * (sound=on, ready=both). Written atomically like the schedule (tmp + fsync + re-read/compare + rename + fsync of the directory). */
static char snd_path[256]="/new_customer/merakiplus/sound";
static const char*const SND_MODE_KEY[3]={"off","on","stock"};
static const char*const SND_RDY_KEY[4]={"off","brew","steam","both"};
static struct { int mode,ready,src; } sndc={1,3,SCS_DEFAULT};
static void snd_cfg_load(void){
    sndc.mode=1; sndc.ready=3; sndc.src=SCS_DEFAULT;
    char b[256]; int fd=open(snd_path,O_RDONLY); if(fd<0) return;
    int n=(int)read(fd,b,sizeof b-1); close(fd); if(n<=0) return; b[n]=0; sndc.src=SCS_FILE;
    for(char*l=strtok(b,"\r\n");l;l=strtok(NULL,"\r\n")){
        char*e=strchr(l,'='); if(!e) continue; *e++=0;
        if(!strcmp(l,"sound")){ for(int k=0;k<3;k++) if(!strcmp(e,SND_MODE_KEY[k])) sndc.mode=k; }
        else if(!strcmp(l,"ready")){ for(int k=0;k<4;k++) if(!strcmp(e,SND_RDY_KEY[k])) sndc.ready=k; }
    }
}
static int snd_cfg_save(void){
    char d[96],chk[96],tmp[280]; int n=snprintf(d,sizeof d,"sound=%s\nready=%s\n",SND_MODE_KEY[sndc.mode],SND_RDY_KEY[sndc.ready]);
    snprintf(tmp,sizeof tmp,"%s.tmp",snd_path);
    if(!write_all(tmp,d,n,0)){ unlink(tmp); sndc.src=SCS_SESSION; return 0; }
    int fd=open(tmp,O_RDONLY); int cn=fd>=0?(int)read(fd,chk,sizeof chk):-1; if(fd>=0) close(fd);
    if(cn!=n||memcmp(chk,d,n)||rename(tmp,snd_path)!=0){ unlink(tmp); sndc.src=SCS_SESSION; return 0; }
    fsync_dir(snd_path); sndc.src=SCS_FILE; return 1;
}
/* stock performSleepActions 0x148324 for ONE entry (hm = minutes after midnight, mask = REP_MASK): the next occurrence strictly after
 * `now` (minute resolution). Returns the days ahead (0..7); *ad = the day of month (wrapped like stock w. the month's length). */
static int sched_next(const struct tm*now,int hm,int mask,int*ad){
    int nowm=now->tm_hour*60+now->tm_min, wd=now->tm_wday, k;
    if(!mask) k=hm>nowm?0:1;                                          /* one-shot: today if later, else tomorrow */
    else if((mask>>wd&1)&&hm>nowm) k=0;                               /* judgeWeekEqual: today, later than now */
    else { for(k=1;k<=7;k++) if(mask>>((wd+k)%7)&1) break; }          /* getLastWeek 0x1496C0: the next flagged weekday (7 = same day next week) */
    int y=now->tm_year+1900, m=now->tm_mon+1, d=now->tm_mday+k, dm=dim_civil(y,m);
    *ad=d>dm?d-dm:d; return k;
}
/* the alarm to send now: 1 + (d,h,m), or 0 = none (0/0/0). A one-shot uses the occurrence it was saved for (once_at). */
static int sched_alarm(int64_t u,int*ad,int*ah,int*am,int64_t*at){
    *ad=*ah=*am=0; if(at) *at=0;
    if(!sch.on||!clk_valid(u)) return 0;
    struct tm lt; clk_local_at(u,&lt);
    if(!REP_MASK[sch.rep]){
        if(sch.once_at<=u) return 0;
        struct tm ot; clk_local_at(sch.once_at,&ot); *ad=ot.tm_mday; *ah=ot.tm_hour; *am=ot.tm_min; if(at) *at=sch.once_at; return 1;
    }
    int k=sched_next(&lt,sch.hm,REP_MASK[sch.rep],ad); *ah=sch.hm/60; *am=sch.hm%60;
    if(at) *at=u-(lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec)+k*86400LL+sch.hm*60LL;   /* approx. UTC of the occurrence (for display) */
    return 1;
}
/* a one-shot: the UTC time of the next occurrence of hh:mm after u (stored when the schedule is saved) */
static int64_t sched_once_target(int64_t u){
    if(!clk_valid(u)) return 0;
    struct tm lt; int dst=clk_local_at(u,&lt); int ad, k=sched_next(&lt,sch.hm,0,&ad);
    int64_t mid=u-(lt.tm_hour*3600+lt.tm_min*60+lt.tm_sec);
    int64_t t=mid+k*86400LL+sch.hm*60LL; struct tm ct; int dst2=clk_local_at(t,&ct); return t+(dst-dst2)*3600LL;   /* across a DST switch */
}
/* load all settings from config.json. The interval is NEVER widened for proven limits:
 * a value outside spec is shown as it is, marked, and blocks activity start
 * until the user has chosen a valid value. */
static void settings_load(void){
    cfg_read(); sched_load(); snd_cfg_load();
    for(int i=0;i<NSET;i++){
        Setting*s=&SET[i]; int v;
        s->lo=s->min; s->hi=s->max;
        if(s->flags&SF_SENSOR){ s->val=0; s->loaded=0; continue; }
        if(s->flags&SF_LOCAL){ s->val=ui_lang; s->loaded=1; continue; }      /* v6.3: the UI language comes from the lang file, not config.json */
        if(s->flags&SF_OWN){ s->val=sched_val(i); s->loaded=1; continue; }   /* v6.16: the schedule file (sched_load) */
        if(s->flags&SF_SND){ s->val=i==S_SOUND?sndc.mode:sndc.ready; s->loaded=1; continue; }   /* v6.18: the sound file (snd_cfg_load) */
        if(cfg_get_int(s->key,&v)){ s->val=v; s->loaded=1; }
        else { s->val=s->def; s->loaded=0; }
        if(s->flags&SF_LOCK){ s->lo=s->hi=s->val; continue; }
        if((s->flags&SF_EST)&&s->loaded&&s->val>s->hi&&s->val<=s->hard) s->hi=s->val;
    }
}
/* word 112/113/114 packing [PARITY §3.7; demo_v9 model_data_init 0xcf284..0xcf43c, onClick 0xeb1ac/0x103858]:
 *   word112 = (preInfusionSwitch ? 0x0100 : 0) | (preInfusionPressure & 0xFF)
 *   word113 = (preInfusionTime << 8) | (breakTime & 0xFF)          word114 = headTemper (whole °C; MCU x10)
 * The MCU recipe 0x1E10 reads word112 lo (only 3/4 are handled), copies word113 hi -> lo
 * (breakTime is overwritten) and uses word13 + word113_lo as the pre-infusion duration. */
static uint16_t pack112(int sw,int press){ return (uint16_t)((sw?0x0100:0)|(press&0xFF)); }
static uint16_t pack113(int t,int brk){ return (uint16_t)(((t&0xFF)<<8)|(brk&0xFF)); }

/* ---- frame policy (declared at the write queue): per-word spec interval + trigger words ---- */
static int word_in(int si,int v){ return set_valid(&SET[si],v); }
static int word_ok(int idx,uint16_t v){
    switch(idx){
    case 0:   return v<=1;                               /* power: 1 = PowerOn; 0 = PowerOff only w. I_POWEROFF (frame_policy) — v6.14 */
    case 1:   return v==0;                               /* 0=awake; 1 (sleep/drain) is never sent */
    case 2:   return word_in(S_GRINDMODE,v);
    case 3: case 6: case 9: case 16: case 22: case 24: return v<=1;   /* start words (intent checked in frame_policy) */
    case 4:   return word_in(S_GRINDW,v);
    case 5: case 13: case 15: return v==60;              /* hardcoded recipe words [fuart_thread 0x146810] */
    case 11:  return word_in(S_BREWT,v);
    case 14:  return word_in(S_STEAMG,v);
    case 17:  return word_in(S_HOTW,v);
    case 23:  return word_in(S_BREWW,v);
    case 25:  return word_in(S_STEAMT,v);
    case 112: return (v>>8)<=1 && word_in(S_PIPRESS,v&0xFF);
    case 113: return word_in(S_PITIME,v>>8);             /* lo byte = breakTime: arbitrary byte, no effect */
    case 114: return word_in(S_HEADT,v);
    default:  return v==0;                               /* 7,8,10,12,18..21,26..31: always 0 */
    }
}
static int frame_policy(uint8_t sub,const uint16_t*w,int len,uint32_t intent){
    if(intent&(TRIG_NEVER|~(TRIG_START|I_POWEROFF))) return WJ_TRIG;
    for(int k=0;k<len;k++){
        int idx=sub+k; uint16_t v=w[k];
        if(idx==0&&v==0&&!(intent&I_POWEROFF)) return WJ_TRIG;                    /* v6.14: PowerOff only w. explicit intent */
        if(idx<32&&v!=0){
            if(TRIG_NEVER&WBIT(idx)) return WJ_TRIG;                              /* calibration/reboot: never */
            if((TRIG_START&WBIT(idx))&&!(intent&WBIT(idx))) return WJ_TRIG;       /* start word without intent */
        }
        if(!word_ok(idx,v)) return WJ_RANGE;
    }
    return 0;
}

/* queues cmd6 write(s) for setting i with new value v. Returns: 1=queued, 0=no MCU part, -1=error.
 * Only single-word/pre-infusion blocks from PARITY §3.7-3.8 — never a trigger word in the frame
 * (grindMode is sent as sub2 len1; stock's sub2 len2 would also send start word 3). */
static int setting_mcu_push(int i,int v){
    uint16_t w[2];
    switch(SET[i].mcu){
    case M_NONE: return 0;
    case M_W4:   w[0]=(uint16_t)v; return wq_push(4,w,1)?1:-1;
    case M_W23:  w[0]=(uint16_t)v; return wq_push(23,w,1)?1:-1;
    case M_W11:  w[0]=(uint16_t)v; return wq_push(11,w,1)?1:-1;
    case M_W25:  w[0]=(uint16_t)v; return wq_push(25,w,1)?1:-1;
    case M_W14:  w[0]=(uint16_t)v; return wq_push(14,w,1)?1:-1;
    case M_W17:  w[0]=(uint16_t)v; return wq_push(17,w,1)?1:-1;
    case M_W2:   w[0]=(uint16_t)v; return wq_push(2,w,1)?1:-1;
    case M_W114: w[0]=(uint16_t)v; return wq_push(114,w,1)?1:-1;
    case M_W112: /* stock: usart_send(6,112,2) — word112+113 together [0xeb570/0x103bc8] */
        w[0]=pack112(i==S_PISW?v:sv(S_PISW), i==S_PIPRESS?v:sv(S_PIPRESS));
        w[1]=pack113(sv(S_PITIME),sv(S_BREAK));
        return wq_push(112,w,2)?1:-1;
    case M_W113: /* stock: usart_send(6,113,1) [0xeb700/0xeb880] */
        w[0]=pack113(i==S_PITIME?v:sv(S_PITIME), i==S_BREAK?v:sv(S_BREAK));
        return wq_push(113,w,1)?1:-1;
    }
    return -1;
}

/* ================= activity recipes (PARITY §3.1-3.4; observed frames, byte-for-byte) =================
 * GRIND  [L BREW.md W1]     cmd6 sub3 len4  : 1, dose, 60, 1
 * BREW   [L BREW.md W3]     cmd6 sub5 len18 : 60,0,0,0,1,0,temp,0,60,gear,60,0,water,0,0,0,0,1
 * WATER  [L HOTWATER.md W1] cmd6 sub9 len11 : 1,0,temp,0,60,gear,60,1,water,0,0
 * STEAM  [S SESSION.md §10] cmd6 sub24 len8 : 1,target,0,0,0,0,0,0   (stop: word24=0)
 * Only the config-derived fields are substituted; all other words are as in the atlas. Each
 * start frame states explicitly which start words it may set (I_*); words 26/27/30 in the
 * steam block are constant 0 and cannot become anything else (frame_policy). */
enum { ACT_NONE=-1, ACT_GRIND=0, ACT_BREW, ACT_WATER, ACT_STEAM };
static int act_push(int kind){
    uint16_t w[18];
    switch(kind){
    case ACT_GRIND: { uint16_t g[4]={1,(uint16_t)sv(S_GRINDW),60,1}; return wq_push_t(3,g,4,I_GRIND); }
    case ACT_BREW: {
        /* sync only if the MCU mirror differs from config (stock pushes its mirror at boot).
         * word113 is compared on the hi byte only: the MCU overwrites the lo byte itself (0x1E1E). */
        if(reg_seen[23] && reg[23]!=(uint16_t)sv(S_BREWW)){ w[0]=(uint16_t)sv(S_BREWW); if(!wq_push(23,w,1)) return 0; }
        if(reg_seen[112]&&reg_seen[113]&&reg_seen[114]){
            uint16_t p[3]={pack112(sv(S_PISW),sv(S_PIPRESS)),pack113(sv(S_PITIME),sv(S_BREAK)),(uint16_t)sv(S_HEADT)};
            if(p[0]!=reg[112]||(p[1]>>8)!=(reg[113]>>8)||p[2]!=reg[114]){ if(!wq_push(112,p,3)){ wq.n=0; return 0; } }
        }
        uint16_t b[18]={60,0,0,0,1,0,(uint16_t)sv(S_BREWT),0,60,(uint16_t)sv(S_STEAMG),60,0,(uint16_t)sv(S_HOTW),0,0,0,0,1};
        if(!wq_push_t(5,b,18,I_BREW)){ wq.n=0; return 0; }      /* never half sequences */
        return 1;
    }
    case ACT_WATER: { uint16_t h[11]={1,0,(uint16_t)sv(S_BREWT),0,60,(uint16_t)sv(S_STEAMG),60,1,(uint16_t)sv(S_HOTW),0,0}; return wq_push_t(9,h,11,I_WATER); }
    case ACT_STEAM: { uint16_t s[8]={1,(uint16_t)sv(S_STEAMT),0,0,0,0,0,0}; return wq_push_t(24,s,8,I_STEAM); }
    }
    return 0;
}
/* STOP must never be rejectable by the policy: the target word is clamped into the spec interval */
static int act_push_steam_stop(void){
    uint16_t s[8]={0,(uint16_t)clampi(sv(S_STEAMT),SET[S_STEAMT].lo,SET[S_STEAMT].hi),0,0,0,0,0,0};
    return wq_push(24,s,8);
}
/* ================= v6.13: manual STOP for grind / brew / water (UI_v6.md §18 + CHANGES-v6.13) =================
 * User-reported bug in v6.12: hot water could not be stopped from the screen — only steam had a STOP button. The stop words are reversed from
 * the MCU firmware (MEM03_V514_260923.bin) and cross-checked against the stock UI's own stop calls (demo_v9, symbols preserved):
 *   GRIND cmd6 sub6  len1: word 6  = 0   aa 55 06 06 01 00 00 c4 47
 *         MCU grind-ctl 0x19C6: the grinder runs while word 6 != 0 (0x1AA2: work_sta |= 0x40, else &= ~0x40). The MCU's own
 *         stops — weight target 0x1A68, timeout 0x1A72, error 0x1A3E/0x1A8C — write exactly word 6 = 0. Stock: onClick 0x10928C
 *         usart_send(6,6,1), after mirror word 6 has been flipped to 0 (the start branch 0x109278 sends sub3 len4).
 *   BREW  cmd6 sub22 len1: word 22 = 0   aa 55 06 16 01 00 00 c0 87
 *         MCU brew-ctl 0x1E9C: on every CHANGE of word 22, running = (word 22 != 0) (0x1F94..0x1FC6). The MCU's weight stop
 *         itself writes word 22 = 0 (0x1F92) — same path, incl. the valve pulse on bit 2 afterwards (0x20B2..0x20EC). Stock:
 *         onClick 0x109694 flips mirror word 22 to 0 and sends the WHOLE block usart_send(6,5,18); merakiplus writes only the word
 *         that changes (words 5..21 in stock's block are the MCU's own polled values — a no-op for the MCU; cmd6 = memcpy).
 *   WATER cmd6 sub16 len1: word 16 = 0   aa 55 06 10 01 00 00 c0 0f   (+ sub19 len1 word 19 = 0 — only if word 19 != 0)
 *         MCU hotwater-ctl 0x1B8C: active = (word 19 | word 16) != 0, recomputed every cycle (0x1C44..0x1C4E). The MCU's weight stop
 *         itself writes word 16 = 0 (0x1C80). Stock: hotWaterStartStop 0x11468C usart_send(6,16,1) (auto mode, = merakiplus's
 *         start) and 0x114720 usart_send(6,19,1) (manual mode; merakiplus never starts it, but stops it if it is seen).
 *   STEAM cmd6 sub24 len8: word 24 = 0   (unchanged from v6)
 * A stop frame contains only zeros: frame_policy does not reject it (start words <= 1, others == 0), and it can never start
 * anything. Same write queue as everything else: CRC + ACK wait + one retransmit. AUTO-STOP is unchanged: merakiplus sends nothing when the MCU
 * stops by itself. Confirmation (stop_step): the activity's work_sta bits are cleared — grind bit 6; brew bits 1+2 (bits 2 + 12 are cleared
 * in the first cycle after stop, 0x20C4); water bits 1+3 (bit 3 is always cleared, 0x1C9A; bits 1+9 are shared with boiler refill). */
static int act_push_stop(int kind){
    uint16_t z=0; int n0=wq.n;
    switch(kind){
    case ACT_GRIND: return wq_push(6,&z,1);
    case ACT_BREW:  return wq_push(22,&z,1);
    case ACT_WATER:
        if(!wq_push(16,&z,1)) return 0;
        if(reg_seen[19]&&reg[19]!=0&&!wq_push(19,&z,1)){ wq.n=n0; return 0; }   /* never a half water stop */
        return 1;
    case ACT_STEAM: return act_push_steam_stop();
    }
    return 0;
}
#define STOP_CONFIRM_MS 5000   /* how long to wait for the bits to clear (measured: ACK ~20-280 ms + MCU cycle 10 ms + poll <= 0.5 s) */
static struct { int kind, active; ms_t t_req; long seq, n_req, n_ok, n_fail; int latched; } stp={ACT_NONE,0,0,0,0,0,0,0};   /* v6.20: latched = a STOP for a latched start word (confirmed when the word reads 0) */

/* ================= phases (work_sta word 33; SESSION.md §6) ================= */
enum { PH_OFFLINE=0, PH_SLEEP, PH_ERROR, PH_HEAT, PH_READY, PH_GRIND, PH_PRE, PH_EXT, PH_WATER, PH_STEAM, PH_DONE, NPHASE };
_Static_assert(TX_PH_DONE-TX_PH_OFFLINE==NPHASE-1,"TX_PH_* must follow PH_*");
static const char* ph_name(int p){ return TR(TX_PH_OFFLINE+((p>=0&&p<NPHASE)?p:PH_OFFLINE)); }
static int link_up(ms_t now){ return now-last_rx_ms<1500; }
static int phase_raw(ms_t now){
    if(!link_up(now)) return PH_OFFLINE;
    uint16_t ws=reg[33];
    if((ws&0x4020)==0x4020) return PH_STEAM;                 /* bits 5+14 */
    if(ws&0x0040) return PH_GRIND;                           /* bit 6 */
    if((ws&0x0006)==0x0006) return (ws&0x0200)?PH_EXT:PH_PRE;/* bits 1+2 (+9 = extraction) */
    if((ws&0x000A)==0x000A) return PH_WATER;                 /* bits 1+3 */
    if(reg[34]||reg[35]||reg[36]) return PH_ERROR;
    if((reg_seen[1]&&reg[1]==1) || (reg_seen[33]&&!(ws&0x0100))) return PH_SLEEP;
    if(reg_seen[40]&&reg_seen[11]&&(int)reg[40] < (int)reg[11]-20) return PH_HEAT;
    return PH_READY;
}
static int phase_active(int p){ return p>=PH_GRIND&&p<=PH_STEAM; }
/* ---- v6.6: phase semantics per STEAMLIVE.md (proven live) ----
 * (1) TWO STEAM PATHS, both = work_sta bits 5+14 (0x4020):
 *     AUTO (latched)  word 24 != 0 — UI start (cmd6 sub24 len8, word24=1). The MCU stops BY ITSELF when the milk
 *                     (word 48) reaches the target (word 25), clears bits 5+14 and resets word 24. No stop write.
 *                     The probe keeps rising approx. 4 °C for ~11 s after stop (thermal inertia) — info only.
 *     WAND (momentary) word 24 == 0 — the physical wand switch (word 44 bit0, word 46 = 1 follows approx. 0.4 s
 *                     later). NO auto-stop, not even above the target: stops when the wand is released.
 *                     Sends no UART frames; a STOP write (word24=0) would do nothing.
 *     The class is decided by word 24 while bits 5+14 are set, and is STICKY for AUTO: after a manual
 *     STOP write, word 24 = 0 (ACK echo) for a moment before the MCU clears the bits — it is still AUTO.
 *     If word 24 has not been read yet: UNKNOWN (neutral text; the STOP button is shown — it is always safe).
 * (2) CONCURRENT PHASES: brew bits (1+2+9+12) and steam bits (5+14) are disjoint and can be set together
 *     (0x5327 = flush during brew). HOME then shows BOTH: the main activity (grind/brew/water: gauge,
 *     weight, time) + a steam marker, and the milk temperature is in the stat row.
 * phase_raw() is UNCHANGED (the state contract: steam first). phase_main() is what HOME shows as the
 * main activity: grind/brew/water take precedence over steam. The guards still use phase_raw(): any
 * activity — including a wand flush — blocks start, SAVE, SWITCH NOW and boot push. */
enum { SK_NONE=0, SK_UNKNOWN, SK_AUTO, SK_WAND };
static struct { int kind, last_kind, wand; ms_t t0; } stm;   /* kind: ongoing steam; last_kind: the most recently finished; wand: the switch was seen */
static int steam_bits(void){ return (reg[33]&0x4020)==0x4020; }
static int wand_sw(void){ return (reg_seen[44]&&(reg[44]&1))||(reg_seen[46]&&reg[46]!=0); }
static void steam_track(ms_t now){
    if(!link_up(now)||!steam_bits()){ if(stm.kind!=SK_NONE){ stm.last_kind=stm.kind; stm.kind=SK_NONE; } return; }
    if(stm.kind==SK_NONE){ stm.kind=SK_UNKNOWN; stm.t0=now; stm.wand=0; }
    if(wand_sw()) stm.wand=1;
    if(reg_seen[24]&&reg[24]!=0) stm.kind=SK_AUTO;                         /* latched — sticky until the bits clear */
    else if(reg_seen[24]&&stm.kind!=SK_AUTO) stm.kind=SK_WAND;             /* word 24 = 0: the wand path */
}
static int steam_stop_visible(void){ return stm.kind==SK_AUTO||stm.kind==SK_UNKNOWN; }   /* WAND: STOP would be a no-op */
static int steam_target(void);                                                           /* word 25 (the MCU's target), else config */
static int phase_main(ms_t now){
    if(!link_up(now)) return PH_OFFLINE;
    uint16_t ws=reg[33];
    if(ws&0x0040) return PH_GRIND;
    if((ws&0x0006)==0x0006) return (ws&0x0200)?PH_EXT:PH_PRE;
    if((ws&0x000A)==0x000A) return PH_WATER;
    return phase_raw(now);
}
static const char* steam_label(int k){ return TR(k==SK_AUTO?TX_PH_STEAM_AUTO:(k==SK_WAND?TX_PH_STEAM_WAND:TX_PH_STEAM)); }
/* v6.13: which STOP buttons HOME shows. The main activity (phase_main: grind / brew / water) ALWAYS has a STOP button as long
 * as its bits are set; latched steam has its own as before. Concurrent main activity + latched steam = two buttons side by side (left =
 * the main activity, right = steam). Wand/flush still has none (a STOP write would do nothing). Returns: number of buttons (0..2). */
static int stop_main_kind(ms_t now){
    switch(phase_main(now)){ case PH_GRIND: return ACT_GRIND; case PH_PRE: case PH_EXT: return ACT_BREW; case PH_WATER: return ACT_WATER; default: return ACT_NONE; }
}
/* are the activity's work_sta bits still set? (same masks as phase_raw/phase_main) */
static int stop_running(int kind){
    uint16_t ws=reg[33];
    switch(kind){ case ACT_GRIND: return (ws&0x0040)!=0; case ACT_BREW: return (ws&0x0006)==0x0006; case ACT_WATER: return (ws&0x000A)==0x000A; case ACT_STEAM: return steam_bits(); }
    return 0;
}
/* v6.20 (review 2026-11, protocol #2): a LATCHED START WORD. A start the MCU ACKed but never ran (NO START · CHECK THE MACHINE)
 * leaves its start word set — grind 6, brew 22, water 16, steam 24 (the ACK echo and the MCU's own words 0..55 say so) — with the
 * status bits clear. v6.13-v6.19 keyed the STOP buttons on the bits only, so there was no way to clear the word from the UI, and
 * stock's isExtractionOrSteamSwitchOn (pw_busy: words 6/22/24) kept the power button and auto off blocked. Whether the MCU later
 * starts by itself from such a word is unknown (UI_v6.md "Word 24 = 1 without the steam starting"; mcu_sim does re-trigger).
 * Now HOME keeps offering that activity's EXISTING STOP (the same all-zero stop frame, through the frame policy) while the word has
 * been set with the bits clear for LATCH_MS, the controller is awake and no start of ours is pending. Nothing is sent by itself;
 * the user's STOP writes the word to 0 (ACK echo), which ends the latch, the busy state and the button. pw_busy and auto off are
 * unchanged (stock parity). After a normal stop the MCU clears the word within ~0.4 s (word 6 is consumed last), far below LATCH_MS. */
#define LATCH_MS 3000
static const uint8_t START_WORD[4]={6,22,16,24};
static struct { int on[4], pend; ms_t t0[4]; } lat={{0,0,0,0},-1,{0,0,0,0}};
static int start_word_set(int k){ int w=START_WORD[k]; return reg_seen[w]&&reg[w]!=0; }
static int latch_cond(int k,ms_t now){ int p=phase_raw(now); return p!=PH_OFFLINE&&p!=PH_SLEEP&&start_word_set(k)&&!stop_running(k); }
static void start_latch_track(ms_t now,int pending){   /* the clock also runs while our start is pending: STOP appears together with NO START */
    lat.pend=pending;
    for(int k=0;k<4;k++){ if(!latch_cond(k,now)) lat.on[k]=0; else if(!lat.on[k]){ lat.on[k]=1; lat.t0[k]=now; } }
}
static int start_latched(int k,ms_t now){ return k>=0&&k<4&&lat.pend!=k&&lat.on[k]&&now-lat.t0[k]>=LATCH_MS&&latch_cond(k,now); }
static int stop_btns(ms_t now,int k[2]){
    int n=0, m=stop_main_kind(now); k[0]=k[1]=ACT_NONE;
    if(m!=ACT_NONE) k[n++]=m;
    if(steam_stop_visible()) k[n++]=ACT_STEAM;
    for(int j=0;j<4&&n<2;j++) if(start_latched(j,now)&&!(n==1&&k[0]==j)) k[n++]=j;   /* v6.20: + a latched start word (its own STOP) */
    return n;
}
static const char* err_text(void){
    uint16_t e1=reg[34],e2=reg[35],e3=reg[36];
    if(e3&0x10) return TR(TX_ER_E3_BREW);
    if(e3&0x20) return TR(TX_ER_E3_STEAM);
    if(e1&0x20) return TR(TX_ER_E1_GROUP);
    if(e1&0x01) return TR(TX_ER_E1_MAINS);
    if(e2&0x01) return TR(TX_ER_E2_BREW);
    if(e2&0x02) return TR(TX_ER_E2_GROUP);
    if(e2&0x08) return TR(TX_ER_E2_STEAM);
    if(e2&0x10) return TR(TX_ER_E2_MILK);
    return TR(TX_ER_UNKNOWN);
}
/* v6.11: the error code (E1/E2/E3) is no longer in the HOME subline (that is plain language); it is shown dimmed in the chart area
 * with a pointer to DIAGNOSE (the E1 / E2 / E3 cell). Same priority as err_text(). */
static const char* err_code(void){
    uint16_t e1=reg[34],e2=reg[35],e3=reg[36];
    if(e3&0x30) return "E3";
    if(e1&0x21) return "E1";
    if(e2&0x1B) return "E2";
    return "?";
}

/* ================= v6.9: display conditioning (ONLY what is drawn) =================
 * User complaint: large swings make the graph look unstable — on the device (ADC noise ±9..±40, maintenance/deadhead
 * pulses on word 51 up to 15-16k for 0.1..1 s, BREW.md §6 / STEAMLIVE.md §5.3) and in the SIM. The fix lives in the UI,
 * so it applies in both places (sim/mcu_sim.py's 'calm' is a separate demo aid, default OFF, and is not used here).
 *  RAW  : reg[] and the ring buffer g_p/g_w/g_m are unchanged. ALL control (writes, auto-stop expectations, guards, thresholds,
 *         phase logic, DIAGNOSE counters) still reads the raw values — v6.9 changes none of them.
 *  CHART: per sample (10 Hz) median of the last 3 (removes single-sample spikes) -> EMA, alpha CD_A_CHART = 0.18
 *         (time constant -1/ln(1-0.18) = 5.0 samples = 0.50 s). Stored next to the raw values in g_pd/g_wd/g_md.
 *         The filters follow median-3 directly for 2 samples when the activity changes (start/stop is not smeared; see SWITCH).
 *  SCALE: y top = max(target/floor x 1.08, P x CD_HEAD (1.15)), P = the CD_PCT percentile (p90) of the window's ACTIVE raw samples
 *         (those above the noise floor: pressure >= 40, weight/milk > 0). Short pulses (deadhead after brew, maintenance at rest) are few
 *         samples and therefore do not flatten the curve; when a raw sample lies above the top, a small tick is drawn at
 *         the top edge (the information is not hidden). Only ACTIVE samples, because a p90 over the WHOLE 40 s window is 0 in the
 *         first approx. 4 s of an extraction (the window is mostly idle) and would squash the curve up into the ceiling — the selftest
 *         measures both. cd.scale=CS_HOLD (highest level held for CD_HOLD samples) and CS_MAX (v6.8) can be selected in the SIM.
 *  IDLE : the pressure curve is drawn only for samples taken during brew/pre-inf./water (g_a). Outside an activity, word 51 is
 *         maintenance/pump pulses (small heating cycles 400-900 for 0.5-1.5 s, deadhead 3000-16670 for 0.1-0.25 s) — they are shown
 *         as small ticks on the baseline. The chart is "active" at rest as long as the 40 s window contains a brew/water (v6.8: one raw
 *         sample >= 40 — so every maintenance pulse turned on a spike chart at rest). The weight line is drawn all the way.
 *  GATE : the pressure curve also has a pulse gate: if median-3 jumps to > 2 x the shown value + 400, the shown value is held
 *         for up to CD_GATE = 3 samples (0.3 s); if the jump lasts longer, it is real and is followed. Median-3 alone only removes
 *         1-sample pulses — a 2-sample deadhead pulse (15500) would otherwise show up as a 1.5 s long hump at the ceiling.
 *  SWITCH: when the activity (or the weight source) changes, the chart filters follow median-3 directly for 2 samples (CD_FAST) instead
 *         of EMA — the step is not smeared, and a pulse sample right at the switch does not get through.
 *  HERO : the hero number (+ gauge and the BAR field, which show the same quantity) = median-3 + EMA alpha CD_A_HERO = 0.25 (0.35 s),
 *         updated at a 10 Hz rate regardless of frame rate. Snaps to the raw value on phase change (incl. DONE), change of
 *         source (weight/milk/boiler), change of setpoint/target, a median jump > CD_HERO_JUMP and after > 0.5 s without update.
 *  WEIGHT: (v6.9, user complaint about periodic weight spikes at rest) the source is LATCHED (wsl, see "stable weight source") instead
 *         of following the phase sample by sample, and the shown weight (hero, chart, stat field) has a DEADBAND: |weight| < 0.25 g
 *         (|word| < CD_WDB = 2.5 in 0.1 g) is shown as 0. Words 38/39 are SIGNED (tare 0xFFFD = -3): all filtering is done on int16
 *         values (s16) — a word interpreted as unsigned would smear a tare of -0.3 g out towards 6553.3 g.
 *  TUNING: -DCD_A_CHART=… -DCD_A_HERO=… -DCD_MED=1|3 -DCD_PCT=… -DCD_HOLD=… -DCD_HEAD=… -DCD_WDB=… at build; in the SIM live via
 *         ctrl {"cmd":"cond", ...}; DIAGNOSE → GRAPH switches to the raw (unconditioned) series + values — this session only. */
#ifndef CD_A_CHART
#define CD_A_CHART 0.18f
#endif
#ifndef CD_A_HERO
#define CD_A_HERO 0.25f
#endif
#ifndef CD_MED
#define CD_MED 3
#endif
#ifndef CD_HOLD
#define CD_HOLD 6
#endif
#ifndef CD_PCT
#define CD_PCT 90
#endif
#ifndef CD_HEAD
#define CD_HEAD 1.15f
#endif
#ifndef CD_WDB
#define CD_WDB 2.5f          /* weight deadband: |weight| < 0.25 g is shown as 0 (user's request) — display only */
#endif
#ifndef CD_GATE
#define CD_GATE 3            /* pulse gate (chart pressure only): median > 2 x shown + CD_GATE_ABS is held for up to 3 samples (0.3 s) */
#endif
#define CD_GATE_ABS 400.f
#define CD_W_JUMP 20.f       /* weight series: median jump > 2.0 g in one sample = cup off/on -> snap (noise ±0.2 g; a brew rises approx. 0.2 g/sample) */
#define CD_HERO_JUMP 150     /* hero: median jump > 15.0 units (g / °) in 0.1 s = a real step (cup off/on) -> snap, no gliding */
enum { CS_HOLD=0, CS_PCT, CS_MAX };
static struct { float a_chart,a_hero,head; int med,hold,pct,scale,raw,gate; } cd={CD_A_CHART,CD_A_HERO,CD_HEAD,CD_MED,CD_HOLD,CD_PCT,CS_PCT,0,CD_GATE};
#if defined(MERAKIPLUS_SIM)||defined(MERAKIPLUS_PREVIEW)
static const char*const CS_KEY[]={"hold","pct","max"};     /* ctrl "cond" + state JSON (SIM/preview only) */
#endif
/* the weight deadband (display only; the RAW view from DIAGNOSE has no deadband) */
static float wdb(float v){ return (cd.raw||fabsf(v)>=CD_WDB)?v:0.f; }
static int wdbi(int v){ return (int)wdb((float)v); }
typedef struct { int h[3],m,fast,held; float y; } Cond;  /* the 3 latest raw samples, latest median, median-follow counter, gate counter, the EMA value */
static int med3(int a,int b,int c){ int lo=a<b?a:b, hi=a<b?b:a; return c<lo?lo:(c>hi?hi:c); }
static void cond_snap(Cond*c,int x){ c->h[0]=c->h[1]=c->h[2]=c->m=x; c->y=(float)x; c->fast=0; c->held=0; }
/* activity switch in the chart: the next CD_FAST samples follow median-3 DIRECTLY (no EMA smearing of the step), but a
 * single pulse sample right at the switch does not get through (a plain snap to the raw value would take the pulse along and let it
 * decay slowly — that is what v6.9-wip looked like when a brew ended in a deadhead pulse) */
#define CD_FAST 2
static void cond_rearm(Cond*c){ c->fast=CD_FAST; c->held=0; }
static float cond_stepg(Cond*c,int x,float a,int gate){
    c->h[0]=c->h[1]; c->h[1]=c->h[2]; c->h[2]=x;
    c->m=cd.med>=3?med3(c->h[0],c->h[1],c->h[2]):x;
    if(c->fast>0){ c->fast--; c->y=(float)c->m; return c->y; }
    if((gate&1)&&cd.gate>0&&(float)c->m>c->y*2.f+CD_GATE_ABS){ if(c->held<cd.gate){ c->held++; return c->y; } }  /* pulse gate: hold */
    else c->held=0;
    if((gate&2)&&fabsf((float)c->m-c->y)>CD_W_JUMP){ c->y=(float)c->m; return c->y; }      /* weight: real step (cup off/on) -> snap */
    c->y+=a*((float)c->m-c->y);
    if(fabsf((float)c->m-c->y)<0.5f) c->y=(float)c->m;      /* the tail lands exactly (otherwise an EMA creeps forever) */
    return c->y;
}
static float cond_step(Cond*c,int x,float a){ return cond_stepg(c,x,a,0); }

/* ================= history (60 s @ 10 Hz) ================= */
#define GN 640
static ms_t g_t[GN]; static int g_p[GN], g_w[GN], g_m[GN];   /* RAW — the only thing control/tests/DIAGNOSE read */
static unsigned char g_a[GN];                                  /* v6.9: sample taken during brew/pre-inf./water (that is where the pressure curve belongs) */
static float g_pd[GN], g_wd[GN], g_md[GN];                    /* v6.9: conditioned (for drawing only) */
static int g_n=0,g_head=0;
static Cond cf_p,cf_w,cf_m; static int cf_ph=-1, g_lsrc=39;
/* v6.15: the graph starts over when an activity starts (display only). g_prim = the main phase (phase_main) of the latest ONLINE
 * sample was active; OFFLINE samples do not count as idle, so a link gap during a brew does not wipe the brew on reconnect.
 * gr.t0 = start of the current activity's curve; resets = idle -> active clears; reanchors = re-clears within G_REANCHOR_MS. */
#define G_REANCHOR_MS 1500
static int g_prim=0;
static struct { int resets,reanchors; ms_t t0; } gr={0,0,-100000};
static void graph_clear(void){ g_n=0; g_head=0; cf_ph=-1; }   /* empty ring + the next sample snaps all three chart filters */
#ifdef MERAKIPLUS_PREVIEW
static int gr_off=0;                                           /* preview/selftest only: 1 = v6.14 behaviour (no reset) for the before/after proof */
#define GR_ON (!gr_off)
#else
#define GR_ON 1
#endif
/* src = the weight word (38/39), chosen by the latched source (wsrc_tick). The weight is read SIGNED (int16). The filters snap to
 * the raw value when the ACTIVITY (grind/pre-inf./extraction/water/steam vs. idle) or the source changes — not on switches between
 * the idle phases (READY/HEATING/SLEEPING …), so threshold flicker at rest does not reset the smoothing. */
static void graph_push(ms_t t,int p,int src){
    int w=(int)(int16_t)reg[src], m=reg[48], pm=phase_main(t), key=(phase_active(pm)?pm:0)<<8|src;
    if(GR_ON&&phase_active(pm)){
        if(!g_prim){ graph_clear(); gr.t0=t; gr.resets++; }                  /* v6.15: idle -> active: the curve starts from the tare */
        else if(g_n>0&&t>=gr.t0&&t-gr.t0<G_REANCHOR_MS&&(src!=g_lsrc||(float)w<(float)g_w[(g_head+GN-1)%GN]-CD_W_JUMP)){
            graph_clear(); gr.reanchors++; }                                     /* late source latch / late tare right after the start */
    }
    if(pm!=PH_OFFLINE) g_prim=phase_active(pm);
    if(cf_ph<0){ cond_snap(&cf_p,p); cond_snap(&cf_w,w); cond_snap(&cf_m,m); cf_p.fast=cf_w.fast=cf_m.fast=CD_FAST+1; }   /* first sample (possibly a pulse: gone again after 2 samples, no tail) */
    else if(key!=cf_ph){ cond_rearm(&cf_p); cond_rearm(&cf_m);                                         /* activity switch: no smearing */
        if(src!=g_lsrc) cond_snap(&cf_w,w); else cond_rearm(&cf_w); }                                 /* source switch: a DIFFERENT weight word — no history to carry over */
    cf_ph=key; g_lsrc=src; g_a[g_head]=(unsigned char)(pm==PH_PRE||pm==PH_EXT||pm==PH_WATER);
    g_t[g_head]=t; g_p[g_head]=p; g_w[g_head]=w; g_m[g_head]=m;
    g_pd[g_head]=cond_stepg(&cf_p,p,cd.a_chart,1); g_wd[g_head]=cond_stepg(&cf_w,w,cd.a_chart,2); g_md[g_head]=cond_step(&cf_m,m,cd.a_chart);
    g_head=(g_head+1)%GN; if(g_n<GN)g_n++;
}
static struct { Cond f; int key,set,ok,n_snap; ms_t t; } hf;   /* the hero filter (hero_v() near the HOME model) */
#if defined(MERAKIPLUS_SIM)||defined(MERAKIPLUS_PREVIEW)
static void cond_reset(void){ cf_ph=-1; }                     /* the next sample snaps all three chart filters (ctrl "cond" / sim_reset) */
#endif
/* latest conditioned pressure (gauge + BAR field during brew); raw when the view is set to RAW or the history is empty/stale */
static float p_disp(ms_t now){
    if(cd.raw||g_n==0) return reg[51];
    int i=(g_head+GN-1)%GN; if(now-g_t[i]>400||now<g_t[i]) return reg[51];
    return g_pd[i];
}
/* latest conditioned weight from source word src (the stat field at rest), w. deadband; raw (s16) when the view is RAW, the history
 * is empty/stale or the latest sample came from the other weight word */
static int w_disp(int src,ms_t now){
    int raw=(int)(int16_t)reg[src];
    if(cd.raw||g_n==0||g_lsrc!=src) return wdbi(raw);
    int i=(g_head+GN-1)%GN; if(now-g_t[i]>400||now<g_t[i]) return wdbi(raw);
    return (int)lroundf(wdb(g_wd[i]));
}

/* backlight live preview (device only; mapping duty=100-7v verified in WRITETEST.md) */
static int bl_enabled=1;
#ifdef SIM_RT
static int sim_bl=-1;                                      /* latest brightness preview (for state JSON only; sysfs is NEVER written in SIM) */
#endif
static void bl_set(int v){
#ifdef DEVICE_RT
    if(!bl_enabled) return;
    int duty=100-7*v; if(duty<1)duty=1; if(duty>99)duty=99;
    int fd=open(BL_SYSFS,O_WRONLY); if(fd<0) return;
    char b[8]; int n=snprintf(b,sizeof b,"%d",duty); if(write(fd,b,n)<0){} close(fd);
#elif defined(SIM_RT)
    sim_bl=v;
#else
    (void)v;
#endif
}
/* v6.14: backlight fully OFF / ON (the power button). Stock powerOff 0x1490C0: uikit_os_back_light_off (= pwm2/enable 0,
 * 0xC2278) and then blpwm_set_dutycycle(2,0). merakiplus writes duty 99 (= dimmest, mapping duty = 100 - 7v) BEFORE enable 0: with
 * "polarity inversed", stock's duty 0 is the BRIGHTEST light if enable 0 should fail to take effect. ON: duty from config's
 * brightness and then enable 1 (stock: app restart -> uikit_os_back_light_set + back_light_on 0xC2298). bl_is_off is the ground truth
 * for the exit paths: a merakiplus process that exits with the light off turns it back on (otherwise the stock UI would be dark after run.sh). */
static int bl_is_off=0;
static void bl_power(int on){
    bl_is_off=!on;
#ifdef DEVICE_RT
    if(!bl_enabled) return;
    int fd;
    if(on){ bl_set(SET[S_BL].val); fd=open(BL_EN_SYSFS,O_WRONLY); if(fd>=0){ if(write(fd,"1",1)<0){} close(fd); } }
    else { fd=open(BL_SYSFS,O_WRONLY); if(fd>=0){ if(write(fd,"99",2)<0){} close(fd); }
           fd=open(BL_EN_SYSFS,O_WRONLY); if(fd>=0){ if(write(fd,"0",1)<0){} close(fd); } }
#endif
}

