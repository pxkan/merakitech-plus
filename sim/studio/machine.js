/* sim/studio/machine.js — the MerakiTech Gen 1 machine model for Sim Studio (browser + node >= 18, no dependencies).
 *
 * A faithful JS port of the PROTOCOL side of sim/mcu_sim.py (the simulated powerMcu) plus a smooth, tunable physics
 * model (boilers, group head, pump/puck, scales, grinder, milk) that drives the 129-word register file the display UI
 * reads. Sources of truth: sim/mcu_sim.py, sim/proto.py, docs/protocol.md, docs/registermap.md, docs/dev/PARITY.md.
 * Not ported: mcu_sim's replay/verify engine, the JSONL assertion file, the ctrl socket and the pty.
 *
 * ---------------------------------------------------------------------------------------------------------------------
 * PUBLIC API
 *   import { Machine, DEFAULT_PARAMS } from './machine.js';
 *   const m = new Machine({ config, spec, seed: 1, params: {} });
 *     config  parsed sandbox config.json (same keys as sim/fixtures/config.json); used like proto.config_to_words.
 *             Omitted -> the fixture baseline embedded below.
 *     spec    parsed sim/fixtures/parity_spec.json (optional): the cmd6 range bounds. Omitted -> FALLBACK_BOUNDS.
 *     seed    PRNG seed (mulberry32). Same seed + same inputs => same outputs.
 *     params  overrides for DEFAULT_PARAMS (physics, noise, ACK profile, write policies).
 *
 *   m.t, m.regs (Uint16Array(129), live), m.events (capped alog array), m.stats, m.onFrame = (dir, f) => {}
 *   m.advance(dt)            advance sim time in <= 10 ms ticks
 *   m.rx(bytes)              bytes the DISPLAY wrote -> frame parser -> handle_frame (replies are scheduled in sim time)
 *   m.takeTx()               MCU->display bytes due by now (Uint8Array, possibly empty); drains them
 *   inputs: setBoilers({brew, steam, group, milk}), setAmbient(c), setParams(p), setWand(on), setCup(on),
 *           tare(scale), setScaleWeight(g, scale), setFaults({e1,e2,e3}), clearFaults(), setOffline(on),
 *           powerKey(down), powerOff(), powerOn(), reboot({factory}), setGrindRate(gps), snapshot()
 *   additions (beyond the agreed API):
 *     m.preheat()                     "ready" preset: boilers + group jump to their setpoints (stage 62/63 = 100)
 *     m.coldStart()                   "cold morning" preset: everything at ambient
 *     m.powerKeyHold(seconds = 1.6)   press + auto-release after `seconds` (mcu_sim power_key action "hold")
 *     m.setAlarm(day, hour, minute)   arm the RTC alarm directly (like the cmd8 alarm field); 0/0/0 disarms
 *     m.injectGarbage(seconds = 2, crcToo = true)  random bytes (+ corrupted 0x83 frames) on the UART (mcu_sim "garbage")
 *     m.setReg(word, value)           raw register poke (mcu_sim "set_reg"); physics words are re-derived next tick
 *     m.setMilk(celsius)              jump the milk probe temperature (mcu_sim "milk_jump")
 *     m.versionDone, m.power ('on'|'off'), m.link ('up'|'silent'), m.mode
 *   helpers exported for the viewer/tests: crc16modbus, buildFrame, buildQuery, buildVersionRequest,
 *     buildVersionResponse, buildPowerFrame, FrameParser, configToWords, wordBounds, heatStage, phaseOf, s16,
 *     regName, hex, REGN, WS, MCU_DEFAULTS, SENSOR_BASELINE, serialWords, ACK_PROFILES, BASELINE_CONFIG
 *
 * ---------------------------------------------------------------------------------------------------------------------
 * REGISTER MAPPING (what the model writes; units as the UI reads them, src/mp/model.c / ui_pages.c)
 *   33 work_sta     mcu_sim phase bits: IDLE 0x0101, GRIND 0x0141, BREW_PRE 0x1107, BREW_EXT 0x1307 (+ the short bit-9
 *                   glitch at 1.14..1.36 s of a pre-infusion), post-brew 0x0105 for 0.21 s, WATER 0x030B, steam |= 0x4020,
 *                   SLEEP 0x0001 (word1=1), 0 in deep sleep, maintenance pulses |= 0x0400/0x0800/0x2000 at rest.
 *   34/35/36        E1/E2/E3 faults (injected, or natural over-temperature: E3 bit4 brew > 120 °C, bit5 steam > 160 °C,
 *                   E1 bit5 group > 110 °C).
 *   38 / 39         grind / brew scale, 0.1 g, signed int16. Brew scale = cup + liquid - tare (the MCU tares at brew/water
 *                   start; -0.3 g until the first drops, like the captures).
 *   40 = 41         brew ("e") boiler NTC, 0.1 °C (sensor = water temperature through a first-order NTC lag).
 *   42 = 128        group head, 0.1 °C.        47 steam boiler, 0.1 °C.        48 milk probe, 0.1 °C.
 *   44 bit0, 46     steam wand switch.          49 mains V, 61 Hz, 69/71/72 ADC noise around the baseline.
 *   50              current ch1: captured activity levels (brew 1533/1046, water 1050, steam 7340, flush 1024) and at rest
 *                   5 + the heater bursts of the two thermostats (so idle "maintenance" current follows the physics).
 *   51              pressure raw = bar * pAnchorRaw(358) / anchor, anchor = word112 low byte (preInfusionPressure, the UI's
 *                   p_bar() anchor; default 4). 9 bar -> 805 raw. Steam keeps mcu_sim's raw drive levels (93 / 16650 ->
 *                   the UI shows "PULSE"), deadhead spikes after a brew and the maintenance pulses likewise.
 *   52              flow velocity, modelled as 0.01 ml/s at the inlet flowmeter (matches the captures: water 610 ≈ 6.1 ml/s,
 *                   pre-infusion 360 ≈ 3.6 ml/s, extraction 85..228).
 *   53 / 55         raw load cells (baseline + weight/40 resp. /45).    58 grinder current (1500 peak, ~868 running).
 *   62 / 63         heat stages exactly as mcu_sim.heat_stage (MCU boiler_heat): 0/30/60/100.
 *   68 / 70         steam / brew boiler water level ADC (flash boiling + refill as mcu_sim).
 *
 * BOILER SETPOINTS (documented choice): the stage-62 "ready" test is word40 >= word11, so the brew boiler is regulated to
 *   word11/10 + params.brewOffsetC (default +2.0 °C; 87.0 °C config -> 89.0 °C) — "ready" (stage 100) is reached at the
 *   brew setting and the thermostat ripple / small post-shot dips stay above it. (The stock MCU was captured idling the e
 *   boiler at 99.7 °C regardless of word11: set params.brewSetpointC = 99.7 to reproduce that.) The steam boiler is
 *   regulated to the steam gear target (word14: 1 -> 120, 2 -> 125, 3 -> 130, else 135 °C), "ready" at target - 10 °C,
 *   exactly what heat_stage and the UI's ready tone (services.c snd_steam_thr) expect. Group head -> word114 (°C).
 *
 * WHAT NEEDS THE MACHINE AWAKE (word0 = 1 and word1 = 0): heating, every activity trigger (grind/brew/water/steam), the
 *   wand flush (also needs stage63 == 100 and no fault). Deep sleep (word0 = 0 or powerOff()) freezes the registers and
 *   silences the UART; physics keeps cooling. Offline (setOffline) silences the UART but the MCU keeps running.
 */

// ===================================================================================================== constants
export const REGN = 129;
const TICK = 0.010;             // 10 ms status task (PROTOCOL §6)
const VER_INTERVAL = 0.020;     // 0x89 every 20 ms until a cmd9 reply
const POLL_LATENCY = 0.005;     // ~5 ms reply time on polls
const BOOT_PUSH_WINDOW = 6.0;   // boot push expected within 6 s of the first RX
const DVALE_DELAY = 0.5;        // word0=0 -> deep sleep within 0.5 s (status task checks every 500 ms)
const POWER_KEY_HOLD_MIN = 0.8; // PC.8 held >= 8 x 100 ms then released -> wake/reset
const EVENT_CAP = 2000;

/** work_sta phase constants (proto.py WS_*). */
export const WS = Object.freeze({
  IDLE: 0x0101, SLEEP: 0x0001, GRIND: 0x0141, BREW_PRE: 0x1107, BREW_EXT: 0x1307,
  WATER: 0x030B, STEAM: 0x4121, PULSE_MASK: 0x2C00,
});

const BOOL_WORDS = {
  0: [0, 1], 1: [0, 1], 3: [0, 1], 6: [0, 1], 7: [0, 1], 8: [0, 1], 9: [0, 1], 16: [0, 1], 19: [0, 1],
  20: [0, 1], 22: [0, 1], 24: [0, 1], 28: [0, 2], 29: [0, 2],
};
const TRIGGER_WORDS = [26, 27, 30];

/** proto.word_bounds(parity_spec.json) — embedded so the model works without the spec. */
const FALLBACK_BOUNDS = {
  2: [1, 2, 1], 4: [10, 309, 1], 8: [0, 1, 1], 11: [850, 960, 10], 14: [1, 4, 1], 17: [300, 5000, 100],
  20: [0, 1, 1], 23: [100, 990, 10], 25: [450, 700, 10], 30: [0, 1, 1], 114: [30, 80, 10],
};

/** MCU defaults from REGISTERMAP §2 (init 0x9966) — the factory start values. */
export const MCU_DEFAULTS = Object.freeze({
  0: 1, 2: 1, 4: 180, 5: 60, 11: 920, 13: 60, 14: 4, 15: 60, 17: 360, 23: 360, 25: 550, 114: 50,
});

/** Idle sensor baseline (first frames of the v3 capture). Temperatures are overwritten by the physics. */
export const SENSOR_BASELINE = Object.freeze({
  33: WS.IDLE, 37: 16, 38: 0, 39: 0, 40: 998, 41: 998, 42: 696, 43: 0, 44: 12, 45: 0, 46: 0, 47: 1302, 48: 232,
  49: 236, 50: 5, 51: 0, 52: 0, 53: 1038, 54: 2116, 55: 1094, 56: 2112, 57: 1, 58: 5, 59: 1278, 60: 1541, 61: 50,
  62: 100, 63: 100, 64: 2906, 65: 3219, 66: 0, 67: 220, 68: 1331, 69: 2304, 70: 1076, 71: 100, 72: 100, 73: 0,
  74: 1808, 75: 1, 80: 13654, 81: 12590, 82: 13358, 88: 13654, 89: 12590, 90: 13358,
});

/** The relevant keys of sim/fixtures/config.json (used when no config is passed). */
export const BASELINE_CONFIG = Object.freeze({
  brewTemper: 870, autoGrindWeight: 185, grindMode: 1, steamGears: 3, preInfusionPressure: 4, preInfusionTime: 6,
  breakTime: 5, headTemper: 70, preInfusionSwitch: 1, autobrewWeight: 360, AtuoSteamTarge: 650,
  autoHotWaterTarget: 400, hotWaterMode: 1,
});

/** ACK latency profiles (proto.ACK_PROFILES). */
export const ACK_PROFILES = Object.freeze({
  normal: { latency_ms: 22, jitter_ms: 3, loss: 0.0, first_delay_ms: 0 },
  observed_v3: { latency_ms: 18, jitter_ms: 5, loss: 0.0, first_delay_ms: 260 },
  slow: { latency_ms: 150, jitter_ms: 20, loss: 0.0, first_delay_ms: 0 },
  lossy: { latency_ms: 22, jitter_ms: 3, loss: 0.35, first_delay_ms: 0 },
  no_ack: { latency_ms: 22, jitter_ms: 0, loss: 1.0, first_delay_ms: 0 },
});

