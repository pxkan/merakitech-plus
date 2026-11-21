# Audio — how the stock UI makes sound, and Meraki+'s start jingle + per-boiler ready tones (v6.18, fix v6.18.1)

The stock UI plays a jingle at power-on and **one** "pre-heat complete" sound once **both** boilers are hot.
Meraki+ v6.18 does the same job with **its own** short sounds, but with a **different tone per boiler**, so you can
hear which one is ready: the brew (extract) boiler for espresso, the steam boiler for milk.

Everything below the "Reversed" heading comes from the stock UI binary (`demo_v9`, symbols kept), its player library
`libhvplayer.so`, the controller firmware (MEM03) and a read-only look at the display (file listings, md5s, device
nodes). **No vendor audio, library or code is in this repository.** Meraki+ ships only its own synthesized WAV files
and a small helper that loads the display's own libraries at run time.

> **Status (v6.18.1):** the speaker path works on a real machine (tone heard, 2026-11-21). The v6.18 helper could not
> load the display's `libmi_common.so` there (`undefined symbol: sem_post`), so **every sound of v6.18 is silent on a
> machine**; v6.18.1 fixes the helper — see [§3 Hardware check and the v6.18.1 fix](#3-hardware-check-and-the-v6181-fix).

---

## 1. Reversed: the stock UI's audio path

### 1.1 Files on the display

| Path | What |
|---|---|
| `/customer/audio/*.mp3` | 11 effect sounds (from `/customer/audio.tar.gz`): `slide`, `keyboard`, `LackWater`, `toasterror`, `fault`, `power`, `preheatCompele`, `nfc`, `brewcOrMilk`, `milkTempHight` (+ a `keyboard.mp3.bak`) |
| `/customer/video/power.mp4` | the power-on animation **with the start jingle as its audio track** |
| `/customer/video/*-animation*.mp4` | brew/steam animations (video only while an activity runs) |
| `/customer/libs/libhvplayer.so` | the player (`mm_player_*` API), built on ffmpeg (`libavformat/avcodec/swresample.so`) |
| `/lib/libmi_sys.so`, `/lib/libmi_ao.so` (+ `libmi_common.so`) | SigmaStar MI layer; audio out = `/dev/mi_ao` |

There is **no ALSA** (`/proc/asound` does not exist, no `aplay`/`madplay`): the only way to the speaker is MI_AO.

### 1.2 Call chain (stock UI `demo_v9`)

| Step | Address | What it does |
|---|---|---|
| `playpthread_init` | `0x137478` | `sem_init(0x1AEF2C)`, starts the sound thread `deal_play` |
| `set_volumn_type(slot)` | `0x1374B8` | stores the slot in `0x1A588C`, `sem_post` — the **only** way UI code requests a sound |
| `deal_play` | `0x13725C` | loop: `sem_wait`; skip if `model+0x15B0` (power-on logo running); volume = config `volume` (`model+0x53`) **capped at 70** (`0x1372C4`; 50 on the factory-test page, `model+0x15B1`); 0 → nothing; skip if `model+0x15AD` (a brew/steam **animation video** runs) — the request is **dropped**, not queued; else `switch(slot)` → `mmplayer_play_audio_once("audio/<file>.mp3")`; slot := 11 (idle) |
| `mmplayer_play_audio_once` | `0x136CE8` | stops a sound still playing, allocates the player context, starts `mm_player_thread` |
| `mm_player_thread` | `0x135C78` | `mm_player_set_opts("audio_device","",0)`, `("audio_only","",1)`, `("video_only","",0)`, …, `("play_mode","",mode)`; `mm_player_open(path, x, y, w, h)`; `mm_player_set_volumn(vol)`; polls `mm_player_get_status()` until ≠ 0; `mm_player_close()` |

Slots (`deal_play` jump table `0x137358`, paths at `0x189A48…0x189AFC`):

| Slot | File | Requested by |
|---|---|---|
| 0 | `slide.mp3` | `lv_roller_deal_sound` `0xD8808` (roller scroll) |
| 1 | `keyboard.mp3` | `lv_keyboard_sound_cb` `0xDB51C` (key click) |
| 2 | `LackWater.mp3` | a page's `onHandle` `0x125D94` (call at `0x125FF0`) |
| 3 | `toasterror.mp3` | same `onHandle` (call at `0x126018`) |
| 4 | `fault.mp3` | `uartRxProcess` `0x1451E0` / `0x145588` / `0x145A44` (E1/E2/E3) |
| 5 | `power.mp3` | — (no caller in this firmware; sets `0x1AEF0D`) |
| **6** | **`preheatCompele.mp3`** | **`uartRxProcess` `0x145C3C`** — see 1.4 |
| 7 | `nfc.mp3` | NFC pages `0xF5318`, `0x120E78`, `0x125F68`, `0x12CFA8` |
| 8, 9 | `brewcOrMilk.mp3` | — |
| 10 | `milkTempHight.mp3` | — |

### 1.3 The start jingle

`page_logo` (`app/pages/page_logo.c`) `onEnter` `0x1001B8` runs at every power-on: backlight on, **`model+0x15B0 = 1`**
(blocks all effect sounds), then `mmplayer_create_video("video/power.mp4", 0)` at 480 × 480 with
`mmplayer_setVolume(min(volume, 70))` (`0x100334…0x100358`). The jingle is the **audio track of `power.mp4`**.
`onHandle` `0x100374` ends the logo after ~3 s (a 200 ms tick counter > 3000 ms), destroys the player, sets `model+0x15AE` and
**clears the pre-heat latch `model+0x15AA = 0`**; `onExit` `0x1004F8` clears `model+0x15B0`.

### 1.4 "Pre-heat complete" — both boilers

`uartRxProcess` `0x145BA0…0x145C3C`:

```
if (!model[0x15AA]                       // not yet played since the last power-on (logo)
    && (u16)model[0x1064] > 99           // word 62
    && (u16)model[0x1066] > 99           // word 63
    && !model[0x15B0]) {                 // logo not running
    model[0x15AA] = 1;  set_volumn_type(6);   // preheatCompele.mp3, ONCE per power-on
}
```

The UI mirrors the controller's register words at `model + 0xFE8 + 2·word` (the `cmd 0x83` copy at `0x144EAC…0x144EF4`:
`memcpy(model+0xFE8 + 2·sub, payload, 2·len)`), so `model+0x1064` = **word 62** and `model+0x1066` = **word 63**.
HOME draws the same two words as its arcs (0–100, `home_date_update` `0x10DFF0` / `0x10E0DC`); `brewUpdateTemper`
`0x11B4C4` / `dealSteamTemper` `0x11F0CC` treat > 78 as "almost ready".

