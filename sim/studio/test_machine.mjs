#!/usr/bin/env node
/* sim/studio/test_machine.mjs — tests for sim/studio/machine.js (run: node sim/studio/test_machine.mjs; exit 0 = pass).
 * No npm dependencies. Offline: no device contact, everything runs in-process. */
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import {
  Machine, DEFAULT_PARAMS, crc16modbus, buildFrame, buildQuery, buildVersionRequest, buildVersionResponse,
  buildPowerFrame, FrameParser, configToWords, wordBounds, hex, s16, WS,
} from './machine.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, '..', '..');
const CONFIG = JSON.parse(readFileSync(join(ROOT, 'sim/fixtures/config.json'), 'utf8'));
const SPEC = JSON.parse(readFileSync(join(ROOT, 'sim/fixtures/parity_spec.json'), 'utf8'));

// ---------------------------------------------------------------------------------------------------- mini harness
let passed = 0, failed = 0;
const failures = [];
function check(name, cond, detail = '') {
  if (cond) passed++;
  else { failed++; failures.push(`${name}${detail ? ' — ' + detail : ''}`); }
}
function eq(name, got, want) {
  const g = JSON.stringify(got), w = JSON.stringify(want);
  check(name, g === w, `got ${g}, want ${w}`);
}
function section(name, fn) {
  try { fn(); } catch (e) { failed++; failures.push(`${name}: threw ${e.stack || e}`); }
}
const unhex = (s) => Uint8Array.from(s.trim().split(/\s+/).map((x) => parseInt(x, 16)));
/** Parse MCU->display bytes into frames (the display's view). */
function frames(bytes) { return new FrameParser().feed(bytes); }
function newMachine(extra = {}) {
  return new Machine({ config: CONFIG, spec: SPEC, seed: 7, params: { coldStart: false, ...extra } });
}
/** Advance in 10 ms steps, calling fn(m) after each step; stops when fn returns true or after maxS. */
function runUntil(m, maxS, fn) {
  const end = m.t + maxS;
  while (m.t < end - 1e-9) { m.advance(0.01); if (fn(m)) return true; }
  return false;
}
/** work_sta transition list [{ws, t}] while running. */
function trackWs(m, maxS, stopFn) {
  const seq = [{ ws: m.regs[33] & ~WS.PULSE_MASK, t: m.t }];
  runUntil(m, maxS, () => {
    const ws = m.regs[33] & ~WS.PULSE_MASK;
    if (ws !== seq[seq.length - 1].ws) seq.push({ ws, t: m.t });
    return stopFn ? stopFn(m) : false;
  });
  return seq;
}
/** Collapse short glitches (< minS, e.g. the captured bit-9 pulse) like mcu_sim's verify filter, then dedupe. */
function compress(seq, minS, keep = []) {
  const out = [];
  for (let i = 0; i < seq.length; i++) {
    const dur = (i + 1 < seq.length ? seq[i + 1].t : Infinity) - seq[i].t;
    if (i > 0 && dur < minS && !keep.includes(seq[i].ws)) continue;
    if (!out.length || out[out.length - 1] !== seq[i].ws) out.push(seq[i].ws);
  }
  return out;
}
const h4 = (v) => '0x' + v.toString(16).padStart(4, '0');

// ---------------------------------------------------------------------------------------------------- 1. vectors
/* Fixed vectors generated ONCE from sim/proto.py with:  python3.12 sim/studio/gen_vectors.py
 * (crc16_modbus, build_frame, build_query, build_version_request/response, build_power_frame, FrameParser fed in 5-byte
 * chunks, config_to_words + MCU_DEFAULTS on sim/fixtures/config.json, word_bounds(parity_spec.json)). Re-run it if
 * proto.py changes. */
