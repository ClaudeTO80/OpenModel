// End-to-end test of the "Risultati FEM" section of the OpenModel Workbench GUI on the real WebUI runtime.
// Needs openmodel-webui running with a Chromium reachable through CDP on port 9222 (see tests/webgl/README.md).
// No CAD backend is needed: the mesh is a plain .inp file.
// Usage: node gui_results_e2e.js <dir> [screenshot-dir]
//   <dir> holds the files written by tests/support/make_beam_results.py
//         (beam.inp, beam.frd, beam.csv, beam_other.frd, beam_modal.frd, beam_elem.csv); outputs are written there too.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , dir, shotDir] = process.argv;
if (!dir) { console.error('usage: gui_results_e2e.js <dir> [shots]'); process.exit(2); }
let failures = 0;
const check = (ok, text) => { console.log((ok ? 'PASS ' : 'FAIL ') + text); if (!ok) failures++; };
const file = (name) => path.resolve(dir, name);

(async () => {
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
    await page.waitForTimeout(500);
  };
  const click = async (text) => { await page.click('aside button:has-text("' + text + '")'); await waitIdle(); };
  const viewer = () => page.evaluate(async () => {
    const w = document.getElementById('viewer').contentWindow;
    for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model); i++) await new Promise((r) => setTimeout(r, 100));
    await new Promise((r) => setTimeout(r, 300));
    const V = w.OpenModelViewer, d = w.document, m = V && V.state.model;
    if (!m) return null;
    return {
      fields: m.fields.map((f) => f.name), mode: V.state.mode, hasDisp: !!m.disp,
      label: d.getElementById('resultsInfo').hidden ? '' : d.getElementById('resultsInfo').textContent,
      defHidden: d.getElementById('defSec').hidden, nodes: m.meta.counts.nodes,
    };
  });
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };
  const loadResults = async (name, step) => {
    await page.fill('#resPath', name ? file(name) : '');
    await page.fill('#resStep', step === undefined ? '' : String(step));
    await click('Load results');
  };

  // 1. No mesh yet: the results are refused with an explanation.
  await loadResults('beam.frd');
  check((await isBad()) && /mesh/.test(await message()), 'results are refused while there is no mesh: "' + (await message()).slice(0, 60) + '..."');

  // 2. Load the mesh the results belong to.
  await page.fill('#loadPath', file('beam.inp'));
  await click('Load and display');
  check(/Mesh loaded/.test(await message()), 'beam.inp loads');
  let v = await viewer();
  check(v && v.fields.length === 0 && !v.hasDisp && v.label === '', 'plain mesh: no fields, no deformation panel');

  // 3. Results, last step.
  await loadResults('beam.frd');
  const m3 = await message();
  check(!(await isBad()) && /Passo usato: 1/.test(m3) && /deformata da DISP/.test(m3), 'results load, last step is used: "' + m3.split('\n')[1] + '"');
  check(/Passi disponibili/.test(m3) && /0: time 1/.test(m3) && /1: time 2/.test(m3), 'the message lists the available steps');
  v = await viewer();
  check(v && v.fields.includes('DISP magnitude') && v.fields.includes('STRESS von Mises') && v.fields.length === 28, 'viewer receives 28 fields: DISP 4 + STRESS 12 + TOSTRAIN 12 (' + (v && v.fields.length) + ')');
  check(v && v.mode === 'scalar' && v.hasDisp && !v.defHidden, 'viewer opens in scalar mode with the deformation panel');
  check(v && /beam\.frd/.test(v.label) && /step 2/.test(v.label), 'viewer shows the results label: "' + (v && v.label) + '"');
  await shot('gui_results_last.png');

  // 4. Explicit step.
  await loadResults('beam.frd', 0);
  check(/Passo usato: 0/.test(await message()), 'step 0 can be selected');
  v = await viewer();
  check(v && /step 1/.test(v.label), 'viewer switches to step 0: "' + (v && v.label) + '"');

  // 5. Wrong step / wrong file: refused, and the view keeps the previous results.
  await loadResults('beam.frd', 9);
  check((await isBad()) && /does not exist/.test(await message()), 'a step that does not exist is refused');
  await loadResults('beam_other.frd');
  check((await isBad()) && /non corrispondono/.test(await message()) && /different mesh/.test(await message()), 'results of a different mesh are refused');
  v = await viewer();
  check(v && /step 1/.test(v.label) && v.hasDisp, 'after a refused load the previous results stay on screen');
  await loadResults('missing.frd');
  check(await isBad(), 'a missing file is reported');

  // 6. Export keeps the results in the MGWV file.
  await page.fill('#outBase', file('gui_out'));
  await loadResults('beam.frd');
  await click('Export MGWV');
  const mgwv = fs.readFileSync(file('gui_out.mgwv'));
  const jsonLength = mgwv.readUInt32LE(12), json = mgwv.subarray(24, 24 + jsonLength).toString('utf8');
  check(json.includes('"displacement":{"offset"') && json.includes('"results":{"label":"beam.frd') && json.includes('DISP magnitude'),
        'exported MGWV contains the fields, the displacement and the results label');

  // 7. Status and removal.
  await click('Stato');
  check(/Results: beam\.frd/.test(await message()), 'status reports the loaded results');
  await click('Remove');
  v = await viewer();
  check(v && v.fields.length === 0 && !v.hasDisp && v.defHidden && v.label === '', 'removing the results restores the plain view');

  // 8. CSV table.
  await loadResults('beam.csv');
  const m8 = await message();
  check(!(await isBad()) && /deformata da U/.test(m8), 'CSV results load with displacement U: "' + m8.split('\n')[1] + '"');
  v = await viewer();
  check(v && v.fields.includes('Temp') && v.fields.includes('U magnitude'), 'CSV fields reach the viewer (' + (v && v.fields.join(', ')) + ')');
  await shot('gui_results_csv.png');

  // 8b. Stress/strain invariants of a static file.
  await loadResults('beam.frd');
  v = await viewer();
  check(v && ['STRESS principal 1', 'STRESS principal 3', 'STRESS Tresca', 'TOSTRAIN equivalent', 'TOSTRAIN principal 1'].every((n) => v.fields.includes(n)),
        'stress and strain invariants reach the viewer');

  // 8c. Vibration modes: all modes are bound, frequencies are listed, the mode selector appears.
  await loadResults('beam_modal.frd');
  const mm = await message();
  check(!(await isBad()) && /5 vibration modes/.test(mm) && /1 = 8355/.test(mm) && /5 = 4749/.test(mm), 'modal file loads with its frequencies: "' + mm.split('\n')[1] + '"');
  const modal = await page.evaluate(async () => {
    const w = document.getElementById('viewer').contentWindow;
    for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model && w.OpenModelViewer.state.model.modes.length); i++) await new Promise((r) => setTimeout(r, 100));
    const S = w.OpenModelViewer.state, d = w.document;
    return { modes: S.model.modes.length, boxHidden: d.getElementById('modeBox').hidden, first: S.model.fields[0].name };
  });
  check(modal.modes === 5 && !modal.boxHidden && modal.first === '|u| (mode)', 'the viewer shows the mode selector with 5 modes');
  await shot('gui_modal.png');
  await page.fill('#outBase', file('gui_modal_out'));
  await click('Export MGWV');
  const modalImage = fs.readFileSync(file('gui_modal_out.mgwv'));
  const modalJson = modalImage.subarray(24, 24 + modalImage.readUInt32LE(12)).toString('utf8');
  check(modalJson.includes('"modes":[{"label":"mode 1: f = 8355.17"') && !modalJson.includes('"displacement":{"offset"'), 'exported MGWV carries the modes');
  // A single mode can be chosen with the step field: a plain static-like view without selector.
  await loadResults('beam_modal.frd', 2);
  const m2 = await message();
  check(!(await isBad()) && /Passo usato: 2 \(mode 3/.test(m2), 'a single mode can be selected as a step: "' + m2.split('\n')[1] + '"');

  // 8d. Element table.
  await loadResults('beam_elem.csv');
  const me = await message();
  check(!(await isBad()) && /13 campi per elemento/.test(me), 'element table loads: "' + me.split('\n').slice(1, 3).join(' / ') + '"');
  v = await viewer();
  check(v && v.fields.includes('stress von Mises') && v.fields.includes('part'), 'element fields reach the viewer');
  await shot('gui_elements.png');

  // 8e. CalculiX text output merged with the .frd of the same run (';' separates the files).
  await page.fill('#resPath', file('beam.frd') + '; ' + file('beam.dat'));
  await page.fill('#resStep', '');
  await click('Load results');
  const md = await message();
  check(!(await isBad()) && /beam\.frd \+ beam\.dat/.test(md) && /12 campi per elemento/.test(md) && /28 campi nodali/.test(md), 'frd + dat are merged: "' + md.split('\n').slice(1, 3).join(' / ') + '"');
  v = await viewer();
  check(v && v.fields.includes('DISP magnitude') && v.fields.includes('STRESS von Mises') && v.fields.includes('STRESS principal 1'), 'nodal and element fields from both files reach the viewer');
  await page.fill('#resPath', file('beam_modal.dat'));
  await click('Load results');
  check((await isBad()) && /eigenvalue table/.test(await message()), 'an eigenvalue-only .dat cannot be painted and is refused: "' + (await message()).slice(0, 90) + '"');
  v = await viewer();
  check(v && v.fields.includes('DISP magnitude'), 'the previous results stay after that refusal');

  // 9. Replacing the mesh drops the results (they belong to the old node numbering).
  await page.fill('#loadPath', file('beam.inp'));
  await click('Load and display');
  v = await viewer();
  check(v && v.fields.length === 0 && !v.hasDisp && v.label === '', 'loading another mesh clears the attached results');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  if (failures) { console.log(failures + ' check(s) failed'); process.exit(1); }
  console.log('all checks passed');
})().catch((e) => { console.error(e); process.exit(1); });
