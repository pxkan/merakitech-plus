#!/usr/bin/env node
// sim/studio/smoke.mjs — headless end-to-end smoke of Sim Studio (node >= 18, no npm dependencies).
//
//   python3 sim/studio/build.py --no-classic && node sim/studio/smoke.mjs [--wasm build/studio/wasm/current.wasm]
//
// Loads the Meraki+ UI (WebAssembly) and the JS machine model, boots them on one clock, waits for READY (link up, boot push
// ACKed), HOLDS BREW with mp_touch on the real hit target from the UI's `layout` hook, runs the shot to the machine's
// auto-stop and asserts the state JSON's phases and weights. Then it measures ms per UI step / per rendered frame.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { Rig } from './rig.js';
import { Machine } from './machine.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');
const arg = (k, d) => { const i = process.argv.indexOf(k); return i > 0 ? process.argv[i + 1] : d; };
const wasmPath = path.resolve(arg('--wasm', path.join(ROOT, 'build', 'studio', 'wasm', 'current.wasm')));
if (!fs.existsSync(wasmPath)) { console.error(`smoke: ${wasmPath} missing — run: python3 sim/studio/build.py --no-classic`); process.exit(2); }

let fails = 0;
const check = (name, ok, extra = '') => { console.log(`  [${ok ? 'OK' : 'FAIL'}]  ${name}${extra ? '  — ' + extra : ''}`); if (!ok) fails++; };

const config = JSON.parse(fs.readFileSync(path.join(ROOT, 'sim', 'fixtures', 'config.json'), 'utf8'));
const spec = JSON.parse(fs.readFileSync(path.join(ROOT, 'sim', 'fixtures', 'parity_spec.json'), 'utf8'));
const module = await WebAssembly.compile(fs.readFileSync(wasmPath));
console.log(`smoke: ${path.relative(ROOT, wasmPath)} (${(fs.statSync(wasmPath).size / 1024).toFixed(1)} KiB)`);

const rig = await Rig.create({ module, config, spec, Machine, seed: 1, name: 'smoke' });
const st = () => rig.ui.state(true);
rig.machine.preheat();                     // boilers + group at their setpoints (heat stages 100)

// 1. boot: link up, boot push done, READY
const ready = rig.runUntil(() => { const s = st(); return s && s.link_up && s.phase === 'READY' && s.boot_pushed && s.view_raw === 'main'; }, 15000, 50);
let s = st();
check('boots to READY with the link up and the boot push ACKed', ready, `phase ${s.phase}, link ${s.link_up}, boot_push ${JSON.stringify(s.boot_push)}`);
check('UI is on HOME', s.page === 'home', s.page);
check('version handshake done (0x89 answered)', rig.machine.snapshot().versionDone === true);

// 2. hold BREW on its real hit target (after the splash -> HOME transition: taps during a transition are ignored, as on the device)
rig.advance(1500);
const L = rig.ui.ctrl({ cmd: 'layout' });
const brew = (L.targets || []).find(t => t.name === 'brew');
check('layout has a BREW target on HOME', !!brew, brew ? `(${brew.x},${brew.y}) r${brew.r}` : JSON.stringify(L).slice(0, 200));
const target = rig.machine.regs[23];
const phases = [];
const note = () => { const p = st().phase; if (phases[phases.length - 1] !== p) phases.push(p); };
if (brew) {
  rig.ui.touch(brew.x, brew.y, 'down');
  for (let i = 0; i < 18; i++) { rig.advance(50); note(); }          // 900 ms hold (>= 650 ms hold-to-start)
  rig.ui.touch(brew.x, brew.y, 'up');
  // 3. run the shot until the UI leaves PRE/EXT (auto-stop), then let the drip settle
  const t0 = rig.t;
  const started = rig.runUntil(() => { note(); return ['PRE', 'EXT'].includes(st().phase); }, 3000, 20);
  check('the hold starts the brew (PRE or EXT)', started, phases.join(' → '));
  const stopped = rig.runUntil(() => { note(); return !['PRE', 'EXT'].includes(st().phase); }, 90000, 50);
  const shotS = (rig.t - t0) / 1000;
  rig.runUntil(() => { note(); return false; }, 4000, 50);
  s = st();
  const w = rig.machine.regs[39] << 16 >> 16;
  check('the shot auto-stops', stopped, `after ${shotS.toFixed(1)} s`);
  check('phases PRE → EXT → (DONE|READY)', phases.includes('PRE') && phases.includes('EXT') && ['DONE', 'READY'].includes(phases[phases.length - 1]), phases.join(' → '));
  check('shot time plausible (15..60 s)', shotS > 15 && shotS < 60, `${shotS.toFixed(1)} s`);
  check('brew weight reaches the target (word 39 within target-1 g .. target+3 g)', w >= target - 10 && w <= target + 30, `${(w / 10).toFixed(1)} g, target ${(target / 10).toFixed(1)} g`);
  check('state JSON shows the weight (weight.shown within 1 g of word 39)', Math.abs((s.weight?.shown ?? -999) - w) <= 10, `shown ${s.weight?.shown}, reg ${w}`);
  check('no blocked/failed writes', s.cmd6.fail === 0 && s.cmd6.block === 0, JSON.stringify(s.cmd6));
}

// 4. measure: ms per UI step and per rendered frame over 20 s of sim time (idle HOME, 1x step size)
{
  const n0 = rig.steps, f0 = rig.frames, t0 = performance.now();
  rig.advance(20000, 2);
  const ms = performance.now() - t0, steps = rig.steps - n0, frames = rig.frames - f0;
  console.log(`smoke: perf: 20 s sim in ${ms.toFixed(0)} ms (${(ms / 20).toFixed(2)} ms per sim second at 1x = ${(ms / 20 / 60 * 1000 / 16.7).toFixed(1)} % of a 60 Hz frame budget), ${steps} steps (${(ms * 1000 / steps).toFixed(1)} µs/step incl. machine), ${frames} rendered frames`);
}
{ // a busy screen: the diagnostics page scrolled by a drag (renders at 30 fps while animating)
  rig.ui.ctrl({ cmd: 'page', name: 'diag' });
  const f0 = rig.frames, t0 = performance.now();
  rig.ui.touch(240, 380, 'down');
  for (let i = 0; i < 30; i++) { rig.ui.touch(240, 380 - i * 8, 'move'); rig.advance(33, 2); }
  rig.ui.touch(240, 140, 'up'); rig.advance(1000, 2);
  const ms = performance.now() - t0, frames = rig.frames - f0;
  console.log(`smoke: perf: drag-scroll on DIAG: ${frames} frames in ${ms.toFixed(0)} ms wall (${(ms / Math.max(1, frames)).toFixed(2)} ms per frame incl. steps)`);
  check('the UI keeps running', !rig.stopped);
}
console.log(`SMOKE: ${fails ? fails + ' FAILED' : 'OK'}`);
process.exit(fails ? 1 : 0);
