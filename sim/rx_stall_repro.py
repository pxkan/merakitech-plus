#!/usr/bin/env python3
"""rx_stall_repro.py — reproduces the rx_feed stall on a bit error in the length byte.

Finding (UI_v6.md v6.4, code reading): merakiplus's rx_feed sees `AA 55` + a length field
L > REGN (129) in a non-special frame and breaks out WITHOUT advancing
(`if(L>REGN||i+tot>rxlen) break;`). The parser stalls until the 8 kB buffer
overflows (~3.5 s at normal poll traffic) — the UI shows OFFLINE meanwhile and
recovers on its own afterwards. Requires a bit error in exactly the length byte;
the crc_garbage scenario does not hit it.

Repro: mcu_sim injects one 0x83 frame with the length byte corrupted (bit7 set,
L: 7 → 135 > 129; the CRC is still valid for the original length).
The injection is a new sim-side inject type: {"type": "bad_len", "sub": 33, "len": 7}.

Measurement (per client):
  * t_inj      : injection time (sim_assertions.jsonl)
  * t_offline  : first state line with link_up=false / phase=OFFLINE after t_inj
  * t_recover  : first state line where crc_ok rises again (first parsed frame)
  * stall      : t_recover - t_inj

Status:
  * V6.4 binary (watchdog not landed): the stall is expected to reproduce (~3.5 s,
    documented bound = 8 kB buffer overflow) → the check is marked PENDING-V6.5.
  * V6.5+ with resync watchdog: recovery within the documented bound
    (merakiplus.c: an impossible length is discarded IMMEDIATELY; incomplete candidates
    after RX_RESYNC_MS = 400 ms < ACK budget 450 ms < OFFLINE 1500 ms)
    → VERIFIED. The check uses 1.0 s as a conservative upper limit (400 ms
    watchdog + poll cadence ~100 ms + measurement uncertainty).

Note: stall_s is an UPPER bound on the recovery time: the measurement waits for
a crc_ok rise after a 0.6 s plateau window (in-flight frames may complete
right after the injection). Fast recovery (<0.6 s) is therefore reported as ~0.6-0.7 s.

Local run only (no device contact). Usage:
  python3 sim/rx_stall_repro.py                     # merakiplus SIM build + reference
  python3 sim/rx_stall_repro.py --only ui           # merakiplus only
  python3 sim/rx_stall_repro.py --run-name rx_stall_final
"""
import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_e2e
from run_e2e import ScenarioRun, find_ui_bin

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
V64_BOUND_S = 6.0        # upper tolerance for the 8 kB overflow stall (~3.5 s measured in v6.4)
V65_WATCHDOG_BOUND_S = 1.0   # conservative check limit; documented bound in the source:
                             # impossible length discarded immediately, RX_RESYNC_MS = 400 ms
LINK_TIMEOUT_S = 1.5

SCEN = {
    "name": "rx_stall",
    "description": "Bit error in length byte (L>REGN): rx_feed stall → OFFLINE → recovery",
    "duration_s": 16,
    "events": [
        {"t": 4, "target": "sim", "event": {"type": "bad_len", "sub": 33, "len": 7}},
        {"t": 3.5, "target": "runner", "op": "snapshot", "label": "before"},
        {"t": 6.5, "target": "runner", "op": "snapshot", "label": "stall"},
        {"t": 12, "target": "runner", "op": "snapshot", "label": "recovered"},
    ],
    "expect": [],
    "universal": {k: False for k in
                  ("circle", "contract", "no_reboot", "no_calib", "no_band_rejects",
                   "ranges", "keepawake", "boot_push", "exit0", "no_cmd6_fail")},
}


