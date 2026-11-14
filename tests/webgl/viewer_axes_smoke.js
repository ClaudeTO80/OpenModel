// Headless test of the axes triad drawn in the bottom left corner of examples/webgl-fem-viewer.
// Usage: node viewer_axes_smoke.js <viewer index.html> <washer.mgwv> <washer.inp>
//   The oracle is independent of the viewer's camera code: the screen direction of each model axis is measured on the
//   projected nodes of the mesh (two nodes that differ along that axis only), and compared with the direction of the
//   coloured arrow found in the pixels of the triad. Checked in the standard views and after rotating with the mouse.
// Exit code 0 on success; needs WebGL2.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, inp] = process.argv;
if (!html || !mgwv || !inp) { console.error('usage: viewer_axes_smoke.js <index.html> <washer.mgwv> <washer.inp>'); process.exit(2); }
let failures = 0;
const check = (ok, message) => { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; };

function parseNodes(file) {
  const nodes = new Map();
  let mode = '';
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    if (line.startsWith('*')) { mode = /^\*NODE/i.test(line) ? 'n' : ''; continue; }
    const f = line.split(',').map((x) => x.trim()).filter(Boolean);
    if (mode === 'n' && f.length >= 4) nodes.set(+f[0], f.slice(1, 4).map(Number));
  }
  return nodes;
}
const COLORS = { X: [229, 72, 77], Y: [48, 164, 108], Z: [62, 99, 221] };

(async () => {
  const nodes = parseNodes(inp);
  const find = (x, y, z) => { for (const [id, c] of nodes) if (Math.hypot(c[0] - x, c[1] - y, c[2] - z) < 1e-6) return id; return -1; };
  // Pairs of nodes that differ along one axis only (washer: bore 3, outer 6, height 2, 15 degree facets).
  const pairs = { X: [find(3, 0, 0), find(6, 0, 0)], Y: [find(0, 3, 0), find(0, 6, 0)], Z: [find(3, 0, 0), find(3, 0, 2)] };
  check(Object.values(pairs).every((p) => p[0] > 0 && p[1] > 0), 'the reference nodes exist in the mesh');

  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  page.on('console', (m) => { if ((m.type() === 'error' || m.type() === 'warning') && !/GPU stall|Performance|willReadFrequently/.test(m.text())) problems.push(m.text()); });
  await page.goto('file://' + path.resolve(html));
  const visible = async () => page.evaluate(() => { const c = document.getElementById('axes'); const r = c.getBoundingClientRect(); return !c.hidden && r.width > 0; });
  check(!(await visible()), 'no model loaded: no triad');
  await page.evaluate((b64) => { const b = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)); window.OpenModelViewer.load(b.buffer, 'washer.mgwv'); }, fs.readFileSync(mgwv).toString('base64'));
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false } }));
  const P = (fn, ...a) => page.evaluate(([f, args]) => window.OpenModelViewer.pick[f](...args), [fn, a]);

  check(await visible(), 'model loaded: the triad is shown');
  const box = await page.evaluate(() => { const r = document.getElementById('axes').getBoundingClientRect(), v = document.getElementById('view').getBoundingClientRect(); return { l: r.left - v.left, b: v.bottom - r.bottom, w: r.width }; });
  check(box.l < 40 && box.b > 0 && box.b < 80, 'the triad sits in the bottom left corner (' + box.l.toFixed(0) + ' px from the left, ' + box.b.toFixed(0) + ' px from the bottom)');

  // Direction (unit vector on screen, y down) of the arrow of one colour in the triad canvas; null when the axis is foreshortened
  // (nearly end-on): the triad is orthographic, the model is seen in perspective, so the two disagree there and nothing is compared.
  const arrow = async (name) => page.evaluate(([nm, col]) => {
    const c = document.getElementById('axes'), g = c.getContext('2d'), d = g.getImageData(0, 0, c.width, c.height).data;
    const cx = c.width / 2, cy = c.height / 2; let sx = 0, sy = 0, n = 0, far = 0;
    for (let y = 0; y < c.height; y++) for (let x = 0; x < c.width; x++) {
      const i = (y * c.width + x) * 4;
      if (d[i + 3] > 200 && Math.abs(d[i] - col[0]) < 40 && Math.abs(d[i + 1] - col[1]) < 40 && Math.abs(d[i + 2] - col[2]) < 40) {
        const px = x - cx, py = y - cy;
        const r = Math.hypot(px, py);
        if (r < c.width * 0.36) { sx += px; sy += py; n++; far = Math.max(far, r); }   // the shaft, not the letter outside the arrow
      }
    }
    // An axis seen nearly end-on only leaves its letter near the centre: its colour does not reach far from it.
    if (n < 5 || far < c.width * 0.2) return null;
    const l = Math.hypot(sx, sy); return l < n * 2 ? null : [sx / l, sy / l, n];
  }, [name, COLORS[name]]);
  const reference = async (name) => {
    const a = await P('project', pairs[name][0]), b = await P('project', pairs[name][1]);
    const l = Math.hypot(b[0] - a[0], b[1] - a[1]);
    return l < 4 ? null : [(b[0] - a[0]) / l, (b[1] - a[1]) / l];
  };
  const compare = async (label) => {
    let seen = 0;
    for (const name of ['X', 'Y', 'Z']) {
      const ref = await reference(name), got = await arrow(name);
      if (ref === null || got === null) { continue; }
      seen++;
      const angle = Math.acos(Math.max(-1, Math.min(1, ref[0] * got[0] + ref[1] * got[1]))) * 180 / Math.PI;
      // The model is seen in perspective and the triad is orthographic, hence the tolerance.
      check(angle < 12, label + ': axis ' + name + ' points the same way as the model (' + angle.toFixed(1) + ' deg apart)');
    }
    check(seen >= (label.startsWith('view') ? 2 : 1), label + ': at least two axes can be compared (' + seen + ')');
  };

  for (const view of ['iso', 'x', 'y', 'z']) {
    await page.evaluate((v) => window.OpenModelViewer.set({ view: v }), view);
    await compare('view ' + view);
  }
  // Rotate with the mouse (left drag), then compare again: the triad must follow the model.
  await page.evaluate(() => window.OpenModelViewer.set({ view: 'iso' }));
  for (const [dx, dy] of [[40, 25], [-50, 30], [30, -45], [60, 20]]) {
    await page.mouse.move(700, 350); await page.mouse.down(); await page.mouse.move(700 + dx, 350 + dy, { steps: 8 }); await page.mouse.up();
    await compare('after dragging (' + dx + ',' + dy + ')');
  }
  // Switch off by the checkbox and by the API.
  await page.evaluate(() => { const c = document.getElementById('axesOn'); c.checked = false; c.dispatchEvent(new Event('change')); });
  await page.waitForTimeout(100);
  check(!(await visible()), 'unchecking "Axes" hides the triad');
  await page.evaluate(() => window.OpenModelViewer.set({ axes: true }));
  check(await visible(), 'set({axes:true}) shows it again');

  check(problems.length === 0, 'no console errors or page errors' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