### 1.5 Words 62 / 63 are the controller's heat **stage** (correction)

`docs/registermap.md` called words 62/63 "heating power". The MCU's `boiler_heat` task (MEM03 `0x3502`) actually writes
a **stage 0 / 30 / 60 / 100**:

| | 0 | 30 | 60 | 100 |
|---|---|---|---|---|
| **word 62 (extract boiler)** `0x35EE…0x3672` | e boiler < 40.0 °C | < 60.0 °C | < brew set temp (word 11) | ≥ word 11 (+ an MCU-internal state test `0x246C`) — and always while work_sta bit 12 |
| **word 63 (steam boiler)** `0x36F6…0x3820` | s boiler < 50.0 °C | < 80.0 °C | < steam target − 10.0 °C | ≥ steam target − 10.0 °C (+ state test) — and always while work_sta bit 5 |

Steam target by steam gear (word 14): 1 → 120.0 °C, 2 → 125.0 °C, 3 → 130.0 °C, else (4) → 135.0 °C (`0x36F6…0x3714`).
Faults / heating off write 0. In every captured live session both words read 100 (machine hot).

### 1.6 Player internals (`libhvplayer.so`) — what reaches MI_AO

| Function | Address | Behaviour |
|---|---|---|
| `mm_audio_init` | `0x46B0` | `MI_AUDIO_Attr_t` on the stack: **48000 Hz** (`0xBB80`, always — ffmpeg resamples), bit width 0 (16 bit), work mode 0, sound mode mono 0 / stereo 1, **FrmNum 6, PtNumPerFrm 2048**, CodecChnCnt 0, ChnCnt 1 / 2 → `MI_AO_SetPubAttr(dev, &attr)`, `MI_AO_Enable(dev)`, `MI_AO_EnableChn(dev, 0)`, `MI_AO_SetVolume(dev, −10)` |
| `mm_audio_play` | `0x45A8` | `MI_AO_SendFrame(dev, 0, &frame, 128)` in chunks of ≤ 25 000 bytes; `frame.apVirAddr[0]` = PCM (`+8`), `u32Len` (`+0x54`); **retries while the return is `0xA005200D`** (channel buffer full) |
| `mm_audio_deinit` | `0x4674` | `MI_AO_DisableChn(dev, 0)`, `MI_AO_Disable(dev)` |
| `mm_player_set_volumn` | `0x563C` | `vol ≤ 0` → mute (and −60 dB); else **dB = min(vol × 90 / 100 − 60, 15)**; `MI_AO_SetVolume` + `MI_AO_SetMute` |

