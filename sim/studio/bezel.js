// sim/studio/bezel.js — the machine-top bezel around the round display, drawn procedurally on a canvas (no images):
// a spun dark-metal ring with an anisotropic sheen, a beveled lip, a black glass well, and a glass reflection on top.
// drawBezel() paints everything under the display, drawGlass() the reflection over it. The same functions draw the
// on-screen bezel and the "display in bezel" PNG export.

// geometry as fractions of the canvas size s: display radius, glass well radius, ring outer radius
export const GEOM = { display: 0.375, well: 0.392, lip: 0.405, ring: 0.488 };

function mulberry(seed) { return () => { seed |= 0; seed = (seed + 0x6D2B79F5) | 0; let t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }

function conic(ctx, cx, cy, start, stops) {
  if (ctx.createConicGradient) {
    const g = ctx.createConicGradient(start, cx, cy);
    for (const [o, c] of stops) g.addColorStop(o, c);
    return g;
  }
  return stops[0][1];
}

export function drawBezel(ctx, s, opts = {}) {
  const cx = s / 2, cy = s / 2, R = s * GEOM.ring, Rl = s * GEOM.lip, Rw = s * GEOM.well;
  ctx.save();
  ctx.clearRect(0, 0, s, s);

  // soft drop shadow of the whole ring on the machine top
  ctx.save();
  ctx.shadowColor = 'rgba(0,0,0,0.55)'; ctx.shadowBlur = s * 0.05; ctx.shadowOffsetY = s * 0.018;
  ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.fillStyle = '#1b1c1f'; ctx.fill();
  ctx.restore();

  // the ring: spun gunmetal = a conic sheen (two broad highlights, like light on brushed metal) …
  ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.arc(cx, cy, Rl, 0, Math.PI * 2, true);
  ctx.fillStyle = conic(ctx, cx, cy, -Math.PI / 2 - 0.5, [
    [0.00, '#3a3c41'], [0.07, '#7c7f86'], [0.13, '#45474d'], [0.30, '#26272b'], [0.43, '#55585e'],
    [0.50, '#3b3d42'], [0.57, '#6e7178'], [0.63, '#3d3f44'], [0.80, '#232428'], [0.92, '#4a4c52'], [1.00, '#3a3c41']]);
  ctx.fill('evenodd');
  // … plus fine concentric lathe rings (deterministic noise, so the export matches the screen)
  const rnd = mulberry(7);
  ctx.save();
  ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.arc(cx, cy, Rl, 0, Math.PI * 2, true); ctx.clip('evenodd');
  const step = Math.max(0.6, s / 900);
  for (let r = Rl; r < R; r += step) {
    const v = rnd();
    ctx.strokeStyle = v > 0.5 ? `rgba(255,255,255,${(v - 0.5) * 0.05})` : `rgba(0,0,0,${(0.5 - v) * 0.10})`;
    ctx.lineWidth = step;
    ctx.beginPath(); ctx.arc(cx, cy, r, 0, Math.PI * 2); ctx.stroke();
  }
  ctx.restore();

  // outer edge: a thin chamfer catching the light at the top, dark at the bottom
  ctx.lineWidth = s * 0.006;
  const edge = ctx.createLinearGradient(0, cy - R, 0, cy + R);
  edge.addColorStop(0, 'rgba(255,255,255,0.35)'); edge.addColorStop(0.5, 'rgba(255,255,255,0.05)'); edge.addColorStop(1, 'rgba(0,0,0,0.5)');
  ctx.strokeStyle = edge; ctx.beginPath(); ctx.arc(cx, cy, R - ctx.lineWidth / 2, 0, Math.PI * 2); ctx.stroke();

  // inner lip: a beveled step down to the glass (dark above, light below = it is recessed)
  const lip = ctx.createLinearGradient(0, cy - Rl, 0, cy + Rl);
  lip.addColorStop(0, '#0b0b0d'); lip.addColorStop(0.55, '#1e1f23'); lip.addColorStop(1, '#5d6067');
  ctx.beginPath(); ctx.arc(cx, cy, Rl, 0, Math.PI * 2); ctx.arc(cx, cy, Rw, 0, Math.PI * 2, true);
  ctx.fillStyle = lip; ctx.fill('evenodd');

  // the glass well (black, slightly blue) — the display is drawn into it
  const well = ctx.createRadialGradient(cx, cy - Rw * 0.2, Rw * 0.1, cx, cy, Rw);
  well.addColorStop(0, '#07080a'); well.addColorStop(1, '#020203');
  ctx.beginPath(); ctx.arc(cx, cy, Rw, 0, Math.PI * 2); ctx.fillStyle = well; ctx.fill();
  if (opts.label) {
    ctx.fillStyle = 'rgba(210,214,222,0.30)';
    ctx.font = `600 ${Math.round(s * 0.017)}px ui-sans-serif, system-ui, -apple-system, sans-serif`;
    ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
    const t = opts.label.toUpperCase().split('').join(' ');
    ctx.fillText(t, cx, cy + (R + Rl) / 2);
  }
  ctx.restore();
}

export function drawGlass(ctx, s) {
  const cx = s / 2, cy = s / 2, Rw = s * GEOM.well;
  ctx.save();
  ctx.clearRect(0, 0, s, s);
  ctx.beginPath(); ctx.arc(cx, cy, Rw, 0, Math.PI * 2); ctx.clip();
  // inner shadow where the glass meets the lip
  const sh = ctx.createRadialGradient(cx, cy, Rw * 0.86, cx, cy, Rw);
  sh.addColorStop(0, 'rgba(0,0,0,0)'); sh.addColorStop(1, 'rgba(0,0,0,0.55)');
  ctx.fillStyle = sh; ctx.fillRect(0, 0, s, s);
  // a broad soft reflection across the upper left …
  const g = ctx.createLinearGradient(cx - Rw, cy - Rw, cx + Rw * 0.2, cy + Rw * 0.4);
  g.addColorStop(0, 'rgba(255,255,255,0.10)'); g.addColorStop(0.45, 'rgba(255,255,255,0.025)'); g.addColorStop(0.46, 'rgba(255,255,255,0)');
  ctx.fillStyle = g;
  ctx.beginPath(); ctx.ellipse(cx - Rw * 0.18, cy - Rw * 0.32, Rw * 1.05, Rw * 0.78, -0.55, 0, Math.PI * 2); ctx.fill();
  // … and a thin bright arc near the top edge (the window behind the camera)
  ctx.lineWidth = s * 0.006; ctx.lineCap = 'round';
  const a = ctx.createLinearGradient(cx - Rw, 0, cx + Rw, 0);
  a.addColorStop(0, 'rgba(255,255,255,0)'); a.addColorStop(0.35, 'rgba(255,255,255,0.20)'); a.addColorStop(0.6, 'rgba(255,255,255,0.05)'); a.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.strokeStyle = a; ctx.beginPath(); ctx.arc(cx, cy, Rw * 0.955, Math.PI * 1.08, Math.PI * 1.62); ctx.stroke();
  ctx.restore();
}

// a complete still: bezel + the display (an image/canvas of 480x480) + glass, at size s
export function composeBezelPng(display, s = 1200, label) {
  const c = document.createElement('canvas'); c.width = c.height = s;
  const ctx = c.getContext('2d');
  drawBezel(ctx, s, { label });
  const d = s * GEOM.display * 2;
  ctx.save(); ctx.beginPath(); ctx.arc(s / 2, s / 2, s * GEOM.display, 0, Math.PI * 2); ctx.clip();
  ctx.imageSmoothingQuality = 'high';
  ctx.drawImage(display, (s - d) / 2, (s - d) / 2, d, d);
  ctx.restore();
  const g = document.createElement('canvas'); g.width = g.height = s;
  drawGlass(g.getContext('2d'), s);
  ctx.drawImage(g, 0, 0);
  return c;
}
