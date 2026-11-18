#!/usr/bin/env python3
"""play.py — INTERACTIVE play mode for the E2E sim environment (Meraki+ / ourui).

Starts sandbox + mcu_sim + UI client in one call (reuses run_e2e.py's
launch mechanics) and serves a local web viewer where you can click around
the UI with a simulated MCU and data:

  python3 sim/play.py [--ui-bin PATH] [--port 8765] [--lang en|da]
                      [--scenario idle|brew|steam_auto|...]
                      [--ack normal|observed_v3|no_ack|lossy]

* The UI binary is auto-detected (build/ourui_sim, OURUI_SIM_BIN,
  --ui-bin). Until Fable's v6.4 SIM build lands, it runs against the reference
  client (sim/ref_client.py): protocol/state play with a simple fb emulation
  (placeholder graphics + state panel). Full visual play requires the v6.4 binary.
* Viewer: live fb (PNG ~6 Hz), state panel, protocol log, click→touch
  (evdev FIFO, tinj_hold format), hold ≥650 ms = hold-for-start, right-click =
  gesture test (v6.12: center hold ≥5 s + corner fallback, real switcher_host in file mode).
* Control panel: activities, fault injection (E1/E2/E3), CRC garbage,
  offline/back online, live ACK profile, language (lang file + config), boot flag,
  runtime switch (dry), MCU restart, speed 1×/2×/8×, pause, snapshot
  export (PNG+state), reset sandbox, UI restart after a crash.
* Keyboard in the viewer: g/b/h/s = grind/brew/hotwater/steam, e = fault cycle,
  l = language, r = reset, space = pause/resume.

Python stdlib only. No device contact — everything runs locally. Writes happen only
in the sandbox (sim/reports/play-*/), designated exports (sim/reports/play-exports/)
and a short-lived /tmp dir for ctrl sockets (removed on exit). Ctrl-C cleans up
pty/FIFO/socket/sandbox. See sim/PLAY.md for full documentation.
"""
import argparse
import hashlib
import json
import os
import shutil
import signal
import socket
import struct
import sys
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, unquote

sys.path.insert(0, str(Path(__file__).resolve().parent))
import proto
import png
import run_e2e
import ref_client as refc
from proto import (W, H, FB_BYTES, touch_raw, evdev_event,
                   EV_SYN, EV_KEY, EV_ABS, SYN_REPORT, BTN_TOUCH,
                   ABS_X, ABS_Y, ABS_MT_POSITION_X, ABS_MT_POSITION_Y,
                   ABS_MT_TRACKING_ID)
from run_e2e import ScenarioRun, Ctrl, find_ui_bin

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
REPORTS = HERE / "reports"
EXPORTS = REPORTS / "play-exports"
VIEWER_HTML = HERE / "play_viewer.html"

HOLD_START_MS = 650             # ourui: hold-for-start on activity buttons
ACTIVITY_MS = 750               # play's simulated hold (a bit over 650)
FAULT_CYCLE = [None, {"e1": 32}, {"e2": 16}, {"e3": 16}]
FAULT_NAMES = ["no fault", "E1 (word34=32)", "E2 (word35=16)", "E3 (word36=16)"]
STOCK_LANGS = ["English", "Deutsch", "Français", "Italiano", "Español",
               "Polski", "العربية", "简中", "繁中", "Korean", "Japanese"]


# ---------------------------------------------------------------- touch-bytes
def _ts():
    t = time.time()
    return int(t), int((t % 1) * 1e6)


def touch_down_bytes(x, y):
    """Down record sequence exactly like tinj_hold (CONTRACT §4)."""
    sec, usec = _ts()
    rx, ry = touch_raw(x), touch_raw(y)
    return b"".join(evdev_event(sec, usec, typ, code, val) for typ, code, val in (
        (EV_ABS, ABS_MT_TRACKING_ID, 1),
        (EV_ABS, ABS_MT_POSITION_X, rx), (EV_ABS, ABS_MT_POSITION_Y, ry),
        (EV_ABS, ABS_X, rx), (EV_ABS, ABS_Y, ry),
        (EV_KEY, BTN_TOUCH, 1), (EV_SYN, SYN_REPORT, 0)))


def touch_up_bytes():
    sec, usec = _ts()
    return b"".join(evdev_event(sec, usec, typ, code, val) for typ, code, val in (
        (EV_ABS, ABS_MT_TRACKING_ID, -1),
        (EV_KEY, BTN_TOUCH, 0), (EV_SYN, SYN_REPORT, 0)))


def touch_move_bytes(x, y):
    """Move record during a hold (same tracking id): POSITION_X/Y + ABS_X/Y + SYN."""
    sec, usec = _ts()
    rx, ry = touch_raw(x), touch_raw(y)
    return b"".join(evdev_event(sec, usec, typ, code, val) for typ, code, val in (
        (EV_ABS, ABS_MT_POSITION_X, rx), (EV_ABS, ABS_MT_POSITION_Y, ry),
        (EV_ABS, ABS_X, rx), (EV_ABS, ABS_Y, ry),
        (EV_SYN, SYN_REPORT, 0)))


def md5_file(path):
    try:
        return hashlib.md5(Path(path).read_bytes()).hexdigest()
    except OSError:
        return None


