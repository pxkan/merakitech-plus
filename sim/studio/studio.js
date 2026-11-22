// sim/studio/studio.js — the Sim Studio viewer: one or two rigs (UI wasm + machine model), the bezel, pointer input,
// the control drawer, scenarios, the live panel, the UART log and PNG export. No build step, no dependencies.
import { Rig } from './rig.js';
import { Machine, DEFAULT_PARAMS } from './machine.js';
import { drawBezel, drawGlass, composeBezelPng, GEOM } from './bezel.js';
import { W, H } from './meraki.js';

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const store = {   // per-viewer conveniences only (speed, open panels, compare); never required
  get(k, d) { try { const v = localStorage.getItem('studio.' + k); return v === null ? d : JSON.parse(v); } catch { return d; } },
  set(k, v) { try { localStorage.setItem('studio.' + k, JSON.stringify(v)); } catch { /* private mode */ } },
};

const S = {
  builds: null, modules: {}, config: null, spec: null,
  rigs: [],            // [{rig, key, el, display, ctx, src, srcCtx, img, seq, ...}]
  speed: store.get('speed', 1), lastReal: 0,
  sel: 0,              // rig shown in the live panel
  fps: { n: 0, t0: 0, val: 0, stepMs: 0 },
  uartSeen: 0,
};

// ------------------------------------------------------------------ loading
async function loadJSON(u) { const r = await fetch(u, { cache: 'no-cache' }); if (!r.ok) throw new Error(`${u}: ${r.status}`); return r.json(); }
async function loadModule(key) {
  if (!S.modules[key]) {
    const url = S.builds[key].wasm;
    const r = await fetch(url, { cache: 'no-cache' });
    if (!r.ok) throw new Error(`${url}: ${r.status}`);
    S.modules[key] = WebAssembly.compileStreaming ? await WebAssembly.compileStreaming(r) : await WebAssembly.compile(await r.arrayBuffer());
  }
  return S.modules[key];
}

async function makeRig(key) {
  const module = await loadModule(key);
  const rig = await Rig.create({ module, config: S.config, spec: S.spec, Machine, seed: 1, name: key });
  rig.machine.preheat();            // open on a ready machine; "Cold morning" shows the heat-up
  const u = { key, rig, seq: -1 };
  u.el = document.createElement('div');
  u.el.className = 'unit';
  u.el.innerHTML = `<div class="machine"><canvas class="bezel"></canvas><canvas class="display" width="${W}" height="${H}"></canvas><canvas class="glass"></canvas></div>
    <div class="caption"><b>${key}</b><span class="tag">${S.builds[key].version} · ${S.builds[key].ref}</span><span class="ph">—</span></div>`;
  u.machineEl = $('.machine', u.el);
  u.bezel = $('.bezel', u.el); u.display = $('.display', u.el); u.glass = $('.glass', u.el);
  u.ctx = u.display.getContext('2d', { alpha: false });
  u.src = document.createElement('canvas'); u.src.width = W; u.src.height = H;
  u.srcCtx = u.src.getContext('2d', { alpha: false });
  u.img = u.srcCtx.createImageData(W, H);
  u.ph = $('.ph', u.el);
  attachPointer(u);
  return u;
}

async function setRigs(keys) {
  const stage = $('#stage');
  for (const u of S.rigs) u.el.remove();
  const old = S.rigs; S.rigs = [];
  for (const k of keys) {
    const prev = old.find(u => u.key === k);
    S.rigs.push(prev || await makeRig(k));
  }
  for (const u of S.rigs) stage.appendChild(u.el);
  S.sel = Math.min(S.sel, S.rigs.length - 1);
  $('#loading')?.remove();
  if (!$('.paused-badge')) { const b = document.createElement('div'); b.className = 'paused-badge'; b.textContent = 'PAUSED'; stage.appendChild(b); }
  layout(); buildTabs(); buildExports();
}

