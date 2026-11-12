// End-to-end test of picking and groups in the OpenModel Workbench on the real WebUI runtime: select faces/nodes in the viewer with the
// mouse, create groups, assign supports/loads (script lines), write the solver input and run a (fake) solver.
// Needs a freshly started openmodel-webui with a Chromium on CDP port 9222 (see README.md).
// Usage: node gui_pick_e2e.js <fixture-dir> <fake_ccx.py> [work-dir] [screenshot-dir]
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , dir, fake, workArg, shotDir] = process.argv;
if (!dir || !fake) { console.error('usage: gui_pick_e2e.js <fixture-dir> <fake_ccx.py> [work] [shots]'); process.exit(2); }
const work = path.resolve(workArg || path.join(dir, 'gui_pick'));
fs.rmSync(work, { recursive: true, force: true });
fs.mkdirSync(work, { recursive: true });
let failures = 0;
const check = (ok, text) => { console.log((ok ? 'PASS ' : 'FAIL ') + text); if (!ok) failures++; };

// Oracle from the .inp: nodes, tetrahedra (local node order as written) and the skin faces.
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
    faces.get(key).push({ element: t.id, nodes: ids, tet: t });
  }
  return { nodes, tets, skin: [...faces.values()].filter((v) => v.length === 1).map((v) => v[0]) };
}
// CalculiX tetrahedron faces (local nodes, 1-based): S1 1-2-3, S2 1-4-2, S3 2-4-3, S4 3-4-1.
function calculixFace(tet, faceNodes) {
  const defs = [[1, 2, 3], [1, 4, 2], [2, 4, 3], [3, 4, 1]];
  const want = faceNodes.slice().sort((a, b) => a - b).join();
  for (let i = 0; i < 4; i++) if (defs[i].map((k) => tet.n[k - 1]).sort((a, b) => a - b).join() === want) return i + 1;
  return 0;
}
function wrapper(name, env) {
  const p = path.join(work, name);
  fs.writeFileSync(p, '#!/bin/sh\n' + Object.entries({ FAKE_CCX_FIXTURES: path.resolve(dir), ...env }).map(([k, v]) => `export ${k}='${v}'`).join('\n') + `\nexec "${path.resolve(fake)}" "$@"\n`);
  fs.chmodSync(p, 0o755);
  return p;
}