# ---------------------------------------------------------------- play-session
class Play:
    """One interactive session: sandbox + mcu_sim + UI client + sampler."""

    def __init__(self, args):
        self.args = args
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.gen = 0                    # session-generation (reset/restart)
        self.sr = None                  # ScenarioRun (reused launch mechanics)
        self.ui_bin = None if args.force_reference else find_ui_bin(args.ui_bin)
        self.ui_lang = args.lang        # "en"/"da" (ourui's own lang file)
        self.fault_idx = 0
        self.link_closed = False
        self.ui_intentional = False     # true while we stop the UI ourselves
        self.snapshot = {}              # latest sampler snapshot (for the viewer)
        self.toasts = deque(maxlen=30)
        self.plog = deque(maxlen=300)   # protocol log (sim assertions + md5)
        self.exports = deque(maxlen=20)
        self.last_gesture = None
        self.ui_stderr = deque(maxlen=200)
        self.sim_stderr = deque(maxlen=200)
        self.touch_lock = threading.Lock()
        self.tio = threading.Lock()
        self.fb_lock = threading.Lock()
        self.fb_cache_key = None
        self.fb_cache_png = None
        self.placeholder_png = self._make_placeholder()
        EXPORTS.mkdir(parents=True, exist_ok=True)

    # ------------------------------------------------------------ startup
    def _build_scen(self):
        """--scenario uses the scenario's config/sim part (not the timeline)."""
        scen = {"name": "play", "description": "interactive play mode",
                "config": {}, "sim": {"profile": self.args.ack, "seed": 1}}
        if self.args.scenario:
            p = HERE / "scenarios" / (self.args.scenario + ".json")
            if not p.exists():
                raise SystemExit(f"unknown scenario: {self.args.scenario} "
                                 f"(see sim/scenarios/ or run_e2e.py --list)")
            src = json.loads(p.read_text())
            scen["config"] = src.get("config", {})
            scen["sim"].update(src.get("sim", {}))
            scen["sim"]["profile"] = self.args.ack
            if src.get("boot_flag_initial"):
                scen["boot_flag_initial"] = True
            # replay/events/expect belong to the runner — not interactive play
        scen["sim"]["require_wand"] = bool(self.args.require_wand)
        return scen

    def start(self):
        with self.lock:
            self.gen += 1
            run_dir = REPORTS / ("play-" + time.strftime("%Y%m%d-%H%M%S")
                                 + f"-{os.getpid()}")
            self.sr = ScenarioRun(self._build_scen(), run_dir, ui_bin=self.ui_bin)
            sr = self.sr
            sr.make_sandbox()
            # UI language file (v6.3+): "en"/"da" — ourui's own labels, not config
            lang_path = sr.sandbox / "new_customer" / "ourui" / "lang"
            lang_path.parent.mkdir(parents=True, exist_ok=True)
            lang_path.write_text(self.ui_lang)
            sr.start_sim()
            sr.start_ui()
            self.ui_intentional = False
            self.link_closed = False
            self.toasts.clear()
            self.plog.clear()
            self.ui_stderr.clear()
            self.sim_stderr.clear()
            self._sampler_gen = -1
            self._state_off = 0
            self._assert_off = 0
            self._sim_ping = {}
            self._layout = None
            self._last_cfg_md5 = None
            self._last_slow = 0.0
            self._drain(sr.ui_proc, self.ui_stderr)
            self._drain(sr.sim_proc, self.sim_stderr)
        kind = "ourui SIM-build" if self.ui_bin else "REFERENCE client (placeholder fb)"
        print(f"[play] UI client: {kind}"
              + (f" ({self.ui_bin})" if self.ui_bin else ""), flush=True)
        print(f"[play] sandbox: {self.sr.sandbox}", flush=True)
        if not self.ui_bin:
            print("[play] NOTE: Fable's v6.4 SIM build (build/ourui_sim) "
                  "does not exist yet — running against ref_client with simple fb emulation. "
                  "Full visual play requires the v6.4 binary (auto-detected).", flush=True)

    def _drain(self, proc, buf):
        def run():
            try:
                for line in proc.stderr:
                    buf.append(line.rstrip("\n"))
            except (ValueError, OSError):
                pass
        threading.Thread(target=run, daemon=True).start()

    # ------------------------------------------------------------ shutdown
    def _teardown_session(self, remove=True):
        with self.lock:
            sr = self.sr
            self.sr = None
            self.ui_intentional = True
        if sr is None:
            return
        for ctrl, proc in ((sr.ui_ctrl, sr.ui_proc), (sr.sim_ctrl, sr.sim_proc)):
            try:
                ctrl.call({"cmd": "exit", "code": 0}, timeout=1.0)
            except Exception:
                pass
        for proc in (sr.ui_proc, sr.sim_proc):
            if proc is None:
                continue
            try:
                proc.wait(timeout=3)
            except Exception:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except Exception:
                    proc.kill()
        shutil.rmtree(sr.sock_dir, ignore_errors=True)
        if remove:
            # sr.dir = <play-run_dir>/play — remove the whole play-run_dir
            shutil.rmtree(sr.dir.parent, ignore_errors=True)

    def cleanup(self):
        self.stop.set()
        self._teardown_session(remove=not self.args.keep)
        print("[play] cleaned up: pty/FIFO/ctrl sockets"
              + ("" if self.args.keep else " + sandbox")
              + f" · exports kept in {EXPORTS}", flush=True)

    def reset(self):
        self._teardown_session(remove=True)
        self.start()
        return {"ok": True, "run_dir": str(self.sr.dir)}

    def restart_ui(self):
        with self.lock:
            sr = self.sr
        if sr is None:
            return {"ok": False, "error": "no session"}
        self.ui_intentional = True
        try:
            sr.ui_ctrl.call({"cmd": "exit", "code": 0}, timeout=1.0)
        except Exception:
            pass
        try:
            sr.ui_proc.wait(timeout=3)
        except Exception:
            sr.ui_proc.terminate()
            try:
                sr.ui_proc.wait(timeout=3)
            except Exception:
                pass
        # fresh state file + pty path (the MCU may have got a new pty on link reopen)
        try:
            sr.state_path.write_text("")
            pf = sr.dir / "pty_path"
            if pf.exists():
                sr.pty_path = pf.read_text().strip()
        except OSError:
            pass
        try:
            sr.start_ui()
        except Exception as e:
            self.ui_intentional = False
            return {"ok": False, "error": str(e)}
        self.ui_intentional = False
        self._drain(sr.ui_proc, self.ui_stderr)
        return {"ok": True}

    # ------------------------------------------------------------ ctrl calls
    def _sim_call(self, req, timeout=2.0):
        with self.lock:
            sr = self.sr
        if sr is None or sr.sim_ctrl is None:
            return {"ok": False, "error": "sim offline"}
        try:
            return sr.sim_ctrl.call(req, timeout=timeout)
        except Exception as e:
            return {"ok": False, "error": str(e)}

    def _ui_call(self, req, timeout=3.0):
        with self.lock:
            sr = self.sr
        if sr is None or sr.ui_ctrl is None:
            return {"ok": False, "error": "UI offline"}
        try:
            return sr.ui_ctrl.call(req, timeout=timeout)
        except Exception as e:
            return {"ok": False, "error": str(e)}

    # ------------------------------------------------------------ touch
    def _fifo_write(self, data):
        with self.lock:
            sr = self.sr
        if sr is None:
            raise RuntimeError("no session")
        fd = os.open(str(sr.touch_path), os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(fd, data)
        finally:
            os.close(fd)

    def inject_touch(self, x, y, ms):
        x = max(0, min(W - 1, int(x)))
        y = max(0, min(H - 1, int(y)))
        ms = max(0, min(10000, int(ms)))
        if not self.touch_lock.acquire(blocking=False):
            return {"ok": False, "error": "touch busy (another hold in progress)"}

        def worker():
            try:
                self._fifo_write(touch_down_bytes(x, y))
                if ms > 0:
                    time.sleep(ms / 1000.0)
                self._fifo_write(touch_up_bytes())
            except OSError as e:
                with self.lock:
                    self.plog.append({"t": time.time(), "kind": "touch_error",
                                      "error": str(e)})
            finally:
                self.touch_lock.release()
        threading.Thread(target=worker, daemon=True).start()
        return {"ok": True, "x": x, "y": y, "ms": ms,
                "raw_x": touch_raw(x), "raw_y": touch_raw(y),
                "hold": ms >= HOLD_START_MS}

    # ---- 1:1 live touch forwarding (pointer/touch events → evdev FIFO) ----
    # The viewer sends down/move/up exactly as the finger moves; ourui's
    # inp_poll/ui_touch decides tap/hold/swipe/scroll itself = same behavior as the device.
    def _touch_io(self, data):
        with self.tio:
            self._fifo_write(data)

    def touch_down(self, x, y):
        x = max(0, min(W - 1, int(x))); y = max(0, min(H - 1, int(y)))
        self._touch_io(touch_down_bytes(x, y))
        return {"ok": True, "ev": "down", "x": x, "y": y}

    def touch_move(self, x, y):
        x = max(0, min(W - 1, int(x))); y = max(0, min(H - 1, int(y)))
        self._touch_io(touch_move_bytes(x, y))
        return {"ok": True, "ev": "move", "x": x, "y": y}

    def touch_up(self):
        self._touch_io(touch_up_bytes())
        return {"ok": True, "ev": "up"}

    def inject_swipe(self, x0, y0, x1, y1, ms=260):
        """Drag/swipe: down at (x0,y0) → interpolated MOVE records → up at (x1,y1).
        ourui switches page on horizontal >= 60 px (the state contract's 'swipe')."""
        x0 = max(0, min(W - 1, int(x0))); y0 = max(0, min(H - 1, int(y0)))
        x1 = max(0, min(W - 1, int(x1))); y1 = max(0, min(H - 1, int(y1)))
        ms = max(60, min(2000, int(ms)))
        if not self.touch_lock.acquire(blocking=False):
            return {"ok": False, "error": "touch busy (another hold in progress)"}

        def worker():
            try:
                self._fifo_write(touch_down_bytes(x0, y0))
                steps = max(4, ms // 30)
                for i in range(1, steps + 1):
                    time.sleep((ms / steps) / 1000.0)
                    fx = x0 + (x1 - x0) * i / steps
                    fy = y0 + (y1 - y0) * i / steps
                    self._fifo_write(touch_move_bytes(int(fx), int(fy)))
                self._fifo_write(touch_up_bytes())
            except OSError as e:
                with self.lock:
                    self.plog.append({"t": time.time(), "kind": "touch_error",
                                      "error": str(e)})
            finally:
                self.touch_lock.release()
        threading.Thread(target=worker, daemon=True).start()
        return {"ok": True, "x0": x0, "y0": y0, "x1": x1, "y1": y1, "ms": ms,
                "dx": x1 - x0, "dy": y1 - y0}

    def layout(self):
        """The REAL touch targets: the SIM build's ctrl `layout` (hit_test geometry);
        the reference client's known button layout as fallback."""
        with self.lock:
            kind = self.sr.client_kind if self.sr else "reference"
        if kind == "sim-build":
            r = self._ui_call({"cmd": "layout"}, timeout=2.0)
            if r.get("ok"):
                return r
        targets = [{"name": n, "x": b[0], "y": b[1], "r": b[2]}
                   for n, b in refc.BUTTONS.items()]
        targets.append({"name": "settings", "x": refc.SETTINGS_BTN[0],
                        "y": refc.SETTINGS_BTN[1], "r": refc.SETTINGS_BTN[2]})
        return {"ok": True, "page": "home", "targets": targets, "reference": True}

    def activity(self, name, via="auto"):
        """Activity button: simulated touch hold (750 ms) on the button's touch target
        (reference layout or the SIM build's ctrl `layout`) — ctrl `action`
        as fallback when the target is not found (contract hook = explicit intent)."""
        if name == "steam_stop":
            return self._ui_call({"cmd": "action", "name": "steam_stop"})
        with self.lock:
            kind = self.sr.client_kind if self.sr else "reference"
        if via == "auto":
            via = "touch"
        if via == "touch":
            if kind == "reference":
                if name not in refc.BUTTONS:
                    return {"ok": False, "error": f"unknown activity {name}"}
                bx, by, _ = refc.BUTTONS[name]
            else:
                lay = self.layout()
                tgt = next((t for t in lay.get("targets", [])
                            if t.get("name") == name), None)
                if tgt is None:
                    r = self._ui_call({"cmd": "action", "name": name})
                    r["via"] = "ctrl-action (fallback)"
                    return r
                bx, by = tgt["x"], tgt["y"]
            r = self.inject_touch(bx, by, ACTIVITY_MS)
            r["via"] = "touch-hold"
            r["at"] = [bx, by]
            return r
        r = self._ui_call({"cmd": "action", "name": name})
        r["via"] = "ctrl-action"
        return r

    def gesture(self, presses=3, mode="center"):
        """v6.12: the switcher's default gesture = CENTER hold ≥5 s (one finger, radius 40
        around (240,240)); mode='corner' tests the fallback (3×1.6 s in the corner box).
        run_e2e.run_gesture inherits os.environ and uses the sim's stream range 4095 →
        SWITCHER_RAW_MAX=4095 + SWITCHER_GESTURE are set temporarily (the switcher's
        file-mode default is the device's raw 0..480 + center gesture)."""
        with self.lock:
            sr = self.sr
        if sr is None:
            return {"ok": False, "error": "no session"}
        keys = ("SWITCHER_GESTURE", "SWITCHER_RAW_MAX")
        saved = {k: os.environ.get(k) for k in keys}
        try:
            os.environ["SWITCHER_RAW_MAX"] = "4095"
            if mode == "corner":
                os.environ["SWITCHER_GESTURE"] = "corner"
                ev = {"name": "play", "presses": presses, "x": 75, "y": 75,
                      "hold_ms": 1600, "gap_ms": 300, "inject_ui": True}
            else:
                os.environ.pop("SWITCHER_GESTURE", None)
                ev = {"name": "play", "presses": 1, "x": 240, "y": 240,
                      "hold_ms": 5200, "gap_ms": 300, "inject_ui": True}
            sr.run_gesture(ev)
            res = sr.gesture_results.get("play", {})
            self.last_gesture = res
            return {"ok": True, **res}
        except Exception as e:
            return {"ok": False, "error": str(e)}
        finally:
            for k, v in saved.items():
                if v is None:
                    os.environ.pop(k, None)
                else:
                    os.environ[k] = v

    # ------------------------------------------------------------ control API
    def fault_cycle(self):
        self.fault_idx = (self.fault_idx + 1) % len(FAULT_CYCLE)
        ev = FAULT_CYCLE[self.fault_idx]
        if ev is None:
            r = self._sim_call({"cmd": "inject", "event": {"type": "fault_clear"}})
        else:
            r = self._sim_call({"cmd": "inject", "event": {"type": "fault", **ev}})
        r["fault"] = FAULT_NAMES[self.fault_idx]
        return r

    def set_lang(self, lang):
        if lang not in ("en", "da"):
            return {"ok": False, "error": "lang must be en|da"}
        self.ui_lang = lang
        with self.lock:
            sr = self.sr
        if sr is None:
            return {"ok": False, "error": "no session"}
        lp = sr.sandbox / "new_customer" / "ourui" / "lang"
        try:
            lp.parent.mkdir(parents=True, exist_ok=True)
            tmp = lp.with_suffix(".tmp")
            tmp.write_text(lang)
            os.replace(str(tmp), str(lp))
        except OSError as e:
            return {"ok": False, "error": str(e)}
        restarted = False
        if self.ui_bin:
            # ourui loads the lang file at boot → restart the UI for a live switch
            r = self.restart_ui()
            restarted = bool(r.get("ok"))
        else:
            # the reference client has no i18n; the config language is reloaded
            self._ui_call({"cmd": "config_reload"})
        return {"ok": True, "lang": lang, "ui_restarted": restarted,
                "note": "DA is not one of stock's 11 languages — ourui's labels are "
                        "fixed Danish/English via the lang file (v6.3+)"}

    def link(self, state):
        r = self._sim_call({"cmd": "inject", "event": {"type": "link", "state": state}})
        if state == "closed":
            self.link_closed = True
        elif state == "up" and self.link_closed:
            self.link_closed = False
            # new pty after reopen → the UI must be restarted with the new path
            rr = self.restart_ui()
            r["ui_restarted"] = bool(rr.get("ok"))
        return r

    def snapshot_export(self, label="snap"):
        with self.lock:
            sr = self.sr
            state = dict(self.snapshot.get("state") or {})
        if sr is None:
            return {"ok": False, "error": "no session"}
        stamp = time.strftime("%Y%m%d-%H%M%S")
        safe = "".join(c for c in label if c.isalnum() or c in "-_")[:32] or "snap"
        out = EXPORTS / f"{stamp}_{safe}"
        out.mkdir(parents=True, exist_ok=True)
        # fb: ask the UI for a fresh dump (contract), otherwise the latest fb.bin
        dump = out / "fb.bin"
        r = self._ui_call({"cmd": "fb_dump", "path": str(dump)})
        if not r.get("ok") or not dump.exists():
            try:
                shutil.copyfile(sr.fb_path, dump)
            except OSError:
                pass
        png_path = out / "fb.png"
        try:
            raw = dump.read_bytes()
            if len(raw) >= FB_BYTES:
                png.fb2png(raw, png_path)
        except (OSError, ValueError):
            pass
        cfg_md5 = md5_file(sr.cfg_path)
        meta = {"t": time.time(), "label": safe, "session": str(sr.dir),
                "client": sr.client_kind, "config_md5": cfg_md5,
                "state": state}
        (out / "state.json").write_text(json.dumps(meta, indent=2, ensure_ascii=False))
        try:
            shutil.copyfile(sr.cfg_path, out / "config.json")
        except OSError:
            pass
        entry = {"dir": str(out), "png": str(png_path) if png_path.exists() else None,
                 "state": str(out / "state.json"), "label": safe,
                 "config_md5": cfg_md5,
                 "url": "/exports/" + out.name + "/fb.png"}
        self.exports.appendleft(entry)
        return {"ok": True, **entry}

    # ------------------------------------------------------------ sampler
    def _read_new_lines(self, path, off):
        """Binary read from a byte offset (the JSONL is UTF-8 with Danish characters)."""
        try:
            size = path.stat().st_size
            if size < off:
                off = 0
            if size == off:
                return [], off
            with open(path, "rb") as f:
                f.seek(off)
                data = f.read()
            idx = data.rfind(b"\n")     # whole lines only (avoid splitting a UTF-8 character)
            if idx < 0:
                return [], off
            chunk = data[:idx + 1]
            return chunk.decode("utf-8", "replace").splitlines(), off + idx + 1
        except OSError:
            return [], off

    def _sampler(self):
        while not self.stop.is_set():
            with self.lock:
                sr = self.sr
                gen = self.gen
            if sr is None:
                time.sleep(0.1)
                continue
            if gen != getattr(self, "_sampler_gen", -1):
                self._sampler_gen = gen
                self._state_off = 0
                self._assert_off = 0
            # state JSONL (new lines)
            lines, self._state_off = self._read_new_lines(sr.state_path, self._state_off)
            latest = self.snapshot.get("state")
            for line in lines:
                try:
                    st = json.loads(line)
                except json.JSONDecodeError:
                    continue
                latest = st
                toast = st.get("toast")
                if toast and (not self.toasts or self.toasts[0]["text"] != toast["text"]):
                    self.toasts.appendleft({"t_ms": st.get("t_ms"), **toast})
            # assertions log (protocol log)
            alines, self._assert_off = self._read_new_lines(sr.assert_path, self._assert_off)
            for line in alines:
                try:
                    self.plog.append(json.loads(line))
                except json.JSONDecodeError:
                    continue
            now = time.monotonic()
            slow = now - getattr(self, "_last_slow", 0) >= 0.5
            if slow:
                self._last_slow = now
            # process status
            ui_status, ui_exit = "running", None
            if sr.ui_proc is not None and sr.ui_proc.poll() is not None:
                ui_exit = sr.ui_proc.returncode
                ui_status = "stopped" if self.ui_intentional else "crashed"
            sim_status = "running"
            if sr.sim_proc is not None and sr.sim_proc.poll() is not None:
                sim_status = "dead"
            sim_ping = getattr(self, "_sim_ping", {})
            if slow and sim_status == "running":
                try:
                    self._sim_ping = sr.sim_ctrl.call({"cmd": "ping"}, timeout=1.0)
                    sim_ping = self._sim_ping
                except Exception as e:
                    self._sim_ping = {"ok": False, "error": str(e)}
                    sim_ping = self._sim_ping
                    if sim_status == "running":
                        sim_status = "unreachable"
            cfg_md5 = md5_file(sr.cfg_path) if slow else self.snapshot.get("config_md5")
            if slow and cfg_md5 and cfg_md5 != getattr(self, "_last_cfg_md5", None):
                if self._last_cfg_md5 is not None:
                    self.plog.append({"t": time.time(), "kind": "config_md5",
                                      "md5": cfg_md5, "file": str(sr.cfg_path)})
                self._last_cfg_md5 = cfg_md5
            lang_file = None
            try:
                lang_file = (sr.sandbox / "new_customer" / "ourui" / "lang").read_text().strip()
            except OSError:
                pass
            # real touch targets from the SIM build (ctrl layout) — cached between ticks
            if slow and sr.client_kind == "sim-build" and ui_status == "running":
                try:
                    lr = sr.ui_ctrl.call({"cmd": "layout"}, timeout=1.0)
                    if lr.get("ok"):
                        self._layout = lr
                except Exception:
                    pass
            layout = getattr(self, "_layout", None) or {}
            with self.lock:
                self.snapshot = {
                    "ok": True,
                    "session": {"run_dir": str(sr.dir), "sandbox": str(sr.sandbox),
                                "client": sr.client_kind,
                                "ui_bin": str(sr.ui_bin) if sr.ui_bin else None,
                                "scenario": self.args.scenario or "(default)",
                                "exports_dir": str(EXPORTS)},
                    "ui": {"status": ui_status, "exit_code": ui_exit,
                           "stderr_tail": list(self.ui_stderr)[-12:],
                           "kind": sr.client_kind},
                    "sim": {"status": sim_status, "ping": sim_ping,
                            "stderr_tail": list(self.sim_stderr)[-6:]},
                    "state": latest,
                    "toasts": list(self.toasts),
                    "log": list(self.plog)[-160:],
                    "config_md5": cfg_md5,
                    "ui_lang": self.ui_lang, "lang_file": lang_file,
                    "stock_langs": STOCK_LANGS,
                    "fault": FAULT_NAMES[self.fault_idx],
                    "speed": sim_ping.get("speed", 1.0),
                    "paused": sim_ping.get("paused", False),
                    "ack_profile": self.args.ack if not sim_ping.get("profile")
                    else next((k for k, v in proto.ACK_PROFILES.items()
                               if v == sim_ping["profile"]), "custom"),
                    "require_wand": sim_ping.get("require_wand", False),
                    "gesture": self.last_gesture,
                    "exports": list(self.exports),
                    "buttons": {n: list(b) for n, b in refc.BUTTONS.items()},
                    "settings_btn": list(refc.SETTINGS_BTN),
                    "targets": layout.get("targets"),
                    "layout_page": layout.get("page"),
                }
            time.sleep(0.1)

    # ------------------------------------------------------------ fb-PNG
    def _make_placeholder(self):
        rgb = bytearray(W * H * 3)
        for i in range(W * H):
            rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2] = 24, 26, 32
        png.draw_text(rgb, W, H, 130, 220, "WAITING FOR FB", (200, 200, 210), 2)
        png.draw_text(rgb, W, H, 105, 260, "SIM-PLAY MODE", (120, 130, 150), 1)
        return png.rgb_to_png_bytes(W, H, bytes(rgb))

    def fb_png(self):
        with self.lock:
            sr = self.sr
        if sr is None:
            return self.placeholder_png
        try:
            st = sr.fb_path.stat()
            key = (st.st_mtime_ns, st.st_size)
        except OSError:
            return self.placeholder_png
        with self.fb_lock:
            if key == self.fb_cache_key and self.fb_cache_png:
                return self.fb_cache_png
            try:
                raw = sr.fb_path.read_bytes()
            except OSError:
                return self.placeholder_png
            if len(raw) < FB_BYTES:
                return self.placeholder_png
            data = png.rgb_to_png_bytes(W, H, png.bgra_to_rgb(raw))
            self.fb_cache_key = key
            self.fb_cache_png = data
            return data


# ---------------------------------------------------------------- HTTP
class Handler(BaseHTTPRequestHandler):
    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (ConnectionResetError, BrokenPipeError, TimeoutError):
            pass  # mobile clients often reset the connection (reload/sleep) — ignore the noise
    play = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        pass                            # no noisy access log

    def _send(self, code, body, ctype="application/json; charset=utf-8", extra=None):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False))

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n <= 0:
            return {}
        try:
            return json.loads(self.rfile.read(n))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return {}

    def do_GET(self):
        p = urlparse(self.path)
        path = unquote(p.path)
        play = self.play
        if path in ("/", "/index.html"):
            try:
                html = VIEWER_HTML.read_text()
            except OSError:
                return self._send(500, "play_viewer.html missing", "text/plain")
            return self._send(200, html, "text/html; charset=utf-8")
        if path == "/fb.png":
            return self._send(200, play.fb_png(), "image/png")
        if path == "/api/state":
            with play.lock:
                snap = dict(play.snapshot)
            if not snap:
                snap = {"ok": False, "error": "sampler not ready yet"}
            return self._json(snap)
        if path == "/api/scenarios":
            names = sorted(q.stem for q in (HERE / "scenarios").glob("*.json"))
            return self._json({"ok": True, "scenarios": names})
        if path.startswith("/exports/"):
            rel = path[len("/exports/"):]
            target = (EXPORTS / rel).resolve()
            if not str(target).startswith(str(EXPORTS.resolve())) or not target.is_file():
                return self._send(404, "not found", "text/plain")
            ctype = "image/png" if target.suffix == ".png" else "application/json"
            return self._send(200, target.read_bytes(), ctype)
        if path == "/favicon.ico":
            return self._send(204, b"", "image/x-icon")
        return self._send(404, "not found", "text/plain")

    def do_POST(self):
        path = urlparse(self.path).path
        play = self.play
        b = self._body()
        try:
            r = self._dispatch(play, path, b)
        except Exception as e:
            r = {"ok": False, "error": str(e)}
        return self._json(r)

    def _dispatch(self, play, path, b):
        if path == "/api/touch":
            return play.inject_touch(b.get("x", 240), b.get("y", 240), b.get("ms", 80))
        if path == "/api/touch_down":
            return play.touch_down(b.get("x", 240), b.get("y", 240))
        if path == "/api/touch_move":
            return play.touch_move(b.get("x", 240), b.get("y", 240))
        if path == "/api/touch_up":
            return play.touch_up()
        if path == "/api/swipe":
            return play.inject_swipe(b.get("x0", 320), b.get("y0", 240),
                                     b.get("x1", 160), b.get("y1", 240), b.get("ms", 260))
        if path == "/api/activity":
            return play.activity(b.get("name", ""), b.get("via", "auto"))
        if path == "/api/layout":
            return play.layout()
        if path == "/api/gesture":
            return play.gesture(int(b.get("presses", 3)), b.get("mode", "center"))
        if path == "/api/fault":
            if "step" in b or not b:
                return play.fault_cycle()
            ev = {"type": "fault"}
            for k in ("e1", "e2", "e3"):
                if k in b:
                    ev[k] = int(b[k])
            if b.get("dur"):
                ev["dur"] = float(b["dur"])
            if b.get("clear"):
                ev = {"type": "fault_clear"}
            return play._sim_call({"cmd": "inject", "event": ev})
        if path == "/api/garbage":
            return play._sim_call({"cmd": "inject", "event": {
                "type": "garbage", "dur": float(b.get("dur", 2.0)),
                "crc_too": bool(b.get("crc_too", True))}})
        if path == "/api/link":
            return play.link(b.get("state", "up"))
        if path == "/api/ack":
            return play._sim_call({"cmd": "ack_profile", "profile": b.get("profile", "normal")})
        if path == "/api/speed":
            return play._sim_call({"cmd": "speed", "factor": float(b.get("factor", 1.0))})
        if path == "/api/pause":
            val = b.get("value")
            if val is None:
                val = not play.snapshot.get("paused", False)
            return play._sim_call({"cmd": "pause", "value": bool(val)})
        if path == "/api/lang":
            return play.set_lang(b.get("value", "en"))
        if path == "/api/stock_lang":
            return play._ui_call({"cmd": "save", "param": "language",
                                  "value": int(b.get("value", 0))})
        if path == "/api/boot_flag":
            val = b.get("value")
            if val is None:
                val = not (play.snapshot.get("state") or {}).get("boot_flag", False)
            return play._ui_call({"cmd": "boot_flag", "value": bool(val)})
        if path == "/api/runtime_switch":
            return play._ui_call({"cmd": "runtime_switch"})
        if path == "/api/reboot":
            return play._sim_call({"cmd": "reboot"})
        if path == "/api/wand":
            return play._sim_call({"cmd": "inject", "event": {
                "type": "wand", "on": bool(b.get("on", True))}})
        if path == "/api/require_wand":
            return play._sim_call({"cmd": "require_wand", "value": bool(b.get("value", True))})
        if path == "/api/calm":
            req = {"cmd": "calm", "factor": float(b.get("factor", 0.35))}
            if "maint" in b:
                req["maint"] = bool(b["maint"])
            return play._sim_call(req)
        if path == "/api/page":
            return play._ui_call({"cmd": "page", "name": b.get("name", "home")})
        if path == "/api/save":
            return play._ui_call({"cmd": "save", "param": b.get("param", "brew_temp"),
                                  "value": int(b.get("value", 870))})
        if path == "/api/config":
            with play.lock:
                sr = play.sr
            if sr is None:
                return {"ok": False, "error": "no session"}
            cfg = json.loads(sr.cfg_path.read_text())
            cfg[b["field"]] = b["value"]
            tmp = sr.cfg_path.with_suffix(".tmp")
            tmp.write_text(json.dumps(cfg, indent="\t", ensure_ascii=False) + "\n")
            os.replace(str(tmp), str(sr.cfg_path))
            if b.get("reload", True):
                play._ui_call({"cmd": "config_reload"})
            return {"ok": True, "field": b["field"], "value": b["value"]}
        if path == "/api/snapshot":
            return play.snapshot_export(b.get("label", "snap"))
        if path == "/api/restart_ui":
            return play.restart_ui()
        if path == "/api/reset":
            return play.reset()
        return {"ok": False, "error": f"unknown endpoint: {path}"}


