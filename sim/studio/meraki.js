// sim/studio/meraki.js — host for one Meraki+ UI instance compiled to WebAssembly (src/mp/wasm_hal.c).
//
//   const ui = await MerakiUI.create(wasmBytes, { config, lang, clock });
//   ui.setTime(ms); ui.uartRx(bytesFromMcu); ui.step(); const toMcu = ui.uartTx();
//   ui.touch(x, y, 'down'|'move'|'up'); ui.rgba(); ui.ctrl({cmd:'layout'}); ui.state();
//
// The sandbox is seeded like sim/run_e2e.py's (customer/config.json, /proc mocks, boot dir, backlight sysfs); the UI's
// own SIM provisioning (sim_hal.c) adds the slot mocks. Browser + node >= 18, no dependencies.
import { Wasi, MemFS, WasiExit } from './wasi.js';

export const W = 480, H = 480;
export const ROOT = '/sandbox';                 // MERAKIPLUS_SIM_ROOT inside the WASI file system
const TTY = '/dev/ttyS1', STATE = '/tmp/merakiplus-state.jsonl', FB = '/tmp/merakiplus-fb.bin';

// a growable byte FIFO
class ByteQueue {
  constructor() { this.buf = new Uint8Array(4096); this.r = 0; this.w = 0; }
  get length() { return this.w - this.r; }
  push(b) {
    if (!b || !b.length) return;
    if (this.w + b.length > this.buf.length) {
      const live = this.buf.subarray(this.r, this.w);
      const nb = new Uint8Array(Math.max(this.buf.length, (live.length + b.length) * 2));
      nb.set(live); this.buf = nb; this.w = live.length; this.r = 0;
    }
    this.buf.set(b, this.w); this.w += b.length;
  }
  take(max = Infinity) {
    const n = Math.min(max, this.length); const out = this.buf.slice(this.r, this.r + n);
    this.r += n; if (this.r === this.w) this.r = this.w = 0; return out;
  }
  clear() { this.r = this.w = 0; }
}

export function seedSandbox(fs, { config, lang, files } = {}) {
  const R = ROOT;
  for (const d of ['customer', 'customer/bak', 'new_customer/merakiplus/boot', 'tmp', 'proc', 'dev/input', 'sys/class/pwm/pwmchip0/pwm2', 'appconfigs'])
    fs.mkdirp(`${R}/${d}`);
  fs.writeFile(`${R}/customer/config.json`, JSON.stringify(config, null, '\t') + '\n');
  fs.writeFile(`${R}/proc/uptime`, '12345.67 8901.23\n');
  fs.writeFile(`${R}/proc/meminfo`, 'MemTotal: 128000 kB\nMemFree: 41000 kB\n');
  fs.writeFile(`${R}/proc/version`, 'Linux version 4.9.84 (mock) #1 SMP\n');
  fs.writeFile(`${R}/sys/class/pwm/pwmchip0/pwm2/duty_cycle`, '37\n');
  if (lang) fs.writeFile(`${R}/new_customer/merakiplus/lang`, lang);
  for (const [rel, text] of Object.entries(files || {})) fs.writeFile(`${R}/${rel.replace(/^\/+/, '')}`, text);
  fs.mkdirp('/tmp'); fs.mkdirp('/dev');
}

export class MerakiUI {
  // module: a compiled WebAssembly.Module (compile once, instantiate many), or bytes
  static async create(module, opts = {}) {
    const mod = module instanceof WebAssembly.Module ? module : await WebAssembly.compile(module);
    const ui = new MerakiUI(opts);
    const inst = await WebAssembly.instantiate(mod, ui.wasi.imports);
    ui._attach(inst);
    return ui;
  }