// ===================================================================================================== physics knobs
/** Every physics/behaviour knob, with units. Override via the constructor's `params` or setParams() (live). */
export const DEFAULT_PARAMS = Object.freeze({
  // ---- environment / start state ----
  ambientC: 20,             // °C room temperature (boilers, group and milk cool toward it)
  coldStart: true,          // true: everything starts at ambient (a cold morning); false: start hot and ready
  noise: 0.3,               // 0..1 sensor noise scale (1 ≈ the raw capture noise of mcu_sim; 0 = clean)
  maintPulses: true,        // idle maintenance/deadhead pulses (work_sta bits 10/11/13 + word51 spikes) + post-brew spikes
  autoFaults: true,         // natural over-temperature faults (E3 brew > 120 / steam > 160 °C, E1 group > 110 °C)
  faultHeaterOff: true,     // any active fault switches the heaters off (safety)
  ackProfile: 'normal',     // name in ACK_PROFILES or an object {latency_ms, jitter_ms, loss, first_delay_ms}
  rangePolicy: 'log',       // cmd6 range violations: 'log' (accept + event, the MCU has no range checks) | 'reject' | 'off'
  triggerPolicy: 'reject',  // cmd6 writes of 26/27/30 != 0: 'reject' (no ACK) | 'pass' (word30 reboots the MCU)
  keepAwakeMaxS: 30,        // s, expected keep-awake (word1=0) period; a longer gap logs a keepalive_gap event

  // ---- thermostats (shared) ----
  thermoBandC: 2.5,         // °C proportional band of the PI thermostats
  thermoKi: 0.006,          // 1/(°C·s) integral gain (conditional, anti-windup): no droop, ~0.3 °C heat-up overshoot
  sensorLagS: 3.0,          // s, NTC time constant (the registers show the lagged sensor, the MCU regulates on it)
  lossPerS: 2.0e-4,         // 1/s heat loss of the boiler bodies to ambient (sleep cooling ≈ 0.01 °C/s at 90 °C)
  heaterBurstS: 1.0,        // s, burst-fire period of the heater SSRs (only shapes the current word 50 at rest)

  // ---- brew ("e") boiler: two-mass model (water+element node, coupled to the boiler body/hydraulics) ----
  brewSetpointC: null,      // °C fixed setpoint; null = word11/10 + brewOffsetC
  brewOffsetC: 2.0,         // °C above the brew setting (word11) the boiler is held at (see header)
  brewHeatCps: 0.6,         // °C/s the water node gains at full power (heater power / water heat capacity)
  brewElementLagS: 3,       // s, element heat-storage time constant (adds to the small overshoot)
  brewBodyRatio: 3,         // body heat capacity / water heat capacity (slows the cold heat-up: ~6 min to ready)
  brewCouplingS: 40,        // s, water <-> body time constant
  brewBoilerMl: 500,        // ml of water in the brew boiler (fresh cold water during a shot/water cools it)
  brewHeaterCur: 650,       // word50 units added while the brew heater burst is on (at rest)

  // ---- steam boiler ----
  steamSetpointC: null,     // °C fixed setpoint; null = by steam gear (word14): 120 / 125 / 130 / 135 °C
  steamHeatCps: 0.7,        // °C/s at full power (also the fast recovery after steaming)
  steamElementLagS: 3,      // s
  steamBodyRatio: 4.5,      // (slower cold heat-up: ~10 min to ready at gear 3)
  steamCouplingS: 50,       // s
  steamHeaterCur: 700,      // word50 units added while the steam heater burst is on (at rest)
  steamPurgeS: 2.5,         // s, steaming draws no heat for the first ~2.5 s (condensate purge; captured hold)
  steamDrawK: 0.2,          // 1/s, steam draw = K * (T - Tvent): fast initial drop (~1 °C/s) that levels off
  steamDropC: 5,            // °C below the setpoint where heater power and draw balance while steaming (latch)
  flushDrawK: 0.3,          // 1/s, wand flush (free venting) draw coefficient
  flushDropC: 6,            // °C equilibrium drop during a wand flush

  // ---- group head ----
  groupHeatCps: 0.12,       // °C/s group heater at full power (thermostat to word114 °C)
  groupCouplingS: 900,      // s, group <-> brew boiler conduction time constant
  groupLossS: 1500,         // s, group -> ambient loss time constant
  groupFlowK: 0.004,        // 1/ml, water flowing through the group pulls it toward the boiler temperature

  // ---- grinder ----
  grindRateGs: 1.94,        // g/s dose rate (mcu_sim 19.4 units/s, live capture)
  grindSpinupS: 0.15,       // s, motor spin-up time constant
  grindStopOverUnits: 2,    // 0.1 g: the MCU stops at dose target + 2 (live: 187 at target 185)

  // ---- shot ----
  brewPressureBar: 9.0,     // bar, extraction pump pressure (pre-infusion uses word112's 3/4 bar)
  brewPumpTauS: 1.0,        // s, pressure rise time constant
  brewErosion: 0.12,        // fraction the pressure declines over ~25 s as the puck erodes
  brewFlowGs: 1.86,         // g/s mean weight rate in the cup (mcu_sim 18.6 units/s, live)
  brewFlowRise: 0.3,        // relative flow increase over the shot (puck erosion)
  brewFirstDropsS: 2.75,    // s after pre-infusion until the first drops reach the cup (mcu_sim drip_t = pre + 2.75)
  brewStopAheadG: 0.8,      // g, predictive stop: the MCU stops at target - 0.8 g (live 352/360)
  brewAfterDripG: 1.2,      // g that still drips into the cup after the stop (live +0.5..1.5 g)
  preFlowMlS: 3.6,          // ml/s inlet flow during pre-infusion (word52 ≈ 360)
  brewBurstMlS: 7.0,        // ml/s extra inlet flow when the pump goes to full pressure (headspace fill, word52 ≈ 919)
  brewSafetyS: 180,         // s, the MCU's brew/water safety stop (no weight reached, e.g. no cup)
  pAnchorRaw: 358,          // raw word51 at the anchor pressure (the UI's P_ANCHOR_RAW)
  bit9Glitch: true,         // the captured short work_sta bit-9 pulse 1.14..1.36 s into a pre-infusion

  // ---- hot water ----
  waterFlowGs: 6.38,        // g/s into the cup (mcu_sim 63.8 units/s)
  waterFlowMlS: 6.1,        // ml/s at the inlet flowmeter (word52 ≈ 610)
  waterPressureBar: 1.5,    // bar, free dispensing
  waterOvershootMaxG: 1.0,  // g, in-flight water after an auto-stop (random 0..max, mcu_sim randint(0,10))
  waterSettleG: 1.1,        // g the reading settles back over ~2 s after the stop (mcu_sim w_end = w - 11)

  // ---- milk ----
  milkRateScale: 1.0,       // x mcu_sim's milk ramp (_milk_rate: 0.98 -> 1.40 -> 1.26 °C/s)
  milkOvershootC: 3.9,      // °C the milk keeps rising after an auto-stop (~11 s, live)
  milkProbeWarmC: 3.0,      // °C the idle probe reads above ambient (mcu_sim idle 23.0 °C)
  milkCoolK: 6.5e-4,        // 1/s Newton cooling of the milk/probe toward the idle temperature

  // ---- brew scale ----
  cupG: 92.9,               // g, the cup (removing it after a tare reads -92.9 g = mcu_sim cup_removed 64607)
});

// ===================================================================================================== protocol helpers
/** CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF). */
export function crc16modbus(bytes, start = 0, end = bytes.length) {
  let crc = 0xFFFF;
  for (let i = start; i < end; i++) {
    crc ^= bytes[i];
    for (let k = 0; k < 8; k++) crc = (crc & 1) ? ((crc >>> 1) ^ 0xA001) : (crc >>> 1);
  }
  return crc;
}

function withCrc(body) {
  const out = new Uint8Array(body.length + 2);
  out.set(body);
  const c = crc16modbus(body);
  out[body.length] = c & 0xFF;
  out[body.length + 1] = c >>> 8;
  return out;
}

/** AA 55 CMD SUB LEN(words) payload(16-bit LE) CRC16 LE. */
export function buildFrame(cmd, sub, words) {
  const body = new Uint8Array(5 + 2 * words.length);
  body[0] = 0xAA; body[1] = 0x55; body[2] = cmd & 0xFF; body[3] = sub & 0xFF; body[4] = words.length & 0xFF;
  words.forEach((w, i) => { body[5 + 2 * i] = w & 0xFF; body[6 + 2 * i] = (w >>> 8) & 0xFF; });
  return withCrc(body);
}

/** 7-byte cmd-3 read query (no payload). */
export function buildQuery(sub, nwords) {
  return withCrc(Uint8Array.of(0xAA, 0x55, 0x03, sub & 0xFF, nwords & 0xFF));
}

/** The MCU's fixed 0x89 version request: AA 55 89 01 00 00 6F 81. */
export function buildVersionRequest() {
  return Uint8Array.of(0xAA, 0x55, 0x89, 0x01, 0x00, 0x00, 0x6F, 0x81);
}

/** cmd-9 version reply (display -> MCU): LEN is BYTES, sub = 0. */
export function buildVersionResponse(ver) {
  const vb = Array.from(ver, (c) => c.charCodeAt(0) & 0x7F);
  return withCrc(Uint8Array.from([0xAA, 0x55, 0x09, vb.length & 0xFF, 0x00, ...vb]));
}

/** cmd-8 power/time frame: fixed 15 bytes (10-byte payload: year u16 LE, month, day, hour, minute, second, alarm d/h/m). */
export function buildPowerFrame(payload10) {
  if (payload10.length !== 10) throw new Error('cmd8 payload must be 10 bytes');
  return withCrc(Uint8Array.from([0xAA, 0x55, 0x08, ...payload10]));
}

/** Resynchronizing AA 55 parser — a line-by-line port of proto.FrameParser (same counters, same resync).
 *  feed() returns frames {cmd, sub, n (cmd3: words requested), words (word frames), payload (Uint8Array for
 *  cmd 8/0x88/9/0x89), bytes (the whole frame incl. CRC)}. */
export class FrameParser {
  constructor() { this.buf = []; this.crcOk = 0; this.crcBad = 0; this.garbage = 0; }

  feed(data) {
    const buf = this.buf;
    for (const b of data) buf.push(b);
    const out = [];
    let i = 0;
    while (i + 7 <= buf.length) {
      if (buf[i] !== 0xAA || buf[i + 1] !== 0x55) { i++; this.garbage++; continue; }
      const cmd = buf[i + 2];
      let sub, ln, total;
      if (cmd === 0x09 || cmd === 0x89) {        // version frames: [3]=LEN(bytes), [4]=SUB
        sub = buf[i + 4]; ln = buf[i + 3]; total = ln + 7;
      } else {
        sub = buf[i + 3]; ln = buf[i + 4];
        if (cmd === 0x03) total = 7;             // read query: LEN = expected words, no payload
        else if (cmd === 0x08 || cmd === 0x88) total = 15;
        else total = 2 * ln + 7;
      }
      if (total < 7 || i + total > buf.length) break;       // wait for more bytes
      let crc = 0xFFFF;
      for (let k = i; k < i + total - 2; k++) {
        crc ^= buf[k];
        for (let j = 0; j < 8; j++) crc = (crc & 1) ? ((crc >>> 1) ^ 0xA001) : (crc >>> 1);
      }
      const crcRx = buf[i + total - 2] | (buf[i + total - 1] << 8);
      if (crc !== crcRx) { this.crcBad++; i++; continue; }  // resync like the original
      this.crcOk++;
      const bytes = Uint8Array.from(buf.slice(i, i + total));
      if (cmd === 0x03) out.push({ cmd, sub, n: ln, words: null, payload: null, bytes });
      else if (cmd === 0x09 || cmd === 0x89) out.push({ cmd, sub, words: null, payload: bytes.slice(5, total - 2), bytes });
      else if (cmd === 0x08 || cmd === 0x88) out.push({ cmd, sub, words: null, payload: bytes.slice(3, total - 2), bytes });
      else {
        const words = [];
        for (let k = 0; k < ln; k++) words.push(bytes[5 + 2 * k] | (bytes[6 + 2 * k] << 8));
        out.push({ cmd, sub, words, payload: null, bytes });
      }
      i += total;
    }
    buf.splice(0, i);
    if (buf.length > 4096) buf.splice(0, buf.length - 64);   // safety valve
    return out;
  }
}