const V = {
  crc: { '123456789': 19255, aa5506160100: 49652, aa5503210007: 37125 },
  frames: {
    write_brew_stop: 'aa 55 06 16 01 00 00 c0 87',
    ack_grind_start: 'aa 55 86 03 04 01 00 b9 00 3c 00 01 00 f4 ac',
    write_steam_start: 'aa 55 06 18 08 01 00 8a 02 00 00 00 00 00 00 00 00 00 00 00 00 cd 67',
    write_neg: 'aa 55 06 27 01 fd ff ce ab',
    query_sub33_len7: 'aa 55 03 21 07 84 46',
    query_sub0_len56: 'aa 55 03 00 38 dc 06',
    version_request: 'aa 55 89 01 00 00 6f 81',
    version_response: 'aa 55 09 08 00 31 2e 31 2e 31 2d 36 39 51 40',
    power_frame: 'aa 55 08 ea 07 0b 15 07 1e 00 16 06 0f 71 0d',
  },
  parser: {
    stream: '00 13 aa aa 55 03 21 07 84 46 aa 55 83 21 07 01 01 00 00 00 00 00 00 10 00 00 00 00 00 12 45 aa 55 06 16 01 00 00 c0 87 aa 55 89 01 00 00 6f 81 aa 55 09 08 00 31 2e 31 2e 31 2d 36 39 51 40 aa 55 08 ea 07 0b 15 07 1e 00 16 06 0f 71 0d aa 55 83 70 03 04 01 05 06 46 00 99 94 aa',
    chunk: 5,
    frames: [[3, 33, 7], [6, 22, [0]], [137, 0, '00'], [9, 0, '31 2e 31 2e 31 2d 36 39'],
      [8, 234, 'ea 07 0b 15 07 1e 00 16 06 0f'], [131, 112, [260, 1541, 70]]],
    crc_ok: 6, crc_bad: 1, garbage: 23,
  },
  config_words: { 0: 1, 1: 0, 2: 1, 4: 185, 5: 60, 11: 870, 13: 60, 14: 3, 15: 60, 17: 400, 23: 360, 25: 650, 112: 260, 113: 1541, 114: 70 },
  boot_regs_0_32: [1, 0, 1, 0, 185, 60, 0, 0, 0, 0, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 0, 360, 0, 650, 0, 0, 0, 0, 0, 0, 0],
  reply_sub112_len3: 'aa 55 83 70 03 04 01 05 06 46 00 99 94',
  word_bounds: { 2: [1, 2, 1], 4: [10, 309, 1], 8: [0, 1, 1], 11: [850, 960, 10], 14: [1, 4, 1], 17: [300, 5000, 100], 20: [0, 1, 1], 23: [100, 990, 10], 25: [450, 700, 10], 30: [0, 1, 1], 114: [30, 80, 10] },
};

section('vectors', () => {
  const enc = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0));
  eq('crc 123456789', crc16modbus(enc('123456789')), V.crc['123456789']);
  eq('crc aa5506160100', crc16modbus(unhex('aa 55 06 16 01 00')), V.crc.aa5506160100);
  eq('crc aa5503210007', crc16modbus(unhex('aa 55 03 21 00 07')), V.crc.aa5503210007);
  const F = V.frames;
  eq('build write_brew_stop', hex(buildFrame(0x06, 22, [0])), F.write_brew_stop);
  eq('build ack_grind_start', hex(buildFrame(0x86, 3, [1, 185, 60, 1])), F.ack_grind_start);
  eq('build write_steam_start', hex(buildFrame(0x06, 24, [1, 650, 0, 0, 0, 0, 0, 0])), F.write_steam_start);
  eq('build write_neg', hex(buildFrame(0x06, 39, [-3])), F.write_neg);
  eq('build query 33/7', hex(buildQuery(33, 7)), F.query_sub33_len7);
  eq('build query 0/56', hex(buildQuery(0, 56)), F.query_sub0_len56);
  eq('build version request', hex(buildVersionRequest()), F.version_request);
  eq('build version response', hex(buildVersionResponse('1.1.1-69')), F.version_response);
  eq('build power frame', hex(buildPowerFrame([0xEA, 0x07, 11, 21, 7, 30, 0, 22, 6, 15])), F.power_frame);
  // parser, chunked exactly like the generator
  const p = new FrameParser();
  const stream = unhex(V.parser.stream);
  const got = [];
  for (let i = 0; i < stream.length; i += V.parser.chunk) {
    for (const f of p.feed(stream.slice(i, i + V.parser.chunk))) {
      got.push([f.cmd, f.sub, f.cmd === 3 ? f.n : f.words ? f.words : hex(f.payload)]);
    }
  }
  eq('parser frames', got, V.parser.frames);
  eq('parser counters', [p.crcOk, p.crcBad, p.garbage], [V.parser.crc_ok, V.parser.crc_bad, V.parser.garbage]);
  // config packing / bounds
  eq('configToWords', configToWords(CONFIG), V.config_words);
  eq('wordBounds(spec)', wordBounds(SPEC), V.word_bounds);
});