// ------------------------------------------------------------------ layout + bezel drawing
function layout() {
  const stage = $('#stage');
  const narrow = matchMedia('(max-width: 900px)').matches;
  const n = S.rigs.length;
  const cs = getComputedStyle(stage);
  const availW = stage.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight);
  const availH = stage.clientHeight - parseFloat(cs.paddingTop) - parseFloat(cs.paddingBottom) - 40;
  let size;
  if (narrow) size = Math.min(availW, 640);
  else size = Math.max(240, Math.min(availH, (availW - (n - 1) * 40) / n, 760));
  size = Math.floor(size);
  const dpr = Math.min(3, window.devicePixelRatio || 1);
  for (const u of S.rigs) {
    u.machineEl.style.setProperty('--msize', size + 'px');
    const px = Math.round(size * dpr);
    for (const c of [u.bezel, u.glass]) { c.width = c.height = px; }
    drawBezel(u.bezel.getContext('2d'), px, { label: S.rigs.length > 1 ? u.key : '' });
    drawGlass(u.glass.getContext('2d'), px);
    const d = size * GEOM.display * 2, off = (size - d) / 2;
    Object.assign(u.display.style, { left: off + 'px', top: off + 'px', width: d + 'px', height: d + 'px' });
    const dp = Math.round(d * dpr);
    if (u.display.width !== dp) { u.display.width = u.display.height = dp; u.seq = -1; }
    u.ctx = u.display.getContext('2d', { alpha: false });
    u.ctx.imageSmoothingEnabled = true; u.ctx.imageSmoothingQuality = 'high';
  }
}

function blit(u) {
  u.img.data.set(u.rig.ui.rgba());
  u.srcCtx.putImageData(u.img, 0, 0);
  u.ctx.drawImage(u.src, 0, 0, u.display.width, u.display.height);
}

// ------------------------------------------------------------------ pointer input (tap, hold, drag, swipe)
function attachPointer(u) {
  const c = u.display;
  let active = null, last = null;
  const pos = (e) => {
    const r = c.getBoundingClientRect();
    return [Math.max(0, Math.min(W - 1, (e.clientX - r.left) * W / r.width)), Math.max(0, Math.min(H - 1, (e.clientY - r.top) * H / r.height))];
  };
  c.addEventListener('pointerdown', (e) => {
    if (active !== null || e.button > 0) return;
    active = e.pointerId; c.setPointerCapture(e.pointerId);
    const [x, y] = pos(e); last = [Math.round(x), Math.round(y)];
    u.rig.ui.touch(x, y, 'down');
    e.preventDefault();
  });
  c.addEventListener('pointermove', (e) => {
    if (e.pointerId !== active) return;
    const evs = e.getCoalescedEvents ? e.getCoalescedEvents() : [e];
    const [x, y] = pos(evs[evs.length - 1] || e);
    const p = [Math.round(x), Math.round(y)];
    if (p[0] !== last[0] || p[1] !== last[1]) { last = p; u.rig.ui.touch(x, y, 'move'); }
  });
  const up = (e) => {
    if (e.pointerId !== active) return;
    const [x, y] = e.type === 'pointercancel' ? last : pos(e);
    u.rig.ui.touch(x, y, 'up'); active = null;
  };
  c.addEventListener('pointerup', up);
  c.addEventListener('pointercancel', up);
  c.addEventListener('contextmenu', (e) => e.preventDefault());
}

// ------------------------------------------------------------------ the frame loop
function stepMsFor(speed) { return speed <= 1 ? 2 : speed <= 4 ? 4 : 10; }
function frame(now) {
  const real = S.lastReal ? Math.min(100, now - S.lastReal) : 16;
  S.lastReal = now;
  const t0 = performance.now();
  if (S.speed > 0) for (const u of S.rigs) u.rig.advance(real * S.speed, stepMsFor(S.speed));
  const t1 = performance.now();
  let blits = 0;
  for (const u of S.rigs) {
    const seq = u.rig.ui.stopped ? u.seq : u.rig.ui.fbSeq();
    if (seq !== u.seq) { u.seq = seq; blit(u); blits++; }
    u.machineEl.classList.toggle('stopped', u.rig.ui.stopped);
  }
  const f = S.fps; f.n += blits ? 1 : 0; f.step = (f.step || 0) * 0.9 + (t1 - t0) * 0.1;
  if (now - f.t0 > 1000) { f.val = Math.round(f.n * 1000 / (now - f.t0)); f.n = 0; f.t0 = now; }
  if (!S.uiTick || now - S.uiTick > 200) { S.uiTick = now; refreshPanels(); }
  requestAnimationFrame(frame);
}