export const s16 = (v) => { v &= 0xFFFF; return v >= 0x8000 ? v - 0x10000 : v; };
export const hex = (bytes) => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join(' ');
const pack112 = (sw, pr) => ((sw & 1) << 8) | (pr & 0xFF);
const pack113 = (ti, br) => ((ti & 0xFF) << 8) | (br & 0xFF);
const unpack112 = (w) => [(w >> 8) & 1, w & 0xFF];
const unpack113 = (w) => [(w >> 8) & 0xFF, w & 0xFF];

/** config.json -> setup words (proto.config_to_words, PARITY §1.3): byte packing 112/113 + boot hardcodes 5/13/15. */
export function configToWords(cfg = {}) {
  const g = (k, d) => (cfg[k] !== undefined && cfg[k] !== null ? Math.trunc(Number(cfg[k])) : d);
  return {
    0: 1, 1: 0, 2: g('grindMode', 1), 4: g('autoGrindWeight', 185), 5: 60, 11: g('brewTemper', 870), 13: 60,
    14: g('steamGears', 3), 15: 60, 17: g('autoHotWaterTarget', 400), 23: g('autobrewWeight', 360),
    25: g('AtuoSteamTarge', 650),
    112: pack112(g('preInfusionSwitch', 1), g('preInfusionPressure', 4)),
    113: pack113(g('preInfusionTime', 6), g('breakTime', 5)),
    114: g('headTemper', 70),
  };
}

/** parity_spec.json -> {word: [min, max, step]} for plain single-word parameters (proto.word_bounds). */
export function wordBounds(spec) {
  const out = {};
  for (const prm of spec?.parameters || []) {
    if (prm.packing || !prm.words || !prm.words.length) continue;
    if (prm.min === null || prm.min === undefined || prm.max === null || prm.max === undefined) continue;
    if (prm.words.length === 1) out[prm.words[0]] = [prm.min, prm.max, prm.step || 1];
  }
  return out;
}

/** Serial number words 96..111 (proto.serial_words, "B000000-0000-0000" + NULs). */
export function serialWords(serial = 'B000000-0000-0000') {
  const b = new Uint8Array(32);
  for (let i = 0; i < serial.length && i < 32; i++) b[i] = serial.charCodeAt(i);
  const out = {};
  for (let i = 0; i < 16; i++) out[96 + i] = b[2 * i] | (b[2 * i + 1] << 8);
  return out;
}

/** Words 62/63 as the MCU's boiler_heat writes them (mcu_sim.heat_stage, registermap §2.7). */
export function heatStage(r) {
  const e = r[40], s = r[47], ws = r[33];
  let stE = e < 400 ? 0 : e < 600 ? 30 : e < r[11] ? 60 : 100;
  const tgt = { 1: 1200, 2: 1250, 3: 1300 }[r[14]] ?? 1350;
  let stS = s < 500 ? 0 : s < 800 ? 30 : s < tgt - 100 ? 60 : 100;
  if (ws & 0x1000) stE = 100;
  if (ws & 0x0020) stS = 100;
  return [stE, stS];
}

/** The UI's phase logic (proto.phase_of = merakiplus phase_raw()). */
export function phaseOf(r, linkUp = true) {
  if (!linkUp) return 'OFFLINE';
  const ws = r[33];
  if ((ws & 0x4020) === 0x4020) return 'STEAM';
  if (ws & 0x0040) return 'GRIND';
  if ((ws & 0x0006) === 0x0006) return (ws & 0x0200) ? 'EXT' : 'PRE';
  if ((ws & 0x000A) === 0x000A) return 'WATER';
  if (r[34] || r[35] || r[36]) return 'ERROR';
  if (r[1] === 1 || !(ws & 0x0100)) return 'SLEEP';
  if (r[40] < r[11] - 20) return 'HEAT';
  return 'READY';
}

const REG_NAMES = {
  0: 'power', 1: 'sleep_drain', 2: 'grind_mode', 3: 'grind_start_trigger', 4: 'grind_auto_weight',
  5: 'grind_over_time', 6: 'grind_tare_trigger', 7: 'grind_run', 8: 'self_inspection_mode', 9: 'brew_start_trigger',
  10: 'reserved10', 11: 'brew_set_temp', 12: 'reserved12', 13: 'brew_recipe_param', 14: 'steam_gear',
  15: 'steam_recipe_param', 16: 'hotwater_auto', 17: 'hotwater_weight', 18: 'steam_set_temp', 19: 'hotwater_sw',
  20: 'water_target', 21: 'reserved21', 22: 'brew_active_flag', 23: 'brew_auto_weight', 24: 'steam_switch',
  25: 'steam_milk_set_temp', 26: 'grind_k_calib', 27: 'extract_k_calib', 28: 'drain_param1', 29: 'drain_param2',
  30: 'reboot_request', 31: 'reserved31', 32: 'reserved32', 33: 'work_sta', 34: 'E1_fault', 35: 'E2_fault',
  36: 'E3_fault', 37: 'grinder_gear', 38: 'grind_weight_cal', 39: 'brew_weight_cal', 40: 'e_boiler_temp',
  41: 'e_boiler_temp_mirror', 42: 'e_head_temp', 43: 'self_inspection_sta', 44: 'input_bits', 45: 'flowmeter_brew',
  46: 'flowmeter_steam', 47: 's_boiler_temp', 48: 'milk_temp', 49: 'mains_voltage_V', 50: 'current_ch1',
  51: 'pressure_candidate', 52: 'flow_rate', 53: 'grind_weight_raw', 54: 'grind_k', 55: 'brew_weight_raw',
  56: 'extract_k', 57: 'flowmeter_pulse', 58: 'current_ch2', 59: 'gears_min', 60: 'gears_5', 61: 'mains_freq_Hz',
  62: 'e_boiler_power', 63: 's_boiler_power', 64: 'gears_25', 65: 'gears_max', 66: 'brew_counter', 67: 'set_voltage',
  68: 's_water_level', 69: 'grinder_position_adc', 70: 'e_water_level', 71: 'Mboard_temp', 72: 'Dboard_temp',
  73: 'self_inspection_result', 74: 'gears_fu4', 75: 'gears_40', 112: 'pro_sw_preinf', 113: 'preinf_time_break',
  114: 'head_set_temp', 128: 'e_head_temp_intern',
};
/** Register name as in proto.reg_name. */
export function regName(n) {
  if (REG_NAMES[n]) return REG_NAMES[n];
  if (n >= 76 && n < 80) return `reserved${n}`;
  if (n >= 80 && n < 88) return `UID_A[${n - 80}]`;
  if (n >= 88 && n < 96) return `UID_B[${n - 88}]`;
  if (n >= 96 && n < 112) return `serial[${n - 96}]`;
  if (n >= 115 && n < 128) return `reserved${n}`;
  return `word${n}`;
}

// ===================================================================================================== small utilities
/** mulberry32 — tiny deterministic PRNG with a Python-random-like surface. */
class Rng {
  constructor(seed) { this.a = (seed >>> 0) || 1; }
  random() {
    let a = (this.a = (this.a + 0x6D2B79F5) | 0);
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  }
  uniform(a, b) { return a + (b - a) * this.random(); }
  randint(a, b) { return a + Math.floor(this.random() * (b - a + 1)); }   // inclusive, like Python
  choice(arr) { return arr[Math.floor(this.random() * arr.length)]; }
}

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const approach = (cur, target, step) => (cur < target ? Math.min(target, cur + step) : Math.max(target, cur - step));
const daysInMonth = (y, mo) => new Date(Date.UTC(y, mo, 0)).getUTCDate();
const r4 = (t) => Math.round(t * 1e4) / 1e4;

/** mcu_sim._milk_rate (units/s = 0.1 °C/s), with the 600-unit step smoothed over 580..620. */
function milkRate(temp) {
  if (temp < 150) return 9.8;
  if (temp < 250) return 9.8 + (temp - 150) * 0.042;
  if (temp < 580) return 14.0;
  if (temp < 620) return 14.0 - (temp - 580) * (1.4 / 40);
  return 12.6;
}

/** A boiler: a water+element node coupled to a body node, regulated by a PI thermostat on a lagged NTC. */
class Boiler {
  constructor(temp) { this.reset(temp); }
  reset(temp) { this.w = temp; this.b = temp; this.el = 0; this.sens = temp; this.I = 0; this.duty = 0; }
}

// ===================================================================================================== the machine
export class Machine {
  constructor({ config, spec, seed = 1, params = {} } = {}) {
    this.params = { ...DEFAULT_PARAMS, ...params };
    this.cfg = config || { ...BASELINE_CONFIG };
    this.bounds = spec ? wordBounds(spec) : { ...FALLBACK_BOUNDS };
    this.rng = new Rng(seed);
    this.t = 0;
    this.regs = new Uint16Array(REGN);
    this.events = [];
    this.onFrame = null;
    this.stats = {
      writes_ok: 0, writes_rejected: 0, acks_sent: 0, acks_dropped: 0, queries: 0, crc_bad_rx: 0,
      garbage_tx_bytes: 0, version_reqs: 0, boot_pushes: 0, reboots: 0, dvale_entries: 0, power_wakes: 0,
      dvale_rx_dropped: 0, rx_frames: 0, tx_frames: 0, shots: 0, grinds: 0, waters: 0, steams: 0, flushes: 0,
    };
    this.parser = new FrameParser();
    this.txq = [];          // [{due, seq, bytes, meta}]
    this.txseq = 0;
    this.outbox = [];       // due bytes waiting for takeTx()
    this.link = 'up';
    this.garbageUntil = -1; this.garbageCrc = false;
    // power / RTC
    this.power = 'on';
    this.dvaleAt = null;
    this.keyDown = false; this.keyT0 = 0; this.keyUpAt = null;
    this.rtc = null; this.rtcFrac = 0; this.alarm = null; this.alarmFired = false;
    // physics
    const amb = this.params.ambientC;
    this.amb = amb;
    this.eb = new Boiler(amb);
    this.sb = new Boiler(amb);
    this.group = { t: amb, sens: amb, I: 0, duty: 0 };
    this.milk = amb + this.params.milkProbeWarmC;
    this.pBar = 0; this.pTau = 0.3;
    this.flowIn = 0;        // ml/s at the inlet flowmeter
    this.flowOut = 0;       // g/s into the cup
    this.grindFlow = 0;     // g/s into the grind cup
    this.scale = { cupOn: true, liquid: 0, tare: this.params.cupG, manual: null, offset: 0 };
    this.gscale = { content: 0, tare: 0, manual: null };
    this.level = { s: SENSOR_BASELINE[68], e: SENSOR_BASELINE[70] };
    this.steamConsumed = false;
    this.natFaults = {};    // word -> bits raised by the over-temperature detector
    this.faults = {};       // word -> injected value
    this._initRegs(false);
    if (!this.params.coldStart) this._presetHot();
    this._publish(0);
    this._log('init', { seed, coldStart: !!this.params.coldStart });
  }