// ---------------------------------------------------------------------------------------------------- 2. protocol
section('protocol: version requests', () => {
  const m = newMachine();
  m.advance(0.2);
  const vr = frames(m.takeTx()).filter((f) => f.cmd === 0x89);
  check('0x89 every 20 ms before cmd9 (~10 in 0.2 s)', vr.length >= 9 && vr.length <= 11, `got ${vr.length}`);
  check('0x89 frame bytes exact', vr.length && hex(vr[0].bytes) === V.frames.version_request);
  m.rx(buildVersionResponse('1.1.1-69'));
  m.advance(0.01); m.takeTx();
  m.advance(0.3);
  eq('no 0x89 after cmd9', frames(m.takeTx()).filter((f) => f.cmd === 0x89).length, 0);
  check('versionDone', m.versionDone === true);
  check('version_handshake event', m.events.some((e) => e.kind === 'version_handshake' && e.version === '1.1.1-69'));
});

function handshaken(extra) {
  const m = newMachine(extra);
  m.advance(0.05);
  m.rx(buildVersionResponse('1.1.1-69'));
  m.advance(0.05);
  m.takeTx();
  return m;
}
function request(m, bytes, wait = 0.06) {
  m.rx(bytes);
  m.advance(wait);
  return frames(m.takeTx());
}

section('protocol: cmd3 reads', () => {
  const m = handshaken();
  let fr = request(m, buildQuery(0, 56));
  check('sub0 len56: one reply', fr.length === 1 && fr[0].cmd === 0x83 && fr[0].sub === 0 && fr[0].words.length === 56);
  if (fr.length === 1) {
    const b = fr[0].bytes;
    check('sub0 len56: header + size', b[0] === 0xAA && b[1] === 0x55 && b[2] === 0x83 && b[3] === 0 && b[4] === 56 && b.length === 2 * 56 + 7);
    check('sub0 len56: CRC LE', crc16modbus(b, 0, b.length - 2) === (b[b.length - 2] | (b[b.length - 1] << 8)));
    eq('sub0 len56: words 0..32 = MCU_DEFAULTS + config_to_words', fr[0].words.slice(0, 33), V.boot_regs_0_32);
    eq('sub0 len56: work_sta idle', h4(fr[0].words[33]), h4(WS.IDLE));
    eq('sub0 len56: reply equals buildFrame(0x83, regs)', hex(b), hex(buildFrame(0x83, 0, fr[0].words)));
  }
  fr = request(m, buildQuery(33, 7));
  check('sub33 len7 reply', fr.length === 1 && fr[0].sub === 33 && fr[0].words.length === 7 && fr[0].bytes.length === 21);
  fr = request(m, buildQuery(112, 3));
  check('sub112 len3 reply exact', fr.length === 1 && hex(fr[0].bytes) === V.reply_sub112_len3, fr.length ? hex(fr[0].bytes) : 'none');
  fr = request(m, buildQuery(120, 20));
  eq('read beyond 129 words: no reply', fr.length, 0);
  check('read_rejected event', m.events.some((e) => e.kind === 'read_rejected'));
});