// ------------------------------------------------------------------ helpers over all rigs
const each = (fn) => S.rigs.forEach(u => fn(u.rig.machine, u.rig.ui, u.rig));
const fmtC = (v) => (v == null || isNaN(v)) ? '—' : `${v.toFixed(1)}<small>°C</small>`;
function simClock(ms) { const s = Math.floor(ms / 1000); return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`; }

async function holdTarget(rig, name, ms = 850) {
  const t = rig.target(name);
  if (!t) return false;
  rig.ui.touch(t.x, t.y, 'down');
  await waitSim(rig, ms);
  rig.ui.touch(t.x, t.y, 'up');
  return true;
}
// resolves after `ms` of SIM time on this rig (respects pause/speed)
function waitSim(rig, ms) {
  const end = rig.t + ms;
  return new Promise(res => { const f = () => (rig.t >= end || rig.ui.stopped) ? res() : setTimeout(f, 15); f(); });
}

// ------------------------------------------------------------------ scenarios
function baseline(m, ui) {
  m.clearFaults(); m.setOffline(false); m.setWand(false); m.setCup(true);
  if (m.snapshot().power === 'off') m.powerOn();
  ui.ctrl({ cmd: 'raw_write', sub: 1, words: [0] });
  ui.ctrl({ cmd: 'page', name: 'home' });
  syncInputs();
}
async function scenario(name) {
  $$('#scenarios button').forEach(b => b.classList.toggle('on', b.dataset.scen === name));
  const jobs = S.rigs.map(async (u) => {
    const { machine: m, ui } = u.rig;
    baseline(m, ui);
    switch (name) {
      case 'cold': m.coldStart(); if (S.speed < 20) setSpeed(20); break;
      case 'ready': m.preheat(); break;
      case 'shot': m.preheat(); await waitSim(u.rig, 400); if (!await holdTarget(u.rig, 'brew')) ui.ctrl({ cmd: 'action', name: 'brew' }); break;
      case 'steam': m.preheat(); await waitSim(u.rig, 400); if (!await holdTarget(u.rig, 'steam')) ui.ctrl({ cmd: 'action', name: 'steam' }); break;
      case 'water': m.preheat(); await waitSim(u.rig, 400); if (!await holdTarget(u.rig, 'water')) ui.ctrl({ cmd: 'action', name: 'water' }); break;
      case 'fault': m.preheat(); m.setFaults({ e1: 32 }); break;
      case 'offline': m.setOffline(true); break;
      case 'sleep': m.preheat(); ui.ctrl({ cmd: 'raw_write', sub: 1, words: [1] }); break;
    }
    if (['cold', 'ready', 'fault', 'offline', 'sleep'].includes(name) === false && S.speed === 0) setSpeed(1);
  });
  await Promise.all(jobs);
  syncInputs();
}

// ------------------------------------------------------------------ drawer wiring
function setSpeed(v) {
  S.speed = v; store.set('speed', v);
  $$('#speed button').forEach(b => b.classList.toggle('on', +b.dataset.speed === v));
  document.body.classList.toggle('paused', v === 0);
}
function bindOutputs() {
  for (const o of $$('output[data-for]')) {
    const inp = $('#' + o.dataset.for), unit = o.dataset.unit ?? (inp.id.startsWith('set_') ? ' °C' : '');
    const f = () => { o.textContent = (+inp.value).toFixed(inp.step && inp.step.includes('.') ? 1 : 0) + unit; };
    inp.addEventListener('input', f); f();
  }
}
// sliders for the physics knobs (machine.js DEFAULT_PARAMS documents all of them; these are the ones worth playing with)
const PARAM_UI = [
  { key: 'ambientC', label: 'Room', min: 5, max: 35, step: 1, unit: '°C' },
  { key: 'brewHeatCps', label: 'Brew heater', min: 0.1, max: 2, step: 0.05, unit: '°C/s' },
  { key: 'steamHeatCps', label: 'Steam heater', min: 0.1, max: 2, step: 0.05, unit: '°C/s' },
  { key: 'brewPressureBar', label: 'Pump', min: 4, max: 12, step: 0.1, unit: 'bar' },
  { key: 'brewFlowGs', label: 'Shot flow', min: 0.5, max: 4, step: 0.05, unit: 'g/s' },
  { key: 'milkRateScale', label: 'Milk heating', min: 0.3, max: 3, step: 0.05, unit: '×' },
  { key: 'noise', label: 'Sensor noise', min: 0, max: 1, step: 0.05, unit: '' },
];
function buildParams() {
  const host = $('#params');
  const meta = PARAM_UI.filter(p => p.key in DEFAULT_PARAMS);
  host.innerHTML = meta.length ? '<div class="subhead">Physics</div>' : '';
  for (const p of meta) {
    const row = document.createElement('div'); row.className = 'row';
    const v = S.rigs[0]?.rig.machine.params?.[p.key] ?? DEFAULT_PARAMS[p.key];
    row.innerHTML = `<label title="${p.key}">${p.label}</label><input type="range" min="${p.min}" max="${p.max}" step="${p.step}" value="${v}"><output></output>`;
    const inp = $('input', row), out = $('output', row);
    const show = () => { out.textContent = (+inp.value).toFixed(p.step < 0.1 ? 2 : p.step < 1 ? 1 : 0) + (p.unit ? ' ' + p.unit : ''); };
    inp.addEventListener('input', () => { show(); each(m => m.setParams({ [p.key]: +inp.value })); });
    show(); host.appendChild(row);
  }
}
function syncInputs() {
  const m = S.rigs[0]?.rig.machine; if (!m) return;
  const s = m.snapshot();
  $('#cup').checked = !!s.cup; $('#wand').checked = !!s.wand; $('#offline').checked = s.link !== 'up';
  for (const c of $$('[data-fault]')) c.checked = !!(s.faults && s.faults[c.dataset.fault]);
}
function wire() {
  $$('.panel h2').forEach(h => h.addEventListener('click', (e) => {
    if (e.target.closest('.tabs')) return;
    const p = h.parentElement; p.dataset.open = p.dataset.open === '1' ? '0' : '1';
    const open = store.get('panels', {}); open[h.textContent.trim().split(' ')[0]] = p.dataset.open; store.set('panels', open);
  }));
  const open = store.get('panels', {});
  $$('.panel').forEach(p => { const k = $('h2', p).textContent.trim().split(' ')[0]; if (open[k] != null) p.dataset.open = open[k]; });
  $$('#speed button').forEach(b => b.addEventListener('click', () => setSpeed(+b.dataset.speed)));
  $$('#scenarios button').forEach(b => b.addEventListener('click', () => scenario(b.dataset.scen)));
  $$('[data-set]').forEach(b => b.addEventListener('click', () => { const k = b.dataset.set; each(m => m.setBoilers({ [k]: +$('#set_' + k).value })); }));
  $('#cup').addEventListener('change', (e) => each(m => m.setCup(e.target.checked)));
  $('#wand').addEventListener('change', (e) => each(m => m.setWand(e.target.checked)));
  $('#scale_set').addEventListener('click', () => each(m => m.setScaleWeight(+$('#scale').value, 'brew')));
  $$('[data-tare]').forEach(b => b.addEventListener('click', () => each(m => m.tare(b.dataset.tare))));
  $('#grindrate').addEventListener('input', (e) => each(m => m.setGrindRate(+e.target.value)));
  const pk = $('#powerkey');
  const pkDown = (e) => { e.preventDefault(); pk.classList.add('down'); each((m, ui) => { m.powerKey(true); ui.ctrl({ cmd: 'key', value: 1 }); }); };
  const pkUp = () => { if (!pk.classList.contains('down')) return; pk.classList.remove('down'); each((m, ui) => { m.powerKey(false); ui.ctrl({ cmd: 'key', value: 0 }); }); };
  pk.addEventListener('pointerdown', pkDown); pk.addEventListener('pointerup', pkUp); pk.addEventListener('pointerleave', pkUp); pk.addEventListener('pointercancel', pkUp);
  $$('[data-fault]').forEach(c => c.addEventListener('change', () => {
    const f = {}; $$('[data-fault]').forEach(x => { f[x.dataset.fault] = x.checked ? (x.dataset.fault === 'e1' ? 32 : 16) : 0; });
    each(m => m.setFaults(f));
  }));
  $('#offline').addEventListener('change', (e) => each(m => m.setOffline(e.target.checked)));
  $('#msleep').addEventListener('change', (e) => each((m, ui) => ui.ctrl({ cmd: 'raw_write', sub: 1, words: [e.target.checked ? 1 : 0] })));
  $('#reboot').addEventListener('click', () => each(m => m.reboot({ factory: true })));
  $('#poweroff').addEventListener('click', () => each(m => m.powerOff()));
  $('#poweron').addEventListener('click', () => each(m => m.powerOn()));
  const d = new Date(); $('#tod').value = `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
  $('#tod_set').addEventListener('click', () => {
    const [h, mi] = $('#tod').value.split(':').map(Number); if (isNaN(h)) return;
    // the UI shows local time from its timezone setting; set the UTC clock so that local time matches the browser's
    const now = new Date(); const local = new Date(now.getFullYear(), now.getMonth(), now.getDate(), h, mi, 0);
    const utc = local.toISOString().replace(/\.\d+Z$/, 'Z');
    each((m, ui) => ui.ctrl({ cmd: 'clock', utc }));
  });
  $$('[data-page]').forEach(b => b.addEventListener('click', () => each((m, ui) => ui.ctrl({ cmd: 'page', name: b.dataset.page }))));
  $('#ui_restart').addEventListener('click', async () => { const keys = S.rigs.map(u => u.key); S.rigs.forEach(u => u.el.remove()); S.rigs = []; await setRigs(keys); });
  $('#compare').checked = store.get('compare', false) && !!S.builds.classic;
  $('#compare').disabled = !S.builds.classic;
  $('#compare').addEventListener('change', async (e) => { store.set('compare', e.target.checked); await setRigs(e.target.checked ? ['current', 'classic'] : ['current']); });
  $('#uartfilter').addEventListener('input', () => { S.uartDirty = true; });
  $('#uartpolls').addEventListener('change', () => { S.uartDirty = true; });
  window.addEventListener('resize', () => { clearTimeout(S.rz); S.rz = setTimeout(layout, 60); });
  window.addEventListener('keydown', (e) => {
    if (e.target.matches('input, textarea')) return;
    if (e.key === ' ') { e.preventDefault(); setSpeed(S.speed === 0 ? (S.prevSpeed || 1) : (S.prevSpeed = S.speed, 0)); }
    if (e.key === '1') setSpeed(1); if (e.key === '2') setSpeed(4); if (e.key === '3') setSpeed(20);
  });
  sheet();
}