def analyze(sr):
    """Find injection, OFFLINE and recovery in the state stream.

    NB: crc_ok may rise once right after the injection (a frame that was already
    partially received completes before the parser reaches the corrupt header).
    Recovery is therefore measured as the first crc_ok rise AFTER the plateau window
    (0.6 s) — not the first rise at all."""
    inj = next((e for e in sr.sim_log if e["kind"] == "bad_len_inject"), None)
    if inj is None:
        return {"error": "no bad_len_inject in the sim log"}
    t_inj = inj["t"]
    c0 = None
    t_offline = t_recover = t_link_back = None
    for (t, st) in sr.samples:
        if t < t_inj:
            c0 = st.get("crc_ok", 0)
            continue
        if c0 is None:
            c0 = st.get("crc_ok", 0)
        if t_offline is None and (st.get("phase") == "OFFLINE"
                                  or not st.get("link_up", True)):
            t_offline = t
        if t_offline is not None and t_link_back is None and st.get("link_up", False):
            t_link_back = t
    # plateau = highest crc_ok in [t_inj, t_inj+0.6] (in-flight frames complete)
    plateau = max((st.get("crc_ok", 0) for (t, st) in sr.samples
                   if t_inj <= t <= t_inj + 0.6), default=c0 or 0)
    for (t, st) in sr.samples:
        if t > t_inj + 0.6 and st.get("crc_ok", 0) > plateau:
            t_recover = t
            break
    last = sr.samples[-1][1] if sr.samples else {}
    resync0 = next((st.get("rx_resync", 0) for (t, st) in reversed(sr.samples)
                    if t < t_inj), 0)
    return {
        "t_inject": t_inj,
        "len_byte": inj.get("len_byte"),
        "crc_ok_at_inject": c0,
        "crc_ok_plateau": plateau,
        "t_offline": t_offline,
        "t_recover": t_recover,
        "t_link_back": t_link_back,
        "stall_s": round(t_recover - t_inj, 3) if t_recover is not None else None,
        "offline_after_inject_s": round(t_offline - t_inj, 3) if t_offline is not None else None,
        "version": last.get("version"),
        "client": last.get("client"),
        "crc_ok_final": last.get("crc_ok"),
        "crc_bad_final": last.get("crc_bad"),
        "rx_resync_at_inject": resync0,
        "rx_resync_final": last.get("rx_resync"),
        "phase_final": last.get("phase"),
        "link_up_final": last.get("link_up"),
    }


def verdict(a, client_kind):
    """(status, checks[]) — pending-v6.5 until the resync watchdog has landed."""
    checks = []

    def chk(name, ok, detail=""):
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    if "error" in a:
        chk("repro", False, a["error"])
        return "error", checks
    ver = a.get("version") or ("V6.4" if client_kind == "sim-build" else "reference")
    is_v65 = ver not in ("V6.4", "reference", None) and ver >= "V6.5"
    chk("bad_len injected (L>REGN)", a.get("len_byte", 0) > 129,
        f"len_byte={a.get('len_byte')}")
    chk("link recovers before end of run",
        a.get("t_recover") is not None and a.get("link_up_final") is True,
        f"t_recover={a.get('t_recover')} link_up_final={a.get('link_up_final')}")
    if client_kind == "reference":
        # The Python parser (proto.FrameParser) waits for 2*L+7 bytes and then fails
        # CRC → short stall (~0.2 s), not 8 kB overflow. For comparison only.
        chk("reference parser: short stall (<1 s)",
            a.get("stall_s") is not None and a["stall_s"] < 1.0,
            f"stall={a.get('stall_s')} s")
        return "reference", checks
    if not is_v65:
        chk("v6.4: stall reproduced (>1.5 s = OFFLINE reached)",
            a.get("stall_s") is not None and a["stall_s"] > LINK_TIMEOUT_S,
            f"stall={a.get('stall_s')} s")
        chk("v6.4: OFFLINE shown during the stall",
            a.get("t_offline") is not None,
            f"offline after {a.get('offline_after_inject_s')} s (link timeout 1.5 s)")
        chk("v6.4: recovery within documented bound (8 kB overflow, ~3.5 s)",
            a.get("stall_s") is not None and a["stall_s"] <= V64_BOUND_S,
            f"stall={a.get('stall_s')} s ≤ {V64_BOUND_S} s")
        chk("v6.5 resync watchdog: recovery < 1.0 s (documented bound: "
            "immediate discard + RX_RESYNC_MS 400 ms)",
            False, "PENDING-V6.5: watchdog not landed in the binary "
                   f"(version={ver}); rerun once Fable's v6.5 is built")
        return "pending-v6.5", checks
    chk("v6.5: recovery within documented bound (immediate discard of L>REGN; "
        "watchdog 400 ms; check limit 1.0 s)",
        a.get("stall_s") is not None and a["stall_s"] <= V65_WATCHDOG_BOUND_S,
        f"stall≤{a.get('stall_s')} s (upper bound; plateau window 0.6 s)")
    chk("v6.5: OFFLINE never shown (recovery < link timeout 1.5 s)",
        a.get("t_offline") is None,
        f"t_offline={a.get('t_offline')}")
    if a.get("rx_resync_final") is not None:
        chk("v6.5: rx_resync counts discarded candidate (≥1 after injection)",
            a["rx_resync_final"] >= a.get("rx_resync_at_inject", 0) + 1,
            f"rx_resync {a.get('rx_resync_at_inject')} → {a['rx_resync_final']}")
    ok = all(c["ok"] for c in checks)
    return ("verified" if ok else "failed"), checks


def run_one(ui_bin, run_dir, label):
    sr = ScenarioRun(dict(SCEN), run_dir, ui_bin=ui_bin)
    try:
        sr.make_sandbox()
        sr.start_sim()
        sr.start_ui()
        sr.sample_loop()
        sr.finish()
    except Exception:
        for proc in (sr.ui_proc, sr.sim_proc):
            if proc and proc.poll() is None:
                proc.terminate()
        raise
    a = analyze(sr)
    status, checks = verdict(a, sr.client_kind)
    return {"label": label, "client": sr.client_kind,
            "ui_bin": str(ui_bin) if ui_bin else None,
            "status": status, "analysis": a, "checks": checks,
            "report_dir": str(sr.dir)}