section('protocol: cmd6 writes', () => {
  const m = handshaken();
  const w = buildFrame(0x06, 23, [380]);
  let fr = request(m, w);
  check('valid write -> exact 0x86 echo', fr.length === 1 && hex(fr[0].bytes) === hex(buildFrame(0x86, 23, [380])));
  eq('memcpy word23', m.regs[23], 380);
  // the ACK arrives with latency (~22 ms), not instantly
  m.rx(buildFrame(0x06, 25, [600]));
  m.advance(0.01);
  eq('ACK not before ~19 ms', frames(m.takeTx()).length, 0);
  m.advance(0.05);
  eq('ACK after latency', frames(m.takeTx()).length, 1);
  // trigger words 26/27/30 != 0 -> rejected, no ACK, register untouched
  for (const [sub, words] of [[26, [1]], [27, [5]], [30, [1]], [24, [0, 650, 0, 3, 0, 0, 0, 0]]]) {
    fr = request(m, buildFrame(0x06, sub, words));
    eq(`trigger write sub${sub} -> no ACK`, fr.length, 0);
  }
  eq('word26 untouched', m.regs[26], 0);
  check('write_rejected trigger events', m.events.filter((e) => e.kind === 'write_rejected' && e.verdict === 'trigger').length === 4);
  // band violation (sub30 len5 ends at 35) -> rejected
  fr = request(m, buildFrame(0x06, 30, [0, 0, 0, 0, 0]));
  eq('band violation -> no ACK', fr.length, 0);
  check('write_rejected band event', m.events.some((e) => e.kind === 'write_rejected' && e.verdict === 'band'));
  fr = request(m, buildFrame(0x06, 40, [1]));
  eq('write into the status block -> no ACK', fr.length, 0);
  // 112..114 band is allowed
  fr = request(m, buildFrame(0x06, 112, [260, 1541, 70]));
  check('boot push sub112 len3 ACKed', fr.length === 1 && fr[0].cmd === 0x86);
  check('boot_push event', m.events.some((e) => e.kind === 'boot_push' && e.block === 'sub112_len3'));
  // range violation: logged, still accepted (the MCU has no range checks)
  fr = request(m, buildFrame(0x06, 11, [990]));
  check('range violation accepted + logged', fr.length === 1 && m.regs[11] === 990 && m.events.some((e) => e.kind === 'range_violation'));
  // cmd8 -> 0x88 echo
  const p10 = [0xEA, 0x07, 11, 21, 7, 30, 0, 0, 0, 0];
  fr = request(m, buildPowerFrame(p10));
  const echo = buildPowerFrame(p10); echo[2] = 0x88;
  const c = crc16modbus(echo, 0, 13); echo[13] = c & 0xFF; echo[14] = c >> 8;
  check('cmd8 -> exact 0x88 echo', fr.length === 1 && hex(fr[0].bytes) === hex(echo));
  check('time_sync event', m.events.some((e) => e.kind === 'time_sync'));
});

section('protocol: onFrame notes', () => {
  const m = handshaken();
  const seen = [];
  m.onFrame = (dir, f) => seen.push([dir, f.cmd, f.note]);
  request(m, buildFrame(0x06, 26, [1]));
  request(m, buildFrame(0x06, 22, [0]));
  request(m, buildQuery(33, 7));
  check('rx rejected note', seen.some(([d, c, n]) => d === 'rx' && c === 6 && n === 'rejected: trigger'));
  check('rx stop note', seen.some(([d, c, n]) => d === 'rx' && c === 6 && n === 'stop brew'));
  check('tx ACK note', seen.some(([d, c, n]) => d === 'tx' && c === 0x86 && n === 'ACK'));
  check('tx reply note', seen.some(([d, c, n]) => d === 'tx' && c === 0x83 && n === 'reply'));
});

// ---------------------------------------------------------------------------------------------------- 3. behaviour
// The frames the UI sends (src/mp/model.c act_push): GRIND sub3 len4, BREW sub5 len18, WATER sub9 len11, STEAM sub24 len8.
const BREW = () => buildFrame(0x06, 5, [60, 0, 0, 0, 1, 0, 870, 0, 60, 3, 60, 0, 400, 0, 0, 0, 0, 1]);
const GRIND = () => buildFrame(0x06, 3, [1, 185, 60, 1]);
const WATER = () => buildFrame(0x06, 9, [1, 0, 870, 0, 60, 3, 60, 1, 400, 0, 0]);
const STEAM = () => buildFrame(0x06, 24, [1, 650, 0, 0, 0, 0, 0, 0]);