  // ------------------------------------------------------------------------------------------------- public: time/UART
  /** Advance sim time by dt seconds in <= 10 ms ticks (deterministic). */
  advance(dt) {
    const target = this.t + Math.max(0, Number(dt) || 0);
    while (this.t < target - 1e-9) {
      const h = Math.min(TICK, target - this.t);
      this.t += h;
      this._tick(h);
    }
  }

  /** Bytes the DISPLAY wrote to the UART (Uint8Array / array). Frames are handled at the current sim time. */
  rx(bytes) {
    const before = this.parser.crcBad;
    for (const f of this.parser.feed(bytes)) {
      this.stats.rx_frames++;
      const note = this._handleFrame(f);
      this._emit('rx', f, note);
    }
    this.stats.crc_bad_rx += this.parser.crcBad - before;
  }

  /** MCU->display bytes due at the current sim time (drained). */
  takeTx() {
    if (!this.outbox.length) return new Uint8Array(0);
    let n = 0;
    for (const b of this.outbox) n += b.length;
    const out = new Uint8Array(n);
    let o = 0;
    for (const b of this.outbox) { out.set(b, o); o += b.length; }
    this.outbox = [];
    return out;
  }

  get versionDone() { return this.versionReceived; }

  // ------------------------------------------------------------------------------------------------- public: inputs
  /** Jump boiler/group (and optionally milk) temperatures now, °C. Body nodes follow so the jump sticks. */
  setBoilers({ brew, steam, group, milk } = {}) {
    const hot = (b, temp, set) => {
      b.reset(temp);
      // preset the integrator to the steady-state duty when the jump lands near the setpoint (no artificial dip)
      if (Math.abs(temp - set) < 3) b.I = this._steadyDuty(b === this.eb ? 'e' : 's', temp);
    };
    if (Number.isFinite(brew)) hot(this.eb, brew, this._eSet());
    if (Number.isFinite(steam)) hot(this.sb, steam, this._sSet());
    if (Number.isFinite(group)) { this.group.t = group; this.group.sens = group; }
    if (Number.isFinite(milk)) this.milk = milk;
    if (this.power === 'on') this._publishTemps(true);
    this._log('boilers', { brew, steam, group, milk, st62: this.regs[62], st63: this.regs[63] });
  }

  /** "Ready" preset: jump brew/steam boiler and group head to their current setpoints (stages 62/63 -> 100 next tick). */
  preheat() {
    this.setBoilers({ brew: this._eSet(), steam: this._sSet(), group: this.regs[114] || 70 });
  }

  /** "Cold morning" preset: everything at ambient. */
  coldStart() {
    this.setBoilers({ brew: this.amb, steam: this.amb, group: this.amb, milk: this.amb + this.params.milkProbeWarmC });
  }

  setAmbient(c) { if (Number.isFinite(c)) { this.amb = c; this.params.ambientC = c; this._log('ambient', { c }); } }

  /** Live-tune physics (any DEFAULT_PARAMS key). */
  setParams(partial = {}) {
    Object.assign(this.params, partial);
    if ('ambientC' in partial) this.amb = this.params.ambientC;
    this._log('params', { keys: Object.keys(partial) });
  }

  /** Steam wand switch (PE.2): word44 bit0 + word46. The edge starts/stops the flush path (latch=0). */
  setWand(on) {
    on = !!on;
    if (on === this.wandState) return;
    const r = this.regs;
    this.wandState = on;
    if (this.power === 'on') {
      r[44] = on ? (r[44] | 1) : (r[44] & ~1);
      r[46] = on ? 1 : 0;
    }
    this._log('wand', { on, input_bits: r[44], ord46: r[46] });
    if (this.power !== 'on') return;
    if (on) {
      if (!this.steam && r[0] === 1 && r[1] === 0 && r[63] === 100 && !this._fault()) this._startSteam(0, 'wand');
    } else if (this.steam && !this.steam.latch) {
      this._stopSteam('wand released');
    }
  }

  /** Cup on the brew scale. Removing it reads -(cup + its liquid) relative to the tare; putting a cup back places an
   *  EMPTY cup (you drank it), which reads 0 again if the scale was tared with a cup. */
  setCup(on) {
    on = !!on;
    if (on === this.scale.cupOn) return;
    this.scale.cupOn = on;
    if (on) this.scale.liquid = 0;
    this._log('cup', { on });
  }

  /** Zero the 'brew' (word 39) or 'grind' (word 38) scale. */
  tare(scale = 'brew') {
    if (scale === 'grind') { this.gscale.tare = this.gscale.content; this.gscale.manual = null; }
    else { this.scale.tare = this._gross(); this.scale.offset = 0; this.scale.manual = null; }
    this._log('tare', { scale });
  }

  /** Hold a manual reading (g) on a scale until the next activity/tare or setScaleWeight(null). */
  setScaleWeight(grams, scale = 'brew') {
    const s = scale === 'grind' ? this.gscale : this.scale;
    s.manual = grams === null || grams === undefined ? null : Number(grams);
    this._log('scale_weight', { scale, grams: s.manual });
  }

  /** Raw fault words (e.g. {e1: 32, e2: 16, e3: 16}); 0/undefined clears that one. */
  setFaults({ e1, e2, e3 } = {}) {
    [[34, e1], [35, e2], [36, e3]].forEach(([w, v]) => {
      if (v) this.faults[w] = v & 0xFFFF; else delete this.faults[w];
      if (this.power === 'on') this.regs[w] = (this.faults[w] || 0) | (this.natFaults[w] || 0);
    });
    this._log('fault_set', { e1: e1 || 0, e2: e2 || 0, e3: e3 || 0 });
  }

  clearFaults() {
    this.faults = {}; this.natFaults = {};
    if (this.power === 'on') this.regs[34] = this.regs[35] = this.regs[36] = 0;
    this._log('fault_clear', { word: 'all' });
  }

  /** UART silent both ways (the MCU keeps running). */
  setOffline(on) {
    const state = on ? 'silent' : 'up';
    if (state === this.link) return;
    this.link = state;
    this._log('link_state', { state });
  }

  /** Physical power button. down=true press, false release; held >= 0.8 s in deep sleep wakes (factory reboot). */
  powerKey(down) {
    if (down) {
      if (!this.keyDown) { this.keyDown = true; this.keyT0 = this.t; this.keyUpAt = null; this._log('power_key', { value: 1 }); }
    } else {
      this.keyUpAt = null;
      this._powerKeyRelease();
    }
  }

  /** Press and auto-release after `seconds` (mcu_sim power_key action "hold"). */
  powerKeyHold(seconds = 1.6) {
    this.keyDown = true; this.keyT0 = this.t; this.keyUpAt = this.t + seconds;
    this._log('power_key', { value: 1, hold_s: seconds });
  }

  /** Deep sleep now without a UART write (as if word0=0 had been written). */
  powerOff() {
    if (this.power === 'on') {
      this.regs[0] = 0;
      this._log('power_write', { word: 0, value: 0, source: 'inject' });
      this._enterDvale();
    }
  }

  /** Wake/reset without a button hold (like the alarm). */
  powerOn() { this._powerWake('ctrl'); }

  /** CPU reset. factory: the MCU start values (the display's boot push restores the config); else config preloaded. */
  reboot({ factory = false } = {}) { this._doReboot(factory, 'ctrl'); }

  setGrindRate(gps) { if (Number.isFinite(gps) && gps > 0) { this.params.grindRateGs = gps; this._log('grind_rate', { gps }); } }

  setAlarm(day = 0, hour = 0, minute = 0) {
    this.alarm = (day || hour || minute) ? [day, hour, minute] : null;
    this.alarmFired = false;
    this._log('alarm_set', { alarm: this.alarm || [0, 0, 0] });
  }

  injectGarbage(seconds = 2, crcToo = true) {
    this.garbageUntil = this.t + seconds; this.garbageCrc = !!crcToo;
    this._log('garbage_start', { dur: seconds });
  }

  setReg(word, value) {
    this.regs[word] = value & 0xFFFF;
    this._log('set_reg', { word, value: this.regs[word] });
  }

  setMilk(c) { if (Number.isFinite(c)) { this.milk = c; this.regs[48] = Math.round(c * 10); } }

  /** Plain object for the viewer. */
  snapshot() {
    const r = this.regs, a = this.act, sc = this.steam;
    const faults = { e1: r[34], e2: r[35], e3: r[36] };
    let activity = null;
    if (a) {
      activity = { kind: a.kind, elapsed: r4(this.t - a.t0), target: a.target };
      if (a.kind === 'brew') Object.assign(activity, { pre: a.pre, stopAt: a.stopW, phase: this.t - a.t0 < a.pre + 0.55 ? 'pre-infusion' : 'extraction' });
      if (a.kind === 'water') activity.manual = a.manual;
    } else if (sc) {
      activity = { kind: sc.latch ? 'steam' : 'flush', elapsed: r4(this.t - sc.t0), target: sc.target };
    }
    return {
      t: r4(this.t), power: this.power, link: this.link, mode: this.mode, phase: this._phaseName(),
      brewC: +this.eb.sens.toFixed(2), steamC: +this.sb.sens.toFixed(2), groupC: +this.group.sens.toFixed(2),
      milkC: +this.milk.toFixed(2), ambientC: this.amb,
      brewSetC: +this._eSet().toFixed(1), steamSetC: +this._sSet().toFixed(1),
      pressureBar: +this.pBar.toFixed(2), flowMlS: +this.flowIn.toFixed(2),
      brewG: s16(r[39]) / 10, grindG: s16(r[38]) / 10, stage62: r[62], stage63: r[63],
      heaters: { brew: +this.eb.duty.toFixed(2), steam: +this.sb.duty.toFixed(2), group: +this.group.duty.toFixed(2) },
      wand: !!this.wandState, cup: this.scale.cupOn, faults, workSta: r[33], versionDone: this.versionReceived,
      activity, steam: sc ? { latch: sc.latch, elapsed: r4(this.t - sc.t0), target: sc.target } : null,
      stats: { ...this.stats },
    };
  }

  // ------------------------------------------------------------------------------------------------- init / reset
  _initRegs(factory) {
    const r = this.regs;
    r.fill(0);
    for (const [w, v] of Object.entries(MCU_DEFAULTS)) r[w] = v;
    for (const [w, v] of Object.entries(SENSOR_BASELINE)) r[w] = v;
    for (const [w, v] of Object.entries(serialWords())) r[w] = v;
    if (!factory) for (const [w, v] of Object.entries(configToWords(this.cfg))) r[w] = v & 0xFFFF;
    this.adcBase = { 49: r[49], 69: r[69], 71: r[71], 72: r[72] };
    this.wrawBase = { 53: r[53], 55: r[55] };
    this.mode = 'idle';
    this.act = null; this.pending = null; this.post = null;
    this.steam = null; this.pendingSteam = null; this.steamClearAt = null; this.steamPost = null;
    this.pulse = null; this.nextPulse = this.t + this.rng.uniform(2.0, 6.0);
    this.wandState = this.wandState || false;
    if (this.wandState) { r[44] |= 1; r[46] = 1; }
    this.versionReceived = false;
    this.nextVer = this.drv ? this.t + 0.05 : 0.0;   // first boot: immediately; after a reset: +50 ms (mcu_sim)
    this.firstRxT = null;
    this.bootPushSetup = this.bootPushPreinf = false;
    this.bootChecked = false;
    this.lastAwakeWrite = this.t;
    this.keepaliveLoggedAt = -1e9;
    this.lastWriteT = -1e9;
  }

  _presetHot() {
    const e = this._eSet(), s = this._sSet();
    this.eb.reset(e); this.eb.I = this._steadyDuty('e', e);
    this.sb.reset(s); this.sb.I = this._steadyDuty('s', s);
    const g = this.regs[114] || 70;
    this.group.t = this.group.sens = g;
  }

