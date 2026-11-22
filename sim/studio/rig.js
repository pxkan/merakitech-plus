// sim/studio/rig.js — one simulated machine: a Meraki+ UI (WebAssembly) wired to a JS machine model over a virtual UART,
// on ONE clock that the host owns. The studio runs one rig (or two side by side in compare mode); smoke.mjs runs one headless.
//
//   const rig = await Rig.create({ module, config, spec, Machine, seed });
//   rig.advance(16.7);            // advance sim time (ms): interleaves machine ticks, UART bytes and UI loop iterations
//   rig.ui.touch(x, y, 'down');   rig.machine.setWand(true);   rig.ui.ctrl({cmd:'layout'})
import { MerakiUI } from './meraki.js';

export class Rig {
  static async create(o) {
    const ui = await MerakiUI.create(o.module, { config: o.config, lang: o.lang, files: o.files, name: o.name, log: o.log, wallMs: o.wallMs });
    const machine = new o.Machine({ config: o.config, spec: o.spec, seed: o.seed ?? 1, params: o.params || {} });
    return new Rig(o.name || 'ui', ui, machine);
  }

  constructor(name, ui, machine) {
    this.name = name;
    this.ui = ui;
    this.machine = machine;
    this.t = 0;                    // ms of sim time since the rig started
    this.uiBase = ui.nowMs;        // the UI's monotonic clock at t = 0 (its "uptime")
    this.frames = 0;               // fb changes
    this.steps = 0;
    this.stepMs = 2;               // the device's loop sleeps 2 ms per iteration
    this.uartLog = [];             // {t, dir:'rx'|'tx', f} decoded frames (capped); dir from the MCU's view as in machine.js
    this.onFrame = null;
    machine.onFrame = (dir, f) => {
      this.uartLog.push({ dir, f });
      if (this.uartLog.length > 400) this.uartLog.splice(0, this.uartLog.length - 400);
      if (this.onFrame) this.onFrame(dir, f);
    };
    ui.setTime(this.uiBase);
    ui.init();
  }

  get stopped() { return this.ui.stopped; }

  // Advance both sides by dtMs of sim time. The UI iterates every stepMs (2 ms like the device; coarser at high speed —
  // its timers are deadline based, so it renders when due, not per iteration); the machine model ticks in <= 10 ms steps.
  advance(dtMs, stepMs = this.stepMs) {
    let frames = 0;
    const end = this.t + dtMs;
    while (this.t < end - 1e-9 && !this.ui.stopped) {
      const h = Math.min(stepMs, end - this.t);
      this.t += h;
      this.machine.advance(h / 1000);
      const toUi = this.machine.takeTx();
      if (toUi.length) this.ui.uartRx(toUi);
      this.ui.setTime(this.uiBase + this.t);
      const r = this.ui.step();
      this.steps++;
      if (r.frame) { frames++; this.frames++; }
      const toMcu = this.ui.uartTx();
      if (toMcu.length) this.machine.rx(toMcu);
    }
    return frames;
  }

  // run until pred(rig) is true or maxMs passes; returns true if pred became true
  runUntil(pred, maxMs, chunkMs = 20) {
    const end = this.t + maxMs;
    while (this.t < end) { this.advance(chunkMs); if (pred(this)) return true; }
    return false;
  }

  // the REAL tap target for a name from the UI's layout hook (x, y = centre in screen px), or null
  target(name) {
    const L = this.ui.ctrl({ cmd: 'layout' });
    return (L.targets || []).find(t => t.name === name) || null;
  }
}