`libhvplayer.so` does **not** call `MI_SYS_Init` — the stock UI does, once at start. `libmi_ao.so` lists only `libc.so.6` as
DT_NEEDED but leaves its OS layer (`CamOsMemAlloc`, `CamOsRwsem*`, `CamOsGetTimeOfDay`, `CamOsPrintf`, …) undefined; in
the stock process those come from **`libmi_common.so`**.

---

## 2. Meraki+ v6.18

### 2.1 What you hear

| Event | When | Sound (`audio/`) |
|---|---|---|
| **start jingle** | once per **power-on**: OFF/sleep → on, and the first "on" after a cold boot of the display (SoC uptime < 120 s) | `start.wav` — rising bell arpeggio C5-E5-G5-C6, 1.1 s |
| **brew boiler ready** | word 62 first reaches stage 100 | `ready_extract.wav` — two warm low notes A4 → E5, 0.55 s |
| **steam boiler ready** | word 63 first reaches stage 100 | `ready_steam.wav` — three quick bright pips E6 F#6 B6, 0.5 s |
| *(test)* | after SAVE of SOUND, READY TONE or VOLUME | the brew boiler tone at the new volume |

With hot boilers at power-on you hear the jingle, then both ready tones (stock: jingle, then *preheatCompele* right
after the logo).

### 2.2 Rules (`src/merakiplus.c`, `snd_tick`)

* **Ready signal = the controller's own heat stage** (words 62/63 ≥ 100, the same signal the stock UI uses, but
  per boiler). Fallback if a word was never received: word 40 ≥ word 11 (extract) / word 47 ≥ steam target − 10 °C.
* **Once per power-on**, then **re-armed only after real cooling**: stage ≤ 30, or 15.0 °C below the ready point.
  A shot (the extract boiler dips a few °C) or a steam session (steam boiler dips below its threshold) never
  re-triggers a tone. Power-off / sleep re-arms both.
* **Start of Meraki+ on a machine that is already on** (switch from the stock UI, watchdog restart): no jingle, and a
  boiler that is already hot counts as announced (decided over the first 1.5 s of link, so stale first samples
  do not count). A boiler that is still heating plays its tone when it gets there.
* **Silent while OFF / sleeping / TURNING ON…** — nothing is queued for later. **OFFLINE** (no UART link) holds
  everything: a link glitch is not a power cycle (no second jingle).
* **During an activity** (grind, brew, water, steam, or a start pending): a ready tone that comes due is **dropped**
  and logged `suppressed`, as stock drops sounds while its animation plays. It is not played late.