  /** Duty that holds a boiler at temp in steady state (all heat goes through the body to ambient). */
  _steadyDuty(which, temp) {
    const p = this.params;
    const a = which === 'e' ? p.brewHeatCps : p.steamHeatCps;
    const m = which === 'e' ? p.brewBodyRatio : p.steamBodyRatio;
    return clamp((p.lossPerS * m * (temp - this.amb)) / a, 0, 0.8);
  }

  _eSet() {
    const p = this.params;
    return Number.isFinite(p.brewSetpointC) ? p.brewSetpointC : this.regs[11] / 10 + p.brewOffsetC;
  }

  _sSet() {
    const p = this.params;
    if (Number.isFinite(p.steamSetpointC)) return p.steamSetpointC;
    return ({ 1: 120, 2: 125, 3: 130 })[this.regs[14]] ?? 135;
  }

  // ------------------------------------------------------------------------------------------------- logging / tx
  _log(kind, data = {}) {
    this.events.push({ t: r4(this.t), kind, ...data });
    if (this.events.length > EVENT_CAP) this.events.splice(0, this.events.length - EVENT_CAP + 200);
  }

  _emit(dir, f, note) {
    if (typeof this.onFrame !== 'function') return;
    const n = f.cmd === 0x03 ? f.n : f.words ? f.words.length : f.payload ? f.payload.length : 0;
    try {
      this.onFrame(dir, { t: r4(this.t), cmd: f.cmd, sub: f.sub, len: n, words: f.words ? f.words.slice() : null, bytes: f.bytes, note: note || '' });
    } catch (e) { /* a viewer callback must never break the model */ }
  }

  _sendTx(bytes, delay, meta) {
    if (this.link !== 'up' || this.power === 'off') return;
    this.txseq++;
    this.txq.push({ due: this.t + delay, seq: this.txseq, bytes, meta });
  }

  _flushTx() {
    if (!this.txq.length) return;
    this.txq.sort((x, y) => x.due - y.due || x.seq - y.seq);
    while (this.txq.length && this.txq[0].due <= this.t + 1e-12) {
      const item = this.txq.shift();
      if (this.link !== 'up' || this.power === 'off') continue;
      this.outbox.push(item.bytes);
      this.stats.tx_frames++;
      this._emit('tx', { ...item.meta, bytes: item.bytes }, item.meta.note);
    }
  }

  // ------------------------------------------------------------------------------------------------- frames
  _handleFrame(f) {
    if (this.power === 'off') { this.stats.dvale_rx_dropped++; return 'dropped (deep sleep)'; }
    if (this.link === 'silent') return 'dropped (offline)';
    if (this.firstRxT === null) { this.firstRxT = this.t; this._log('link_connect'); }
    switch (f.cmd) {
      case 0x03: return this._handleQuery(f.sub, f.n);
      case 0x06: return this._handleWrite(f.sub, f.words);
      case 0x08: {
        const p = f.payload;
        this.rtc = { y: p[0] | (p[1] << 8), mo: p[2], d: p[3], h: p[4], mi: p[5], s: p[6] };
        this.rtcFrac = 0;
        const [ad, ah, am] = [p[7], p[8], p[9]];
        this.alarm = (ad || ah || am) ? [ad, ah, am] : null;
        this.alarmFired = false;
        this._log('time_sync', { payload: hex(p), alarm: [ad, ah, am] });
        const echo = withCrc(Uint8Array.from([0xAA, 0x55, 0x88, ...p]));
        this._sendTx(echo, 0.005, { cmd: 0x88, sub: p[0], words: null, payload: p, note: 'time echo' });
        return this.alarm ? 'time sync + alarm' : 'time sync';
      }
      case 0x09: {
        this.versionReceived = true;
        const ver = String.fromCharCode(...f.payload).replace(/\0+$/, '');
        this._log('version_handshake', { version: ver });
        return `version ${ver}`;
      }
      default:
        this._log('unknown_cmd', { cmd: f.cmd, sub: f.sub });
        return 'unknown cmd';
    }
  }

  _handleQuery(sub, n) {
    if (sub + n > REGN) { this._log('read_rejected', { sub, len: n }); return 'rejected: read range'; }
    this.stats.queries++;
    const words = Array.from(this.regs.subarray(sub, sub + n));
    this._sendTx(buildFrame(0x83, sub, words), POLL_LATENCY, { cmd: 0x83, sub, words, note: 'reply' });
    return 'poll';
  }

  /** Band + trigger + range checks (mcu_sim._check_write). Returns reasons[]. */
  _checkWrite(sub, words) {
    const reasons = [];
    const n = words.length, end = sub + n;
    const bandOk = end < 33 || end === 113 || end === 114 || end === 115;
    if (!bandOk || end > REGN) reasons.push(`band:sub${sub}+len${n}=${end}`);
    words.forEach((w, i) => { if (TRIGGER_WORDS.includes(sub + i) && w !== 0) reasons.push(`trigger:word${sub + i}=${w}`); });
    if (this.params.rangePolicy !== 'off') {
      words.forEach((w, i) => {
        const word = sub + i;
        if (this.bounds[word]) {
          const [lo, hi] = this.bounds[word];
          if (!(lo <= w && w <= hi)) reasons.push(`range:word${word}=${w}∉[${lo},${hi}]`);
        } else if (BOOL_WORDS[word]) {
          const [lo, hi] = BOOL_WORDS[word];
          if (!(lo <= w && w <= hi)) reasons.push(`range:word${word}=${w}∉[${lo},${hi}]`);
        } else if (word === 112) {
          const [sw, pr] = unpack112(w);
          if (pr !== 3 && pr !== 4) reasons.push(`range:word112.tryk=${pr}∉[3,4]`);
          if (sw !== 0 && sw !== 1) reasons.push(`range:word112.switch=${sw}`);
        } else if (word === 113) {
          const [ti] = unpack113(w);
          if (ti < 5) reasons.push(`range:word113.tid=${ti}<5`);
        }
      });
    }
    return reasons;
  }

  _handleWrite(sub, words) {
    const n = words.length;
    const reasons = this._checkWrite(sub, words);
    const has = (p) => reasons.some((r) => r.startsWith(p));
    const named = {};
    words.forEach((w, i) => { named[regName(sub + i)] = w; });
    const entry = { sub, len: n, words: words.slice(), named };
    if (has('band:')) {
      this.stats.writes_rejected++;
      this._log('write_rejected', { verdict: 'band', reasons, ...entry });
      return 'rejected: band';
    }
    if (has('trigger:') && this.params.triggerPolicy === 'reject') {
      this.stats.writes_rejected++;
      this._log('write_rejected', { verdict: 'trigger', reasons, ...entry });
      return 'rejected: trigger';
    }
    const rangeV = reasons.filter((r) => r.startsWith('range:'));
    if (rangeV.length) {
      for (const r of rangeV) this._log('range_violation', { reason: r, ...entry });
      if (this.params.rangePolicy === 'reject') {
        this.stats.writes_rejected++;
        this._log('write_rejected', { verdict: 'range', reasons, ...entry });
        return 'rejected: range';
      }
    }
    // memcpy (REGISTERMAP §3: the MCU has no range checks)
    words.forEach((w, i) => { this.regs[sub + i] = w & 0xFFFF; });
    this.stats.writes_ok++;
    const prevWriteT = this.lastWriteT;
    this.lastWriteT = this.t;
    this._log('write', { verdict: 'ok', range_violations: rangeV, ...entry });
    words.forEach((w, i) => {
      const word = sub + i;
      if (word === 1) {
        if (w === 0) { this.lastAwakeWrite = this.t; this.keepaliveLoggedAt = -1e9; }
        else this._log('sleep_write', { word: 1, value: w });
      }
      if (word === 0) {
        this._log('power_write', { word: 0, value: w });
        if (w === 0 && this.power === 'on' && this.dvaleAt === null) this.dvaleAt = this.t + DVALE_DELAY;
      }
      if (word === 30 && w !== 0) {
        this._log('reboot_trigger', { word: 30, value: w });
        if (this.params.triggerPolicy === 'pass') this._doReboot(false, 'word30');
      }
      if ((word === 26 || word === 27) && w !== 0) this._log('calib_trigger', { word, value: w });
    });
    let note = 'write';
    if (sub === 1 && n === 30) { this.bootPushSetup = true; this.stats.boot_pushes++; this._log('boot_push', { block: 'sub1_len30' }); note = 'boot push'; }
    else if (sub === 112 && n === 3) { this.bootPushPreinf = true; this.stats.boot_pushes++; this._log('boot_push', { block: 'sub112_len3' }); note = 'boot push'; }
    else note = describeWrite(sub, words);
    this._scheduleAck(sub, words, prevWriteT);
    return note;
  }

  _scheduleAck(sub, words, prevWriteT) {
    const p = this._ackProfile();
    if (this.rng.random() < (p.loss || 0)) {
      this.stats.acks_dropped++;
      this._log('ack_dropped', { sub, len: words.length });
      return;
    }
    let heavy = false;
    words.forEach((w, i) => {
      const word = sub + i;
      if (word === 3 && w === 1) heavy = true;
      if (word === 9 && w === 1 && sub <= 22 && 22 < sub + words.length && words[22 - sub] === 1) heavy = true;
    });
    let ms = p.latency_ms;
    if (heavy && p.first_delay_ms && this.t - prevWriteT > 2.0) ms = p.first_delay_ms;
    ms += this.rng.uniform(-(p.jitter_ms || 0), p.jitter_ms || 0);
    this._sendTx(buildFrame(0x86, sub, words), Math.max(0.001, ms / 1000), { cmd: 0x86, sub, words: words.slice(), note: 'ACK' });
    this.stats.acks_sent++;
    this._log('ack', { sub, len: words.length, words: words.slice() });
  }

  _ackProfile() {
    const ap = this.params.ackProfile;
    return typeof ap === 'object' && ap ? ap : (ACK_PROFILES[ap] || ACK_PROFILES.normal);
  }

  // ------------------------------------------------------------------------------------------------- power
  _enterDvale() {
    const kind = this.act ? this.act.kind : null;
    const hadSteam = !!this.steam;
    if (kind) this._log('activity_stop', { activity: kind, reason: 'deep sleep (word0=0)', work_sta: '0x0' });
    if (hadSteam) this._log('activity_stop', { activity: 'steam', reason: 'deep sleep (word0=0)', work_sta: '0x0' });
    this.act = this.pending = this.post = null;
    this.steam = this.pendingSteam = this.steamClearAt = this.steamPost = null;
    this.pulse = null;
    this.regs[33] = 0; this.regs[62] = 0; this.regs[63] = 0;
    this.txq = [];
    this.power = 'off';
    this.dvaleAt = null;
    this.mode = 'off';
    this.stats.dvale_entries++;
    this._log('dvale_enter', { word0: 0, work_sta: 0, activity: kind, steam: hadSteam });
  }

  _powerWake(cause) {
    if (this.power !== 'off') return;
    this.stats.power_wakes++;
    this._log('power_wake', { cause });
    this._doReboot(true, cause);
  }

  _powerKeyRelease() {
    if (!this.keyDown) return;
    const held = this.t - this.keyT0;
    this.keyDown = false;
    this._log('power_key', { value: 0, held_s: r4(held) });
    if (this.power === 'off' && held >= POWER_KEY_HOLD_MIN - 1e-9) this._powerWake('power_key');
  }

