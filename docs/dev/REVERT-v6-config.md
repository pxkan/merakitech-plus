# REVERT — Meraki+ / ourui (phase 2 soft replace)

> **Note (public edition):** Development notes written during development. Paths such as
> `artifacts/…`, `boot_install.py` steps and md5 tables refer to the author's working environment; as
> an end user, use `./install.sh` and `./revert.sh` (see the [README](../../README.md)). The
> manufacturer's files are not included; IP addresses and other personal data have been removed.
> References to `BOOT.md` are to [docs/boot.md](../boot.md); `WRITETEST.md`, `INSTALL-REPORT.md` and
> `GROUND-TRUTH.md` are the author's working notes.

> **Name (v6.3):** the UI is called **Meraki+** on the screen. `ourui` is still the code name — the
> binary, the process, the directory `/new_customer/ourui/` and all commands below are unchanged. UI
> texts are quoted in English (a fresh installation starts in English); the Danish wording is in
> UI_v6.md, Appendix A. v6.3's only new file is the language file — see
> [that section](#v63-the-language-file-lang--whats-new-and-how-to-undo-it).

Normal revert (**v6.8, unchanged in v6.9 and v6.10**): SYSTEM → **SWITCH TO ORIGINAL NOW** → **SWITCH
NOW** on the Meraki+ screen. In run.sh mode it is a clean exit (after the exit reminder, if the config
was saved) → `run.sh`'s trap SIGCONTs demo_v9 → the original UI is back in < 1 s. No reboot. In a slot
B boot (no paused stock UI) the same button starts the stock UI. **The REVERT button on HOME no longer
exists from v6.8** (it was a phase 2 safeguard; see [the v6.8 section](#v68-the-revert-button-on-home-has-been-removed)).
Up to and including v6.7: tap **REVERT** on HOME.

Manual revert (telnet root@<display-ip>):
```sh
kill -CONT $(pidof demo_v9)     # the original UI may run again
kill $(pidof ourui) 2>/dev/null # if ourui hangs (the run.sh trap does this too)
```
If run.sh itself was killed without the trap: `kill -CONT $(pidof demo_v9)` alone is enough.

Guarantees (v4, v6, v6.1, v6.2, v6.3, v6.4, v6.5, v6.6, v6.7, v6.8, v6.9 and v6.10 — v6.9 only changes
what is drawn ([section](#v69-calm-graph-and-stable-weight--nothing-new-on-the-device-beyond-the-display)),
and v6.10 only how the pressure is shown ([section](#v610-pressure-as-a-bar-estimate--display-only));
the v6.2 additions and v6.3's language file are in their own sections; v6.4 changes nothing on the
device; v6.5's boot push is in [its section](#v65-boot-push-at-startup--whats-new-and-how-to-undo-it);
v6.6's re-push in [its section](#v66-re-push-after-an-mcu-restart-64-bit-clock-steam-display--whats-new-and-how-to-undo-it);
v6.7's network guard and v6.8's removed REVERT button in the last two sections):
* The demo_v9 binary, libs, pinit and test.sh are NOT changed.
* ourui lives in /new_customer/ourui (a separate directory). With the boot integration
  ([boot.md](../boot.md)) the same binary is also placed as /new_customer/demo_v9 — put there by
  `boot_install.py` (now `tools/merakiplus.py`), not by ourui.
* The MCU only receives the normal protocol (cmd3 poll / cmd6 write / cmd9 reply); no OTA.
* **v6.1:** every cmd6 frame passes a frame policy (UI_v6.md §7a). ourui **cannot** send words 26/27
  ≠ 0 (weight calibration, writes MCU flash), word 30 ≠ 0 (MCU reboot), drain (28/29), back-flush
  (20), self-test (8), sleep/drain (word1 = 1) or power-off (word0 = 0). There is therefore no MCU
  flash state to revert after ourui. *(v6.14 adds power-off via the physical power button only,
  through the new intent `I_POWEROFF` — see [ui-guide.md §19](../ui-guide.md#19-the-power-button-v614).)*
* The watchdog (test.sh) only checks that the demo_v9 PID exists; SIGSTOP = the PID exists → no
  restart.
* ourui never kills demo_v9 — and from v6.2 it is broader: ourui does not send a signal to any process
  at all (0 `kill()` calls in the source). **Still true in v6.7:** the network guard never kills a
  wpa_supplicant (not even a hanging init script).
* **v6.7:** when the Wi-Fi stack is DOWN (no wpa_supplicant), ourui starts it the way the stock UI
  does: `/config/wifi/ssw01bInit.sh` (only if `wlan0` is missing),
  `/config/wifi/wpa_supplicant -i wlan0 -Dnl80211 -c /appconfigs/wpa_supplicant.conf` and
  `udhcpc -i wlan0 -s /etc/init.d/udhcpc.script -a -q -b -t 20 -T 1`. The only file that can be
  written is `/appconfigs/wpa_supplicant.conf` — only on a bring-up and only if the contents differ
  from what config.json gives (the stock UI writes the same file in the same format). Log:
  `/tmp/ourui_net.log`. A healthy stack is not touched. `OURUI_NONET=1` disables it.
* **v6.2:** ourui can create/remove the boot flag `/customer/new` and can start `/customer/demo_v9`.
  Both only happen from two rows at the bottom of SYSTEM, each with confirmation.
* **v6.3:** the only new write is the language file `/new_customer/ourui/lang` (2 bytes), and only on
  SAVE of UI LANGUAGE. The stock UI's `config.json` (including `language`) is not touched by it, and
  no frame is sent to the MCU.
* **v6.5:** at startup ourui sends the stock UI's two boot push blocks (`cmd6 sub1 len30` +
  `sub112 len3`) with the values from `config.json`. These are writes to the controller's **RAM**
  through the same frame policy as everything else; no file is written, and there is nothing to revert
  afterwards.

Remove the installation completely:
```sh
rm -rf /new_customer/ourui      # NOTE v6: keep cfgbak/ first if the config was changed (see below); also removes the language file lang (v6.3)
```

---

## v6: persistent writes — what's new

**v4 never wrote to disk. v6 does, when the user taps SAVE in the settings.** These are the only
persistent changes, and they only hit these paths:

| Path | What | When |
|---|---|---|
| `/customer/config.json` | one numeric field replaced (atomic tmp+rename) | every SAVE |
| `/customer/.ourui_cfg.tmp` | temporary; only exists for milliseconds | during SAVE |
| `/new_customer/ourui/cfgbak/config.json.orig` | **pristine copy** from before ourui's very first write; never overwritten | first SAVE |
| `/new_customer/ourui/cfgbak/config.json.bak` | the version immediately before the latest write | every SAVE |
| `/new_customer/ourui/cfgbak/config_writes.log` | one line per write: time, field, old→new, md5 before/after | every SAVE |
| `/new_customer/ourui/cfgbak/RESTART_NEEDED` | flag: the stock UI has not yet read the new config | every SAVE |

In addition, v6 changes state that is **not** on disk:
* **MCU registers** (words 2, 4, 11, 14, 17, 23, 25, 112–114) on SAVE — take effect immediately in the
  controller. They are RAM in the MCU and are restored from `config.json` every time demo_v9 starts:
  `fuart_thread` boot-pushes `cmd6 sub1 len30` + `cmd6 sub112 len3` (PARITY.md §1.4). After a demo_v9
  restart the controller therefore equals the config — regardless of what ourui has written in the
  meantime.
* **Backlight PWM** (`/sys/class/pwm/pwmchip0/pwm2/duty_cycle`) while editing BRIGHTNESS. CANCEL and
  exit restore the saved value.
* **Input grab** (`EVIOCGRAB` on `/dev/input/event0`). Released automatically when the ourui process
  dies — also on `kill -9`.

### See what ourui has changed
```sh
cat /new_customer/ourui/cfgbak/config_writes.log
md5sum /customer/config.json /new_customer/ourui/cfgbak/config.json.orig
```
Identical md5 = the config is back at the starting point (known baseline: `<config-md5-redacted>`).

### Restore the config (full revert of all settings changes)
Run while ourui is **not** running. The same tmp+mv pattern as WRITETEST.md §7 — never in place:
```sh
cd /new_customer/ourui/cfgbak
md5sum config.json.orig                       # confirm it is the expected baseline
cp config.json.orig /customer/.restore.tmp
md5sum /customer/.restore.tmp                 # must match the line above
mv /customer/.restore.tmp /customer/config.json
kill $(pidof demo_v9)                         # the watchdog restarts it; reads the config + boot-pushes to the MCU
sleep 5; pidof demo_v9; md5sum /customer/config.json
rm -f RESTART_NEEDED
```
Only one step back: use `config.json.bak` instead of `config.json.orig`.

If `cfgbak/` does not exist, v6 has never written the config. If the directory is gone even though
the config was changed: use the local copy `artifacts/write_test/config.json.baseline` (md5
`<config-md5>…`), pushed as in WRITETEST.md §3.

### After a SAVE: restart the stock UI before it is used (not a revert)
demo_v9 reads `config.json` **only at startup**. After REVERT (SIGCONT) it keeps running on its old RAM
mirror, and that has three consequences until it is restarted:
1. The stock UI **shows** the old values.
2. Its next block write (e.g. a brew start, which sends along the brew temp, steam level and water
   weight) sends the **old** values to the controller and overwrites the ones ourui set.
3. If you change a setting in the stock UI, it writes its whole old config tree — ourui's changes in
   the file can be lost, or the stock UI's own can (its write fd points at the old inode).

Therefore, when `cfgbak/RESTART_NEEDED` exists: from v6.2 ourui shows the reminder **on the screen at
REVERT** (*"STOCK UI NEEDS A RESTART OF demo_v9 …"* + the command) and on stdout on exit — but only
when it is true: a stock UI is running that was started before the latest SAVE in the same boot. After
a machine restart, after a demo_v9 restart and in a slot B boot (where no stock UI is paused) it is not
shown, even if the file is there:
```sh
kill $(pidof demo_v9) && rm -f /new_customer/ourui/cfgbak/RESTART_NEEDED
sleep 5; pidof demo_v9          # the watchdog has started it again
```
At startup demo_v9 reads the new config and boot-pushes it to the controller, so the config, the stock
UI and the controller are in sync. Note the OTA caveat in WRITETEST.md §7: a restart can trigger a
vendor OTA if a new version has been released.

This also applies to **STOCK LANG.** (editable from v6.1; called LANGUAGE before v6.3): the field only
affects the stock UI, and only after a restart. If the stock UI is set to a language you cannot read,
set STOCK LANG. back to ENGLISH in ourui (the SYSTEM page) or restore the config as above, and restart
demo_v9. Meraki+'s own language is a different setting (UI LANGUAGE, v6.3) and is not in the config —
see the v6.3 section.

### If a SAVE was interrupted (power/crash in the middle)
* `rename()` is atomic: `config.json` is either the old or the new one, never half.
* A leftover `/customer/.ourui_cfg.tmp` is harmless: `rm -f /customer/.ourui_cfg.tmp`.
* The MCU may have received the new value while the config has the old one. Resolved by a demo_v9
  restart (the boot push sets the controller equal to the config) or by saving the parameter again in
  ourui.

### Back to v4 (monitor-only, no writes)
Depends on how the integration engineer installs v6; the recommended layout is v6 as `ourui` and v4
kept as `ourui_v4.bin`:
```sh
cd /new_customer/ourui && cp ourui_v4.bin ourui
```
Note that v4 has the keep-awake bug (writes word1=1 = sleep/drain).

### Back to v6.0 (before the spec alignment)
Not recommended: v6.0's limits were assumptions (e.g. brew temp 80–98 °C and group temp up to 90 °C
vs. the spec's 85–96 / 30–80), `timemode` was inverted, and there was no frame policy. The v6.0 binary
had md5 `90bcf094253e69f0559051339d2032f5`; v6.1 has `4d927802cbeb69cc949f463021b29b99`. A config
saved by v6.0 with an out-of-spec value is shown in red in v6.1 and blocks activity starts until the
value is fixed (or the config is restored from `config.json.orig`).

---

## v6.2: the BOOT UI flag and runtime switching — what's new, and how to undo it

v6.2 can do two new things from the SYSTEM page (UI_v6.md, CHANGES-v6.2; requirements from
[boot.md](../boot.md) §9). The boot installation itself (the switcher daemon, the guard line in
`demo.sh`, ourui as `/new_customer/demo_v9`) belongs to the boot work stream and is undone according
to **boot.md §5** — not here.

### What v6.2 touches

| Path / state | What | When |
|---|---|---|
| `/customer/new` | the vendor's boot flag: exists = boot slot B, does not exist = boot slot A. Created (empty file) or removed, followed by `sync()` | **only** on SAVE of BOOT UI — and only when `/new_customer` is mounted and its `demo_v9` is ourui |
| `/new_customer/ourui/cfgbak/boot_ui.log` | one line per flag change and per runtime switch (time, what, reason) | on the two actions |
| `/new_customer/ourui/cfgbak/RESTART_NEEDED` | has gained a last line `boot_id=… mono_s=…` (used to decide whether the reminder still applies); removed by ourui after a successful runtime switch | every SAVE / on a switch |
| a new process `/customer/demo_v9` | started by ourui with `fork` + `setsid` + `execl`, cwd `/customer`, stdio `/dev/null` | **only** on SWITCH TO ORIGINAL NOW, when there is not already a stock UI |

v6.2 writes **nothing** in `/tmp`, touches neither `demo.sh`, `test.sh`, the `demo_v9` binaries, pinit
nor libs, and **does not send a signal to any process** (the source has 0 `kill()` calls;
`round_v6/boot_check.txt`). No helper process is started: what ourui forks *is* the stock UI from the
first instruction after `execl`. There is therefore **no helper to kill**.

### See the state
```sh
ls -l /customer/new 2>/dev/null || echo "no flag = slot A (original) at next boot"
for p in $(pidof demo_v9); do echo "$p $(readlink /proc/$p/exe)"; done   # /customer/… = stock, /new_customer/… = ourui
pidof switcher; tail -5 /tmp/switcher.log
cat /new_customer/ourui/cfgbak/boot_ui.log
```
The same is shown on ourui's DIAGNOSTICS page (scroll down to *BOOT · SLOT · SWITCHER*).

### Undo the BOOT UI choice (only takes effect at the next restart)
In ourui: SYSTEM → BOOT UI → choose the other one → SAVE. Or from telnet — this is exactly what ourui
does:
```sh
rm -f /customer/new && sync      # original (slot A) at next boot
touch /customer/new && sync      # ourui (slot B) at next boot — ONLY if /new_customer/demo_v9 is ourui
```
The flag is the entire persistence; there is nothing else to clean up. Note boot.md §7: a vendor OTA
sets or removes the flag by itself and can put its own binary in slot B. ourui detects this (BOOT UI
shows *"LOCKED · SLOT B IS NOT MERAKI+"*) and then does not touch the flag.

### Undo a runtime switch (ourui → stock, without a restart)
The switch changes no files. Back to ourui:
* **Gesture:** see [gesture.md](../gesture.md) (from v6.12: hold one finger in the center for 5 s;
  before: 3 long presses (≥ 1.5 s) in the top-left corner within 6 s) — requires the switcher daemon
  to be running (boot.md §2).
* **Restart:** the machine follows the flag.
* **Telnet, booted in slot B:** `kill $(pidof demo_v9)` — the watchdog is `test.sh new_customer` and
  starts ourui again within about 1 s.
* **Telnet, booted in slot A:** that is the stock → ourui direction; see below.

### If a switch goes wrong
ourui only exits if the stock process is still alive 400 ms after the start. If it dies before then,
ourui takes the screen back and shows *"SWITCH FAILED"*. If it dies later, the watchdog starts the boot
slot's binary (ourui on a B boot). Should something still be wrong:
```sh
for p in $(pidof demo_v9); do echo "$p $(readlink /proc/$p/exe)"; done
# two UIs at once (must not happen): end the one that should not run — ourui exits cleanly on SIGTERM
kill <pid of /new_customer/demo_v9>
# no UI and no watchdog restart after a few seconds: start the stock UI by hand (like demo.sh's A branch)
cd /customer && export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/customer/libs:/config/wifi && ./demo_v9 &
```
Touch does not work in the stock UI after a switch: a process is still holding `EVIOCGRAB`.
`ls -l /proc/*/fd 2>/dev/null | grep event0` shows who; v6.2 closes all of its fds before `execl`, so
it should only be possible for it to be a hanging ourui.

### The reverse path for testing (stock → ourui without a restart) — not part of ourui
The switcher daemon does this on the gesture (boot.md §3.2), and `boot_install.py boot-test` step c
tests it with `tinj_hold`. By hand it is the same pattern, **stop-first** so the watchdog never sees a
gap:
```sh
A=$(pidof demo_v9)                      # must be exactly one pid, and its exe must be /customer/demo_v9
kill -STOP $A                           # the watchdog still sees the pid
cd /new_customer && LD_LIBRARY_PATH=/new_customer/libs:/config/wifi ./demo_v9 >>/tmp/ourui_boot.log 2>&1 &
B=$!; sleep 1; readlink /proc/$B/exe    # must show /new_customer/demo_v9
kill -9 $A                              # only now; if B did not come up:  kill -CONT $A
```
Do not run `run.sh` in a slot B boot: it assumes a stock UI to pause (boot.md §9.3).

### Remove everything v6.2 has put down
```sh
rm -f /customer/new && sync                         # only if the flag was set by us (see boot_ui.log)
rm -f /new_customer/ourui/cfgbak/boot_ui.log
```
Full rollback of the boot integration (guard line, switcher, vendor binary back in slot B): boot.md §5.

### Back to v6.1
`artifacts/ourui/ourui_v6_1.bin` (md5 `4d927802cbeb69cc949f463021b29b99`, source `ourui_v6_1.c`) is
v6.1 unchanged. `boot_install.py install` uploads `artifacts/ourui/ourui_v6` and reads the md5 at run
time, so a rollback is `cp ourui_v6_1.bin ourui_v6` locally and then `install`. Note what you lose in a
slot B boot: v6.1 has no SWITCH TO ORIGINAL NOW, and its REVERT just exits — after which the watchdog
starts it again. The v6.2 binary has md5 `0bb93da0ed0824c424f1e16530f07022`.

---

## v6.3: the language file `lang` — what's new, and how to undo it

v6.3 gives the UI a product name (Meraki+) and two languages (UI_v6.md, CHANGES-v6.3). The only new
thing on disk is one small file.

### What v6.3 touches

| Path | What | When |
|---|---|---|
| `/new_customer/ourui/lang` | the UI language as plain text: exactly `en` or `da` (2 bytes, no newline) | **only** on SAVE of SYSTEM → UI LANGUAGE |
| `/new_customer/ourui/lang.tmp` | temporary; only exists for milliseconds (tmp → fsync → reread → `rename()`) | during the same SAVE |

v6.3 does **not** write to `/customer/config.json` for the language (`language` there is the stock UI's
own field and is independent), sends no cmd6 frame, makes no `cfgbak/` backup and no
`RESTART_NEEDED`. The file is read once at startup. If it is missing, cannot be read, or contains
anything other than `en`/`da`, the UI starts in **English**.

### See and change the language from telnet
```sh
cat /new_customer/ourui/lang 2>/dev/null || echo "(no file = English)"
echo da > /new_customer/ourui/lang     # Danish from the next start (the newline is tolerated)
echo en > /new_customer/ourui/lang     # English from the next start
```
A running ourui does not read the file again; a change in the UI (SYSTEM → UI LANGUAGE → −/+ → SAVE)
takes effect immediately without a restart. DIAGNOSTICS at the bottom (*ABOUT*) shows the active
language and its source: *FROM FILE*, *DEFAULT (NO FILE)*, *UNKNOWN FILE VALUE* or *NOT SAVED*.

### Undo (back to the default)
```sh
rm -f /new_customer/ourui/lang /new_customer/ourui/lang.tmp     # English at the next start
```
There is nothing else to clean up. A leftover `lang.tmp` (power lost in the middle of a SAVE) is
harmless; `lang` is either the old or the new one, never half.

### If the UI is set to a language you cannot read
UI LANGUAGE is the **top row on SYSTEM** (page 4 of 5: swipe left three times from HOME), and the two
choices are always called *ENGLISH* and *DANSK*. Or from telnet: `rm -f /new_customer/ourui/lang` and
start ourui again.

### Environment variable
`OURUI_LANGFILE=<file>` points the language file somewhere else (test / simulator). The default is the
path above.

### Back to v6.2
`artifacts/ourui/ourui_v6_2.bin` (md5 `0bb93da0ed0824c424f1e16530f07022`, source `ourui_v6_2.c`) is
v6.2 unchanged. Rollback as for v6.1: `cp ourui_v6_2.bin ourui_v6` locally and then
`boot_install.py install` (it reads the md5 at run time). v6.2 does not know the language file and
leaves it alone; it is Danish-only and shows the name OURUI. The v6.3 binary has md5
`5815176d0e766c7a6532b88014e932fb`.

## v6.4: the SIM build — nothing new on the device

v6.4 makes the UI runnable without hardware against the test work stream's simulated MCU (UI_v6.md §13
+ CHANGES-v6.4). **The device binary does the same as v6.3.** The source text after the preprocessor is
token for token the same except for two things, both text only:

* the version string `V6.4` (splash, DIAGNOSTICS, the binary tag `@(#)ourui V6.4 …` — the BOOT UI
  guard still recognizes all versions by the first 10 characters);
* the toast on SAVE without an ACK: *SAVE FAILED · NO ACK · NOT SAVED* (before: *NO ACK · NOT SAVED*).

What this means for revert:

* **No new files, paths, writes or environment variables on the device.** Everything in the sections
  above applies unchanged. The `OURUI_SIM_*` variables only exist in the SIM build; the device binary
  does not contain them (`strings ourui_v6 | grep -c OURUI_SIM` = 0).
* **The boot push** (`cmd6 sub1 len30` + `sub112 len3`), which the SIM build sends because the test
  contract requires it, is **not** in the device binary. *(It was added to the device binary in v6.5.)*
  After an ourui SAVE the rule therefore still applies: restart the stock UI before it is used (the
  section *After a SAVE*).
* The SIM binaries `ourui_v6_sim` (host) and `ourui_v6_sim_arm` (static ARM) must **not** be installed
  on the device. `ourui_v6_sim_arm` would exit with code 2 because `OURUI_SIM_ROOT` is missing — it
  never opens `/dev/fb0`, `/dev/ttyS1` or `/dev/input/event0`.
* The E2E runs only write in `sim/reports/…` (sandboxes) and a short `/tmp` directory for sockets.
  `artifacts/config.json` is untouched (md5 `<config-md5-redacted>`).

### Back to v6.3
`artifacts/ourui/ourui_v6_3.bin` (md5 `5815176d0e766c7a6532b88014e932fb`, source `ourui_v6_3.c`, md5
`4ac62cd08de36f7fea026eec5a3494ab`) is v6.3 unchanged. Rollback as for the earlier versions:
`cp ourui_v6_3.bin ourui_v6` locally and then `boot_install.py install`. The v6.4 binary has md5
`1fe0b5b8c3b24abf0cadcde7b099076a`. The language file and `cfgbak/` are the same in v6.3 and v6.4.

## v6.5: boot push at startup — what's new, and how to undo it

v6.5 changes two things in the device binary (UI_v6.md CHANGES-v6.5):

1. **Boot push.** When ourui starts — in a slot B boot and in run.sh mode — it sends, after PowerOn
   and keep-awake, the two blocks the stock UI sends at its own startup: `cmd6 sub1 len30` (words
   1..30) and `cmd6 sub112 len3` (words 112..114), with the values from `/customer/config.json`. If an
   activity is already running when ourui starts, it is skipped.
2. **RX resync watchdog.** Pure receive logic; sends nothing and writes nothing.

### What the boot push touches

| | |
|---|---|
| Files | **none** — neither `config.json`, `cfgbak/`, the flag nor the language file |
| The controller | the register file in **RAM** (words 1..30 and 112..114). It is reset anyway on every power cycle, and the stock UI overwrites it with its own boot push every time it starts |
| MCU flash | nothing. Words 26/27 (calibration) and 30 (reboot) are 0 in the block and cannot be anything else (the frame policy) |
| The stock UI's RAM mirror | not touched. The rule *After a SAVE: restart the stock UI before it is used* applies unchanged |

There is therefore **no state to revert** after a boot push. If you want the controller back on what
the stock UI itself would set: restart the stock UI (`kill $(pidof demo_v9)` — the watchdog starts it,
and its boot push writes the same words). With an unchanged config these are the same values.

Two things the boot push does in run.sh mode that ourui v6.4 did not:

* Recipe words the stock UI may have set from an NFC card or a preset (words 13, 15, 18) go back to
  the boot values (60 / 60 / 0) — as on a restart of the stock UI.
* The low byte of word 113 gets `breakTime` again; the controller overwrites it itself at the next
  brew.

### See what it did
DIAGNOSTICS, scrolled down one row: **BOOT PUSH 1-30** and **BOOT PUSH 112-114** show OK · NO ACK ·
REJECTED (config out of spec) · NO CONFIG · SKIPPED (an activity was running at startup) · OFF.
`CMD6 OK` is 4 right after a normal start (PowerOn, awake, two blocks). The boot push writes no log
file.

### Disable it without switching binaries
```sh
OURUI_NOBOOTPUSH=1 ./run.sh          # the v6.4 behavior: no boot push; DIAGNOSTICS shows OFF
OURUI_READONLY=1 ./run.sh            # as before: no cmd6 at all (not even keep-awake)
```
In a slot B boot `demo.sh` starts the binary without environment variables; there the boot push is
disabled by installing the v6.4 binary (below) — or by booting the stock UI (`rm /customer/new`).

### Back to v6.4
`artifacts/ourui/ourui_v6_4.bin` (md5 `1fe0b5b8c3b24abf0cadcde7b099076a`, source `ourui_v6_4.c`, md5
`5bdaf341f77dcec3554707cf6cd9df70`) is v6.4 unchanged. Rollback as for the earlier versions:
`cp ourui_v6_4.bin ourui_v6` locally and then `boot_install.py install` (it reads the md5 at run
time). The v6.5 binary has md5 `e84785d514c11ef88705ac97240b98c1`. The language file, `cfgbak/` and
the boot flag are the same in v6.4 and v6.5; v6.4 does not know `OURUI_NOBOOTPUSH` and ignores it.

## v6.6: re-push after an MCU restart, 64-bit clock, steam display — what's new, and how to undo it

v6.6 changes three things in the device binary (UI_v6.md CHANGES-v6.6). Only the first one sends
anything v6.5 did not send:

1. **Re-push.** If the controller restarts while ourui is running, the boot push is sent again: the
   **same two blocks** (`cmd6 sub1 len30` + `cmd6 sub112 len3`), the same values from `config.json`,
   through the same frame policy, write queue and activity guard. It is triggered by the controller's
   version request (`0x89`) arriving again after startup, or by words 112 and 113 both being 0 (the
   MCU's start values) in three reads in a row on an idle machine. At least 5 s between two; at most 3
   signature-triggered ones per 10 min.
2. **64-bit clock.** No change in what is sent or when — the clock simply no longer wraps around after
   24.86 days. **v6.5 and older must not be used as a permanent slot B boot:** at 24.86 days of uptime
   they send a stream of keep-awake writes for 30 s, and the poll or the screen can then stall until
   the process is restarted.
3. **Steam display.** Screen only: *STEAM·AUTO* / *STEAM·WAND*, a steam badge during a brew, the STOP
   button only for latched steam. The start and stop frames are the same as in v6.5.

### What v6.6 touches

| | |
|---|---|
| Files | **no new ones, no changed formats** — `config.json`, `cfgbak/`, `RESTART_NEEDED`, the flag and the language file are as in v6.5 |
| The controller | like the boot push: the register file in RAM (words 1..30 and 112..114), no flash, words 26/27/30 = 0 |
| Environment variables | no new ones on the device. `OURUI_NOBOOTPUSH=1` now also disables the re-push; `OURUI_READONLY=1` as before |

There is **no state to revert** after a re-push: it writes the values the controller already had
before its restart.

### See what it did
DIAGNOSTICS, scrolled down one row. Without a re-push the row is called *BOOT PUSH 1-30* /
*BOOT PUSH 112-114*, as in v6.5. After a re-push it is called **RE-PUSH n · 1-30** / **112-114**,
where n is the number since ourui started; the cells show the outcome of the latest push. `CMD6 OK`
has increased by 2 per re-push. No log file is written.

### Disable it without switching binaries
```sh
OURUI_NOBOOTPUSH=1 ./run.sh          # no boot push and no re-push; DIAGNOSTICS shows OFF
```
In a slot B boot `demo.sh` starts the binary without environment variables; there the way is the v6.5
binary (below) — with the caveat about the clock.

### Back to v6.5
`artifacts/ourui/ourui_v6_5.bin` (md5 `e84785d514c11ef88705ac97240b98c1`, source `ourui_v6_5.c`, md5
`f6e502b3d4cb7d212d1a1786520b2de4`) is v6.5 unchanged. Rollback as for the earlier versions:
`cp ourui_v6_5.bin ourui_v6` locally and then `boot_install.py install` (it reads the md5 at run
time). The v6.6 binary has md5 `c22153af54069d9d1fcbe43fcb118c99`. The language file, `cfgbak/`,
`RESTART_NEEDED` and the boot flag are the same in v6.5 and v6.6, so there is nothing to clean up when
switching in either direction. The BOOT UI guard recognizes both binaries as ourui
(`round_v6/boot_check.txt`).

---

## v6.7: Wi-Fi at startup and self-healing (network guard) — what's new, and how to undo it

**Why:** on a slot B boot the stock UI never starts, and it is the stock UI (libmi_wlan, controlled by
`/config/wifi/wlan.json`) — not `demo.sh` — that brings Wi-Fi up. A B-booted machine was therefore
without a network (INSTALL-REPORT.md §3.1): no telnet, no remote revert. v6.7 does the same as the
stock UI, but **only when the stack is down**.

### What v6.7 touches

| | When | What |
|---|---|---|
| Processes | only when no wpa_supplicant is running (at startup: immediately; otherwise after 3 s) | `sh -c "source /config/wifi/ssw01bInit.sh dummy1 dummy2 "` if `wlan0` is missing (insmod of the driver, `mkdir /tmp/wifi/run`) · `wpa_supplicant` with the stock UI's arguments · 1 s later `udhcpc` with the stock UI's arguments — unless `wlan0` still has its IP and default route (then they are kept). The children get PPid 1 and none of ourui's fds |
| | wpa_supplicant is alive, but `wlan0` has been administratively down for 3 s | `ifconfig wlan0 up` (ioctl, like the stock UI's MI_WLAN_Open) |
| | wpa_supplicant is alive, no IP (8 s) or no default route despite carrier (3 s), no udhcpc | `udhcpc` with the stock UI's arguments (backoff up to 60 s on repeats) |
| Files | on bring-up, only if the contents differ | `/appconfigs/wpa_supplicant.conf` (tmp + fsync + rename), byte for byte in wpa_supplicant's `save_config` format; ssid/password from `config.json` (read, never written). `demo.sh` copies `.conf_bak` over the file on **every** boot, and the stock UI's `save_config` writes it again, so there is nothing lasting to undo |
| | always (tmpfs) | `/tmp/ourui_net.log` (transitions only; the password is never written; rotated at 128 kB), `/tmp/wifi/run/`, a dead `/tmp/wifi/run/wpa_supplicant/wlan0` socket is removed before the start |
| The controller | — | nothing new from the network guard. **Word 113 in the boot push** is now sent as the stock UI's recorded value (`0x0600`, low byte 0 — GROUND-TRUTH.md); the SAVE path is unchanged |

**Never:** a signal to a process (0 `kill()`), a change to `config.json`, slots or the flag, a network
when `wifionoff = 0` in the config, and nothing at all when `OURUI_NONET=1` or `OURUI_ROOT` is set.

### See what it did
```sh
cat /tmp/ourui_net.log                                   # one line per transition (up at start / self-heal done in N ms / ...)
/new_customer/ourui/ourui --net-status                   # read-only: probe + whether the conf file matches config.json's
```
DIAGNOSTICS (scrolled down): **NETWORK · WPA PID / IP / STATE**, e.g. `841 / <display-ip> / UP`
(green) or `- / <display-ip> / STARTING` (amber).

### Disable it without switching binaries
```sh
OURUI_NONET=1 ./run.sh          # the network guard is disabled completely (DIAGNOSTICS: OFF)
```
If the network was already started by ourui, it is an ordinary wpa_supplicant with the stock UI's
arguments (PPid 1). It can keep running; at the next reboot the stock UI / ourui start from scratch.
If it has to go: `kill $(pidof wpa_supplicant)` (ourui never does that itself).

### Back to v6.6
`artifacts/ourui/ourui_v6_6.bin` (md5 `c22153af54069d9d1fcbe43fcb118c99`) is v6.6 unchanged — **but
v6.6 in a slot B boot gives a machine without a network** (INSTALL-REPORT §3.1). The predecessor's
unfinished v6.7 intermediate state is kept as `ourui_v6_7wip.c` / `ourui_v6_7wip.bin` for
traceability; it must not be installed (among other things 10 red self-tests and a `kill()` call).

---

## v6.8: the REVERT button on HOME has been removed

The REVERT pill at the bottom of HOME was a phase 2 safeguard. From v6.8 HOME is: gauge + stats +
activity icons + page dots. There is **no touch zone** left where the pill was (self-test: the whole
field 150..330 × 404..480 gives no target — both idle and during a brew).

**The escape routes are unchanged:**
1. SYSTEM → **SWITCH TO ORIGINAL NOW** → **SWITCH NOW**. In run.sh mode = a clean exit (the exit
   reminder first, if the config was saved) → run.sh SIGCONTs demo_v9 — exactly what REVERT did. In a
   slot B boot it starts the stock UI (boot.md §9.2). Blocked during an activity.
2. SYSTEM → **BOOT UI** (next restart: original).
3. The switcher gesture — unchanged.
4. `run.sh`'s trap on any exit (crash, Ctrl-C, `kill $(pidof ourui)`) and the telnet commands at the
   top of this file.

The run.sh text on the switch screen is now *RUN.SH MODE · CLEAN EXIT* (before: *· LIKE REVERT*). No
files, no frames, nothing on the device is changed by v6.8. **Back to the v6.7 behavior** (with the
button) does not exist as a separate binary; v6.6 (`ourui_v6_6.bin`) has the button, but not the
network guard.

---

## v6.9: calm graph and stable weight — nothing new on the device beyond the display

v6.9 changes **only what is drawn** (UI_v6.md §14 and "CHANGES-v6.9"): the graph, hero, gauge and BAR
field show median-3 + EMA of the raw samples, the weight has a fixed source (a latch instead of the
phase per sample) and a dead band below 0.25 g, and DIAGNOSTICS has the row **GRAPH** (smooth / raw).

### What v6.9 touches
| | |
|---|---|
| Files | **no new ones.** The GRAPH choice on DIAGNOSTICS only applies to the session and is not written anywhere |
| The controller | **nothing new** — the same frames, the same frame policy, the same boot push, the same auto-stop (the MCU's). The build check shows 29 control/protocol fragments token-identical to v6.8 |
| Processes / network | nothing new (the network guard is v6.7's, unchanged) |

Control logic, writes, guards, thresholds and DIAGNOSTICS counters still read the **raw** values. See
them raw on the screen: DIAGNOSTICS → **GRAPH · TAP TO TOGGLE** → **RAW · UNFILTERED** (the chart is
labelled **RAW**; forgotten on restart).

### Back to v6.8
`artifacts/ourui/ourui_v6_8wip.bin` (md5 `75887fb154ffef20cc72696064e16c39`, source `ourui_v6_8wip.c`
md5 `5cdcc14b0503c67b939e27b6579655ff`) is v6.8 — the previous device binary, with the network guard
and without the REVERT button. Put it in `ourui`'s place like any other version (no data, config or
flag to clean up). The state at the start of the v6.9 round (the predecessor's partial v6.9):
`ourui_v6_9wip.c` / `ourui_v6_9wip.bin` — **must not be installed** (2 red self-tests, the weight
source follows the phase, no dead band).


### Status at the end of v6.9 (2026-11-17, 23:00)
* **The device binary** `ourui_v6` has md5 `2dce4eba0b4ce68dcd0369e955cf3b77`. The source is
  `ourui_v6.c` md5 `440be5124c0d986ec23e4c3b08f84149`, and a new build from it is byte-identical. It
  has **not** been put on the device.
* **No touch zone from REVERT (v6.8):** the self-test still gives `T_NONE` in the whole field
  150..329 × 404..479, both idle and during a brew. No UI string mentions REVERT. The mutant `pille`
  is still caught (mutation probes 10/10).
* **The network guard's self-healing on the device (v6.7)** is still **not proven on the device**. It
  did not answer on the network (<display-ip> "Host is down"). `net_heal_proof.py` is unchanged and
  ready. It does no reboots, does not touch the flag, slots or config and only uses tmpfs, plus one
  controlled kill of the stock wpa_supplicant while demo_v9 is SIGSTOPped.
* **Undo v6.9** as above: `ourui_v6_8wip.bin` in `ourui`'s place. There is no data to clean up.

## v6.10: pressure as a ≈bar estimate — display only

v6.10 changes **only how the pressure is shown** (UI_v6.md §16 and "CHANGES-v6.10"):
*PRESSURE\* UNCALIBRATED* has become **≈BAR · EST.** with a single linear anchor (pre-infusion 358 raw
in the recording = config `preInfusionPressure`, 4 bar → k = 0.01117 bar/raw), pulses above 20 bar are
shown as *PULSE* instead of a number, DIAGNOSTICS has the block **PRESSURE · ESTIMATE**, and a tap on
the chart/BAR field gives a toast with the basis.

### What v6.10 touches
| | |
|---|---|
| Files | **no new ones.** The anchor is *read* from `config.json` (`preInfusionPressure`, the same loading as the settings table) — nothing is written |
| The controller | **nothing new** — the same frames, frame policy, boot push and auto-stop. Build check: 37 control/protocol/conditioning fragments are token-identical to v6.9; the toast tap sends no frames (self-test) |
| Processes / network | nothing new |

### Back to v6.9
`artifacts/ourui/ourui_v6_9.bin` (md5 `2dce4eba0b4ce68dcd0369e955cf3b77`, source `ourui_v6_9.c` md5
`440be5124c0d986ec23e4c3b08f84149`) is v6.9 — the previous device binary. SIM builds:
`ourui_v6_9_sim` / `ourui_v6_9_sim_arm`. Put it in `ourui`'s place like any other version; there is no
data, config or flag to clean up. Raw pressure values can also be seen without undoing anything:
DIAGNOSTICS → **GRAPH** → **RAW · UNFILTERED** (the BAR field then shows word 51 raw under
*PRESSURE RAW*).

### Status at the end of v6.10 (2026-11-17)
* **The device binary** `ourui_v6` has md5 `29bc801c1c777d9cb3484bfa960c66bc` (source `ourui_v6.c`
  md5 `23672df8557a1fbb4d54cdb6b220c244`; reproducible). It has **not** been put on the device; the
  device was not contacted in the round. No reboots, flag, slot or config writes.
* The mutant `pille` (the REVERT zone) is still caught; mutation probes 14/14.

## v6.11: UX review — display and touch routes only

v6.11 changes **how the UI looks and is navigated** (UI_v6.md §17 and "CHANGES-v6.11"): plain language
instead of word numbers and raw codes, raised contrast, a start hint in the empty chart field, a gear
for the settings, a close chevron and swipe-down in the details, a swipe guard and DIAGNOSTICS in
sections.

### What v6.11 touches
| | |
|---|---|
| Files | **no new ones.** The gear, the chevron and swipe-down only change the page or close a view (like CANCEL). Nothing is written |
| The controller | **nothing new.** The same frames, frame policy, boot push, activity recipes and auto-stop. Build check: 47 control/protocol fragments are token-identical to v6.10. The self-test confirms that the gear and edge taps send no frames |
| Processes / network | nothing new |
| The revert path | **unchanged:** SYSTEM → *SWITCH TO ORIGINAL NOW* → *SWITCH NOW*. The texts there are the same. The gear sits where the REVERT pill was before v6.8, but it **only changes the page** (self-test + the mutant `pille`) |

### Back to v6.10
`artifacts/ourui/ourui_v6_10.bin` (md5 `29bc801c1c777d9cb3484bfa960c66bc`, source `ourui_v6_10.c` md5
`23672df8557a1fbb4d54cdb6b220c244`) is v6.10, the previous device binary. SIM builds:
`ourui_v6_10_sim` / `ourui_v6_10_sim_arm`. Put it in `ourui`'s place as for earlier versions. There is
no data to clean up; the UI language file has the same format.

### Status at the end of v6.11 (2026-11-18)
* **The device binary** `ourui_v6` has md5 `725386ccadade594275f20cc998885eb` (source `ourui_v6.c`
  md5 `e9eea8caebfa5d832884d96996dbd2c7`; reproducible). It has **not** been put on the device; the
  device was not contacted in the round. No reboots, flag, slot or config writes.
* The mutant `pille` (the REVERT zone) is still caught; mutation probes 20/20.

## v6.13: manual STOP for water / brew / grind

v6.13 adds STOP buttons on HOME (UI_v6.md §18 and "CHANGES-v6.13"). A tap sends one of three new stop
frames to the controller: `sub16 len1` (word 16 = 0, + `sub19 len1` if word 19 ≠ 0), `sub22 len1`
(word 22 = 0) or `sub6 len1` (word 6 = 0). These are the same words the MCU's own auto-stop resets.

### What v6.13 touches
| | |
|---|---|
| Files | **no new ones.** STOP writes neither to the config, the flag nor the lang file |
| The controller | **only on a STOP tap:** one cmd6 write of one word to 0 (water possibly two). The frame policy, the start recipes, the steam stop, the boot push and the auto-stop are token-identical to v6.12 (build check: 47 fragments). A stop frame only contains zeros and cannot start anything |
| Processes / network | nothing new |
| The revert path | **unchanged:** SYSTEM → *SWITCH TO ORIGINAL NOW* → *SWITCH NOW*. The STOP button sits in the action row (y 344–400), not in the SWITCH/gear zones |

### Back to v6.12
`artifacts/ourui/ourui_v6_12.bin` (md5 `725386ccadade594275f20cc998885eb`, source `ourui_v6_12.c` md5
`e9eea8caebfa5d832884d96996dbd2c7`) is v6.12, the deployed binary. SIM builds: `ourui_v6_12_sim` /
`ourui_v6_12_sim_arm`. Put it in `ourui`'s place in the same way as for the earlier rollbacks (md5
check before and after). No config/flag changes are needed: v6.13 has no state on disk.

### Status at the end of v6.13 (2026-11-18)
* **The device binary** `ourui_v6` has md5 `163692b5b3e11d8294471c5a0eae578b` (source `ourui_v6.c`
  md5 `eec5085ddc1861c212d396c70771e9dc`; reproducible). It has **not** been put on the device. The
  machine was offline for the whole round, so there were no reboots and no flag, slot or config writes.
* The on-device proof of STOP is run with `python3 verify_stop_v613.py` when the machine is switched
  on. The script uses run.sh mode in `/tmp/ourui_s`, touches no config, flag or slots and does no
  reboot.
