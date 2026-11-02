// Headless test of picking (nodes, elements, faces, rectangle, angle, ids) in examples/webgl-fem-viewer on real pixels.
// Usage: node viewer_pick_smoke.js <viewer index.html> <model.mgwv> <model.inp> [screenshot-dir]
//   model.mgwv: written by `openmodel export-web model.inp model.mgwv --results ...` (tetrahedral beam with displacements)
//   model.inp:  the same mesh; it is the independent oracle (its nodes and tetrahedra are parsed here).
// Exit code 0 on success. Requires WebGL2 (SwiftShader is enough).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, inp, shotDir] = process.argv;
if (!html || !mgwv || !inp) { console.error('usage: viewer_pick_smoke.js <index.html> <model.mgwv> <model.inp> [dir]'); process.exit(2); }
let failures = 0;
const check = (ok, message) => { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; };

// Oracle: nodes and tetrahedra of the .inp, and the skin faces computed independently of the viewer and the writer.
function parseInp(file) {
  const nodes = new Map(), tets = [];
  let mode = '';
  for (const line of fs.readFileSync(file, 'utf8').split(/\r?\n/)) {
    if (line.startsWith('*')) { mode = /^\*NODE/i.test(line) ? 'n' : /^\*ELEMENT/i.test(line) ? 'e' : ''; continue; }
    const f = line.split(',').map((x) => x.trim()).filter(Boolean);
    if (mode === 'n' && f.length >= 4) nodes.set(+f[0], f.slice(1, 4).map(Number));
    if (mode === 'e' && f.length >= 5) tets.push({ id: +f[0], n: f.slice(1, 5).map(Number) });
  }
  const faces = new Map();
  for (const t of tets) for (const tri of [[0, 1, 2], [0, 1, 3], [1, 2, 3], [0, 2, 3]]) {
    const ids = tri.map((i) => t.n[i]).sort((a, b) => a - b), key = ids.join(',');
    if (!faces.has(key)) faces.set(key, []);
    faces.get(key).push({ element: t.id, nodes: ids });
  }
  const skin = [...faces.values()].filter((v) => v.length === 1).map((v) => v[0]);
  return { nodes, tets, skin };
}