  /** CPU reset. The physical temperatures survive a reset (the registers are re-derived from the physics). */
  _doReboot(factory, cause) {
    this.stats.reboots++;
    this._initRegs(factory);
    this.regs[33] = factory ? WS.IDLE : WS.SLEEP;
    const defaults = factory ? Object.fromEntries([0, 4, 11, 14, 17, 23, 25, 114].map((w) => [w, this.regs[w]])) : null;
    this._log('reboot', { factory, cause, defaults });
    if (this.alarm) this._log('alarm_cleared', { alarm: this.alarm, cause: 'reset' });
    this.alarm = null; this.alarmFired = false;   // the cmd8 alarm lives in MCU RAM
    this.power = 'on';
    this.dvaleAt = null;
    this.keyDown = false; this.keyUpAt = null;
    // injected faults persist (the fault condition is physical); natural ones are re-detected next tick
    this.flowIn = this.flowOut = this.grindFlow = 0;
    this._publish(0);
  }

  _tickRtc(dt) {
    if (!this.rtc) return;
    this.rtcFrac += dt;
    const r = this.rtc;
    while (this.rtcFrac >= 1.0) {
      this.rtcFrac -= 1.0;
      if (++r.s >= 60) { r.s = 0; r.mi++; }
      if (r.mi >= 60) { r.mi = 0; r.h++; }
      if (r.h >= 24) {
        r.h = 0; r.d++;
        if (r.d > daysInMonth(r.y, r.mo)) { r.d = 1; if (++r.mo > 12) { r.mo = 1; r.y++; } }
      }
    }
    if (this.alarm && !this.alarmFired && this.power === 'off'
        && r.d === this.alarm[0] && r.h === this.alarm[1] && r.mi === this.alarm[2]) {
      this.alarmFired = true;
      this._powerWake('alarm');
    }
  }

  // ------------------------------------------------------------------------------------------------- main tick
  _tick(dt) {
    if (this.dvaleAt !== null && this.t >= this.dvaleAt - 1e-9) this._enterDvale();
    if (this.keyUpAt !== null && this.t >= this.keyUpAt - 1e-9) { this.keyUpAt = null; this._powerKeyRelease(); }
    this._tickRtc(dt);
    if (this.power === 'off') {          // deep sleep: registers frozen, UART silent; the physics keeps cooling
      this._resetDrivers();
      this._physics(dt, false);
      return;
    }
    const r = this.regs;
    this._resetDrivers();
    this._tickFaults();
    this._tickTriggers();
    this._tickActivity(dt);
    this._tickSteam(dt);
    this._tickPost(dt);
    this._tickSteamPost(dt);
    this._physics(dt, r[0] === 1 && r[1] === 0);
    this._publish(dt);
    this._tickKeepalive();
    this._tickBootPushCheck();
    if (this.link === 'up') {
      if (!this.versionReceived && this.t >= this.nextVer - 1e-9) {
        this._sendTx(buildVersionRequest(), 0, { cmd: 0x89, sub: 0, words: null, payload: Uint8Array.of(0), note: 'version request' });
        this.stats.version_reqs++;
        this.nextVer = this.t + VER_INTERVAL;
      }
      if (this.t < this.garbageUntil) this._garbage();
      this._flushTx();
    }
  }

  _garbage() {
    const n = this.rng.randint(1, 24);
    const junk = new Uint8Array(n);
    for (let i = 0; i < n; i++) junk[i] = this.rng.randint(0, 255);
    this.outbox.push(junk);
    this.stats.garbage_tx_bytes += n;
    this._emit('tx', { cmd: null, sub: null, words: null, payload: null, bytes: junk }, 'garbage');
    if (this.garbageCrc && this.rng.random() < 0.3) {
      const bad = buildFrame(0x83, 33, Array.from(this.regs.subarray(33, 40)));
      bad[bad.length - 1] ^= 0xFF;
      this.outbox.push(bad);
      this._emit('tx', { cmd: 0x83, sub: 33, words: null, payload: null, bytes: bad }, 'garbage (bad CRC)');
    }
  }

  _resetDrivers() {
    // per-tick physics drivers, set by the activity/post ticks
    this.drv = { pTarget: 0, pTau: 0.33, flowIn: 0, flowOut: 0, grind: 0, steamDraw: 0, flushDraw: 0, milkHeat: false };
  }

  _fault() { const r = this.regs; return !!(r[34] || r[35] || r[36]); }

  _tickFaults() {
    const r = this.regs, p = this.params;
    if (p.autoFaults) {
      const nat = (w, bit, on, off) => {
        const cur = this.natFaults[w] || 0;
        if (on && !(cur & bit)) { this.natFaults[w] = cur | bit; this._log('fault_natural', { word: w, bit }); }
        else if (off && (cur & bit)) { this.natFaults[w] = cur & ~bit; this._log('fault_clear', { word: w, bit }); }
      };
      nat(36, 0x10, this.eb.sens > 120, this.eb.sens < 115);
      nat(36, 0x20, this.sb.sens > 160, this.sb.sens < 155);
      nat(34, 0x20, this.group.sens > 110, this.group.sens < 105);
    }
    for (const w of [34, 35, 36]) r[w] = ((this.faults[w] || 0) | (this.natFaults[w] || 0)) & 0xFFFF;
  }

  _tickTriggers() {
    const r = this.regs;
    const awake = r[0] === 1 && r[1] === 0;
    if (this.pending && this.t >= this.pending.at - 1e-9) {
      const kind = this.pending.kind;
      this.pending = null;
      if (this.mode === 'idle' && awake) this._startActivity(kind);
    }
    // display steam (word24=1) is an INDEPENDENT latch path (no wand needed), can start on top of a primary activity
    if (this.pendingSteam && this.t >= this.pendingSteam.at - 1e-9) {
      this.pendingSteam = null;
      if (awake && !this._fault()) this._startSteam(1, 'display');
    }
    // the MCU's delayed word24 reset after an auto-stop — BEFORE the trigger check
    if (this.steamClearAt !== null && this.t >= this.steamClearAt - 1e-9) {
      r[24] = 0; this.steamClearAt = null;
      this._log('steam_ord24_clear', { word: 24 });
    }
    if (r[24] !== 0 && !this.pendingSteam && (!this.steam || !this.steam.latch) && awake && !this._fault()
        && this.steamClearAt === null) {
      this.pendingSteam = { at: this.t + 0.30 };
    }
    // primary triggers (only in idle — like the UI's guards)
    if (this.mode !== 'idle' || this.pending || !awake) return;
    if (r[3] !== 0) this.pending = { kind: 'grind', at: this.t + 0.39 };
    else if (r[9] !== 0) {
      if (r[16] === 1 || r[19] === 1) this.pending = { kind: 'water', at: this.t + 0.18 };
      else if (r[22] === 1) this.pending = { kind: 'brew', at: this.t + 0.17 };
      else { r[9] = 0; this.tare('brew'); }          // plain tare without a product flag
    }
  }

  _startActivity(kind) {
    const r = this.regs, p = this.params;
    this.post = null;
    if (kind === 'grind') {
      const target = r[4];
      this.act = { kind, t0: this.t, target, stopW: target + p.grindStopOverUnits };
      r[3] = 0;
      this.gscale.tare = this.gscale.content; this.gscale.manual = null;   // tare
      r[38] = 0;
      r[33] = WS.GRIND;
      this.stats.grinds++;
    } else if (kind === 'brew') {
      const target = r[23];
      const [sw, pPre] = unpack112(r[112]);
      const pre = sw ? unpack113(r[113])[0] : 0;
      this.act = {
        kind, t0: this.t, target, pre, stopW: Math.max(1, target - Math.round(p.brewStopAheadG * 10)),
        dripT: pre + p.brewFirstDropsS, pPre: pPre >= 1 && pPre <= 15 ? pPre : 4,
        bit9Done: pre < 2 || !p.bit9Glitch, flushOverlap: false,
      };
      r[9] = 0;
      this.scale.tare = this._gross(); this.scale.manual = null; this.scale.offset = -0.3;   // tare ~ -0.3 g
      r[39] = 0xFFFD;
      r[33] = WS.BREW_PRE;
      this.stats.shots++;
    } else if (kind === 'water') {
      const target = r[17];
      const manual = r[16] === 0 && r[19] === 1;
      this.act = { kind, t0: this.t, target, manual, overshootG: this.rng.randint(0, 10) / 10 * (p.waterOvershootMaxG / 1.0) };
      r[9] = 0;
      this.scale.tare = this._gross(); this.scale.manual = null; this.scale.offset = 0;
      r[39] = 0;
      r[33] = WS.WATER;
      this.stats.waters++;
    }
    this.mode = kind;
    this._log('activity_start', { activity: kind, target: this.act.target, work_sta: '0x' + r[33].toString(16) });
  }

  _startSteam(latch, src) {
    const r = this.regs;
    if (this.steam) Object.assign(this.steam, { latch: 1, t0: this.t, target: r[25], src: 'display' });
    else this.steam = { latch, t0: this.t, target: latch ? r[25] : null, src };
    this.steamPost = null;
    if (!this.act) this.mode = 'steam';
    r[33] |= 0x4020;
    if (latch) this.stats.steams++; else this.stats.flushes++;
    this._log('activity_start', { activity: 'steam', target: latch ? r[25] : null, work_sta: '0x' + r[33].toString(16), latch, src });
  }

  _stopSteam(reason) {
    const r = this.regs;
    const sc = this.steam || {};
    const auto = !!sc.latch && reason.includes('milk');
    r[33] &= ~0x4020;
    const milkStop = this.milk;
    if (sc.latch) {
      if (auto) {
        this.steamClearAt = this.t + 0.18;
        this.steamPost = { t0: this.t, auto: true, peak: milkStop + this.params.milkOvershootC };
      } else {
        r[24] = 0; this.steamClearAt = null;
        this.steamPost = { t0: this.t, auto: false, peak: milkStop + this.rng.randint(1, 8) / 10 };
      }
    } else {
      this.steamPost = { t0: this.t, flush: true };
    }
    this.steam = null;
    this.mode = r[0] === 0 ? 'off' : r[1] === 1 ? 'sleep' : (this.act ? this.act.kind : 'idle');
    this._log('activity_stop', { activity: 'steam', reason, work_sta: '0x' + r[33].toString(16), latch: sc.latch, milk: Math.round(milkStop * 10) });
  }

  _stopActivity(reason, manual = false) {
    const r = this.regs;
    const a = this.act || {};
    const kind = a.kind || this.mode;
    if (kind === 'grind') {
      r[33] = WS.IDLE;
      this.post = { kind, t0: this.t, tareClearAt: this.t + 0.4 };
    } else if (kind === 'brew') {
      r[33] = WS.IDLE;
      this.post = { kind, t0: this.t, pulse: !a.flushOverlap, flagClearAt: this.t + 0.16 };
    } else if (kind === 'water') {
      r[33] = WS.IDLE;
      r[16] = 0; r[19] = 0;                      // the MCU clears hotwater_auto itself
      this.post = { kind, t0: this.t, overshootG: manual ? 0 : (a.overshootG || 0), settled: 0 };
    }
    if (this.steam) r[33] |= 0x4020;
    const weight = kind === 'grind' ? r[38] : r[39];
    this._log('activity_stop', { activity: kind, reason, work_sta: '0x' + r[33].toString(16), manual, weight: s16(weight) });
    this.act = null;
    this.mode = r[0] === 0 ? 'off' : r[1] === 1 ? 'sleep' : (this.steam ? 'steam' : 'idle');
  }