section('behaviour: brew', () => {
  const m = handshaken();
  let fr = request(m, BREW(), 0.05);
  check('brew start ACKed', fr.some((f) => f.cmd === 0x86 && f.sub === 5));
  let tStart = null, tStop = null;
  const seq = trackWs(m, 60, (mm) => {
    const ev = mm.events[mm.events.length - 1];
    if (ev.kind === 'activity_start' && ev.activity === 'brew' && tStart === null) tStart = ev.t;
    if (ev.kind === 'activity_stop' && ev.activity === 'brew' && tStop === null) tStop = ev.t;
    return tStop !== null && mm.t > tStop + 3.5;
  });
  const c = compress(seq, 0.3, [0x0105]);
  eq('work_sta IDLE -> PRE -> EXT -> post bit2 -> IDLE', c.map(h4), [WS.IDLE, WS.BREW_PRE, WS.BREW_EXT, 0x0105, WS.IDLE].map(h4));
  const st = m.events.find((e) => e.kind === 'activity_start' && e.activity === 'brew');
  const sp = m.events.find((e) => e.kind === 'activity_stop' && e.activity === 'brew');
  check('brew started ~0.17 s after the write', st && st.t > 0.1 && st.t < 0.35 + 0.1 + 0.05, st && `t=${st.t}`);
  check('auto-stop (predictive)', sp && sp.reason.includes('predictive'), sp && sp.reason);
  const shot = sp && st ? sp.t - st.t : 0;
  check('shot time 20..40 s', shot >= 20 && shot <= 40, `shot ${shot.toFixed(2)} s`);
  const final = s16(m.regs[39]);
  check('final brew weight within target..target+20', final >= 360 && final <= 380, `final ${final}`);
  eq('word22 cleared by the MCU', m.regs[22], 0);
  eq('word9 consumed', m.regs[9], 0);
  console.log(`  brew: shot ${shot.toFixed(1)} s, final ${(final / 10).toFixed(1)} g, ws ${c.map(h4).join(' -> ')}`);
});

section('behaviour: brew pressure/flow curves', () => {
  const m = handshaken({ noise: 0 });
  request(m, BREW(), 0.05);
  let maxPre = 0, maxExt = 0, extEnd = 0, flowExt = 0;
  runUntil(m, 60, (mm) => {
    const s = mm.snapshot();
    if (!mm.events.some((e) => e.kind === 'activity_start')) return false;
    if (s.phase === 'pre-infusion' && s.activity && s.activity.elapsed > 2) maxPre = Math.max(maxPre, s.pressureBar);
    if (s.phase === 'extraction') { maxExt = Math.max(maxExt, s.pressureBar); extEnd = s.pressureBar; flowExt = s.flowMlS; }
    return s.phase === 'idle';
  });
  check('pre-infusion plateau 2..4.5 bar', maxPre >= 2 && maxPre <= 4.5, `pre ${maxPre}`);
  check('extraction peak 8.5..9.5 bar', maxExt >= 8.5 && maxExt <= 9.5, `ext ${maxExt}`);
  check('pressure declines as the puck erodes', extEnd < maxExt - 0.3, `end ${extEnd} vs peak ${maxExt}`);
  check('extraction flow 1..4 ml/s', flowExt > 1 && flowExt < 4, `flow ${flowExt}`);
  const raw = Math.round(9 * 358 / 4);
  check('word51 = bar * 358 / anchor (4 bar)', Math.abs(Math.round(maxExt * 358 / 4) - raw) < 30);
});

section('behaviour: brew manual stop + fault stop', () => {
  let m = handshaken();
  request(m, BREW(), 0.05);
  runUntil(m, 15, () => false);
  request(m, buildFrame(0x06, 22, [0]), 0.05);
  m.advance(0.2);
  check('manual stop word22=0', m.events.some((e) => e.kind === 'activity_stop' && e.activity === 'brew' && e.reason.includes('manual')));
  m = handshaken();
  request(m, BREW(), 0.05);
  runUntil(m, 10, () => false);
  m.setFaults({ e3: 16 });
  m.advance(0.1);
  check('fault stops the brew', m.events.some((e) => e.kind === 'activity_stop' && e.reason.includes('fault')));
  eq('fault phase', m.snapshot().phase, 'fault');
  m.clearFaults(); m.advance(0.1);
  eq('faults cleared', [m.regs[34], m.regs[35], m.regs[36]], [0, 0, 0]);
});