// bottom sheet on phones: drag the handle to resize, tap to toggle
function sheet() {
  const h = $('#handle'), d = $('#drawer');
  let y0 = null, h0 = 0, moved = false;
  const set = (px) => { const v = Math.max(90, Math.min(innerHeight * 0.88, px)); document.documentElement.style.setProperty('--sheet', v + 'px'); };
  h.addEventListener('pointerdown', (e) => { y0 = e.clientY; h0 = d.getBoundingClientRect().height; moved = false; h.setPointerCapture(e.pointerId); d.classList.add('dragging'); });
  h.addEventListener('pointermove', (e) => { if (y0 === null) return; if (Math.abs(e.clientY - y0) > 4) moved = true; set(h0 + (y0 - e.clientY)); });
  const end = () => { if (y0 === null) return; d.classList.remove('dragging'); if (!moved) set(h0 < innerHeight * 0.5 ? innerHeight * 0.8 : innerHeight * 0.34); y0 = null; setTimeout(layout, 220); };
  h.addEventListener('pointerup', end); h.addEventListener('pointercancel', end);
}

function buildTabs() {
  const t = $('#livetabs'); t.innerHTML = '';
  if (S.rigs.length < 2) return;
  S.rigs.forEach((u, i) => { const b = document.createElement('button'); b.textContent = u.key; b.className = i === S.sel ? 'on' : ''; b.onclick = (e) => { e.stopPropagation(); S.sel = i; buildTabs(); S.uartDirty = true; }; t.appendChild(b); });
}
function buildExports() {
  const host = $('#exports'); host.innerHTML = '';
  for (const u of S.rigs) {
    for (const [kind, label] of [['display', 'Display'], ['bezel', 'In bezel']]) {
      const b = document.createElement('button'); b.className = 'mini'; b.textContent = `${label}${S.rigs.length > 1 ? ' · ' + u.key : ''}`;
      b.onclick = () => exportPng(u, kind); host.appendChild(b);
    }
  }
}
function exportPng(u, kind) {
  blit(u);
  const c = kind === 'display' ? u.src : composeBezelPng(u.src, 1200, S.rigs.length > 1 ? u.key : '');
  c.toBlob((blob) => {
    const a = document.createElement('a');
    const st = u.rig.ui.state(false);
    a.download = `meraki-${u.key}-${(st && st.phase || 'ui').toLowerCase()}-${kind}-${Date.now()}.png`;
    a.href = URL.createObjectURL(blob); a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  }, 'image/png');
}