(async () => {
  const model = parseInp(path.resolve(dir, 'beam.inp'));
  const browser = await chromium.connectOverCDP('http://127.0.0.1:9222');
  const page = browser.contexts()[0].pages().find((p) => p.url().startsWith('http://localhost'));
  if (!page) { console.error('GUI page not found'); process.exit(2); }
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  await page.waitForFunction(() => document.querySelectorAll('#mesher option').length > 0, null, { timeout: 15000 });
  const message = () => page.textContent('#result');
  const isBad = () => page.evaluate(() => document.getElementById('result').className === 'bad');
  const waitIdle = async () => {
    await page.waitForFunction(() => document.querySelectorAll('button:not([disabled])').length > 3, null, { timeout: 60000 });
    await page.waitForTimeout(400);
  };
  const click = async (text) => { await page.click('aside button:has-text("' + text + '")'); await waitIdle(); };
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };
  const V = (expr, arg) => page.evaluate(`(async (arg) => { const w = document.getElementById('viewer').contentWindow; for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model); i++) await new Promise((r) => setTimeout(r, 100)); await new Promise((r) => setTimeout(r, 300)); const V = w.OpenModelViewer; return (${expr}); })(${JSON.stringify(arg === undefined ? null : arg)})`);
  // Page coordinates of a node: the viewer reports coordinates inside its own window; the iframe adds its offset.
  const nodeXY = async (id) => {
    const c = await V('V.pick.project(arg)', id);
    const f = await page.evaluate(() => { const r = document.getElementById('viewer').getBoundingClientRect(); return [r.left, r.top]; });
    return [c[0] + f[0], c[1] + f[1]];
  };
  const centroid = async (nodes) => { const p = []; for (const n of nodes) p.push(await nodeXY(n)); return [p.reduce((a, q) => a + q[0], 0) / p.length, p.reduce((a, q) => a + q[1], 0) / p.length]; };
  const script = () => page.inputValue('#anScript');

  // 1. Mesh loaded: the selection controls come alive.
  check(await page.isDisabled('#selMode'), 'without a model the selection mode is disabled');
  await page.fill('#loadPath', path.resolve(dir, 'beam.inp'));
  await click('Load and display');
  check(/Mesh loaded/.test(await message()), 'beam.inp loads');
  await V('V.set({deform: {on: false}, wire: "none"})');
  check(await V('V.pick.available()'), 'the viewer offers picking for the loaded mesh');
  await page.waitForTimeout(900);
  check(await page.isEnabled('#selMode'), 'the selection mode becomes enabled');
  check(/No groups/.test(await page.textContent('#grpList')), 'the group list is empty');
  check(await page.isDisabled('#btnAsFix') && await page.isDisabled('aside #btnGrpNode'), 'assign and group buttons need a selection');

  // 2. Face selection with the mouse and the angle option, through the page controls.
  await page.selectOption('#selMode', 'face');
  await page.fill('#selAngle', '1'); await page.dispatchEvent('#selAngle', 'change');
  const topFaces = model.skin.filter((f) => f.nodes.every((id) => Math.abs(model.nodes.get(id)[2] - 0.5) < 1e-6));
  const face0 = topFaces[Math.floor(topFaces.length / 2)];
  const c0 = await centroid(face0.nodes);
  await page.mouse.click(c0[0], c0[1]);
  await page.waitForTimeout(900);
  check(/80 faces/.test(await page.textContent('#selInfo')), 'one click + angle 1 selects the whole top face: "' + (await page.textContent('#selInfo')) + '"');
  check(await page.isEnabled('#btnAsPress') && await page.isEnabled('#btnAsFix'), 'assign buttons are enabled once faces are selected');
  await shot('pick_gui_selection.png');

  // 3. Pressure on the selected faces: group, script line.
  await page.fill('#asPress', '2.5');
  await click('Pressure');
  check(!(await isBad()) && /Group 'PRESS1'/.test(await message()), 'pressure creates the face group PRESS1: "' + (await message()).split('\n')[0] + '"');
  check(/pressure group:PRESS1 p=2.5/.test(await script()), 'the script gets "pressure group:PRESS1 p=2.5"');
  check(/PRESS1.*80 faces/.test(await page.textContent('#grpList')), 'the group list shows PRESS1 with 80 faces');

  // 4. Support on the nodes of a face selection (named group), force on the end face by plain click.
  await page.fill('#grpName', 'Support');
  await page.fill('#asDof', '1,2');
  await click('Fix');
  check(!(await isBad()) && /Group 'Support' created: 63 nodes/.test(await message()), 'support: node group of the 63 nodes of the top face: "' + (await message()).split('\n')[0] + '"');
  check(/fix group:Appoggio dof=1,2/.test(await script()), 'the script gets "fix group:Appoggio dof=1,2"');
  const endFaces = model.skin.filter((f) => f.nodes.every((id) => Math.abs(model.nodes.get(id)[0] - 10) < 1e-6));
  await page.fill('#selAngle', '1'); await page.dispatchEvent('#selAngle', 'change');
  const ec = await centroid(endFaces[0].nodes);
  await page.mouse.click(ec[0], ec[1]);
  await page.waitForTimeout(900);
  check(/8 faces/.test(await page.textContent('#selInfo')), 'click on the end face with angle 1 selects its 8 triangles: "' + (await page.textContent('#selInfo')) + '"');
  await page.fill('#asForce', '0,-80,0');
  await click('Force');
  check(/force group:FORCE1 F=0,-80,0/.test(await script()) && /Group 'FORCE1' created: 9 nodes/.test(await message()), 'force: 9-node group on the end face and its script line: "' + (await message()).split('\n')[0] + '" / ' + (await script()).split('\n').filter((l) => /force/.test(l)).join(' | '));

  // 5. Input validation in the page and in the group rules.
  await page.fill('#asForce', '0,abc,0');
  await click('Force');
  check((await isBad()) && /tre numeri/.test(await message()), 'a malformed force is refused in the page');
  await page.fill('#asDof', '1,5');
  await click('Fix');
  check((await isBad()) && /degrees of freedom/.test(await message()), 'a bad dof list is refused');
  await page.fill('#asDof', '1,2,3'); await page.fill('#grpName', 'Support');
  await click('Node group');
  check((await isBad()) && /already/.test(await message()) || /already exists/.test(await message()), 'a duplicate group name is refused: "' + (await message()).slice(0, 70) + '"');
  await page.fill('#grpName', 'bad name');
  await click('Node group');
  check((await isBad()) && /Invalid group name/.test(await message()), 'an invalid group name is refused');
  await page.fill('#grpName', '');

  // 6. Pressure needs faces: with a node selection it is refused without creating anything.
  await page.selectOption('#selMode', 'node');
  const nodeA = [...model.nodes].find(([, c]) => Math.abs(c[2] - 0.5) < 1e-6 && Math.abs(c[1]) < 1e-6 && Math.abs(c[0] - 4) < 1e-6)[0];
  const na = await nodeXY(nodeA);
  await page.mouse.click(na[0], na[1]);
  await page.waitForTimeout(900);
  check(/1 nodes/.test(await page.textContent('#selInfo')), 'a node is selected by clicking it: "' + (await page.textContent('#selInfo')) + '"');
  check(await page.isDisabled('#btnAsPress'), 'with only nodes selected the pressure button is disabled');
  const groupsBefore = (await page.$$('#grpList li')).length;
  // Ribbon: tab Analisi, group creation from the ribbon button.
  await page.click('#ribbon .tab[data-tab="solve"]');
  const pickOn = await page.evaluate(() => document.querySelector('#ribbon [data-act="pick-node"]').getAttribute('aria-pressed'));
  check(pickOn === 'true', 'the ribbon "Nodes" button shows the active mode');
  await page.click('#ribbon .rb[data-act="grp-make"]');
  await waitIdle();
  check(/Group 'NODES1' created: 1 nodes/.test(await message()) && (await page.$$('#grpList li')).length === groupsBefore + 1, 'the ribbon creates the node group NODES1 with one node');

  // 7. Show a group: the viewer selects its faces again.
  await page.selectOption('#selMode', 'off');
  await page.click('#grpList li:has-text("PRESS1") button');
  await waitIdle();
  const shown = await V('V.pick.get()');
  check(shown.mode === 'face' && shown.faces.length === 80, 'Mostra puts the 80 faces of PRESS1 back in the viewer selection (' + shown.faces.length + ')');
  await shot('pick_gui_group.png');
  await page.click('#grpList li:has-text("Appoggio") button');
  await waitIdle();
  const shownNodes = await V('V.pick.get()');
  check(shownNodes.mode === 'node' && shownNodes.nodes.length === 63, 'Mostra puts the 63 nodes of Appoggio in the viewer selection (' + shownNodes.nodes.length + ')');

  // 8. The generated input: groups from picks drive the solver input; checked against the oracle.
  await page.fill('#anJob', path.join(work, 'job'));
  await page.fill('#anSolver', wrapper('ccx_ok.sh', {}));
  await page.fill('#anScript', 'material E=210000 nu=0.3 rho=7.85e-9\n' + (await script()).split('\n').filter((l) => /^(fix|force|pressure) group:/.test(l)).join('\n') + '\nstep static\n');
  await click('Write input');
  const wm = await message();
  check(!(await isBad()) && /pressure on 80 faces/.test(wm) && /63 constrained nodes/.test(wm) && /force on 9 nodes/.test(wm), 'input written with 80 pressure faces, 63 supports, 9 loaded nodes: "' + wm.split('\n')[1] + '"');
  const inp = fs.readFileSync(path.join(work, 'job', 'job.inp'), 'utf8');
  const dload = inp.split('*DLOAD')[1].split('*')[0].trim().split('\n').map((l) => l.split(',').map((x) => x.trim()));
  const expect = new Map(topFaces.map((f) => [f.element, calculixFace(f.tet, f.nodes)]));
  check(dload.length === 80 && dload.every(([e, p, v]) => expect.get(+e) === +p.slice(1) && +v === 2.5 && /^P[1-4]$/.test(p)),
        'every *DLOAD line is a top face with its independently derived CalculiX face number (' + dload.length + ' lines)');
  check(/\*NSET, NSET=MG_FIX1/.test(inp) && !/TYPE=S3/.test(inp), 'the face elements created for the group are not written as structure');

  // 9. Run, results arrive, groups survive the reload of the view.
  await click('Run solver');
  await page.waitForFunction(() => /terminato/.test(document.getElementById('solverLog').textContent), null, { timeout: 30000 });
  await waitIdle();
  check(/Analysis completed/.test(await message()), 'the run completes with the groups from the picks');
  check(/PRESS1/.test(await page.textContent('#grpList')) && /Appoggio/.test(await page.textContent('#grpList')), 'the groups are still listed after the results reload the view');
  check(await V('V.pick.available()') && (await page.inputValue('#selMode')) === 'off', 'the reloaded viewer starts with the selection off');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems[0] : ''));
  console.log(failures ? failures + ' FAILED' : 'all passed');
  await browser.close();
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
