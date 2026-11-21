# NFC — reverse findings and plan (planning, not implemented)

Status: **plan for review**. Nothing here is implemented. The preset model, file format and NDEF tag format are in
[presets-spec.md](presets-spec.md). Addresses are virtual addresses in `demo_v9` (1.1.1-69, symbols and debug info kept).
Confidence: **H** = proven from code; **M** = strong inference; **L** = guess.

The findings in §2 are **static only**. No reader register was touched, and the read-only device probe (§9) is still
outstanding because the machine was offline during this round.

## 1. Summary

| Topic | Finding |
|---|---|
| Reader | An RC522-compatible ISO 14443 reader on **I²C bus 1 (`/dev/i2c-1`), 7-bit address 0x3F**. The init sequence unlocks extended registers through 0x37 and has a Type B mode, which **points to a Fudan FM175xx-class chip** (FM17550/17580), not an NXP MFRC522. Exact part: unknown until VersionReg is read. |
| Power/enable | `gpio0`, exported and set high by the boot script `/customer/demo.sh`. The stock NFC loop also re-writes `gpio0=1` on every pass. |
| Tags stock accepts | **MIFARE Classic 1K only**, with the default key A `FFFFFFFFFFFF`. It reads blocks 4–29 (sectors 1–7, 20 data blocks = 320 bytes). |
| Stock payload | ASCII **base64 → AES-128-CBC (static key/IV in the binary) → JSON with 17 keys** (`gs pw bt bw n1 n2 el pr ct rl bgn o1 o2 o3 f1 f2 f3`). This is obfuscation, not security: anyone with the binary can make a card. |
| UID | **Not used by stock.** There is no UID → profile mapping. |
| Writes tags? | **No.** `PcdWrite` and the SR176 write paths have no reachable caller. |
| On tap | The recipe goes to the controller **immediately and without confirmation**: one block frame cmd6 sub 4 len 21 (words 4–24), then `config.json` setters, then the stock preset is cleared. After that the decrypted JSON is written to `/customer/nfc.json` (write-only; `demo_v9` never reads it), and a bean card page appears with `nfc.mp3`. |
| Type B / ID card / SR176 | The code is present (`PcdRequestB`, `GetIdcardNum`, `ReadSr176`…) but **dead**: 0 callers. |
| Exclusivity | `demo_v9` opens `/dev/i2c-1` once and holds the fd. There is **no locking**. Two processes using the reader at the same time corrupt each other. Meraki+ may only touch the reader while `demo_v9` is SIGSTOPped (run.sh mode) or not running (slot B boot). |

## 2. Stock NFC, reversed

### 2.1 Bus and chip

| Item | Detail | Where | Conf. |
|---|---|---|---|
| Device node | `hw_i2c_init` opens `/dev/i2c-1` `O_RDWR` once. fd global at 0x1A5F04. | 0x142708, called from `app_nfc_thread` 0x142934 | H |
| Addressing | `ioctl(fd, I2C_SLAVE=0x706, 0x3F)` before each transfer (the last address is cached at 0x1E0505). Then plain `write`/`read`, with the register as the first byte. No `I2C_RDWR`. | `hw_i2c_write` 0x142784, `hw_i2c_read` 0x14282C, `IIC_ReadOneByte` 0x140948 (addr at 0x140970), `IIC_Write_Byte` 0x140988 (0x1409BC) | H |
| `i2c_mcu_read/write` | These are misnamed generic wrappers whose only callers are the reader's byte helpers. **The brew controller is not on I²C**; it stays on `/dev/ttyS1` UART. | 0x14085C / 0x1408A8 | H |
| Touch | Touch is a separate bus: Techwin TW31xx kernel driver on i2c-0, address 0x0C. Bus 1 carries only the reader (as far as is known). | FINDINGS (lsmod, input devices) | M |
| Reset | `PcdReset`: CommandReg 0x01 = 0x0F (SoftReset). | 0x140C1C | H |
| Version check | `IC_ver` reads register 0x37 (VersionReg) and only tests it for **non-zero**; there is no expected value. Reader absent → reads 0 → the stock loop skips the pass. | 0x140D0C | H |
| Chip family | `PcdConfig('A'/'B')` **writes** 0x37: saves it, writes 0, then 0x5E, pokes 0x35=0xED and 0x3B=0xA5, writes 0x37=0xAE, then 0x3B=0x72, and restores 0x37. That is the extended-register unlock of the FM175xx family. A genuine MFRC522 has a read-only 0x37 and no Type B. | `PcdConfig` 0x140D4C | M |
| A-mode regs | ModeReg 0x11=0x3D, TxMode/RxMode 0x12/0x13=0x00, TxASK 0x15=0x40, RxSel 0x17=0x86, RFCfg 0x26=0x58, plus GsN/CWGsP | 0x140D4C | H |
| B-mode regs | 0x11=0x3F, 0x12/0x13=0x83, 0x1E=0x13 (dead path) | 0x140D4C | H |
| Enable line | `IC_ver` runs `system("echo 1 > /sys/class/gpio/gpio0/value")` on every pass. `/customer/demo.sh` exports gpio0 as an output = 1 at boot, before either slot's UI starts, so gpio0 is also high on a slot B boot. Whether gpio0 is *only* the reader enable or is shared (the boot script's comment area reads like panel power/reset) is unknown. | 0x140D1C; demo.sh line 7 | M |
| Antenna | Not determinable statically. HOME uses `imgs/home/NFC_ring.png` (0x10B050), probably a "tap here" hint ring. | — | L |