def main():
    ap = argparse.ArgumentParser(description="rx_feed stall repro (bit error in length byte)")
    ap.add_argument("--only", choices=["ui", "ref"], help="only one client")
    ap.add_argument("--ui-bin", help="merakiplus SIM build (default: auto-detect)")
    ap.add_argument("--reports-dir", default=str(HERE / "reports" / "v6.5-scenarios"))
    ap.add_argument("--run-name", default="rx_stall-" + time.strftime("%Y%m%d-%H%M%S"))
    args = ap.parse_args()
    run_dir = Path(args.reports_dir) / args.run_name
    run_dir.mkdir(parents=True, exist_ok=True)
    results = []
    if args.only != "ref":
        ui_bin = find_ui_bin(args.ui_bin)
        if ui_bin is None:
            print("no merakiplus SIM build found — skipping the ui part", file=sys.stderr)
        else:
            print(f"[rx_stall] merakiplus: {ui_bin}", flush=True)
            results.append(run_one(ui_bin, run_dir / "ui", "merakiplus"))
    if args.only != "ui":
        print("[rx_stall] reference: sim/ref_client.py", flush=True)
        results.append(run_one(None, run_dir / "ref", "reference"))
    lines = ["# rx_feed stall — bit error in the length byte (repro)", "",
             f"Generated {time.strftime('%Y-%m-%d %H:%M:%S')} by sim/rx_stall_repro.py. "
             "Local run only; no device contact.", "",
             "Finding: UI_v6.md v6.4 (code reading) — `rx_feed` stalls on L>REGN "
             "until the 8 kB buffer overflows (~3.5 s). Repro: mcu_sim inject "
             "`{\"type\":\"bad_len\",\"sub\":33,\"len\":7}` (LEN bit 7 set: 7→135).", ""]
    overall_ok = True
    for r in results:
        a = r["analysis"]
        lines += [f"## {r['label']} ({r['client']}) — status: **{r['status']}**", "",
                  f"- version: {a.get('version')} · injection t={a.get('t_inject')} s "
                  f"(len_byte={a.get('len_byte')})",
                  f"- stall: **{a.get('stall_s')} s** · OFFLINE after "
                  f"{a.get('offline_after_inject_s')} s · link back t={a.get('t_link_back')}",
                  f"- crc_ok at injection: {a.get('crc_ok_at_inject')} → "
                  f"{a.get('crc_ok_final')} · crc_bad: {a.get('crc_bad_final')} · "
                  f"rx_resync: {a.get('rx_resync_at_inject')} → {a.get('rx_resync_final')} · "
                  f"final phase: {a.get('phase_final')}", "",
                  "| Check | Result | Detail |", "|---|---|---|"]
        for c in r["checks"]:
            lines.append(f"| {c['name']} | {'✅' if c['ok'] else '❌'} | {c['detail']} |")
            if not c["ok"] and "PENDING" not in c["detail"]:
                overall_ok = False
        lines += ["", f"Details: [{r['label']}/report.json]({r['label']}/report.json) · "
                  f"[state]({r['label']}/rx_stall/state.jsonl)", ""]
    lines += ["## Documented bound (UI_v6.md CHANGES-v6.5 item 3)", "",
              f"- **v6.4 (baseline):** stall until 8 kB overflow, documented ~3.5 s "
              f"(tolerance ≤{V64_BOUND_S} s on host). OFFLINE is shown after 1.5 s. "
              "Measured baseline in this repro: see rx_stall_v64_baseline.md "
              "(Fable's independent baseline against the v6.4 binary: 3.98 s).",
              f"- **v6.5 (landed):** resync watchdog (merakiplus.c rx_feed): "
              f"(1) impossible length (L>REGN) discarded IMMEDIATELY (0 ms — same call); "
              f"(2) incomplete candidate discarded after RX_RESYNC_MS = 400 ms "
              f"(< ACK budget 450 ms < OFFLINE 1500 ms — 'a resync can never "
              f"show OFFLINE'). Check limit: {V65_WATCHDOG_BOUND_S} s "
              "(conservative; stall_s is an upper bound due to the 0.6 s plateau window "
              "in the measurement). The rx_resync counter (DIAGNOSE 'RX CRC / RESYNC') "
              "must count the discarded candidate.", ""]
    (run_dir / "rx_stall_report.md").write_text("\n".join(lines))
    (run_dir / "rx_stall_report.json").write_text(json.dumps(
        {"run": run_dir.name, "results": results}, indent=2, ensure_ascii=False))
    print("\n".join(lines))
    print(f"[rx_stall] report: {run_dir}/rx_stall_report.md")
    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main())