(async () => {
  const model = parseInp(inp);
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  page.on('console', (m) => { if ((m.type() === 'error' || m.type() === 'warning') && !/GPU stall|Performance/.test(m.text())) problems.push(m.text()); });
  await page.goto('file://' + path.resolve(html));
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };
  await page.evaluate((b64) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    window.OpenModelViewer.load(bytes.buffer, 'beam.mgwv');
  }, fs.readFileSync(mgwv).toString('base64'));
  // Static view without deformation so that the oracle geometry is the drawn geometry (deformation is tested separately).
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false }, mode: 'flat', wire: 'none' }));
  const P = (fn, ...a) => page.evaluate(([f, args]) => { const r = window.OpenModelViewer.pick[f](...args); return r === undefined ? null : r; }, [fn, a]);
  const sel = () => P('get');
  const canvasBox = await page.evaluate(() => { const r = document.getElementById('gl').getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; });

  check(await P('available'), 'the file carries the picking chunks');
  check(await page.evaluate(() => !document.getElementById('pickSec').hidden), 'the Selezione section is shown');
  check(await page.evaluate(() => window.OpenModelViewer.state.model.meta.counts.nodes) === model.nodes.size, 'node count agrees with the .inp (' + model.nodes.size + ')');

  // 1. Every vertex id of the file is a node of the mesh and its coordinates agree with the .inp (the ids are real).
  const idCheck = await page.evaluate(() => {
    const m = window.OpenModelViewer.state.model, o = m.meta.origin, out = [];
    for (let v = 0; v < m.vertexNodeId.length; v++) out.push([m.vertexNodeId[v], m.positions[3 * v] + o[0], m.positions[3 * v + 1] + o[1], m.positions[3 * v + 2] + o[2]]);
    return out;
  });
  const badId = idCheck.filter(([id, x, y, z]) => { const c = model.nodes.get(id); return !c || Math.hypot(c[0] - x, c[1] - y, c[2] - z) > 1e-4; });
  check(idCheck.length > 0 && badId.length === 0, idCheck.length + ' vertex ids match the coordinates of the .inp (' + badId.length + ' wrong)');

  // 2. Nodes: click at the projected position of every node. Visible ones must come back as themselves, hidden ones must not.
  await P('setMode', 'node');
  const self = [];
  for (const id of model.nodes.keys()) {
    const at = await page.evaluate((nid) => { const pk = window.OpenModelViewer.pick, c = pk.project(nid); return c ? pk.at(c[0], c[1]) : null; }, id);
    if (at && at.node === id) self.push(id);
  }
  const bottomInterior = [...model.nodes].filter(([, c]) => Math.abs(c[2] + 0.5) < 1e-6 && Math.abs(c[1]) < 1e-6 && c[0] > 1 && c[0] < 9).map(([id]) => id);
  const topInterior = [...model.nodes].filter(([, c]) => Math.abs(c[2] - 0.5) < 1e-6 && Math.abs(c[1]) < 1e-6 && c[0] > 1 && c[0] < 9).map(([id]) => id);
  check(bottomInterior.length > 3 && bottomInterior.every((id) => !self.includes(id)), 'nodes on the hidden bottom face are not pickable (' + bottomInterior.length + ' tested)');
  check(topInterior.length > 3 && topInterior.every((id) => self.includes(id)), 'nodes on the visible top face are picked as themselves (' + topInterior.length + ' tested)');
  check(self.length > 60 && self.length < model.nodes.size, 'visible nodes: ' + self.length + ' of ' + model.nodes.size);

  // 3. Real mouse: click selects, ctrl+click toggles, empty space clears.
  const clickNode = async (id, mods) => {
    const c = await P('project', id);
    if (mods) await page.keyboard.down('Control');
    await page.mouse.click(c[0], c[1]);
    if (mods) await page.keyboard.up('Control');
  };
  const [A, B] = topInterior;
  await clickNode(A);
  let s = await sel();
  check(s.mode === 'node' && s.nodes.length === 1 && s.nodes[0] === A, 'click selects node ' + A + ' -> ' + JSON.stringify(s.nodes));
  await clickNode(B, true);
  s = await sel();
  check(s.nodes.length === 2 && s.nodes.includes(A) && s.nodes.includes(B), 'ctrl+click adds a node');
  await clickNode(A, true);
  s = await sel();
  check(s.nodes.length === 1 && s.nodes[0] === B, 'ctrl+click on a selected node removes it');
  await shot('pick_node.png');
  await page.mouse.click(canvasBox.x + 6, canvasBox.y + 6);
  check((await sel()).nodes.length === 0, 'click on empty space clears the selection');
  // The selected node is drawn on top: pixels around it differ from a render without selection.
  await clickNode(A);
  const marker = await page.evaluate(async (nid) => {
    const pk = window.OpenModelViewer.pick, c = pk.project(nid), cv = document.getElementById('gl'), r = cv.getBoundingClientRect(), k = cv.width / r.width;
    window.OpenModelViewer.set({});
    const g = cv.getContext('webgl2'), x = Math.round((c[0] - r.left) * k), y = cv.height - Math.round((c[1] - r.top) * k);
    const px = new Uint8Array(4); g.readPixels(x, y, 1, 1, g.RGBA, g.UNSIGNED_BYTE, px);
    return Array.from(px);
  }, A);
  check(marker[0] > 200 && marker[1] > 80 && marker[1] < 160 && marker[2] < 60, 'the selected node is drawn as an orange marker: rgb(' + marker.slice(0, 3) + ')');

  // 4. Rectangle on nodes: the whole canvas must give exactly the nodes that are visible one by one.
  await P('clear');
  const rectNodes = async (alt) => {
    if (alt) await page.keyboard.down('Alt'); else await P('setBox', true);
    await page.mouse.move(canvasBox.x + 3, canvasBox.y + 3);
    await page.mouse.down();
    await page.mouse.move(canvasBox.x + canvasBox.w / 2, canvasBox.y + canvasBox.h / 2, { steps: 4 });
    await page.mouse.move(canvasBox.x + canvasBox.w - 3, canvasBox.y + canvasBox.h - 3, { steps: 4 });
    await page.mouse.up();
    if (alt) await page.keyboard.up('Alt'); else await P('setBox', false);
    return (await sel()).nodes;
  };
  let rn = await rectNodes(false);
  const same = (a, b) => a.length === b.length && a.every((x) => b.includes(x));
  check(rn.length > 60 && same(rn.slice().sort(), self.slice().sort()) , 'rectangle over everything selects exactly the ' + self.length + ' visible nodes (got ' + rn.length + ')');
  check(!rn.some((id) => bottomInterior.includes(id)), 'the rectangle does not include hidden nodes');
  await P('clear');
  rn = await rectNodes(true);
  check(rn.length === self.length, 'Alt+drag also draws a rectangle (' + rn.length + ' nodes)');

  // 5. Elements: the element under the cursor must own the skin face (oracle: skin faces of the .inp).
  await P('setMode', 'element');
  const topFaces = model.skin.filter((f) => f.nodes.every((id) => Math.abs(model.nodes.get(id)[2] - 0.5) < 1e-6));
  check(topFaces.length === 80, 'oracle: 80 skin triangles on the top face (' + topFaces.length + ')');
  const face0 = topFaces[Math.floor(topFaces.length / 2)];
  const centre = async (nodes) => {
    const pts = [];
    for (const id of nodes) pts.push(await P('project', id));
    return [pts.reduce((a, p) => a + p[0], 0) / 3, pts.reduce((a, p) => a + p[1], 0) / 3];
  };
  const c0 = await centre(face0.nodes);
  await page.mouse.click(c0[0], c0[1]);
  s = await sel();
  check(s.mode === 'element' && s.elements.length === 1 && s.elements[0] === face0.element, 'click on a top triangle selects its element ' + face0.element + ' -> ' + JSON.stringify(s.elements));
  await shot('pick_element.png');
  // All top faces, each by its own click: the element of every one must be the oracle's.
  let wrong = 0;
  for (const f of topFaces.filter((_, i) => i % 7 === 0)) {
    const c = await centre(f.nodes);
    const at = await page.evaluate(([x, y]) => window.OpenModelViewer.pick.at(x, y), c);
    if (at.element !== f.element) wrong++;
  }
  check(wrong === 0, 'the element under 12 top triangles is always the one of the oracle (' + wrong + ' wrong)');
  // Highlight on pixels: the selected element's face is orange now.
  const px = await page.evaluate(([x, y]) => {
    window.OpenModelViewer.set({});
    const cv = document.getElementById('gl'), r = cv.getBoundingClientRect(), k = cv.width / r.width, g = cv.getContext('webgl2');
    const out = new Uint8Array(4); g.readPixels(Math.round((x - r.left) * k), cv.height - Math.round((y - r.top) * k), 1, 1, g.RGBA, g.UNSIGNED_BYTE, out);
    return Array.from(out);
  }, c0);
  check(px[0] > px[2] + 60, 'the selected element is highlighted: rgb(' + px.slice(0, 3) + ')');

  // 6. Faces.
  await P('setMode', 'face');
  await page.mouse.click(c0[0], c0[1]);
  s = await sel();
  check(s.faces.length === 1 && s.faces[0].element === face0.element && [...s.faces[0].nodes].sort((a, b) => a - b).join() === face0.nodes.join(),
        'click selects one face: element ' + (s.faces[0] && s.faces[0].element) + ', nodes ' + (s.faces[0] && s.faces[0].nodes));
  await P('setAngle', 1);
  await page.mouse.click(c0[0], c0[1]);
  s = await sel();
  const keyOf = (f) => [...f.nodes].sort((a, b) => a - b).join();
  const expectTop = new Set(topFaces.map((f) => f.nodes.join()));
  check(s.faces.length === 80, 'angle 1 deg: the whole top face (80 triangles) is selected (' + s.faces.length + ')');
  // each returned face is one of the oracle's top faces (3 corner ids of a triangle of the face, here exactly the face)
  check(s.faces.every((f) => expectTop.has(keyOf(f)) && model.skin.some((q) => q.element === f.element && q.nodes.join() === keyOf(f))), 'every selected face is an oracle top face with the right element');
  await shot('pick_face_angle.png');
  await P('setAngle', 100);
  await page.mouse.click(c0[0], c0[1]);
  s = await sel();
  check(s.faces.length === model.skin.length, 'angle 100 deg: all ' + model.skin.length + ' skin triangles (' + s.faces.length + ')');
  // Ctrl+click removes a face from the selection
  await P('setAngle', 0);
  await page.keyboard.down('Control'); await page.mouse.click(c0[0], c0[1]); await page.keyboard.up('Control');
  s = await sel();
  check(s.faces.length === model.skin.length - 1, 'ctrl+click removes one face (' + s.faces.length + ')');

  // 7. Set / get round trip with ids (as used to show the content of a group).
  const before = await sel();
  await P('clear');
  const r = await page.evaluate((x) => window.OpenModelViewer.pick.set(x), before);
  const after = await sel();
  check(r.missing === 0 && after.faces.length === before.faces.length, 'setSelection restores ' + after.faces.length + ' faces, ' + r.missing + ' missing');
  const rr = await page.evaluate(() => window.OpenModelViewer.pick.set({ mode: 'node', nodes: [99999999, 1] }).missing).catch(() => -2);
  check(rr === 1, 'an unknown node id is reported as missing (' + rr + ')');

  // 8. Deformation and section: what is picked is what is drawn.
  await P('setMode', 'node');
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: true, factor: 5 * window.OpenModelViewer.state.defFactor } }));
  const moved = await page.evaluate((nid) => { const pk = window.OpenModelViewer.pick; const c = pk.project(nid); return pk.at(c[0], c[1]).node === nid; }, A);
  check(moved, 'with a strong deformation the node is picked at its drawn (deformed) position');
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false }, clip: true, clipAxis: 0, clipPos: 0.5 }));
  const clipped = await page.evaluate(() => {
    const m = window.OpenModelViewer.state.model, pk = window.OpenModelViewer.pick, o = m.meta.origin;
    const out = { hiddenPicked: 0, hidden: 0, keptPicked: 0, kept: 0 };
    for (let v = 0; v < m.vertexNodeId.length; v++) {
      const x = m.positions[3 * v] + o[0], id = m.vertexNodeId[v];
      const c = pk.project(id); if (!c) continue;
      const at = pk.at(c[0], c[1]);
      if (x > 5.2) { out.hidden++; if (at.node === id) out.hiddenPicked++; } else if (x < 4.8 && Math.abs(m.positions[3 * v + 2] + o[2] - 0.5) < 1e-6) { out.kept++; if (at.node === id) out.keptPicked++; }
    }
    return out;
  });
  check(clipped.hidden > 10 && clipped.hiddenPicked === 0, 'nodes cut away by the section plane are not pickable (' + clipped.hidden + ' tested, ' + clipped.hiddenPicked + ' picked)');
  check(clipped.kept > 10 && clipped.keptPicked >= clipped.kept * 0.8, 'visible top nodes stay pickable with the section (' + clipped.keptPicked + '/' + clipped.kept + ')');

  // 9. The visible render still works after picking (the pick pass must not leave the offscreen buffer bound).
  const pix = await page.evaluate(() => {
    window.OpenModelViewer.set({ clip: false });
    const cv = document.getElementById('gl'), g = cv.getContext('webgl2'), w = cv.width, h = cv.height, px = new Uint8Array(w * h * 4);
    g.readPixels(0, 0, w, h, g.RGBA, g.UNSIGNED_BYTE, px);
    const bg = [px[0], px[1], px[2]]; let covered = 0;
    for (let i = 0; i < w * h; i++) if (Math.abs(px[4 * i] - bg[0]) + Math.abs(px[4 * i + 1] - bg[1]) + Math.abs(px[4 * i + 2] - bg[2]) > 12) covered++;
    return { covered, total: w * h, err: g.getError() };
  });
  check(pix.covered > 2000 && pix.err === 0, 'the normal render still draws after picking (' + pix.covered + ' px, GL error ' + pix.err + ')');

  // 10. Picking off: clicks do nothing and rotate as before.
  await P('setMode', 'off');
  await page.mouse.click(canvasBox.x + canvasBox.w / 2, canvasBox.y + canvasBox.h / 2);
  check((await sel()).nodes.length === 0 && !(await page.evaluate(() => document.getElementById('gl').style.cursor)), 'with the selection off, clicks select nothing');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems[0] : ''));
  console.log(failures ? failures + ' FAILED' : 'all passed');
  await browser.close();
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