section('behaviour: grind', () => {
  const m = handshaken();
  request(m, GRIND(), 0.05);
  let stop = null;
  const seq = trackWs(m, 30, (mm) => {
    stop = stop || mm.events.find((e) => e.kind === 'activity_stop' && e.activity === 'grind');
    return stop && mm.t > stop.t + 3;
  });
  eq('work_sta IDLE -> GRIND -> IDLE', compress(seq, 0.3).map(h4), [WS.IDLE, WS.GRIND, WS.IDLE].map(h4));
  eq('grind weight = target + 2', s16(m.regs[38]), 187);
  const st = m.events.find((e) => e.kind === 'activity_start' && e.activity === 'grind');
  const dur = stop.t - st.t;
  check('grind time ~9.6 s (19.4 units/s)', dur > 8.5 && dur < 11, `dur ${dur.toFixed(2)}`);
  eq('word6 consumed', m.regs[6], 0);
  eq('word3 consumed', m.regs[3], 0);
});

section('behaviour: hot water', () => {
  const m = handshaken();
  request(m, WATER(), 0.05);
  let stop = null;
  const seq = trackWs(m, 120, (mm) => {
    stop = stop || mm.events.find((e) => e.kind === 'activity_stop' && e.activity === 'water');
    return stop && mm.t > stop.t + 3;
  });
  eq('work_sta IDLE -> WATER -> IDLE', compress(seq, 0.3).map(h4), [WS.IDLE, WS.WATER, WS.IDLE].map(h4));
  check('auto-stop at word39 >= word17', stop && stop.reason.includes('word17'));
  const final = s16(m.regs[39]);
  check('final water weight ~ target (±15)', Math.abs(final - 400) <= 15, `final ${final}`);
  eq('word16 cleared by the MCU', m.regs[16], 0);
});

section('behaviour: steam (latch) auto-stop', () => {
  const m = handshaken();
  request(m, STEAM(), 0.05);
  let stop = null, peak = 0, cleared = null;
  runUntil(m, 90, (mm) => {
    stop = stop || mm.events.find((e) => e.kind === 'activity_stop' && e.activity === 'steam');
    if (stop) peak = Math.max(peak, mm.regs[48]);
    if (stop && cleared === null && mm.regs[24] === 0) cleared = mm.t;
    return stop && mm.t > stop.t + 15;
  });
  check('steam auto-stop at milk >= target', stop && stop.reason.includes('milk') && stop.milk >= 650, stop && JSON.stringify(stop));
  check('MCU clears word24 ~0.18 s after the stop', cleared !== null && cleared - stop.t > 0.1 && cleared - stop.t < 0.3, `dt ${cleared && (cleared - stop.t)}`);
  check('milk overshoot +2..+5 °C after stop', peak - 650 >= 20 && peak - 650 <= 50, `peak ${peak}`);
  eq('steam bits cleared', m.regs[33] & 0x4020, 0);
  const st = m.events.find((e) => e.kind === 'activity_start' && e.activity === 'steam');
  check('steam start 0.30 s after the write', st && st.t - 0.1 > 0.25 && st.t - 0.1 < 0.45, st && `t ${st.t}`);
  const dur = stop.t - st.t;
  check('steam session 20..50 s', dur > 20 && dur < 50, `dur ${dur.toFixed(1)}`);
});

section('behaviour: wand flush', () => {
  const m = handshaken();
  m.setWand(true);
  m.advance(0.05);
  eq('flush: steam bits, word24 = 0', [h4(m.regs[33] & ~WS.PULSE_MASK), m.regs[24], m.regs[46], m.regs[44] & 1], [h4(WS.STEAM), 0, 1, 1]);
  eq('phase flush', m.snapshot().phase, 'flush');
  const s0 = m.snapshot().steamC;
  m.advance(5);
  check('steam boiler drops during flush', m.snapshot().steamC < s0 - 1, `${s0} -> ${m.snapshot().steamC}`);
  m.setWand(false);
  m.advance(0.05);
  eq('flush stops on release', m.regs[33] & 0x4020, 0);
  m.advance(30);
  check('steam boiler recovers', m.snapshot().steamC > 128, `${m.snapshot().steamC}`);
});

