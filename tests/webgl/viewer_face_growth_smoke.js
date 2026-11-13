// Headless test of the growth of a face selection in examples/webgl-fem-viewer on real pixels.
// Usage: node viewer_face_growth_smoke.js <viewer index.html> <arch.mgwv> <arch.inp> <triangles per quad: 1|2>
//   arch.inp / arch.mgwv: the annular sector written by tests/support/make_arch_mesh.py (arch.inp holds hexahedra, so a
//   quadrilateral face is one face, arch_tet.inp holds tetrahedra with mixed winding, so it is two triangle faces).
// The expected sizes are computed from the construction of the mesh (outer facets 2 degrees apart), not from the viewer.
// Exit code 0 on success. Requires WebGL2 (SwiftShader is enough).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, inp, perQuadArg] = process.argv;
if (!html || !mgwv || !inp || !['1', '2'].includes(perQuadArg)) { console.error('usage: viewer_face_growth_smoke.js <index.html> <arch.mgwv> <arch.inp> <1|2>'); process.exit(2); }
const perQuad = Number(perQuadArg);
let failures = 0;
const check = (ok, message) => { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; };

// Construction of the mesh (must match make_arch_mesh.py).
const STEP_DEG = 2, THETA_DIV = 45, R_DIV = 2, Z_DIV = 2;
const CLICKED_COLUMN = 5;                                   // outer facet between 10 and 12 degrees
const SKIN_QUADS = 2 * THETA_DIV * Z_DIV                    // outer and inner cylinders
                 + 2 * THETA_DIV * R_DIV                    // top and bottom planes
                 + 2 * R_DIV * Z_DIV;                       // the two end caps
// The clicked column plus the columns within the angle on each side, clipped by the two ends of the arch.
const columnsWithin = (angle) => {
  const k = Math.floor(angle / STEP_DEG + 1e-9);
  return Math.min(k, CLICKED_COLUMN) + 1 + Math.min(k, THETA_DIV - 1 - CLICKED_COLUMN);
};

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

(async () => {
  const nodes = parseNodes(inp);
  const nearest = (r, thetaDeg, z) => {
    const x = r * Math.cos(thetaDeg * Math.PI / 180), y = r * Math.sin(thetaDeg * Math.PI / 180);
    let best = -1, bd = Infinity;
    for (const [id, c] of nodes) { const d = Math.hypot(c[0] - x, c[1] - y, c[2] - z); if (d < bd) { bd = d; best = id; } }
    return best;
  };
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  page.on('console', (m) => { if ((m.type() === 'error' || m.type() === 'warning') && !/GPU stall|Performance/.test(m.text())) problems.push(m.text()); });
  await page.goto('file://' + path.resolve(html));
  await page.evaluate((b64) => { const b = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)); window.OpenModelViewer.load(b.buffer, 'arch.mgwv'); }, fs.readFileSync(mgwv).toString('base64'));
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false }, mode: 'flat', wire: 'none', view: 'iso' }));
  const P = (fn, ...a) => page.evaluate(([f, args]) => { const r = window.OpenModelViewer.pick[f](...args); return r === undefined ? null : r; }, [fn, a]);
  await P('setMode', 'face');

  // The centre of the lower facet of the clicked column on the outer cylinder, in pixels.
  const lo = CLICKED_COLUMN * STEP_DEG, hi = lo + STEP_DEG;
  const corners = [nearest(5, lo, 0), nearest(5, hi, 0), nearest(5, hi, 1), nearest(5, lo, 1)];
  const pts = [];
  for (const id of corners) pts.push(await P('project', id));
  const cx = pts.reduce((a, p) => a + p[0], 0) / 4, cy = pts.reduce((a, p) => a + p[1], 0) / 4;
  const select = async (growth, angle) => {
    await P('clear'); await P('setGrowth', growth); await P('setAngle', angle);
    await page.mouse.click(cx, cy);
    return P('get');
  };

  // Default state of the viewer: a click already selects the whole planar facet, and nothing more.
  await P('clear');
  await page.mouse.click(cx, cy);
  let s = await P('get');
  const rows = Z_DIV;
  check(await page.evaluate(() => window.OpenModelViewer.state.pickAngle === 1 && window.OpenModelViewer.state.pickGrowth === 'same-normal'), 'default: angle 1 deg, growth "same-normal"');
  check(s.faces.length === rows * perQuad, 'default click: the ' + rows + ' coplanar facets of the column (' + s.faces.length + ', expected ' + rows * perQuad + ')');

  s = await select('same-normal', 0);
  check(s.faces.length === 1, 'angle 0: only the clicked face (' + s.faces.length + ')');

  // Same normal: the angle is against the clicked face, so the facets 2 degrees apart join only from 2 degrees on, and
  // the selection does not run away along the cylinder.
  for (const angle of [1, 1.9, 2.1, 4.1, 6.1, 20.1, 60]) {
    s = await select('same-normal', angle);
    const expected = columnsWithin(angle) * rows * perQuad;
    check(s.faces.length === expected, 'same normal, angle ' + angle + ' deg: ' + columnsWithin(angle) + ' columns x ' + rows + ' rows (' + s.faces.length + ', expected ' + expected + ')');
  }
  // The neighbours are the columns on both sides of the clicked one, and nothing else.
  s = await select('same-normal', 4.1);
  const ids = new Set();
  for (const f of s.faces) for (const n of f.nodes) ids.add(n);
  const angles = [...ids].map((id) => { const c = nodes.get(id); return { r: Math.hypot(c[0], c[1]), a: Math.atan2(c[1], c[0]) * 180 / Math.PI }; });
  check(angles.every((p) => Math.abs(p.r - 5) < 1e-6), 'the selected faces are all on the outer cylinder');
  check(Math.min(...angles.map((p) => p.a)) > lo - 2 * STEP_DEG - 1e-6 && Math.max(...angles.map((p) => p.a)) < hi + 2 * STEP_DEG + 1e-6,
        'the selection spans 2 columns on each side of the clicked one (' + Math.min(...angles.map((p) => p.a)).toFixed(1) + ' to ' + Math.max(...angles.map((p) => p.a)).toFixed(1) + ' deg)');

  // Smooth surface: the angle is between neighbouring faces, so the whole cylinder is reached from 2 degrees on, and still
  // not the top, bottom or caps, which are 90 degrees away.
  s = await select('smooth', 1);
  check(s.faces.length === rows * perQuad, 'smooth, angle 1 deg: still the clicked column (' + s.faces.length + ')');
  s = await select('smooth', 2.1);
  check(s.faces.length === THETA_DIV * rows * perQuad, 'smooth, angle 2.1 deg: the whole outer cylinder (' + s.faces.length + ', expected ' + THETA_DIV * rows * perQuad + ')');
  s = await select('smooth', 91);
  check(s.faces.length === SKIN_QUADS * perQuad, 'smooth, angle 91 deg: every skin face (' + s.faces.length + ', expected ' + SKIN_QUADS * perQuad + ')');
  s = await select('same-normal', 91);
  check(s.faces.length === SKIN_QUADS * perQuad, 'same normal, angle 91 deg: every skin face (' + s.faces.length + ')');

  check(problems.length === 0, 'no console errors or page errors' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