  constructor(opts = {}) {
    this.name = opts.name || 'ui';
    this.fs = new MemFS();
    this.toUi = new ByteQueue();      // MCU -> display (the UI reads these from /dev/ttyS1)
    this.fromUi = new ByteQueue();    // display -> MCU (the UI writes these to /dev/ttyS1)
    this.lastState = null;            // bytes of the latest state JSON line
    this.logs = [];                   // stdout/stderr lines (capped)
    this.stopped = false;
    this.exitCode = null;
    const sb = [];
    this.fs.mkdev('/dev/null', { read: () => null, write: () => {} });
    this.fs.mkdev(TTY, { read: (n) => this.toUi.take(n), write: (b) => this.fromUi.push(b) });
    this.fs.sink(STATE, (bytes) => {              // keep only the latest complete line
      for (let i = 0; i < bytes.length; i++) {
        if (bytes[i] === 10) { if (sb.length) { this.lastState = Uint8Array.from(sb); sb.length = 0; } }
        else sb.push(bytes[i]);
      }
    });
    this.fs.sink(FB, null);                       // the fb dump file: discarded (the canvas reads the framebuffer)
    seedSandbox(this.fs, opts);
    this.clock = { mono_ns: BigInt(Math.round((opts.uptimeMs ?? 20000) * 1e6)), wall_offset_ns: null };
    if (opts.wallMs != null) this.clock.wall_offset_ns = BigInt(Math.round(opts.wallMs * 1e6)) - this.clock.mono_ns;
    this.wasi = new Wasi({
      fs: this.fs,
      clock: this.clock,
      env: {
        MERAKIPLUS_SIM: '1', MERAKIPLUS_SIM_ROOT: ROOT, MERAKIPLUS_ROOT: ROOT,
        MERAKIPLUS_SIM_TTY: TTY, MERAKIPLUS_SIM_STATE: STATE, MERAKIPLUS_SIM_FB: FB,
        MERAKIPLUS_CONFIG: `${ROOT}/customer/config.json`, MERAKIPLUS_BAKDIR: `${ROOT}/customer/bak`,
        MERAKIPLUS_LANGFILE: `${ROOT}/new_customer/merakiplus/lang`,
        MERAKIPLUS_NOGRAB: '1', MERAKIPLUS_NOBL: '1', MERAKIPLUS_SWITCH_DRY: '1',
        ...(opts.env || {}),
      },
      log: (stream, line) => { this.logs.push(`${stream === 2 ? 'E' : 'O'} ${line}`); if (this.logs.length > 500) this.logs.shift(); if (opts.log) opts.log(stream, line); },
    });
  }

  _attach(inst) {
    this.x = inst.exports;
    this.wasi.setMemory(this.x.memory);
    if (this.x._initialize) this.x._initialize();
  }
  _guard(fn) {
    if (this.stopped) return 2;
    try { return fn(); }
    catch (e) {
      if (e instanceof WasiExit) { this.stopped = true; this.exitCode = e.code; return 2; }
      this.stopped = true; this.error = e; throw e;
    }
  }

  // ---- time: the host owns the UI's monotonic clock (ms) and the wall clock offset
  get nowMs() { return Number(this.clock.mono_ns / 1000000n); }
  setTime(ms) { this.clock.mono_ns = BigInt(Math.round(ms * 1e6)); }
  advanceTime(ms) { this.clock.mono_ns += BigInt(Math.round(ms * 1e6)); }
  setWallClock(utcMs) { this.clock.wall_offset_ns = BigInt(Math.round(utcMs * 1e6)) - this.clock.mono_ns; }

  // ---- lifecycle
  init() { return this._guard(() => this.x.mp_init()); }
  // one main-loop iteration; returns {frame: fb changed, stopped}
  step() {
    const r = this._guard(() => this.x.mp_step());
    if (r & 2) this.stopped = true;
    return { frame: !!(r & 1), stopped: !!(r & 2) };
  }

  // ---- UART byte pipes to the machine model
  uartRx(bytes) { this.toUi.push(bytes); }
  uartTx() { return this.fromUi.take(); }

  // ---- input: screen pixels (0..479); kind 'down' | 'move' | 'up'
  touch(x, y, kind) {
    const k = kind === 'down' ? 1 : kind === 'up' ? 0 : 2;
    this._guard(() => this.x.mp_touch(Math.round(x), Math.round(y), k));
  }

  // ---- framebuffer
  fbSeq() { return Number(this.x.mp_fb_seq()); }
  rgba() {   // Uint8ClampedArray view (480*480*4) over wasm memory — copy it before the next step if you keep it
    const p = this.x.mp_fb_rgba();
    return new Uint8ClampedArray(this.x.memory.buffer, p, W * H * 4);
  }

  // ---- test hooks (sim/CONTRACT.md) and state
  ctrl(req) {
    const enc = new TextEncoder().encode(JSON.stringify(req));
    const cap = this.x.mp_inbuf_size();
    if (enc.length >= cap) throw new Error('ctrl request too long');
    const p = this.x.mp_inbuf();
    const u8 = new Uint8Array(this.x.memory.buffer);
    u8.set(enc, p); u8[p + enc.length] = 0;
    const rp = this._guard(() => this.x.mp_ctrl());
    if (typeof rp !== 'number' || this.stopped) return { ok: false, error: 'ui stopped' };
    const mem = new Uint8Array(this.x.memory.buffer);
    let e = rp; while (mem[e]) e++;
    return JSON.parse(new TextDecoder().decode(mem.subarray(rp, e)));
  }
  state(fresh = true) {
    if (fresh) this._guard(() => this.x.mp_state());
    return this.lastState ? JSON.parse(new TextDecoder().decode(this.lastState)) : null;
  }
}