* **SOUND** (SYSTEM › DISPLAY · SOUND): OFF / ON (Meraki+ tones) / STOCK SOUNDS (the display's own
  `/customer/audio/power.mp3` + `preheatCompele.mp3` for both boilers, if present; never shipped; else ours).
  **READY TONE**: OFF / BREW BOILER / STEAM BOILER / BOTH BOILERS (default). Both live in Meraki+'s own file
  `/new_customer/merakiplus/sound` (`sound=off|on|stock`, `ready=off|brew|steam|both`); config.json is not touched.
* **VOLUME** = config `volume` (shared with the stock UI; moved from STOCK UI to DISPLAY · SOUND), **capped at 70**
  like stock; **0 = mute** (events are still detected and logged `muted`).
* **No UART frame** is ever sent for sound; control logic, guards and frames are unchanged (the self-test checks the
  write queue and TX buffer across all sound events).

### 2.3 Playback: `merakiplus_snd`

Meraki+ is a static musl binary; the MI libraries are glibc shared objects. So one sound = one short-lived helper
process: `/new_customer/merakiplus/merakiplus_snd <file> <volume>` (source `src/merakiplus_snd.c`, 8 KB, linked
against glibc 2.28 armhf like the display, `-lpthread -ldl`; DT_NEEDED `libc.so.6 libpthread.so.0 libdl.so.2`). merakiplus starts it with `LD_LIBRARY_PATH=/lib:/customer/libs`,
plays one sound at a time (queue of 4) and never sends a signal (the helper ends itself after 8 s via `alarm()`; while one
is still running after 10 s, sounds are skipped as *no player*), and logs every event to
`/tmp/merakiplus_snd.log` (`<ms> <event> <outcome> vol=… mode=… ready=…`; the helper's own errors go there too).

* **WAV (ours):** `dlopen("libpthread.so.0")` (RTLD_GLOBAL, for libmi_common's `sem_*`/`pthread_*` — v6.18.1, §3),
  `dlopen("libmi_common.so")` (RTLD_GLOBAL, for `CamOs*`), `libmi_sys.so`, `libmi_ao.so`;
  `MI_SYS_Init()`; then **exactly libhvplayer's sequence and parameters** (1.6): 48 kHz (our 16 kHz files are upsampled
  ×3, linear), 16 bit, mono, FrmNum 6, PtNumPerFrm 2048; `SetPubAttr → Enable → EnableChn → SetVolume(dB) → SetMute →
  SendFrame…` in 4096-byte frames with the `0xA005200D` retry; drain; `DisableChn → Disable`.
* **MP3 (STOCK SOUNDS):** `libpthread.so.0` RTLD_GLOBAL first (as above), then `libhvplayer.so`'s `mm_player_*` with
  stock's options and polling (1.2).
* `merakiplus_snd --check` loads the libraries and resolves all symbols without playing anything; exit 0 and
  `merakiplus_snd: MI_SYS + MI_AO ok (sem_post global)` when everything is there (v6.18 printed no `(sem_post …)`).
* The outcome `played` in `/tmp/merakiplus_snd.log` means *handed to the helper*: merakiplus does not read the helper's
  exit code. A helper failure shows up as the helper's own `merakiplus_snd: …` line right after it (§3.1).
* Exit codes: 0 ok, 2 usage, 3 not a 16-bit PCM WAV, 4 library/symbol missing, 5 an MI_AO call failed, 6 mp3 player failed.

### 2.4 Our sounds

`tools/gen_audio.py` synthesizes all three with the Python standard library (a few sine harmonics with an exponential
decay per note; no samples). 16 kHz, mono, 16-bit PCM WAV, together ~68 KB. `./build.sh` (and so CI) runs
`tools/gen_audio.py --check`: the committed files must equal the generator's output (±1 LSB). `tests/hygiene.py`
md5-gates them against `AUDIO_MD5` in `tools/merakiplus.py` and fails on any other audio/video file in the repository.
They are original work of this project (MIT).

| File | md5 |
|---|---|
| `audio/start.wav` | `90efa9a33cc07d433adb0818e74b2c37` |
| `audio/ready_extract.wav` | `d001a4f478723ad32f6f2f709aa9aa6f` |
| `audio/ready_steam.wav` | `55d5039558b361e3747841fd47928b5d` |

### 2.5 Where to see it

* **DIAGNOSTICS › SOUND:** `SOUND · VOLUME` (*ON · 31*, *STOCK · 31*, *OFF*, amber *VOLUME 0* / *NO PLAYER*) and
  `LAST SOUND` (*STEAM · PLAYED*, *BREW · SKIPPED*, *JINGLE · MUTED*, …).
* **SIM state JSON** `audio`: `mode`, `ready_mask`, `vol`, `machine` (0 hold / 1 off / 2 on), `armed`, `ready`,
  `cold_boot`, `n_<event>` / `played_<event>` per event (`start_jingle`, `boiler_ready_extract`, `boiler_ready_steam`,
  `preview`), `n_suppressed`, `n_muted`, `n_off`, `log` (oldest first, `event:outcome,…`), `file`, `last`. The play viewer
  (`sim/play.py`) shows the log and has *Cold boilers / Cool brew boiler / Hot* buttons.
* **Simulator:** `mcu_sim.py` models words 62/63 as the MCU's stages once a `boilers` event switches the model on
  (`{"type":"boilers","e":…,"s":…,"e_rate":…,"s_rate":…}`); scenarios `audio_boiler_ready`, `audio_muted`,
  `audio_power_cycle`, `audio_sound_off` (see `sim/CONTRACT.md` ADDENDUM v6.18).

---

## 3. Hardware check and the v6.18.1 fix

### 3.1 What the machine showed (2026-11-21, v6.18 installed)

* `merakiplus_snd --check` → `merakiplus_snd: dlopen: /lib/libmi_common.so: undefined symbol: sem_post`,
  `MI_SYS + MI_AO MISSING`, **exit 4**. So the jingle, both ready tones and the SAVE preview were all **silent**, although
  `/tmp/merakiplus_snd.log` listed them as `played` (= handed to the helper, §2.3) — each followed by that dlopen line.
* The same helper with `LD_PRELOAD=/lib/libpthread.so.0`: `--check` ok (exit 0), and playing `audio/*.wav` directly
  exits 0 and **the tone is heard from the speaker**. `libmi_ao.so` prints four lines
  `_MI_AO_Open{Vqe,Src,G711,G726}Lib: Can not load lib….so!` — optional VQE / resampler / codec libraries the display
  does not have and plain 48 kHz PCM does not need; they are harmless.
* **Speaker and amplifier work with what `demo.sh` already sets at boot** — no extra enable GPIO is needed.
* During the check Meraki+ was the only UI process (slot B, no paused stock UI), so MI_AO was free. Whether MI_AO also
  plays while a **SIGSTOPped stock UI** still holds the MI layer (runtime switch A→B, [boot.md](boot.md) §3.2) was not
  tested and stays open.

### 3.2 Root cause

The display's `/lib/libmi_common.so` lists only `libc.so.6` as DT_NEEDED, but uses `sem_init`, `sem_post`, `sem_wait`,
`sem_timedwait`, `sem_trywait`, `sem_getvalue`, `sem_destroy`, `pthread_create`, `pthread_join`, `pthread_once`,
`pthread_key_create`, `pthread_{get,set}specific`, `pthread_rwlock_*`, `pthread_spin_*`, `pthread_mutex_trylock`,
`pthread_condattr_setclock`, `pthread_attr_setstacksize`, `pthread_{set,get}name_np` — which glibc 2.28 defines **only
in `libpthread.so.0`** (`libc.so.6` forwards just a few mutex/cond/attr calls). The stock `demo_v9` links libpthread,
so in the stock UI they are in the global scope already. The v6.18 helper was linked `-ldl` only, so
`dlopen("libmi_common.so", RTLD_NOW)` failed on the first of them.

The v6.18 container test did not catch it because it had no copy of the real `libmi_common.so`: it used a stand-in that
only exported the `CamOs*` functions, so the real library's own dependencies were never exercised. The v6.18.1 test uses
byte-exact copies of the display's `/lib` (§3.4).

### 3.3 Fix (`src/merakiplus_snd.c`, v6.18.1)

* `pthread_global()`: `dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL)` **before** `libmi_common.so`, in the WAV
  path (`mi_load`) and the MP3 path (`mp3_play`; `libhvplayer.so` NEEDs libmi_common too). Works whatever the link flags.
* Belt and braces: linked `-lpthread` **with one reference to `sem_post`**, so `libpthread.so.0` really is a DT_NEEDED
  (a linker drops a library none of whose symbols is used — plain `-lpthread` alone left the helper unchanged) and
  ld.so maps it at start into the global scope, like `demo_v9`.
* `--check` also prints whether `sem_post` is in the global scope: `MI_SYS + MI_AO ok (sem_post global)`.
* Unchanged: the MI_AO call sequence and parameters (48 kHz, 16 bit, 6 × 2048, retry on `0xA005200D`), the volume →
  dB curve, `alarm(8)` (the helper still ends itself; merakiplus sends no signal), the exit codes, `--check` exit 0 when
  everything resolves. merakiplus itself only changed its version string.
* `tests/hygiene.py` fails if `bin/merakiplus_snd` lacks the `libpthread.so.0` DT_NEEDED or the source stops loading
  libpthread RTLD_GLOBAL before `libmi_common.so`.

### 3.4 Resolve test against the display's own libraries

32-bit ARM container (`arm32v7/debian:bullseye-slim`, `--network none`, non-root), with read-only copies of the
display's `ld-2.28.so`, `libc`, `libdl`, `libpthread`, `librt`, `libgcc_s`, `libmi_common.so`, `libmi_sys.so`,
`libmi_ao.so` (md5 checked against the display, kept outside the repository) and the display's own loader:
`ld-linux-armhf.so.3 --library-path <copies> ./merakiplus_snd --check` — no LD_PRELOAD, no container libraries used.

| helper | DT_NEEDED | `--check` |
|---|---|---|
| v6.18 `161edd2c…` | libc, libdl | `undefined symbol: sem_post`, exit **4** (as on the machine) |
| v6.18 + `LD_PRELOAD=libpthread.so.0` | libc, libdl | ok, exit 0 (as on the machine) |
| **v6.18.1** `a7659c17…` | libc, **libpthread**, libdl | `MI_SYS + MI_AO ok (sem_post global)`, exit **0** |
| test variant: dlopen only (no sem_post reference) | libc, libdl | ok (sem_post global), exit 0 |
| test variant: link only (no dlopen of libpthread) | libc, libpthread, libdl | ok (sem_post global), exit 0 |

Either half of the fix suffices alone. `--check` resolves `MI_SYS_Init` and the eight `MI_AO_*` functions with
`RTLD_NOW`, i.e. every relocation of the three MI libraries is bound. (Playing in the container ends at once in a
`libmi_ao.so` assertion — there is no `/dev/mi_ao` — so the speaker itself can only be checked on a machine.)

### 3.5 Check after installing v6.18.1

1. `telnet <display-ip>` → `/new_customer/merakiplus/merakiplus_snd --check` → `MI_SYS + MI_AO ok (sem_post global)`,
   exit 0 — **without** LD_PRELOAD.
2. `/new_customer/merakiplus/merakiplus_snd /new_customer/merakiplus/audio/ready_steam.wav 40` → three pips
   from the speaker, exit 0. If it exits 5, see `/tmp/merakiplus_snd.log` (an MI_AO return code).
3. Power the machine off and on with the button: the jingle, then the two ready tones as the boilers heat
   (`cat /tmp/merakiplus_snd.log`: no `merakiplus_snd: dlopen` lines; DIAGNOSTICS › SOUND shows the last one).
4. Change VOLUME in SYSTEM › DISPLAY · SOUND: SAVE plays the test tone at the new level.