section('behaviour: deep sleep + power key wake', () => {
  const m = handshaken();
  let fr = request(m, buildFrame(0x06, 0, [0, 0]), 0.05);
  check('word0=0 ACKed', fr.length === 1 && fr[0].cmd === 0x86);
  m.advance(0.5);
  m.takeTx();
  eq('deep sleep entered', m.power, 'off');
  fr = request(m, buildQuery(33, 7), 0.2);
  eq('silent in deep sleep (no reply, no 0x89)', fr.length, 0);
  eq('work_sta 0 in deep sleep', m.regs[33], 0);
  m.powerKey(true);
  m.advance(0.5);
  m.powerKey(false);
  m.advance(0.2);
  eq('short press (0.5 s) does not wake', m.power, 'off');
  m.powerKey(true);
  m.advance(1.0);
  m.powerKey(false);
  m.advance(0.2);
  eq('1 s hold wakes', m.power, 'on');
  const vr = frames(m.takeTx()).filter((f) => f.cmd === 0x89);
  check('0x89 round after the wake', vr.length >= 5, `got ${vr.length}`);
  eq('factory start values (word11=920, word114=50)', [m.regs[0], m.regs[11], m.regs[114]], [1, 920, 50]);
  check('power_wake + reboot events', m.events.some((e) => e.kind === 'power_wake') && m.events.some((e) => e.kind === 'reboot' && e.factory));
});

section('behaviour: offline + cup + sleep word1', () => {
  const m = handshaken();
  m.setOffline(true);
  let fr = request(m, buildQuery(33, 7), 0.2);
  eq('offline: no reply', fr.length, 0);
  m.setOffline(false);
  fr = request(m, buildQuery(33, 7), 0.05);
  eq('online again: reply', fr.length, 1);
  m.setCup(false);
  m.advance(0.05);
  eq('cup removed reads -92.9 g (mcu_sim 64607)', m.regs[39], 64607);
  m.setCup(true);
  m.advance(0.05);
  check('cup back reads ~0', Math.abs(s16(m.regs[39])) <= 1, `${s16(m.regs[39])}`);
  m.setScaleWeight(250);
  m.advance(0.05);
  eq('manual scale weight', m.regs[39], 2500);
  m.tare('brew');
  m.advance(0.05);
  check('tare -> ~0', Math.abs(s16(m.regs[39])) <= 1);
  request(m, buildFrame(0x06, 1, [1]), 0.1);
  eq('word1=1 -> work_sta SLEEP', h4(m.regs[33]), h4(WS.SLEEP));
  eq('sleep: heat stages 0', [m.regs[62], m.regs[63]], [0, 0]);
  eq('sleep phase', m.snapshot().phase, 'sleep');
});