### 2.2 The stock reader thread

`app_nfc_thread` (0x14291C) is started **unconditionally** from `main` (0x14BD3C → `portingInit`). No `config.json` switch
was found (M–H). One loop pass:

1. `PcdReset` → `IC_ver`. If the version reads 0, the pass is skipped.
2. For each block 4..29 except the sector trailers 7, 11, 15, 19, 23, 27: `card_block_check` (0x142644) → `ComReqA(mode=0, blk)`
   (0x141E80). Each block does **a full new session**: `PcdConfig('A')` (antenna off/on, ≈ 9 ms), `PcdRequest(0x52 WUPA)`,
   `PcdAnticoll` (**cascade level 1 only** → 4-byte UIDs), `PcdSelect`, `PcdAuthState(0x60 key A, FFFFFFFFFFFF)`,
   `PcdRead` (16 bytes).
3. The 320 bytes are concatenated → decode (§2.3).
4. `delay_ms(200)` (0x1438A4), then loop.

Consequences:
* **NTAG / Ultralight / NFC Forum Type 2 tags never work with stock**: they have a 7-byte UID (CL2) and no Crypto1 auth.
  Meraki+'s own tag type (NTAG21x, presets-spec §7) therefore never triggers the stock path (H).
* **No duplicate suppression in the thread.** A valid card left on the reader is re-read and re-applied every cycle (M; a
  UI-level guard was not found).
* `ComReqA(mode=1)` → `PcdWrite` exists, but the only caller passes 0. **Stock never writes tags** (H).
* Dead code (0 callers): `ComReqB`, `PcdRequestB`, `PcdSlotMarker`, `PcdAttriB`, `GetIdcardNum`, `PcdHaltB`,
  `SelectSr`/`ReadSr176`/`WriteSr176`/`ProtectSr176`, `Card_Check`, `ComReqA_Block`, `test_data`,
  `coffee_decoding_and_analysis`, `encrypt_aes`, and `page_home_test_trigger_nfc` (a test-tag injector for a
  "Codex Test Blend").

### 2.3 Stock payload

`decrypt_aes` (0x143C8C): `base64_decode` → `AES_set_decrypt_key(128)` → `AES_cbc_encrypt(decrypt)` with a static key and IV
in `.data`. The plaintext is JSON (cJSON, 0x142B14). All 17 keys must be present, otherwise the log says "json error" (0x142D2C).

| Key | Meaning | Use | Conf. |
|---|---|---|---|
| `gs` | recommended grind-dial setting | UI only (`app+0x15D0`) | M |
| `pw` | dose, g (float) | ×10 → word 4 + `autoGrindWeight` | H |
| `bt` | brew temp, °C as displayed | **×10 − 50** → word 11 + `brewTemper` (the same +5.0 °C display offset as stock presets; presets-spec Q-P1) | H/M |
| `bw` | yield, g | ×10 → word 23 + `autobrewWeight` | H |
| `n1`, `n2` | name lines (`"00"` = empty) | card page | H |
| `o1`–`o3` | origin lines | card page | H |
| `f1`–`f3` | flavour notes | card page | H |
| `el` | elevation, m | card page | M |
| `pr`, `rl` | process, roast level (int) | card page | L |
| `ct` | unknown int | — | L |
| `bgn` | background image name → `S:imgs/nfc/<bgn>.png` | card page | L–M |