// ------------------------------------------------------------------ live panels (~5 Hz)
const REGS = [
  [33, 'work_sta', v => '0x' + v.toString(16).padStart(4, '0')], [39, 'brew g', v => ((v << 16 >> 16) / 10).toFixed(1)], [38, 'grind g', v => ((v << 16 >> 16) / 10).toFixed(1)],
  [51, 'press raw', v => v], [52, 'flow', v => v], [50, 'cur1', v => v], [40, 'brew °C', v => (v / 10).toFixed(1)], [47, 'steam °C', v => (v / 10).toFixed(1)],
  [42, 'group °C', v => (v / 10).toFixed(1)], [48, 'milk °C', v => (v / 10).toFixed(1)], [62, 'heat E', v => v], [63, 'heat S', v => v],
  [0, 'power', v => v], [1, 'sleep', v => v], [24, 'steam sw', v => v], [44, 'inputs', v => '0x' + v.toString(16)],
  [34, 'E1', v => v], [35, 'E2', v => v], [36, 'E3', v => v], [22, 'brew flag', v => v],
];
function refreshPanels() {
  const f = S.fps;
  const r0 = S.rigs[0];
  if (r0) $('#simclock').textContent = `${simClock(r0.rig.t)} · ${f.val} fps · ${f.step.toFixed(1)} ms`;
  for (const u of S.rigs) {
    const st = u.rig.ui.state(false) || {};
    u.ph.textContent = u.rig.ui.stopped ? 'STOPPED' : (st.phase || '—');
    u.lastState = st;
  }
  const u = S.rigs[S.sel]; if (!u) return;
  const m = u.rig.machine, s = m.snapshot(), st = u.rig.ui.state(true) || {};
  const ph = $('#phase'); ph.textContent = st.phase || '—'; ph.className = 'phase ' + (st.phase || '');
  $('#livekv').innerHTML = [
    ['UI page', `${st.page || '—'}${st.tab && st.page !== 'home' ? ' · ' + st.tab : ''}`],
    ['MCU', `${s.power} · ${s.link}${s.activity ? ' · ' + (s.activity.kind || s.phase) : ''}`],
    ['Model', s.phase || '—'],
    ['Shot', `${(s.pressureBar ?? 0).toFixed(1)} bar · ${(s.flowMlS ?? 0).toFixed(1)} ml/s · ${(s.brewG ?? 0).toFixed(1)} g`],
  ].map(([k, v]) => `<span>${k}</span><span>${v}</span>`).join('');
  const g = (id, v, hot, ok) => { const el = $('#' + id); el.innerHTML = fmtC(v); el.parentElement.classList.toggle('ok', !!ok); el.parentElement.classList.toggle('hot', !!hot && !ok); };
  g('g_brew', s.brewC, s.stage62 > 0 && s.stage62 < 100, s.stage62 === 100);
  g('g_steam', s.steamC, s.stage63 > 0 && s.stage63 < 100, s.stage63 === 100);
  g('g_group', s.groupC); g('g_milk', s.milkC);
  const regs = $('#regs');
  if (!regs.children.length) regs.innerHTML = REGS.map(([w, l]) => `<div data-w="${w}"><label>${w} · ${l}</label><output></output></div>`).join('');
  for (const [w, , fmt] of REGS) {
    const el = regs.querySelector(`[data-w="${w}"]`), o = el.lastChild, v = fmt(m.regs[w]) + '';
    if (o.textContent !== v) { if (o.textContent) { el.classList.add('chg'); setTimeout(() => el.classList.remove('chg'), 30); } o.textContent = v; }
  }
  refreshUart(u);
}
function hex(b, n = 24) { let s = ''; for (let i = 0; i < Math.min(n, b.length); i++) s += b[i].toString(16).padStart(2, '0') + ' '; return s + (b.length > n ? '…' : ''); }
function refreshUart(u) {
  const panel = $('#uart').closest('.panel');
  const log = u.rig.uartLog;
  $('#uartcount').textContent = `${log.length} frames`;
  if (panel.dataset.open !== '1' || $('#uartpause').checked) return;
  const last = log.length ? log[log.length - 1] : null;
  if (!S.uartDirty && last === S.uartLast) return;
  S.uartDirty = false; S.uartLast = last;
  const q = $('#uartfilter').value.trim().toLowerCase(), polls = $('#uartpolls').checked;
  const rows = [];
  for (let i = log.length - 1; i >= 0 && rows.length < 160; i--) {
    const { dir, f } = log[i];
    const isPoll = f.cmd === 0x03 || f.cmd === 0x83;
    if (isPoll && !polls) continue;
    const name = `cmd${f.cmd.toString(16)}`, txt = `${name} sub${f.sub} len${f.len} ${f.note || ''} ${hex(f.bytes)}`;
    if (q && !txt.toLowerCase().includes(q) && !(`cmd${f.cmd}`).includes(q)) continue;
    const cls = dir === 'rx' ? 'rx' : 'tx', arrow = dir === 'rx' ? 'UI→MCU' : 'MCU→UI';
    const bad = /reject|bad|drop/i.test(f.note || '') ? ' bad' : '';
    rows.push(`<div><span class="t">${(f.t ?? 0).toFixed(2).padStart(7)}</span> <span class="${cls}">${arrow}</span> <span class="${cls}">0x${f.cmd.toString(16).padStart(2, '0')}</span> s${f.sub} l${f.len} <span class="n${bad}">${f.note || ''}</span> <span class="t">${hex(f.bytes)}</span></div>`);
  }
  $('#uart').innerHTML = rows.join('') || '<div class="t">no frames match</div>';
}

// ------------------------------------------------------------------ boot
async function main() {
  try {
    [S.builds, S.config, S.spec] = await Promise.all([loadJSON('builds.json'), loadJSON('fixtures/config.json'), loadJSON('fixtures/parity_spec.json').catch(() => null)]);
    const b = S.builds;
    $('#buildinfo').textContent = `current ${b.current.version} · ${b.current.ref}` + (b.classic ? `   ·   classic ${b.classic.version} · ${b.classic.ref}` : '');
    bindOutputs(); wire(); setSpeed(S.speed);
    await setRigs($('#compare').checked ? ['current', 'classic'] : ['current']);
    buildParams(); syncInputs();
    requestAnimationFrame(frame);
    window.studio = S;   // for poking around in the console
  } catch (e) {
    $('#stage').innerHTML = `<div class="loading">Could not start: ${e.message}</div>`;
    console.error(e);
  }
}
main();