# ---------------------------------------------------------------- smoke
def run_smoke(play, base):
    """Headless smoke: viewer pages + touch/API injection + state changes.
    Runs against the reference client now; the same checks run against the v6.4 binary
    once it lands (auto-detect) = full visual smoke."""
    import urllib.request
    import urllib.error
    checks = []

    def chk(name, ok, detail=""):
        checks.append((name, bool(ok)))
        print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"  [{detail}]" if detail and not ok else ""),
              flush=True)

    def get(path, timeout=10):
        with urllib.request.urlopen(base + path, timeout=timeout) as r:
            return r.status, r.read()

    def post(path, obj=None, timeout=15):
        req = urllib.request.Request(base + path, data=json.dumps(obj or {}).encode(),
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())

    def wait_state(pred, timeout_s, what):
        t0 = time.monotonic()
        last = None
        while time.monotonic() - t0 < timeout_s:
            try:
                snap = json.loads(get("/api/state")[1])
                last = snap.get("state") or {}
                if pred(last, snap):
                    return True, last
            except Exception:
                pass
            time.sleep(0.15)
        return False, last

    def wait_log(kind, timeout_s, **match):
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout_s:
            try:
                snap = json.loads(get("/api/state")[1])
                for e in snap.get("log", []):
                    if e.get("kind") == kind and all(e.get(k) == v for k, v in match.items()):
                        return True, e
            except Exception:
                pass
            time.sleep(0.2)
        return False, None

    print(f"[smoke] client: {play.sr.client_kind} · base: {base}", flush=True)
    # 1. viewer pages
    code, html = get("/")
    chk("GET / = viewer HTML", code == 200 and b"Meraki" in html)
    snap = json.loads(get("/api/state")[1])
    need = {"t_ms", "page", "phase", "work_sta", "regs", "lang", "boot_flag",
            "crc_ok", "crc_bad", "cmd6", "frame_ms"}
    st = snap.get("state") or {}
    chk("state panel: contract fields in JSONL", need <= set(st.keys()),
        f"missing={sorted(need - set(st.keys()))}")
    code, fb = get("/fb.png")
    chk("GET /fb.png = PNG", code == 200 and fb[:8] == b"\x89PNG\r\n\x1a\n" and len(fb) > 1000)
    # 2. touch via API → brew (reference: hold on BREW button; sim-build: ctrl-action)
    r = post("/api/activity", {"name": "brew"})
    chk("activity brew accepted", r.get("ok"), json.dumps(r))
    ok, st = wait_state(lambda s, _: s.get("phase") in ("PRE", "EXT"), 12, "brew phase")
    chk("BREW → phase PRE/EXT in state", ok, f"phase={st.get('phase') if st else None}")
    # the state change must also be visible directly in the JSONL file
    jsonl_ok = False
    try:
        for line in play.sr.state_path.read_text().splitlines()[-80:]:
            if json.loads(line).get("phase") in ("PRE", "EXT"):
                jsonl_ok = True
    except (OSError, json.JSONDecodeError):
        pass
    chk("brew phase visible in state.jsonl", jsonl_ok)
    code, fb2 = get("/fb.png")
    chk("fb changes during brew (PNG hash)", fb2 != fb)
    # 3. speed 8× → brew auto-stop sooner
    r = post("/api/speed", {"factor": 8})
    chk("speed 8×", r.get("ok") and r.get("speed") == 8)
    ok, e = wait_log("activity_stop", 25, activity="brew")
    chk("brew auto-stop (sim-log)", ok, "timeout")
    # 4. steam → auto-stop at the milk setpoint
    r = post("/api/activity", {"name": "steam"})
    chk("activity steam accepted", r.get("ok"), json.dumps(r))
    ok, st = wait_state(lambda s, _: s.get("phase") == "STEAM", 10, "steam phase")
    chk("STEAM phase in state", ok, f"phase={st.get('phase') if st else None}")
    ok, e = wait_log("activity_stop", 20, activity="steam")
    chk("steam auto-stop at setpoint (sim log)", ok,
        "timeout" if not ok else "")
    if ok:
        chk("steam stop reason = milk ≥ setpoint", "milk" in (e.get("reason") or ""),
            e.get("reason", ""))
    post("/api/speed", {"factor": 1})
    # 5. fault injection E1 → ERROR → clear → READY
    r = post("/api/fault", {})
    ok, st = wait_state(lambda s, _: s.get("phase") == "ERROR", 5, "ERROR")
    chk("fault cycle E1 → ERROR phase", ok and r.get("ok"), json.dumps(r))
    post("/api/fault", {})              # E2
    post("/api/fault", {})              # E3
    r = post("/api/fault", {})          # clear
    ok, st = wait_state(lambda s, _: s.get("phase") in ("READY", "HEAT"), 8, "READY")
    chk("fault clear → READY/HEAT", ok, f"phase={st.get('phase') if st else None}")
    # 6. no_ack → SAVE blocks (config unchanged) → normal → SAVE ok + md5 log
    md5_before = md5_file(play.sr.cfg_path)
    post("/api/ack", {"profile": "no_ack"})
    post("/api/save", {"param": "brew_temp", "value": 890})
    ok, st = wait_state(lambda s, _: (s.get("save") or {}).get("state") == "fail", 6, "save fail")
    chk("no_ack: SAVE fails (save=fail)", ok)
    chk("no_ack: config unchanged (md5)", md5_file(play.sr.cfg_path) == md5_before)
    post("/api/ack", {"profile": "normal"})
    post("/api/save", {"param": "brew_temp", "value": 880})
    ok, st = wait_state(lambda s, _: (s.get("save") or {}).get("state") == "ok", 6, "save ok")
    chk("normal: SAVE ok (save=ok)", ok)
    cfg = json.loads(play.sr.cfg_path.read_text())
    chk("settings SAVE: config.brewTemper=880 in sandbox", cfg.get("brewTemper") == 880)
    md5_after = md5_file(play.sr.cfg_path)
    chk("settings SAVE: config md5 changed", md5_after != md5_before)
    ok, e = wait_log("write", 3, sub=11, words=[880])
    chk("settings SAVE: MCU write sub11=[880] in log", ok)
    ok, e = wait_log("config_md5", 3)
    chk("config md5 logged in protocol log", ok)
    # 7. language file, boot flag, pause, gesture, snapshot, touch tap, reset
    r = post("/api/lang", {"value": "da"})
    lp = play.sr.sandbox / "new_customer" / "ourui" / "lang"
    chk("language switch: lang file = 'da'", r.get("ok") and lp.read_text().strip() == "da")
    flag0 = (json.loads(get("/api/state")[1]).get("state") or {}).get("boot_flag")
    post("/api/boot_flag", {})
    ok, st = wait_state(lambda s, _: s.get("boot_flag") != flag0, 4, "boot flag")
    chk("boot flag toggle in state", ok)
    r = post("/api/pause", {"value": True})
    chk("pause sim time", r.get("ok") and r.get("paused"))
    r = post("/api/pause", {"value": False})
    chk("resume sim time", r.get("ok") and not r.get("paused"))
    if run_e2e.SWITCHER_HOST.exists():
        r = post("/api/gesture", {"mode": "center"})
        chk("gesture: center hold 5.2 s triggers switcher (v6.12)", r.get("ran") and r.get("trigger"),
            json.dumps(r)[:200])
        r = post("/api/gesture", {"presses": 3, "mode": "corner"})
        chk("gesture: corner fallback 3× corner hold triggers switcher", r.get("ran") and r.get("trigger"),
            json.dumps(r)[:200])
    else:
        chk("gesture: switcher_host not built (skipped)", True)
    r = post("/api/touch", {"x": 240, "y": 240, "ms": 80})
    chk("touch tap via API (raw mapping)", r.get("ok") and r.get("raw_x") == 2048,
        json.dumps(r))
    r = post("/api/snapshot", {"label": "smoke"})
    chk("snapshot export (PNG+state)", r.get("ok") and r.get("png")
        and Path(r["png"]).exists() and Path(r["state"]).exists(), json.dumps(r)[:200])
    r = post("/api/reset")
    ok, st = wait_state(lambda s, _: s.get("t_ms", 1e9) < 8000, 10, "fresh session")
    chk("reset sandbox → fresh session", r.get("ok") and ok)
    npass = sum(1 for _, ok in checks if ok)
    print(f"[smoke] {npass}/{len(checks)} PASS · client={play.sr.client_kind}", flush=True)
    return 0 if npass == len(checks) else 1


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description="Interactive play mode for the ourui SIM environment")
    ap.add_argument("--ui-bin", help="ourui SIM build (auto-detected otherwise)")
    ap.add_argument("--force-reference", action="store_true",
                    help="run against ref_client even if a SIM binary exists")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--calm", action="store_true",
                    help="start with EMA-smoothed sensor values (smooth graph); can be toggled in the viewer")
    ap.add_argument("--host", default="127.0.0.1",
                    help="bind address; use 0.0.0.0 to reach the viewer from other machines on the LAN")
    ap.add_argument("--lang", choices=["en", "da"], default="da",
                    help="UI language (ourui's lang file; default da)")
    ap.add_argument("--scenario", help="scenario name: uses its config/sim part "
                    "(not the timeline) as the starting point")
    ap.add_argument("--ack", default="normal",
                    choices=list(proto.ACK_PROFILES), help="ACK profile at start")
    ap.add_argument("--require-wand", action="store_true",
                    help="require wand contact (PE.2) for steam (default: no in play)")
    ap.add_argument("--keep", action="store_true",
                    help="keep sandbox/run dir on exit (debugging)")
    ap.add_argument("--no-browser", action="store_true", help="do not open a browser automatically")
    ap.add_argument("--smoke", action="store_true",
                    help="headless smoke test (viewer + API + state changes) and exit")
    args = ap.parse_args()

    play = Play(args)
    play.start()
    sampler = threading.Thread(target=play._sampler, daemon=True)
    sampler.start()
    # wait for the first state snapshot
    for _ in range(100):
        if play.snapshot.get("state"):
            break
        time.sleep(0.05)
    if args.calm:
        play._sim_call({"cmd": "calm", "factor": 0.30, "maint": False})

    Handler.play = play
    try:
        httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    except OSError as e:
        print(f"[play] cannot bind port {args.port}: {e}", file=sys.stderr)
        play.cleanup()
        return 2
    httpd.daemon_threads = True
    port = httpd.server_address[1]
    base = f"http://127.0.0.1:{port}"
    print(f"[play] viewer: {base}  (Ctrl-C stops and cleans up)", flush=True)
    if args.host == "0.0.0.0":
        print(f"[play] LAN: http://<this-machine-ip>:{port}  (bound on all interfaces)", flush=True)

    rc = 0
    if args.smoke:
        srv_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
        srv_thread.start()
        try:
            rc = run_smoke(play, base)
        except Exception as e:
            import traceback
            traceback.print_exc()
            print(f"[smoke] ERROR: {e}", flush=True)
            rc = 1
        finally:
            httpd.shutdown()
            httpd.server_close()
            play.cleanup()
        return rc

    if not args.no_browser:
        try:
            webbrowser.open(base)
        except Exception:
            pass

    def on_sigterm(*_):
        raise SystemExit(0)
    signal.signal(signal.SIGTERM, on_sigterm)

    def on_sigint(*_):
        # explicit handler: background jobs in non-interactive shells inherit
        # SIG_IGN for SIGINT — otherwise Ctrl-C/kill -INT would not clean up
        raise KeyboardInterrupt
    signal.signal(signal.SIGINT, on_sigint)
    try:
        httpd.serve_forever()
    except (KeyboardInterrupt, SystemExit):
        pass
    finally:
        httpd.server_close()
        play.cleanup()
    return rc


if __name__ == "__main__":
    sys.exit(main())