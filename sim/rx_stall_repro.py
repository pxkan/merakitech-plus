#!/usr/bin/env python3
"""rx_stall_repro.py — reproducerer rx_feed-stallet ved bit-fejl i længde-byten.

Fund (UI_v6.md v6.4, kode-læsning): ourui's rx_feed ser `AA 55` + et længde-felt
L > REGN (129) i en ikke-special-frame og bryder ud UDEN at rykke frem
(`if(L>REGN||i+tot>rxlen) break;`). Parseren står stille, til 8 kB-bufferen
løber over (~3,5 s ved normal poll-trafik) — UI'et viser OFFLINE undervejs og
kommer sig selv bagefter. Kræver en bit-fejl i netop længde-byten; crc_garbage-
scenariet rammer det ikke.

Repro: mcu_sim injicerer én 0x83-frame med længde-byten korrumperet (bit7 sat,
L: 7 → 135 > 129; CRC'en er stadig gyldig for den oprindelige længde).
Injektionen er en ny sim-side inject-type: {"type": "bad_len", "sub": 33, "len": 7}.

Måling (pr. klient):
  * t_inj      : injektionstidspunktet (sim_assertions.jsonl)
  * t_offline  : første state-linje med link_up=false / phase=OFFLINE efter t_inj
  * t_recover  : første state-linje hvor crc_ok igen stiger (første parsede frame)
  * stall      : t_recover - t_inj

Status:
  * V6.4-binær (watchdog ikke landet): stallet forventes reproduceret (~3,5 s,
    dokumenteret bound = 8 kB-buffer-overflow) → checket markeres PENDING-V6.5.
  * V6.5+ med resync-watchdog: recovery inden for den dokumenterede bound
    (ourui_v6.c: umulig længde forkastes STRAKS; ufuldstændige kandidater
    efter RX_RESYNC_MS = 400 ms < ACK-budget 450 ms < OFFLINE 1500 ms)
    → VERIFIED. Checket bruger 1,0 s som konservativ øvre grænse (400 ms
    watchdog + poll-kadence ~100 ms + måle-usikkerhed).

Bemærk: stall_s er en ØVRE grænse for recovery-tiden: målingen venter på
crc_ok-stigning efter et 0,6 s plateau-vindue (in-flight frames kan fuldendes
lige efter injektionen). Hurtig recovery (<0,6 s) rapporteres derfor som ~0,6-0,7 s.

Kun lokal kørsel (ingen enhedskontakt). Brug:
  python3 sim/rx_stall_repro.py                     # ourui SIM-build + reference
  python3 sim/rx_stall_repro.py --only ui           # kun ourui
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
V64_BOUND_S = 6.0        # øvre tolerance for 8 kB-overflow-stallet (~3,5 s målt i v6.4)
V65_WATCHDOG_BOUND_S = 1.0   # konservativ check-grænse; dokumenteret bound i kilden:
                             # umulig længde forkastes straks, RX_RESYNC_MS = 400 ms
LINK_TIMEOUT_S = 1.5

SCEN = {
    "name": "rx_stall",
    "description": "Bit-fejl i længde-byte (L>REGN): rx_feed-stall → OFFLINE → recovery",
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
    """Find injektion, OFFLINE og recovery i state-strømmen.

    NB: crc_ok kan stige én gang lige efter injektionen (en frame der allerede
    var delvist modtaget, fuldendes før parseren når den korrupte header).
    Recovery måles derfor som første crc_ok-stigning EFTER plateau-vinduet
    (0,6 s) — ikke den første stigning overhovedet."""
    inj = next((e for e in sr.sim_log if e["kind"] == "bad_len_inject"), None)
    if inj is None:
        return {"error": "ingen bad_len_inject i sim-loggen"}
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
    # plateau = højeste crc_ok i [t_inj, t_inj+0,6] (in-flight frames fuldendes)
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
    """(status, checks[]) — pending-v6.5 indtil resync-watchdog'en er landet."""
    checks = []

    def chk(name, ok, detail=""):
        checks.append({"name": name, "ok": bool(ok), "detail": detail})

    if "error" in a:
        chk("repro", False, a["error"])
        return "error", checks
    ver = a.get("version") or ("V6.4" if client_kind == "sim-build" else "reference")
    is_v65 = ver not in ("V6.4", "reference", None) and ver >= "V6.5"
    chk("bad_len injiceret (L>REGN)", a.get("len_byte", 0) > 129,
        f"len_byte={a.get('len_byte')}")
    chk("link recoverer før kørselens slutning",
        a.get("t_recover") is not None and a.get("link_up_final") is True,
        f"t_recover={a.get('t_recover')} link_up_final={a.get('link_up_final')}")
    if client_kind == "reference":
        # Python-parseren (proto.FrameParser) venter på 2*L+7 bytes og CRC-fejler
        # derefter → kort stall (~0,2 s), ikke 8 kB-overflow. Kun til sammenligning.
        chk("reference-parser: kort stall (<1 s)",
            a.get("stall_s") is not None and a["stall_s"] < 1.0,
            f"stall={a.get('stall_s')} s")
        return "reference", checks
    if not is_v65:
        chk("v6.4: stall reproduceret (>1,5 s = OFFLINE nået)",
            a.get("stall_s") is not None and a["stall_s"] > LINK_TIMEOUT_S,
            f"stall={a.get('stall_s')} s")
        chk("v6.4: OFFLINE vist under stallet",
            a.get("t_offline") is not None,
            f"offline efter {a.get('offline_after_inject_s')} s (link-timeout 1,5 s)")
        chk("v6.4: recovery inden for dokumenteret bound (8 kB-overflow, ~3,5 s)",
            a.get("stall_s") is not None and a["stall_s"] <= V64_BOUND_S,
            f"stall={a.get('stall_s')} s ≤ {V64_BOUND_S} s")
        chk("v6.5 resync-watchdog: recovery < 1,0 s (dokumenteret bound: "
            "straks-forkast + RX_RESYNC_MS 400 ms)",
            False, "PENDING-V6.5: watchdog ikke landet i binæren "
                   f"(version={ver}); kør igen når Fable's v6.5 er bygget")
        return "pending-v6.5", checks
    chk("v6.5: recovery inden for dokumenteret bound (straks-forkast af L>REGN; "
        "watchdog 400 ms; check-grænse 1,0 s)",
        a.get("stall_s") is not None and a["stall_s"] <= V65_WATCHDOG_BOUND_S,
        f"stall≤{a.get('stall_s')} s (øvre grænse; plateau-vindue 0,6 s)")
    chk("v6.5: OFFLINE aldrig vist (recovery < link-timeout 1,5 s)",
        a.get("t_offline") is None,
        f"t_offline={a.get('t_offline')}")
    if a.get("rx_resync_final") is not None:
        chk("v6.5: rx_resync tæller forkastet kandidat (≥1 efter injektion)",
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
    ap = argparse.ArgumentParser(description="rx_feed-stall repro (bit-fejl i længde-byte)")
    ap.add_argument("--only", choices=["ui", "ref"], help="kun én klient")
    ap.add_argument("--ui-bin", help="ourui SIM-build (default: auto-detekt)")
    ap.add_argument("--reports-dir", default=str(HERE / "reports" / "v6.5-scenarios"))
    ap.add_argument("--run-name", default="rx_stall-" + time.strftime("%Y%m%d-%H%M%S"))
    args = ap.parse_args()
    run_dir = Path(args.reports_dir) / args.run_name
    run_dir.mkdir(parents=True, exist_ok=True)
    results = []
    if args.only != "ref":
        ui_bin = find_ui_bin(args.ui_bin)
        if ui_bin is None:
            print("intet ourui SIM-build fundet — springer ui-delen over", file=sys.stderr)
        else:
            print(f"[rx_stall] ourui: {ui_bin}", flush=True)
            results.append(run_one(ui_bin, run_dir / "ui", "ourui"))
    if args.only != "ui":
        print("[rx_stall] reference: sim/ref_client.py", flush=True)
        results.append(run_one(None, run_dir / "ref", "reference"))
    lines = ["# rx_feed-stall — bit-fejl i længde-byten (repro)", "",
             f"Genereret {time.strftime('%Y-%m-%d %H:%M:%S')} af sim/rx_stall_repro.py. "
             "Kun lokal kørsel; ingen enhedskontakt.", "",
             "Fund: UI_v6.md v6.4 (kode-læsning) — `rx_feed` står stille ved L>REGN "
             "til 8 kB-bufferen løber over (~3,5 s). Repro: mcu_sim inject "
             "`{\"type\":\"bad_len\",\"sub\":33,\"len\":7}` (LEN-bit 7 sat: 7→135).", ""]
    overall_ok = True
    for r in results:
        a = r["analysis"]
        lines += [f"## {r['label']} ({r['client']}) — status: **{r['status']}**", "",
                  f"- version: {a.get('version')} · injektion t={a.get('t_inject')} s "
                  f"(len_byte={a.get('len_byte')})",
                  f"- stall: **{a.get('stall_s')} s** · OFFLINE efter "
                  f"{a.get('offline_after_inject_s')} s · link tilbage t={a.get('t_link_back')}",
                  f"- crc_ok ved injektion: {a.get('crc_ok_at_inject')} → "
                  f"{a.get('crc_ok_final')} · crc_bad: {a.get('crc_bad_final')} · "
                  f"rx_resync: {a.get('rx_resync_at_inject')} → {a.get('rx_resync_final')} · "
                  f"slut-fase: {a.get('phase_final')}", "",
                  "| Check | Resultat | Detalje |", "|---|---|---|"]
        for c in r["checks"]:
            lines.append(f"| {c['name']} | {'✅' if c['ok'] else '❌'} | {c['detail']} |")
            if not c["ok"] and "PENDING" not in c["detail"]:
                overall_ok = False
        lines += ["", f"Detaljer: [{r['label']}/report.json]({r['label']}/report.json) · "
                  f"[state]({r['label']}/rx_stall/state.jsonl)", ""]
    lines += ["## Dokumenteret bound (UI_v6.md CHANGES-v6.5 pkt. 3)", "",
              f"- **v6.4 (baseline):** stall til 8 kB-overflow, dokumenteret ~3,5 s "
              f"(tolerance ≤{V64_BOUND_S} s på vært). OFFLINE vises efter 1,5 s. "
              "Målt baseline i denne repro: se rx_stall_v64_baseline.md "
              "(Fable's uafhængige baseline mod v6.4-binæren: 3,98 s).",
              f"- **v6.5 (landet):** resync-watchdog (ourui_v6.c rx_feed): "
              f"(1) umulig længde (L>REGN) forkastes STRAKS (0 ms — samme kald); "
              f"(2) ufuldstændig kandidat forkastes efter RX_RESYNC_MS = 400 ms "
              f"(< ACK-budget 450 ms < OFFLINE 1500 ms — 'en resync kan aldrig "
              f"vise OFFLINE'). Check-grænse: {V65_WATCHDOG_BOUND_S} s "
              "(konservativ; stall_s er en øvre grænse pga. 0,6 s plateau-vindue "
              "i målingen). rx_resync-tælleren (DIAGNOSE 'RX CRC / RESYNC') "
              "skal tælle den forkastede kandidat.", ""]
    (run_dir / "rx_stall_report.md").write_text("\n".join(lines))
    (run_dir / "rx_stall_report.json").write_text(json.dumps(
        {"run": run_dir.name, "results": results}, indent=2, ensure_ascii=False))
    print("\n".join(lines))
    print(f"[rx_stall] rapport: {run_dir}/rx_stall_report.md")
    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main())