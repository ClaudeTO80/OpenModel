// Headless test of the display of constraints on the degrees of freedom in examples/webgl-fem-viewer, on real pixels.
// Usage: node viewer_constraints_smoke.js <viewer index.html> <washer_bc.mgwv> <washer_bc.json> <plain.mgwv>
//   washer_bc.mgwv / .json: the washer with boundary conditions written by tests/support/make_constrained_washer.py (the json holds
//   the constrained nodes and degrees of freedom that the construction implies). plain.mgwv: a mesh without constraints.
// The symbols are checked by switching them off and on and comparing the pixels around each probe node: the pixels that change must
// be of the colour of the axis (X red, Y green, Z blue; a ring around a rotation axis is a lighter shade) and nothing else.
// Exit code 0 on success; needs WebGL2.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, jsonFile, plain] = process.argv;
if (!html || !mgwv || !jsonFile || !plain) { console.error('usage: viewer_constraints_smoke.js <index.html> <washer_bc.mgwv> <washer_bc.json> <plain.mgwv>'); process.exit(2); }
let failures = 0;
const check = (ok, message) => { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; };
const expected = JSON.parse(fs.readFileSync(jsonFile, 'utf8'));

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  page.on('console', (m) => { if ((m.type() === 'error' || m.type() === 'warning') && !/GPU stall|Performance|willReadFrequently/.test(m.text())) problems.push(m.text()); });
  await page.goto('file://' + path.resolve(html));
  const load = (file) => page.evaluate((b64) => { const b = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)); window.OpenModelViewer.load(b.buffer, 'm.mgwv'); }, fs.readFileSync(file).toString('base64'));
  const V = (fn, ...a) => page.evaluate(([f, args]) => window.OpenModelViewer[f](...args), [fn, a]);
  const P = (fn, ...a) => page.evaluate(([f, args]) => { const r = window.OpenModelViewer.pick[f](...args); return r === undefined ? null : r; }, [fn, a]);
  const rowsHidden = () => page.evaluate(() => document.getElementById('consRows').hidden);

  // A mesh without constraints: nothing to show, no controls.
  await load(plain);
  check((await V('constraints')).length === 0 && await rowsHidden(), 'a mesh without constraints: none reported and the controls are hidden');
  check(!(await page.evaluate(() => document.getElementById('stats').textContent)).includes('Constrained'), 'a mesh without constraints: no "Constrained nodes" row');

  await load(mgwv);
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false }, mode: 'flat', wire: 'none', view: 'iso', axes: false }));
  await P('setMode', 'node');

  // What the file holds, against what the construction implies.
  const got = (await V('constraints')).map((c) => ({ node: c.node, dofs: c.dofs, prescribed: c.prescribed })).sort((a, b) => a.node - b.node);
  check(JSON.stringify(got) === JSON.stringify(expected.constraints), 'constraints() lists exactly the ' + expected.constraints.length + ' constrained nodes with their degrees of freedom and prescribed flag');
  check(!(await rowsHidden()), 'the controls are shown');
  check((await page.evaluate(() => document.getElementById('stats').textContent)).includes('Constrained nodes' + expected.constraints.length), 'the statistics give the number of constrained nodes');
  check(await page.evaluate(() => window.OpenModelViewer.state.consOn === true), 'constraints are shown by default');

  // Pixels: classify what changes around a node when the symbols are switched off.
  const snapshot = async (clip) => {
    const png = (await page.screenshot({ clip })).toString('base64');
    return page.evaluate(async (b64) => {
      const img = new Image(); img.src = 'data:image/png;base64,' + b64; await img.decode();
      const c = document.createElement('canvas'); c.width = img.width; c.height = img.height;
      const g = c.getContext('2d', { willReadFrequently: true }); g.drawImage(img, 0, 0);
      return Array.from(g.getImageData(0, 0, c.width, c.height).data);
    }, png);
  };
  const HALF = 40;
  const probe = async (node) => {
    const [x, y] = await P('project', node);
    return { x, y, clip: { x: Math.max(0, Math.round(x - HALF)), y: Math.max(0, Math.round(y - HALF)), width: 2 * HALF, height: 2 * HALF } };
  };
  // Classes of the changed pixels: dominant red, green, blue, orange (the dot), other.
  const classify = (on, off) => {
    const n = { red: 0, green: 0, blue: 0, orange: 0, other: 0, total: 0 };
    for (let i = 0; i < on.length; i += 4) {
      if (Math.abs(on[i] - off[i]) + Math.abs(on[i + 1] - off[i + 1]) + Math.abs(on[i + 2] - off[i + 2]) < 60) continue;
      const r = on[i], g = on[i + 1], b = on[i + 2];
      n.total++;
      if (r > 200 && g > 120 && g < 200 && b < 90) n.orange++;
      else if (r > g + 40 && r > b + 40) n.red++;
      else if (g > r + 25 && g > b + 25) n.green++;
      else if (b > r + 40 && b > g + 25) n.blue++;
      else n.other++;
    }
    return n;
  };
  const diffAt = async (node, setup) => {
    const p = await probe(node);
    await V('set', { constraints: true });
    const on = await snapshot(p.clip);
    await V('set', { constraints: false });
    const off = await snapshot(p.clip);
    await V('set', { constraints: true });
    return classify(on, off);
  };
  await V('set', { constraintSize: 8 });

  const ux = await diffAt(expected.probes.ux);
  check(ux.total > 40 && ux.red > 0.6 * ux.total && ux.green === 0 && ux.blue === 0, 'ux only: red symbol (' + JSON.stringify(ux) + ')');
  const uy = await diffAt(expected.probes.uy);
  check(uy.total > 40 && uy.green > 0.6 * uy.total && uy.red === 0 && uy.blue === 0, 'uy only: green symbol (' + JSON.stringify(uy) + ')');
  const uz = await diffAt(expected.probes.uz);
  check(uz.orange > 5, 'uz with a prescribed value: an orange dot (' + JSON.stringify(uz) + ')');
  check(uz.blue > 0.5 * uz.total && uz.red + uz.green < 0.05 * uz.total, 'uz: a blue symbol, and no red or green one (edge pixels of the dot aside)');
  const rot = await diffAt(expected.probes.rot);
  check(rot.total > 20 && rot.red > 0 && rot.green > 0 && rot.blue === 0 && rot.orange === 0, 'rx and ry: a red and a green ring, nothing blue (' + JSON.stringify(rot) + ')');
  const unconstrained = await diffAt(find(expected));
  check(unconstrained.total === 0, 'a node without constraints: nothing changes around it (' + unconstrained.total + ' pixels)');

  // The size control scales the symbols.
  const sizeCount = async (size) => { await V('set', { constraintSize: size }); return (await diffAt(expected.probes.ux)).total; };
  const small = await sizeCount(4), large = await sizeCount(12);
  check(large > 2 * small && small > 0, 'a larger symbol size covers more pixels (' + small + ' at 4 %, ' + large + ' at 12 %)');
  await V('set', { constraintSize: 8 });

  // The checkbox and the API agree.
  await page.evaluate(() => { const c = document.getElementById('consOn'); c.checked = false; c.dispatchEvent(new Event('change')); });
  check(await page.evaluate(() => window.OpenModelViewer.state.consOn === false), 'unchecking "Constraints" turns the symbols off');
  await V('set', { constraints: true });
  check(await page.evaluate(() => document.getElementById('consOn').checked), 'set({constraints:true}) turns them on and checks the box');

  // A clipped-away node loses its symbols together with the surface (the clip uses the model position).
  const before = (await diffAt(expected.probes.ux)).total;
  await V('set', { clip: true, clipAxis: 2, clipPos: 0.2 });
  const clipped = (await diffAt(expected.probes.ux)).total;
  await V('set', { clip: false });
  check(before > 40 && clipped < before / 4, 'the section removes the symbols of the cut-away nodes (' + before + ' -> ' + clipped + ' pixels)');

  check(problems.length === 0, 'no console errors or page errors' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);

  function find(exp) {   // a node that is on the top face and not constrained: the neighbour of the ux probe along the ring
    const taken = new Set(exp.constraints.map((c) => c.node));
    for (let id = 1; id < 1e6; id++) if (!taken.has(id)) return id;
    return 1;
  }
})().catch((e) => { console.error(e); process.exit(1); });
