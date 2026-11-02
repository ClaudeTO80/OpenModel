// End-to-end test of the ribbon bar of the OpenModel Workbench GUI on the real WebUI runtime.
// Needs a freshly started openmodel-webui with a Chromium on CDP port 9222 (see tests/webgl/README.md); no CAD backend needed.
// Usage: node gui_ribbon_e2e.js <dir> [screenshot-dir]
//   <dir> holds the files written by tests/support/make_beam_results.py (beam.inp, beam.frd, beam_modal.frd, ...).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , dir, shotDir] = process.argv;
if (!dir) { console.error('usage: gui_ribbon_e2e.js <dir> [shots]'); process.exit(2); }
let failures = 0;
const check = (ok, text) => { console.log((ok ? 'PASS ' : 'FAIL ') + text); if (!ok) failures++; };

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
    await page.waitForFunction(() => document.querySelectorAll('aside button:not([disabled])').length > 3, null, { timeout: 60000 });
    await page.waitForTimeout(900);   // the ribbon synchronises every 700 ms
  };
  const rb = (act) => page.locator('#ribbon [data-act="' + act + '"]:visible').first();
  const tab = async (name) => { await page.click('#ribbon .tab[data-tab="' + name + '"]'); };
  const press = async (act) => { await rb(act).click(); await page.waitForTimeout(250); };
  const viewer = () => page.evaluate(async () => {
    const w = document.getElementById('viewer').contentWindow;
    for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model); i++) await new Promise((r) => setTimeout(r, 100));
    const V = w.OpenModelViewer, S = V && V.state;
    if (!S || !S.model) return null;
    return { mode: S.mode, wire: S.wire, clipOn: S.clipOn, clipAxis: S.clipAxis, defOn: S.defOn, animate: S.animate, modeIndex: S.modeIndex,
             modes: S.model.modes.length, fields: S.model.fields.length, field: S.fieldIndex, factor: S.defFactor,
             view: JSON.stringify(S.rot), dark: w.document.documentElement.dataset.theme || '' };
  });
  const state = (act) => page.evaluate((a) => { const b = document.querySelector('#ribbon [data-act="' + a + '"]'); return { disabled: b.disabled, on: b.classList.contains('on') }; }, act);
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };
  const choose = async (folder, file) => {
    await page.waitForSelector('#modal:not([hidden])');
    await page.waitForFunction(() => document.querySelectorAll('#entries li').length > 0, null, { timeout: 10000 });   // initial listing
    await page.fill('#dirPath', folder);
    await page.press('#dirPath', 'Enter');   // regression: the inline handler used to resolve `list` to input.list
    try {
      await page.waitForSelector('#entries li:text-is("' + file + '")', { timeout: 8000 });
    } catch (e) {
      console.log('entries: ' + (await page.$$eval('#entries li', (l) => l.map((x) => x.textContent).slice(0, 15).join(' | '))) + ' / dir=' + (await page.inputValue('#dirPath')));
      throw e;
    }
    await page.click('#entries li:text-is("' + file + '")');
  };

  // 1. Structure: seven tabs, File selected, each panel has buttons.
  const tabs = await page.$$eval('#ribbon .tab', (t) => t.map((x) => x.textContent));
  check(tabs.join(',') === 'File,Geometria,Mesh,Analisi,Risultati,Vista,Verifica', 'seven tabs: ' + tabs.join(', '));
  const panels = await page.$$eval('#ribbon .panel', (p) => p.map((x) => ({ name: x.dataset.panel, hidden: x.hidden, buttons: x.querySelectorAll('button').length })));
  check(panels.filter((p) => !p.hidden).length === 1 && panels.every((p) => p.buttons >= 1), 'exactly one panel visible, every panel has actions');
  const actions = await page.$$eval('#ribbon [data-act]', (b) => [...new Set(b.map((x) => x.dataset.act))]);
  const missing = await page.evaluate((list) => list.filter((a) => typeof RIBBON_ACTIONS === 'undefined' || !(a in RIBBON_ACTIONS || a in RIBBON_EXTRA)), actions);
  check(missing.length === 0, 'every ribbon button is wired to a handler (' + actions.length + ' distinct actions' + (missing.length ? '; missing ' + missing : '') + ')');

  // 2. Before any model: viewer-dependent buttons are disabled, opening ones are enabled.
  check((await state('mode-block')).disabled && (await state('save-mgmb')).disabled && !(await state('demo')).disabled && !(await state('cad')).disabled,
        'without a model, viewer and save actions are disabled; open actions are enabled');

  // 3. Demo model from the ribbon.
  await press('demo'); await waitIdle();
  check(!(await isBad()) && (await viewer()), 'ribbon "Modello demo" loads a model into the viewer');
  check(!(await state('mode-block')).disabled && !(await state('save-mgmb')).disabled, 'buttons are enabled once a model is shown');
  check((await state('mode-scalar')).disabled && (await state('deform')).disabled && (await state('mode-next')).disabled,
        'result-only buttons stay disabled (no fields, displacement or modes)');

  // 4. Mesh from file through the file dialog.
  await tab('file');
  await press('mesh');
  await choose(dir, 'beam.inp');
  await waitIdle();
  check(/Mesh caricata/.test(await message()), 'Apri mesh... opens the dialog and loads the picked file: "' + (await message()).split('\n')[0] + '"');
  check((await page.inputValue('#loadPath')).endsWith('beam.inp'), 'the picked path is shown in the side panel');
  await press('mesh');
  await page.waitForSelector('#modal:not([hidden])');
  await page.keyboard.press('Escape');
  check(await page.evaluate(() => document.getElementById('modal').hidden), 'Escape closes the dialog without loading');

  // 5. Results from the ribbon, step with modes.
  await tab('res');
  await press('res');
  await choose(dir, 'beam_modal.frd');
  await waitIdle();
  check(/5 modi di vibrare/.test(await message()), 'Apri risultati... loads the modal file: "' + (await message()).split('\n')[1] + '"');
  let v = await viewer();
  check(v.modes === 5 && v.fields > 0 && v.modeIndex === 0, 'viewer has 5 modes');
  check(!(await state('mode-next')).disabled && !(await state('deform')).disabled && !(await state('res-clear')).disabled, 'mode and deformation buttons are enabled');
  check((await page.textContent('#rbModeLabel')) === 'modo 1 / 5', 'mode label: ' + (await page.textContent('#rbModeLabel')));
  await press('mode-next'); await press('mode-next');
  v = await viewer();
  check(v.modeIndex === 2 && (await page.textContent('#rbModeLabel')) === 'modo 3 / 5', 'next mode twice selects mode 3');
  await press('mode-prev'); await press('mode-prev'); await press('mode-prev');
  v = await viewer();
  check(v.modeIndex === 4, 'previous wraps around to the last mode');
  await press('animate');
  v = await viewer();
  check(v.animate === true && (await state('animate')).on, 'Anima starts the animation and shows as pressed');
  await shot('ribbon_modes.png');
  await press('animate');
  check((await viewer()).animate === false && !(await state('animate')).on, 'Anima toggles off');
  const before = (await viewer()).factor;
  await press('def-more');
  const after = (await viewer()).factor;
  check(Math.abs(after / before - 2) < 1e-9, 'Scala x2 doubles the factor (' + before.toPrecision(4) + ' -> ' + after.toPrecision(4) + ')');
  await press('def-less'); await press('def-less');
  check(Math.abs((await viewer()).factor / before - 0.5) < 1e-9, 'Scala /2 halves it');
  await press('deform');
  check((await viewer()).defOn === false && !(await state('deform')).on, 'Deformata toggles off');
  await press('deform');
  check((await viewer()).defOn === true && (await state('deform')).on, 'Deformata toggles on');
  const options = await page.$$eval('#rbField option', (o) => o.map((x) => x.textContent));
  check(options.length > 0 && options[0] === '|u| (modo)', 'ribbon field list follows the viewer: ' + options.slice(0, 2).join(', '));

  // 6. Field choice from the ribbon.
  await page.selectOption('#rbField', '0');
  check((await viewer()).mode === 'scalar', 'choosing a field colours by it');

  // 7. View tab.
  await tab('view');
  const iso0 = (await viewer()).view;
  await press('view-x'); const vx = (await viewer()).view;
  await press('view-z'); const vz = (await viewer()).view;
  check(vx !== iso0 && vz !== vx, 'the orientation buttons change the camera');
  for (const [act, key, val] of [['mode-block', 'mode', 'block'], ['mode-flat', 'mode', 'flat'], ['mode-scalar', 'mode', 'scalar'],
                                  ['wire-none', 'wire', 'none'], ['wire-all', 'wire', 'all'], ['wire-skin', 'wire', 'skin']]) {
    await press(act);
    const st = await viewer();
    check(st[key] === val && (await state(act)).on, act + ' -> ' + key + ' = ' + val + ' and the button is pressed');
  }
  check(!(await state('mode-block')).on && !(await state('wire-all')).on, 'only the active choice of a group is pressed');
  await press('clip');
  check((await viewer()).clipOn === true && (await state('clip')).on, 'Sezione switches the section plane on');
  await press('axis-0');
  check((await viewer()).clipAxis === 0 && (await state('axis-0')).on && !(await state('axis-2')).on, 'axis buttons choose the plane normal');
  await press('clip-flip');
  check(await page.evaluate(() => document.getElementById('viewer').contentWindow.document.getElementById('clipFlip').checked), 'Inverti flips the cut side');
  await shot('ribbon_view.png');
  await press('clip');
  check((await viewer()).clipOn === false && !(await state('axis-0')).on, 'Sezione off');
  const theme0 = (await viewer()).dark;
  await press('theme');
  check((await viewer()).dark !== theme0, 'Tema viewer toggles the viewer theme');
  await press('fit');
  check(!(await isBad()), 'Inquadra runs');

  // 8. Verifica tab and results removal.
  await tab('check');
  await press('status'); await waitIdle();
  check(/beam_modal\.frd/.test(await message()) || /Risultati/.test(await message()), 'Riepilogo reports the loaded results');
  await press('validate'); await waitIdle();
  check(!(await isBad()), 'Valida mesh runs: "' + (await message()).split('\n')[0] + '"');
  await tab('res');
  await press('res-clear'); await waitIdle();
  v = await viewer();
  check(v.modes === 0 && (await state('mode-next')).disabled && (await state('res-clear')).disabled, 'Togli risultati removes them; modal buttons are disabled again');

  // 9. Export from the ribbon.
  await tab('file');
  await page.fill('#outBase', path.resolve(dir, 'ribbon_out'));
  await press('save-mgwv'); await waitIdle();
  check(fs.existsSync(path.resolve(dir, 'ribbon_out.mgwv')), 'MGWV export from the ribbon writes the file');
  await press('save-mgmb'); await waitIdle();
  check(fs.existsSync(path.resolve(dir, 'ribbon_out.mgmb')), 'MGMB export from the ribbon writes the file');
  await press('save-inp'); await waitIdle();
  check(fs.existsSync(path.resolve(dir, 'ribbon_out.inp')), 'INP export from the ribbon writes the file');

  // 10. Mesh tab shortcuts and keyboard navigation.
  await tab('mesh');
  await press('focus-mesh');
  check(await page.evaluate(() => document.getElementById('secMesh').classList.contains('flash')), 'Parametri... highlights the mesh section');
  await page.focus('#ribbon .tab[data-tab="mesh"]');
  await page.keyboard.press('ArrowRight');
  check((await page.getAttribute('#ribbon .tab[data-tab="solve"]', 'aria-selected')) === 'true', 'arrow keys move between tabs');
  await shot('ribbon_tabs.png');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  if (failures) { console.log(failures + ' check(s) failed'); process.exit(1); }
  console.log('all checks passed');
})().catch((e) => { console.error(e); process.exit(1); });
