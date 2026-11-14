// Headless test of the recognition of holes in the face selection of examples/webgl-fem-viewer, on real pixels.
// Usage: node viewer_hole_smoke.js <viewer index.html> <washer.mgwv> <washer.inp> <triangles per quad: 1|2> <full|half>
//   The meshes come from tests/support/make_washer_mesh.py: a washer (full) or half a washer (half), bore radius 3, outer radius 6,
//   facets 15 degrees apart, 2 rings x 2 layers of elements. With tetrahedra (2) every quadrilateral is two triangle faces and the
//   winding of the skin triangles is mixed.
// The expected sizes follow from the construction of the mesh, not from the viewer. Exit code 0 on success; needs WebGL2.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, inp, perQuadArg, kind] = process.argv;
if (!html || !mgwv || !inp || !['1', '2'].includes(perQuadArg) || !['full', 'half'].includes(kind)) {
  console.error('usage: viewer_hole_smoke.js <index.html> <washer.mgwv> <washer.inp> <1|2> <full|half>');
  process.exit(2);
}
const perQuad = Number(perQuadArg);
let failures = 0;
const check = (ok, message) => { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; };

// Construction of the mesh (must match make_washer_mesh.py).
const R_IN = 3, R_OUT = 6, R_MID = 4.5, HEIGHT = 2, STEP_DEG = 15, Z_DIV = 2, R_DIV = 2;
const COLUMNS = kind === 'full' ? 24 : 12;
const WALL = COLUMNS * Z_DIV * perQuad;           // faces on the wall of the bore
const COLUMN = Z_DIV * perQuad;                   // faces of one column of a wall: what a click selects when it is not a hole
const TOP = COLUMNS * R_DIV * perQuad;            // faces of the top plane

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
  const radius = (id) => Math.hypot(nodes.get(id)[0], nodes.get(id)[1]);
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
  await page.evaluate((b64) => { const b = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)); window.OpenModelViewer.load(b.buffer, 'washer.mgwv'); }, fs.readFileSync(mgwv).toString('base64'));
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false }, mode: 'flat', wire: 'none', view: 'iso' }));
  const P = (fn, ...a) => page.evaluate(([f, args]) => { const r = window.OpenModelViewer.pick[f](...args); return r === undefined ? null : r; }, [fn, a]);
  await P('setMode', 'face');

  // Pixel at the centre of the quadrilateral of column j (angle 15 j .. 15 (j+1)) between two radii and two heights.
  const centre = async (r0, r1, z0, z1, j) => {
    const ids = [nearest(r0, STEP_DEG * j, z0), nearest(r1, STEP_DEG * j, z0), nearest(r1, STEP_DEG * (j + 1), z1), nearest(r0, STEP_DEG * (j + 1), z1)];
    const pts = [];
    for (const id of ids) pts.push(await P('project', id));
    return [pts.reduce((a, p) => a + p[0], 0) / 4, pts.reduce((a, p) => a + p[1], 0) / 4];
  };
  const click = async (x, y) => { await P('clear'); await page.mouse.click(x, y); return P('get'); };
  const nodesOf = (s) => [...new Set(s.faces.flatMap((f) => f.nodes))];
  const onRadius = (s, r) => s.faces.length > 0 && nodesOf(s).every((id) => Math.abs(radius(id) - r) < 1e-6);

  // A visible facet of the wall at the radius r: the first column whose click lands on that wall.
  const findWall = async (r) => {
    for (let j = 0; j < COLUMNS; j++) {
      const [x, y] = await centre(r, r, 1, HEIGHT, j);
      const s = await click(x, y);
      if (onRadius(s, r)) return { x, y, j };
    }
    return null;
  };

  await P('setAngle', 1); await P('setHoles', true); await P('setHoleArc', 330);
  check(await page.evaluate(() => window.OpenModelViewer.state.pickHoles === true && window.OpenModelViewer.state.pickHoleArc === 330), 'default: holes on, arc 330 deg');

  const bore = await findWall(R_IN);
  check(bore !== null, 'a facet of the bore is visible and can be clicked');
  const outer = await findWall(R_OUT);
  check(outer !== null, 'a facet of the outer wall is visible and can be clicked');

  if (bore) {
    let s = await click(bore.x, bore.y);
    if (kind === 'full') {
      check(s.faces.length === WALL && onRadius(s, R_IN), 'closed hole: a click on the bore selects the whole wall, ' + COLUMNS + ' columns x ' + Z_DIV + ' rows (' + s.faces.length + ', expected ' + WALL + ')');
      const ring = new Set(nodesOf(s).map((id) => Math.round(Math.atan2(nodes.get(id)[1], nodes.get(id)[0]) * 180 / Math.PI / STEP_DEG * 1e3) / 1e3));
      check(ring.size === COLUMNS, 'the wall goes all the way round (' + ring.size + ' distinct angles, expected ' + COLUMNS + ')');
    } else {
      check(s.faces.length === COLUMN && onRadius(s, R_IN), 'half hole with arc 330: not a hole, one column of facets (' + s.faces.length + ', expected ' + COLUMN + ')');
      await P('setHoleArc', 170);
      s = await click(bore.x, bore.y);
      check(s.faces.length === WALL && onRadius(s, R_IN), 'half hole with arc 170: the whole half wall, ' + COLUMNS + ' columns x ' + Z_DIV + ' rows (' + s.faces.length + ', expected ' + WALL + ')');
      await P('setHoleArc', 190);
      s = await click(bore.x, bore.y);
      check(s.faces.length === COLUMN, 'half hole with arc 190: 180 deg is short of it, one column (' + s.faces.length + ')');
      await P('setHoleArc', 330);
    }
    await P('setHoleArc', kind === 'full' ? 330 : 170);
    await P('setAngle', 0);
    s = await click(bore.x, bore.y);
    check(s.faces.length === 1, 'angle 0: a single face, holes are not looked for (' + s.faces.length + ')');
    await P('setAngle', 1);
    await P('setHoles', false);
    s = await click(bore.x, bore.y);
    check(s.faces.length === COLUMN, 'holes off: one column of facets, as before (' + s.faces.length + ', expected ' + COLUMN + ')');
    await P('setHoles', true);
  }

  if (outer) {
    await P('setHoleArc', kind === 'full' ? 330 : 170);
    const s = await click(outer.x, outer.y);
    check(s.faces.length === COLUMN && onRadius(s, R_OUT), 'the outer wall is convex, so it is not a hole: one column of facets (' + s.faces.length + ', expected ' + COLUMN + ')');
  }

  // A plane is never a hole: the top annulus is selected whole, as before.
  await P('setHoleArc', kind === 'full' ? 330 : 170);
  const [tx, ty] = await centre(3, R_MID, HEIGHT, HEIGHT, 3);
  const top = await click(tx, ty);
  check(top.faces.length === TOP, 'the top plane is selected whole (' + top.faces.length + ', expected ' + TOP + ')');

  check(problems.length === 0, 'no console errors or page errors' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