  _tickActivity() {
    const r = this.regs, a = this.act, p = this.params, d = this.drv;
    if (!a) return;
    const el = this.t - a.t0;
    if (this._fault()) { this._stopActivity('fault E1/E2/E3'); return; }
    // manual STOP (v6.13): the same words the MCU's own auto-stop writes to 0
    if (a.kind === 'grind' && r[6] === 0) { this._stopActivity('manual stop word6=0', true); return; }
    if (a.kind === 'brew' && r[22] === 0) { this._stopActivity('manual stop word22=0', true); return; }
    if (a.kind === 'water' && r[16] === 0 && r[19] === 0) { this._stopActivity('manual stop word16/19=0', true); return; }

    if (a.kind === 'grind') {
      d.grind = p.grindRateGs * (1 - Math.exp(-el / Math.max(0.01, p.grindSpinupS)));
      r[33] = WS.GRIND;
      const w = Math.floor(this._grindClean() * 10 + 1e-6);
      if (w >= a.stopW) {
        this.gscale.content = this.gscale.tare + a.stopW / 10;        // the MCU stops exactly at target + 2
        d.grind = 0;
        this._stopActivity('weight target');
      } else if (el > Math.max(5, r[5])) {
        d.grind = 0;
        this._stopActivity('timeout word5');
      }
    } else if (a.kind === 'brew') {
      const pre = a.pre, pMax = p.brewPressureBar;
      if (el < pre + 0.55) {                                            // ---- pre-infusion (work_sta PRE)
        r[33] = WS.BREW_PRE;
        if (!a.bit9Done && el >= 1.14 && el <= 1.36) r[33] |= 0x0200;   // the captured short bit-9 pulse
        if (el > 1.36) a.bit9Done = true;
        d.pTarget = pre > 0 ? a.pPre : pMax;
        d.pTau = pre > 0 ? 0.6 : p.brewPumpTauS;
        const sat = pre > 0 ? Math.min(1, el / pre) : 0;                // the puck soaks up water
        d.flowIn = p.preFlowMlS * Math.min(1, this.pBar / Math.max(1, a.pPre)) * (1 - 0.25 * sat);
      } else {                                                          // ---- extraction (work_sta EXT)
        const ee = el - (pre + 0.55);
        r[33] = WS.BREW_EXT;
        const erosion = p.brewErosion * Math.min(1, ee / 25);
        d.pTarget = pMax * (1 - erosion);
        d.pTau = p.brewPumpTauS;
        if (el >= a.dripT) {
          const x = el - a.dripT;
          const norm = (1 - p.brewErosion / 2) * (1 + p.brewFlowRise / 2);
          const shape = (this.pBar / pMax) * (1 + p.brewFlowRise * Math.min(1, x / 20)) / norm;
          d.flowOut = p.brewFlowGs * (1 - Math.exp(-x / 0.8)) * shape;
          this.scale.offset = 0;                                        // first drops: the -0.3 g tare artefact ends
        }
        d.flowIn = d.flowOut + p.brewBurstMlS * Math.exp(-ee / 0.6) * Math.min(1, this.pBar / pMax) + 0.25;
        if (el >= a.dripT && Math.floor(this._brewClean() * 10 + 1e-6) >= a.stopW) {
          this._stopActivity('weight target (predictive)');
          return;
        }
      }
      if (el > p.brewSafetyS) this._stopActivity('safety');
    } else if (a.kind === 'water') {
      r[33] = WS.WATER;
      d.flowOut = p.waterFlowGs * (1 - Math.exp(-el / 0.15));
      d.flowIn = Math.min(p.waterFlowMlS, el * 13.0);
      d.pTarget = p.waterPressureBar; d.pTau = 0.3;
      if (Math.floor(this._brewClean() * 10 + 1e-6) >= a.target) this._stopActivity('weight target word39≥word17');
      else if (el > p.brewSafetyS) this._stopActivity('safety');
    }
  }

  _tickSteam() {
    const r = this.regs, sc = this.steam, d = this.drv;
    if (!sc) return;
    const el = this.t - sc.t0;
    if (r[0] === 0 || r[1] === 1) { this._stopSteam('sleep/power-off'); return; }
    if (this._fault()) { this._stopSteam('fault E1/E2/E3'); return; }
    if (this.act && this.act.kind === 'brew') this.act.flushOverlap = true;
    d.milkHeat = true;
    if (sc.latch) d.steamDraw = el < this.params.steamPurgeS ? 0 : 1 - Math.exp(-(el - this.params.steamPurgeS) / 1.0);
    else d.flushDraw = 1;
    this.steamConsumed = true;
    r[33] |= 0x4020;
    if (sc.latch) {
      if (r[24] === 0) this._stopSteam('manual stop word24=0');
      else if (r[35] & 0x10) this._stopSteam('E2 milk NTC');
      else if (Math.floor(this.milk * 10 + 1e-6) >= sc.target) this._stopSteam('milk ≥ setpoint word48≥word25');
      else if (el > Math.max(5, r[15])) this._stopSteam('timeout word15');
    } else {
      if (!(r[44] & 1)) this._stopSteam('wand released');
      else if (el > Math.max(5, r[15])) this._stopSteam('timeout word15');
    }
  }

  _tickPost() {
    const r = this.regs, pp = this.post, d = this.drv;
    if (!pp) return;
    const el = this.t - pp.t0;
    if (pp.kind === 'grind') {
      if (this.t >= pp.tareClearAt) r[6] = 0;                     // word6 is consumed late (BREW §5.1)
      if (el > 2.5) this.post = null;
    } else if (pp.kind === 'brew') {
      r[33] = pp.pulse && el < 0.21 ? 0x0105 : WS.IDLE;           // post-stop bit-2 valve pulse
      if (this.steam) r[33] |= 0x4020;
      if (this.t >= pp.flagClearAt) r[22] = 0;                    // the MCU clears the brew flag ~0.16 s after stop
      const tau = 0.45;
      d.flowOut = (this.params.brewAfterDripG / tau) * Math.exp(-el / tau);   // drip-through after the stop
      d.pTarget = 0; d.pTau = 0.33;
      if (el > 3.0) this.post = null;
    } else if (pp.kind === 'water') {
      const tau = 0.2;
      d.flowOut = (pp.overshootG / tau) * Math.exp(-el / tau);    // in-flight water
      d.pTarget = 0; d.pTau = 0.33;
      d.flowIn = Math.max(0, this.params.waterFlowMlS * (1 - el / 1.7));
      if (el > 2.5) this.post = null;
    }
  }

  _tickSteamPost() {
    const sp = this.steamPost, d = this.drv;
    if (!sp) return;
    const el = this.t - sp.t0;
    if (sp.flush) {
      if (el < 0.9) d.flushDraw = 1 - el / 0.9;                    // thermal lag after the wand release
      if (el > 2.5) this.steamPost = null;
      return;
    }
    if (sp.auto) {
      if (el > 50) this.steamPost = null;                          // milk handled by idle cooling from here on
    } else if (el > 4.0) this.steamPost = null;
  }

  // ------------------------------------------------------------------------------------------------- physics
  _gross() { const s = this.scale; return s.cupOn ? this.params.cupG + s.liquid : 0; }
  _brewClean() { const s = this.scale; return s.manual !== null ? s.manual : this._gross() - s.tare + s.offset; }
  _grindClean() { const g = this.gscale; return g.manual !== null ? g.manual : g.content - g.tare; }

  _physics(dt, awake) {
    const p = this.params, d = this.drv || {}, amb = this.amb;
    const heat = awake && this.power === 'on' && !(p.faultHeaterOff && this._fault());
    // ---- boilers
    const eDraw = (d.flowIn || 0) * Math.max(0, this.eb.w - amb) / Math.max(50, p.brewBoilerMl);
    this._boiler(this.eb, dt, heat, this._eSet(), p.brewHeatCps, p.brewElementLagS, p.brewBodyRatio, p.brewCouplingS, eDraw, false);
    const sSet = this._sSet();
    let sDraw = 0;
    if (d.steamDraw) {
      const tv = sSet - p.steamDropC - p.steamHeatCps / p.steamDrawK;
      sDraw += d.steamDraw * p.steamDrawK * Math.max(0, this.sb.w - tv);
    }
    if (d.flushDraw) {
      const tv = sSet - p.flushDropC - p.steamHeatCps / p.flushDrawK;
      sDraw += d.flushDraw * p.flushDrawK * Math.max(0, this.sb.w - tv);
    }
    const forceSteam = heat && !!this.steam;   // the MCU drives the steam heater flat out while steaming
    this._boiler(this.sb, dt, heat, sSet, p.steamHeatCps, p.steamElementLagS, p.steamBodyRatio, p.steamCouplingS, sDraw, forceSteam);
    // ---- group head: own heater (word114 °C) + conduction from the brew boiler + water flowing through
    const g = this.group;
    const gSet = this.regs[114] || 0;
    g.sens += (g.t - g.sens) * Math.min(1, dt / p.sensorLagS);
    if (heat && gSet > 0) {
      const err = gSet - g.sens;
      const u = err / 2.0 + g.I;
      g.duty = clamp(u, 0, 1);
      if (u > 0 && u < 1) g.I = clamp(g.I + 0.001 * err * dt, 0, 0.6);
    } else g.duty = 0;
    g.t += (g.duty * p.groupHeatCps + (this.eb.w - g.t) / p.groupCouplingS - (g.t - amb) / p.groupLossS
      + (d.flowIn || 0) * p.groupFlowK * (this.eb.w - g.t)) * dt;
    // ---- pump pressure (first order toward the driver target)
    const pTarget = d.pTarget || 0;
    this.pBar += (pTarget - this.pBar) * Math.min(1, dt / Math.max(0.02, d.pTau || 0.33));
    if (this.pBar < 0.005 && pTarget === 0) this.pBar = 0;
    this.flowIn = d.flowIn || 0;
    this.flowOut = d.flowOut || 0;
    this.grindFlow = d.grind || 0;
    // ---- scales
    if (this.scale.cupOn) this.scale.liquid += this.flowOut * dt;
    if (this.post && this.post.kind === 'water' && this.scale.cupOn) {
      const el = this.t - this.post.t0;                              // the reading settles back ~1.1 g over 2 s
      const want = p.waterSettleG * Math.min(1, el / 2.0);
      const step = want - this.post.settled;
      this.post.settled = want;
      this.scale.liquid = Math.max(0, this.scale.liquid - step);
    }
    this.gscale.content += this.grindFlow * dt;
    // ---- milk probe
    const sp = this.steamPost;
    if (d.milkHeat) {
      this.milk += (milkRate(this.milk * 10) / 10) * p.milkRateScale * dt;
    } else if (sp && !sp.flush && sp.auto && this.t - sp.t0 < 11.5) {
      this.milk += (sp.peak - this.milk) * (dt / 3.2);              // thermal inertia after an auto-stop
    } else if (sp && !sp.flush && sp.auto && this.t - sp.t0 < 50) {
      /* hold the peak (live: ~40 s) */
    } else if (sp && !sp.flush && !sp.auto && this.t - sp.t0 < 0.15) {
      this.milk += (sp.peak - this.milk) * Math.min(1, dt / 0.05);
    } else {
      this.milk += (amb + p.milkProbeWarmC - this.milk) * p.milkCoolK * dt;
    }
  }

  _boiler(b, dt, enabled, set, a, tauEl, m, coupS, draw, forceFull) {
    const p = this.params;
    b.sens += (b.w - b.sens) * Math.min(1, dt / p.sensorLagS);
    if (enabled) {
      const err = set - b.sens;
      const u = err / p.thermoBandC + b.I;
      b.duty = forceFull ? 1 : clamp(u, 0, 1);
      // conditional integration (anti-windup): only near the setpoint and while the output is not saturated —
      // removes the droop of the pure P band without winding up during shots, steaming or recoveries
      if (!forceFull && Math.abs(err) < 2 * p.thermoBandC && ((u > 0 && u < 1) || (u >= 1 && err < 0) || (u <= 0 && err > 0))) {
        b.I = clamp(b.I + p.thermoKi * err * dt, 0, 1);
      }
    } else b.duty = 0;
    b.el += (b.duty * a - b.el / tauEl) * dt;
    const q = b.el / tauEl;
    const xfer = (b.w - b.b) / coupS;
    b.w += (q - xfer - draw) * dt;
    b.b += (xfer / m - p.lossPerS * (b.b - this.amb)) * dt;
  }