`/customer/nfc.json` (written by `nfc_data_save` 0x1438B0) is this decrypted JSON, kept for reference only. The vendor key
and IV are **not** reproduced anywhere in this repository and will not be (§7).

### 2.4 Stock UI flow on tap

1. The thread decodes the card, then `usart_send(6, 4, 21)` (0x1437E8) **writes words 4..24 as one block**. That includes word 24
   (the steam start word, sent with its mirror value) and the other start words in that range.
2. Setters for `autoGrindWeight`/`brewTemper`/`autobrewWeight` → `setPresetAppliedIndex(255)` (the stock preset is cleared) →
   `data_save` (`config.json`). Both saves are skipped when the flag `app+0x15B1` is set (purpose unknown).
3. `app+0x15A9 = 1` (0x143888) → `deal_nfc_update` (0x10DB2C, from HOME's handler) → `clickGotoNfcPage` (0x120D80) →
   `home_nfc_deal` (0x10E17C) draws the bean card (name, background, origin/flavour/elevation/process/roast),
   `nfc_enter_anim`, and `audio/nfc.mp3` via `deal_play` (0x137404). `nfcCountDownExit` (0x121E00) closes it after ≈ 5 s (L–M).
4. `dealBrewNfc` (0x11B138) marks the brew page as "applied from card" (star icons).

**Meraki+ will not copy two things:** the block write that includes start words, and the silent re-apply while a card rests
on the reader.

## 3. Constraints for Meraki+

| # | Constraint | Consequence |
|---|---|---|
| C1 | One physical reader, no locking, stock keeps its fd | Meraki+ touches the reader **only** when `demo_v9` is stopped (`/proc/<pid>/stat` state `T`) or absent. The NFC helper checks this before every session and goes idle otherwise. |
| C2 | SIGSTOP can land mid-transaction (Crypto1 on, half-filled FIFO, RF field on) | Every Meraki+ session starts with SoftReset + full `PcdConfig`. Stock does the same on every pass, so after SIGCONT it recovers within one cycle (M–H). Before handing back to stock, Meraki+ leaves the chip with the antenna off and idle. |
| C3 | A vendor card left on the reader when stock resumes is applied by stock (block write + `config.json`) | This is stock behaviour and cannot be changed. The SWITCH TO ORIGINAL hint mentions it. Meraki+ tags are NTAG and invisible to stock. |
| C4 | The vendor payload needs the vendor key | Meraki+ **does not decrypt vendor cards** and ships no key. It can still *link* a vendor card by its UID (any card works as a "preset button"). Reading the key at runtime from the on-device binary is technically possible but out of scope (licensing). |
| C5 | Slot B boot: gpio0 is set by `demo.sh` before the UI starts | The reader should be powered on slot B as well. To be verified (§9 H3). Meraki+ never writes GPIO unless that is proven necessary *and* gpio0's other users are known. |
| C6 | I²C syscalls are blocking (≈ 5–15 ms per Type A session at 100/400 kHz) | They must stay off the UI loop. Hence a helper process (§4). |
| C7 | The RF field must not disturb the scale or load cell | Stock polls constantly without visible effect, so the risk is low. Meraki+ still polls **only while idle** (no grind/brew/steam/water), since taps during a shot are ignored anyway. |

## 4. Architecture: `merakiplus_nfc` helper

This follows the `merakiplus_snd` precedent: a separate small binary in `bin/` (static musl, `./build.sh`, reproducible, listed in
`MD5SUMS`/`BIN_MD5`), started and stopped by the UI.

```
merakiplus (UI loop)  ──stdin cmds──►  merakiplus_nfc  ──/dev/i2c-1 @0x3F──►  FM175xx ⇄ tag
                      ◄──stdout JSONL──
```

* **Driver.** A minimal RC522/FM175xx Type A driver:
  * SoftReset, A-mode config (the stock register values from §2.1, as data)
  * REQA/WUPA, anticollision **CL1 + CL2** (7-byte UIDs), SELECT, HLTA, CRC_A via the chip
  * NTAG READ (0x30), WRITE (0xA2), GET_VERSION (0x60) to tell 213/215/216 apart
  * MIFARE Classic: UID and SAK only (no auth, no read, **never a write**)

  Type B is not needed.
* **Poll.** One WUPA every 250 ms (4 Hz), but only while the UI says `poll on`. That is HOME/sheets idle; it is off during
  activity and in `V_OFF`. Between polls the antenna is off.
* **Debounce.** It sends `{"ev":"tag","uid":"04A1B2C3D4E580","type":"ntag215","ndef":"<hex>"}` once per arrival, and
  `{"ev":"gone"}` after 2 consecutive empty polls. The same tag must leave before it triggers again. This avoids stock's
  re-apply loop.
* **Commands** (stdin, one per line): `poll on|off`, `write <hex NDEF TLV>` (only to the tag present now, only to NTAG, with a
  read-back verify), `diag`, `quit`. Replies: `{"ev":"write","ok":true}` / `{"ev":"err","code":"…"}`.
* **Diag.** VersionReg value, chip guess, poll count, error count, last UID/type and the exclusivity state are shown on
  DIAGNOSTICS → NFC. The first hardware session needs this.
* **Exclusivity guard (C1).** Before each session it re-checks that no `demo_v9` is running unless stopped. If one is running, it
  reports `{"ev":"busy","why":"stock"}` and does nothing.
* **Reader absent.** VersionReg 0 or I²C NACK at 0x3F → `{"ev":"absent"}` once. The UI hides every NFC row; Gen1 machines
  without a reader just see no NFC UI.
* **SIM.** In the sim build the UI does not spawn the helper. The sim ctrl socket injects the same event lines
  (`{"cmd":"nfc","name":"tap","uid":…,"type":…,"ndef":…}`, `remove`, `absent`, `write_fail`), and the state JSON gets
  `nfc:{reader,poll,tag,last_ev,writes[]}`. The UI-side parser is the one used on the device, so the selftest drives it
  directly. This is an additive `sim/CONTRACT.md` addendum, since `sim/` is shared with the test harness.

## 5. NFC features, prioritized

| Prio | Feature | Needs | Phase |
|---|---|---|---|
| 0 | **Presets in the UI, no NFC** (presets-spec) | — | 1 |
| 1 | **Tap tag → apply linked preset.** Any tag, vendor card or NTAG: the UID is mapped to a preset id. The link is made in the preset sheet (LINK NFC TAG → "HOLD A TAG TO THE READER" → done). Tapping applies immediately via presets-spec §4.1, with the busy guard; an unknown tag shows `UNKNOWN TAG — LINK IT FROM A PRESET`. | reader read + UID | 2 |
| 2 | **DIAGNOSTICS → NFC** (reader version, last tag, counters) | reader | 2 |
| 3 | **Recipe card: write a preset to an NTAG** (WRITE TO TAG) and **read a card** from any Meraki+ machine. Tap → card view → APPLY ONCE / SAVE AS PRESET; a known `id` → applies its preset. Shareable; phones can write it too (MIME record, presets-spec §7). | NTAG read/write, NDEF | 3 |
| 4 | **Bean card**: the same record with the bean keys (`bn by or pr rl al fl rd`), shown as a card like stock's NFC page | 3 | 3 |
| 5 | Roast-date freshness on HOME ("DAY 12 SINCE ROAST") from the last bean card | 4 | 4 |
| 6 | **Maintenance tags**: a CLEAN / DESCALE card opens the guide and logs the date; a reminder appears after N shots/days (Meraki+ counts brews itself) | 2 | 4 |
| 7 | **Guest mode tag**: toggles a reduced UI (presets + start only, settings hidden). This is convenience, **not security** (a UID can be cloned); for real locking use the `sig=` HMAC from presets-spec §7 | 2 | 4 |
| 8 | **User profile tag**: a person's default preset plus a per-person shot counter | 2 | 4 |
| — | Tap-to-wake / tap-to-preheat | Off by default and only behind an explicit setting: a tag should not heat a boiler unattended | 4 (optional) |
| — | Reading vendor card *contents* | Rejected (C4). Vendor cards can still be linked by UID | — |

## 6. Phasing

Effort is in focused developer days including selftest, previews, docs and the review loop. It excludes waiting for the machine.

### Phase 0 — hardware probe (read-only), runs in parallel with phase 1 · 0.5 d + one session at the machine
* **Scope:** the read-only probe from §9 (fds, threads, gpio state, bus listing). The one register read (VersionReg 0x37) is done
  only with `demo_v9` stopped by a running Meraki+, and only after explicit approval, because a bus read from a second process is not
  strictly "no device interaction".
* **Output:** chip part (VersionReg value), confirmation of 0x3F, gpio0 state on slot A and slot B boots, antenna position
  (tap test with a vendor card while stock runs and the stock card page is watched). Results go into §2 and §9 of this file.

### Phase 1 — presets in the UI (no NFC) · 4–5 d (1a ≈ 3.5 d, 1b ≈ 1.5 d)
* **1a scope:** presets-spec §3–§5, §8 (without rename), §9, §10. This covers the presets file, the effective overlay (boot push,
  controller-restart re-push, run.sh assert), apply/base/tweak, the HOME badge, the PRESETS list and preset sheet, the base push on
  SWITCH TO ORIGINAL, and EN/DA strings.
* **1b scope:** stock import (§6) and the name roller editor (if Q-P2 = yes).
* **Files:** `src/merakiplus.c`, `docs/presets-spec.md` (status), `docs/dev/UI_v6.md` (+ section, appendix), `docs/ui-guide.md`,
  README, CHANGELOG, `tools/merakiplus.py` (revert removes `presets`; VERSION), additive `sim/` scenarios + CONTRACT addendum,
  `bin/` rebuild.
* **Tests:** presets-spec §10, including the **`config.json` md5 invariant**.
* **Hardware verify:**
  1. apply 2 presets and read the controller words back on DIAGNOSTICS
  2. reboot (slot B) → the preset is re-applied
  3. controller restart → re-push
  4. SWITCH TO ORIGINAL → stock shows base and the controller matches base
  5. back → assert re-applies
  6. `config.json` md5 is unchanged throughout
* **Risks:** the boot-push overlay touches the most safety-relevant path (it needs the frame-policy tests); Q-P1 (brew temp offset)
  affects only import.

### Phase 2 — NFC read → switch preset · 5–7 d
* **Scope:** the `merakiplus_nfc` helper (driver CL1/CL2, poll, debounce, guard, diag), UI integration (spawn/stop with the
  helper's lifecycle, poll gating, event parser), LINK NFC TAG flow, tap → apply, the NFC map file, DIAGNOSTICS → NFC.
* **NFC map file `/new_customer/merakiplus/nfc`** (same atomic pattern):
  ```
  v=1
  enabled=1
  tag=04A1B2C3D4E580 7f3a91c2
  tag=3B9C11F0 0c41d2e9
  ```
  At most 32 `tag=` lines. A UID can link to only one preset; a preset can have several tags. Deleting a preset drops its links.
* **Files:** new `src/merakiplus_nfc.c`, `build.sh`, `bin/merakiplus_nfc` + `MD5SUMS`/`BIN_MD5`, `tests/check_bins.py`
  (reproducible list), `tests/hygiene.py` (no key material; helper allowed), `src/merakiplus.c`, `tools/merakiplus.py`
  (install/revert the helper and the `nfc` file), docs, sim addendum.
* **Sim/tests:** selftest `st_nfc`:
  * event parser fuzz (truncated or oversized lines)
  * tap known → apply; tap unknown → toast; tap during activity → ignored
  * same tag held → one apply only
  * link flow; absent reader → no NFC rows
  * helper `busy:stock` → idle

  Sim scenarios: `nfc_tap_apply`, `nfc_tap_busy`, `nfc_link`, `nfc_absent`. The driver gets a host unit test against a scripted
  fake register file (REQA/anticoll/CL2 traces taken from the datasheet sequences).
* **Hardware verify:**
  1. the DIAG page shows VersionReg and a rising poll count
  2. NTAG215 → 7-byte UID; vendor card → 4-byte UID with SAK 0x08
  3. link and tap → the preset is applied (controller words checked)
  4. the card stays on → no repeat
  5. SWITCH TO ORIGINAL with a vendor card on the reader → stock still reads it (regression: the stock NFC path is intact)
  6. 1 h soak: error count about 0, UI frame time unchanged
  7. slot B boot: the reader is detected
* **Risks:** chip variant quirks (FM175xx vs RC522 timeouts); gpio0 on slot B (H3); stock not stopped (the guard covers it);
  I²C bus speed and NACKs (retry once per session; on persistent failure report `absent`).

### Phase 3 — NFC write/share + recipe & bean cards · 4–5 d
* **Scope:** NDEF TLV build/parse (Type 2: CC page 3, TLV 0x03, short and long records), the MIME + URI message from
  presets-spec §7, WRITE TO TAG (NTAG only, capacity check per type, write → read back → compare, refuse MIFARE Classic and
  locked tags), card view (recipe + bean keys), APPLY ONCE / SAVE AS PRESET, known `id` → its preset.
* **Tests:** NDEF round trip for each tag size; tags written by a phone (Android "NFC Tools" with a MIME record) parse; an
  oversize payload is refused; `v=2` → `NEWER CARD FORMAT`; a half-written tag (power pulled) → parse fail, no apply.
* **Hardware verify:**
  1. write a preset to an NTAG213/215 and tap it on the same machine → applied
  2. a phone reads it → the URL opens and the MIME record is visible
  3. a phone-written card works on the machine
  4. the stock UI ignores it (no stock card page, no recipe change)
* **Risks:** write reliability while the user moves the tag (the read-back check catches it); NTAG213 capacity with bean keys
  (the UI warns and offers recipe-only).

### Phase 4 — extras · 1–2 d each
Roast-date freshness, maintenance tags and reminders, guest mode, user profiles, optional tap-to-wake (default off). Each is
specified when it is picked.

## 7. Security and privacy

* **Tags carry no secrets**: no Wi-Fi data, no keys, no personal data beyond what the user types as a name. Recipe cards are meant to
  be public.
* **A UID is not identity.** UIDs are readable by anyone and cloneable (magic cards, phone emulation). Linking a UID is a
  convenience button and never an authorization. Features that would need trust (guest lock) require an NDEF payload with an HMAC
  under a per-machine key that stays on the display (presets-spec §7, `sig=`).
* **A tag can never start anything.** Tags only select values. Every value passes the `SET` range check and the frame policy; start
  words are never written by a tag path; words 26/27/30 stay compile-time forbidden.
* **Unknown cards do not auto-apply.** Only *linked* UIDs and cards whose `id` matches a local preset apply on tap. A foreign
  recipe card always shows the card view with an explicit APPLY.
* **No vendor key material** goes into the repo, the docs or the binaries; `tests/hygiene.py` gains a check. Meraki+ never writes
  MIFARE Classic, so vendor cards cannot be damaged.
* `nfc` and `presets` live under `/new_customer/merakiplus/` and are removed by `revert`.

## 8. Effort overview

| Phase | Effort | Depends on |
|---|---|---|
| 0 probe | 0.5 d + session | machine online |
| 1 presets | 4–5 d | — |
| 2 NFC switch | 5–7 d | 0, 1a |
| 3 write/share/cards | 4–5 d | 2 |
| 4 extras | 1–2 d each | 2 or 3 |

## 9. Open hardware questions and the read-only probe

| # | Question | How to answer |
|---|---|---|
| H1 | Exact chip: the VersionReg (0x37) value and the chip marking | A register read with stock stopped (approval needed), or a photo of the board |
| H2 | Is 0x3F the only device on i2c-1? | `ls /sys/bus/i2c/devices/` (read-only). **No `i2cdetect`**: its probe writes can upset devices |
| H3 | gpio0 on a slot B boot; is gpio0 shared with something else? | `cat /sys/class/gpio/gpio0/{direction,value}` on both boots; read the board/boot script context |
| H4 | Does `demo_v9` hold `/dev/i2c-1` open? | `ls -l /proc/$(pidof demo_v9)/fd`. An earlier session already saw the fd and labelled it "touch"; touch is on i2c-0, so it is the reader |
| H5 | Can Meraki+ read while stock is SIGSTOPped, and does stock recover after SIGCONT? | Phase 2 hardware verify, steps 1 and 5 |
| H6 | Does the reader handle NTAG21x (CL2, READ/WRITE) at the antenna's range? | Phase 2/3 hardware verify |
| H7 | Antenna position and read range (through the display glass?) | Tap test |
| H8 | Do Gen1 machines have the reader at all? | Probe on a Gen1, or reports from owners; the UI hides NFC when it is absent |

Read-only probe commands (each one only reads; none touches the bus):

```sh
ls -l /dev/i2c-* ; for d in /sys/bus/i2c/devices/*; do echo "$d $(cat $d/name 2>/dev/null)"; done
P=$(pidof demo_v9); grep State /proc/$P/status; ls -l /proc/$P/fd
for t in /proc/$P/task/*; do echo "$t $(cat $t/comm)"; done
ls /sys/class/gpio; cat /sys/class/gpio/gpio0/direction /sys/class/gpio/gpio0/value
ls -l /customer/nfc.json /customer/audio/nfc.mp3 /customer/imgs/nfc/
grep -c presetStore /customer/config.json
```