section('behaviour: heat-up from 20 °C', () => {
  const m = new Machine({ config: CONFIG, spec: SPEC, seed: 3 });
  check('cold start at ambient', Math.abs(m.snapshot().brewC - 20) < 0.5 && Math.abs(m.snapshot().steamC - 20) < 0.5);
  eq('cold: stages 0', [m.regs[62], m.regs[63]], [0, 0]);
  const eSet = m.snapshot().brewSetC, sSet = m.snapshot().steamSetC;
  let tE = null, tS = null, maxE = 0, maxS = 0, tUiReady = null;
  for (let i = 0; i < 25 * 60; i++) {
    m.advance(1);
    const s = m.snapshot();
    maxE = Math.max(maxE, s.brewC); maxS = Math.max(maxS, s.steamC);
    if (tE === null && s.stage62 === 100) tE = s.t;
    if (tS === null && s.stage63 === 100) tS = s.t;
    if (tUiReady === null && m.regs[40] >= m.regs[11] - 20) tUiReady = s.t;
  }
  check('brew boiler ready (stage62=100) in 4..8 min', tE !== null && tE >= 240 && tE <= 480, `tE ${tE}`);
  check('steam boiler ready (stage63=100) in 7..12 min', tS !== null && tS >= 420 && tS <= 720, `tS ${tS}`);
  check('brew overshoot < 3 °C', maxE - eSet < 3, `max ${maxE.toFixed(2)} set ${eSet}`);
  check('steam overshoot < 3 °C', maxS - sSet < 3, `max ${maxS.toFixed(2)} set ${sSet}`);
  const s = m.snapshot();
  check('settled near setpoints', Math.abs(s.brewC - eSet) < 1 && Math.abs(s.steamC - sSet) < 1.5, `${s.brewC} / ${s.steamC}`);
  eq('ready phase', s.phase, 'idle');
  check('group head approaches word114 (70 °C)', s.groupC > 60 && s.groupC < 75, `${s.groupC}`);
  console.log(`  heat-up: brew ready ${(tE / 60).toFixed(1)} min (UI READY ${(tUiReady / 60).toFixed(1)} min), steam ready ${(tS / 60).toFixed(1)} min, overshoot brew ${(maxE - eSet).toFixed(2)} °C steam ${(maxS - sSet).toFixed(2)} °C`);
  // cooling in deep sleep
  m.powerOff();
  m.advance(600);
  check('cools in deep sleep', m.snapshot().brewC < eSet - 3 && m.snapshot().brewC > 40, `${m.snapshot().brewC}`);
});

section('determinism', () => {
  const run = () => {
    const m = new Machine({ config: CONFIG, spec: SPEC, seed: 42, params: { coldStart: false, noise: 1 } });
    const log = [];
    m.advance(0.1);
    m.rx(buildVersionResponse('1.1.1-69'));
    m.rx(BREW());
    for (let i = 0; i < 400; i++) {
      m.advance(0.1);
      if (i % 5 === 0) m.rx(buildQuery(32, 80));
      log.push(hex(m.takeTx()));
    }
    return [log.join('|'), Array.from(m.regs).join(',')];
  };
  const a = run(), b = run();
  check('same seed + inputs -> identical tx bytes', a[0] === b[0]);
  check('same seed + inputs -> identical registers', a[1] === b[1]);
  const c = new Machine({ config: CONFIG, spec: SPEC, seed: 43, params: { coldStart: false, noise: 1 } });
  c.advance(5);
  const d = new Machine({ config: CONFIG, spec: SPEC, seed: 42, params: { coldStart: false, noise: 1 } });
  d.advance(5);
  check('different seed -> different noise', Array.from(c.regs).join() !== Array.from(d.regs).join());
});

section('misc API', () => {
  const m = new Machine();   // no config/spec: embedded fallbacks
  eq('fallback config words', [m.regs[11], m.regs[23], m.regs[112], m.regs[113], m.regs[114]], [870, 360, 260, 1541, 70]);
  check('DEFAULT_PARAMS frozen + documented keys', Object.isFrozen(DEFAULT_PARAMS) && 'noise' in DEFAULT_PARAMS);
  m.setBoilers({ brew: 89, steam: 130, group: 70 });
  m.advance(0.05);
  eq('setBoilers -> ready stages', [m.regs[62], m.regs[63]], [100, 100]);
  m.setBoilers({ brew: 125 });
  m.advance(0.05);
  check('natural E3 over-temperature', (m.regs[36] & 0x10) !== 0);
  m.clearFaults();
  const snap = m.snapshot();
  for (const k of ['t', 'power', 'link', 'mode', 'phase', 'brewC', 'steamC', 'groupC', 'milkC', 'pressureBar', 'flowMlS',
    'brewG', 'grindG', 'stage62', 'stage63', 'wand', 'cup', 'faults', 'workSta', 'versionDone', 'activity', 'stats']) {
    check(`snapshot.${k}`, k in snap);
  }
  m.powerOff();
  eq('powerOff -> off', m.snapshot().phase, 'off');
  m.powerOn();
  eq('powerOn -> on', m.power, 'on');
  m.events.length = 0;
  for (let i = 0; i < 3000; i++) m.setAmbient(20);
  check('events capped', m.events.length <= 2000);
});

// ---------------------------------------------------------------------------------------------------- summary
for (const f of failures) console.log('FAIL', f);
console.log(`test_machine: ${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