  // ------------------------------------------------------------------------------------------------- registers
  _noise(amp) { return Math.round(amp * this.params.noise * (2 * this.rng.random() - 1)); }

  _pressureRaw(bar) {
    const anchor = this.regs[112] & 0xFF;
    const a = anchor >= 1 && anchor <= 15 ? anchor : 4;
    return Math.max(0, Math.round((bar * this.params.pAnchorRaw) / a));
  }

  _publishTemps(stages) {
    const r = this.regs;
    r[40] = clamp(Math.round(this.eb.sens * 10) + this._noise(3), 0, 65535);
    r[41] = r[40];
    r[47] = clamp(Math.round(this.sb.sens * 10) + this._noise(3), 0, 65535);
    r[42] = clamp(Math.round(this.group.sens * 10) + this._noise(2), 0, 65535);
    r[128] = r[42];
    r[48] = clamp(Math.round(this.milk * 10) + (this.steam ? 0 : this._noise(1)), 0, 65535);
    if (stages) {
      if (r[0] === 1 && r[1] === 0) [r[62], r[63]] = heatStage(r);
      else r[62] = r[63] = 0;
    }
  }

  /** Physics -> registers (the status task's sensor round) + work_sta at rest + maintenance pulses. */
  _publish() {
    const r = this.regs, p = this.params;
    const awake = r[0] === 1 && r[1] === 0;
    this._publishTemps(false);
    // weights (signed 0.1 g) + raw load cells
    const wb = this.scale.manual !== null ? this.scale.manual * 10 : this._brewClean() * 10 + this._noise(1.5);
    const wg = this.gscale.manual !== null ? this.gscale.manual * 10 : this._grindClean() * 10;
    r[39] = clamp(Math.round(wb), -32768, 32767) & 0xFFFF;
    r[38] = clamp(Math.round(wg), -32768, 32767) & 0xFFFF;
    r[53] = this.wrawBase[53] + Math.floor(Math.max(0, s16(r[38])) / 40) + this._noise(1);
    r[55] = this.wrawBase[55] + Math.floor(Math.max(0, s16(r[39])) / 45) + this._noise(1);
    // wand
    r[44] = this.wandState ? (r[44] | 1) : (r[44] & ~1);
    r[46] = this.wandState ? 1 : 0;
    // ADC / mains noise around the baseline
    r[49] = this.adcBase[49] + this._noise(2) - 1;
    r[61] = 50;
    r[69] = this.adcBase[69] + this._noise(1);
    r[71] = this.adcBase[71] + this._noise(2);
    r[72] = this.adcBase[72] + this._noise(2);
    this._publishLevels(awake);
    // current / pressure / flow by owner (activity > post > steam > steam after-phase > rest)
    const pr = this._pressureRaw(this.pBar);
    const flow = Math.max(0, Math.round(this.flowIn * 100) + (this.flowIn > 0 ? this._noise(20) : 0));
    const sc = this.steam, sp = this.steamPost, a = this.act, pp = this.post;
    const spEl = sp ? this.t - sp.t0 : 0;
    const spCur = !!(sp && !sp.flush && sp.auto && spEl < 19);
    if (a) {
      const el = this.t - a.t0;
      if (a.kind === 'grind') {
        r[58] = el < 0.25 ? 1500 : 868 + this._noise(60);
        r[50] = 5 + this._noise(1);
        r[51] = this._noise(1) & 1; r[52] = 0;
      } else if (a.kind === 'brew') {
        r[50] = (el < a.pre + 0.55 ? 1533 : 1046) + this._noise(40);
        r[51] = Math.max(0, pr + (pr > 0 ? this._noise(12) : 0));
        r[52] = flow;
        if (sc && !sc.latch) r[51] += 85;                             // live: brew pressure ~1301 -> 1387 during a flush
      } else if (a.kind === 'water') {
        r[50] = el < 0.15 ? 1556 : 1050 + this._noise(30);
        r[51] = Math.max(0, pr + this._noise(12));
        r[52] = flow;
      }
    } else if (pp && pp.kind !== 'grind' && !sc) {
      const el = this.t - pp.t0;
      r[50] = Math.max(5, Math.round(1046 * Math.exp(-2.2 * el)));
      r[51] = pr;
      if (pp.kind === 'brew' && p.maintPulses && el > 0.4 && el < 2.2 && this.rng.random() < 0.06) {
        r[51] = this.rng.choice([4876, 10385, 15428]);               // deadhead spikes after the valve closes
      }
      r[52] = flow;
    } else if (sc) {
      const el = this.t - sc.t0;
      if (sc.latch) {
        r[50] = el < 5 ? 54 + this._noise(15) : 7340 + this._noise(250);
        r[51] = el < 5 ? 93 + this._noise(30) : 16650 + this._noise(240);   // deadhead/max drive (the UI shows PULSE)
      } else {
        r[50] = 1024 + this._noise(30);
        r[51] = 1387 + this._noise(20);
      }
      r[52] = 0;
    } else if (spCur) {
      if (spEl < 3) { r[50] = 7300 + this._noise(150); r[51] = 16570 + this._noise(120); }
      else {
        r[50] = Math.max(5, Math.round(7300 * Math.exp(-(spEl - 3) / 3.5)));
        if (this.rng.random() < 0.5) r[50] = this.rng.randint(800, 5500);
        r[51] = this.rng.choice([0, 0, this.rng.randint(3000, 16670)]);
      }
      r[52] = 0;
    } else {
      r[50] = 5 + this._heaterCurrent() + (this._noise(1) & 1);
      r[51] = this.rng.random() < 0.5 ? 0 : 1;
      r[52] = 0;
    }
    if (pp && pp.kind === 'grind') r[58] = Math.max(5, Math.round(727 * Math.exp(-2.5 * (this.t - pp.t0))));
    else if (!a || a.kind !== 'grind') r[58] = 5 + (this.rng.random() < 0.5 ? 0 : 1);
    // heat stages (after the temperatures and with this tick's work_sta)
    if (awake) [r[62], r[63]] = heatStage(r);
    else r[62] = r[63] = 0;
    // work_sta at rest (+ maintenance pulses, also during steam)
    if (a || pp) {
      if (sc && this.pulse && this.t < this.pulse.until) r[33] |= this.pulse.bits;
      return;
    }
    if (r[0] === 0) { r[33] = 0; this.mode = 'off'; return; }
    if (r[1] === 1) { r[33] = WS.SLEEP; this.mode = 'sleep'; return; }
    this.mode = sc ? 'steam' : 'idle';
    const base = WS.IDLE | (sc ? 0x4020 : 0);
    if (this.pulse && this.t < this.pulse.until) {
      r[33] = base | this.pulse.bits;
      if (!sc && !spCur) { r[50] = this.pulse.cur; r[51] = this.pulse.p; }
    } else {
      this.pulse = null;
      r[33] = base;
      if (this.t >= this.nextPulse && p.maintPulses) {
        this.pulse = {
          until: this.t + this.rng.uniform(0.10, 0.25), bits: this.rng.choice([0x0400, 0x0800, 0x2000, 0x0C00, 0x2800]),
          cur: this.rng.randint(800, 4900), p: this.rng.randint(3000, 16670),
        };
        this.nextPulse = this.t + this.rng.uniform(3.0, 9.0);
      }
    }
  }

  /** Burst-fire heater current at rest: each SSR is on for duty x period (the brew/steam bursts are phase-shifted). */
  _heaterCurrent() {
    const p = this.params, per = Math.max(0.1, p.heaterBurstS);
    const ph = (this.t / per) % 1;
    let cur = 0;
    if (ph < this.eb.duty) cur += p.brewHeaterCur;
    if (((ph + 0.5) % 1) < this.sb.duty) cur += p.steamHeaterCur;
    return cur;
  }

  /** Water level ADCs (mcu_sim: flash boiling during steam, refill afterwards; slow drift in sleep). */
  _publishLevels(awake) {
    const r = this.regs, L = this.level, sc = this.steam, dt = TICK;
    if (sc) {
      const el = this.t - sc.t0;
      if (sc.latch) L.s = el < 1.5 ? Math.max(800, L.s - 230 * dt) : approach(L.s, 858, 2.0 * dt);
      else L.s = Math.max(1050, L.s - 130 * dt);
    } else if (awake) {
      if (this.steamConsumed && L.s < 1340) {
        if (L.s < 1080) L.s += 20 * dt;
        else if (L.s < 1285) L.s = approach(L.s, 1290, Math.max(0.5, (1290 - L.s) * 0.05) * dt);
        else L.s = approach(L.s, 1345, Math.max(0.15, (1345 - L.s) * 0.004) * dt);
      } else if (L.s < 1200) L.s += 0.35 * dt;
    } else L.s += 0.055 * dt;
    if (awake) { if (L.e < 900) L.e += (910 - L.e) * (dt / 280); } else L.e += 0.06 * dt;
    r[68] = Math.max(0, Math.round(L.s) + this._noise(sc ? 12 : 3));
    r[70] = Math.max(0, Math.round(L.e) + this._noise(3));
  }

  _tickKeepalive() {
    if (this.firstRxT === null) return;
    const gap = this.t - this.lastAwakeWrite, kmax = this.params.keepAwakeMaxS;
    if (gap > kmax + 0.5 && this.t - this.keepaliveLoggedAt > kmax) {
      this.keepaliveLoggedAt = this.t;
      this._log('keepalive_gap', { gap_s: Math.round(gap * 100) / 100, expected_max_s: kmax });
    }
  }

  _tickBootPushCheck() {
    if (this.firstRxT === null || this.bootChecked) return;
    if (this.t - this.firstRxT > BOOT_PUSH_WINDOW) {
      if (!this.bootPushSetup) this._log('boot_push_missing', { block: 'sub1_len30' });
      if (!this.bootPushPreinf) this._log('boot_push_missing', { block: 'sub112_len3' });
      this.bootChecked = true;
    }
  }

  _phaseName() {
    const r = this.regs;
    if (this.power === 'off') return 'off';
    if (this.act) {
      if (this.act.kind === 'brew') return this.t - this.act.t0 < this.act.pre + 0.55 ? 'pre-infusion' : 'extraction';
      return this.act.kind;
    }
    if (this._fault()) return 'fault';
    if (this.post && this.post.kind === 'brew') return 'drip';
    if (this.steam) return this.steam.latch ? 'steam' : 'flush';
    if (r[0] !== 1 || r[1] === 1) return 'sleep';
    if (r[62] < 100 || r[63] < 100) return 'heating';
    return 'idle';
  }
}

/** Short human note for a cmd6 write (the recipes the UI sends, src/mp/model.c act_push/act_push_stop). */
function describeWrite(sub, words) {
  const n = words.length;
  if (sub === 3 && n === 4 && words[0] === 1) return 'start grind';
  if (sub === 5 && n === 18 && words[4] === 1 && words[17] === 1) return 'start brew';
  if (sub === 9 && n === 11 && words[0] === 1) return 'start water';
  if (sub === 24 && n === 8) return words[0] ? 'start steam' : 'stop steam';
  if (n === 1 && words[0] === 0 && [6, 22, 16, 19].includes(sub)) return `stop ${({ 6: 'grind', 22: 'brew', 16: 'water', 19: 'water' })[sub]}`;
  if (sub === 1 && n === 1) return words[0] === 0 ? 'keep-awake' : 'sleep';
  if (sub === 0 && words[0] === 0) return 'power off';
  if (sub === 0 && words[0] === 1) return 'power on';
  return 'write';
}
