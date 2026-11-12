// End-to-end test of the "Analisi e solver" section and ribbon tab of the OpenModel Workbench on the real WebUI runtime.
// A stand-in for CalculiX (tests/support/fake_ccx.py) is used: this checks the plumbing (script -> .inp -> process ->
// log -> results in the viewer), not the solver physics.
// Needs a freshly started openmodel-webui with a Chromium on CDP port 9222 (see README.md).
// Usage: node gui_solver_e2e.js <fixture-dir> <fake_ccx.py> [work-dir] [screenshot-dir]
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , dir, fake, workArg, shotDir] = process.argv;
if (!dir || !fake) { console.error('usage: gui_solver_e2e.js <fixture-dir> <fake_ccx.py> [work] [shots]'); process.exit(2); }
const work = path.resolve(workArg || path.join(dir, 'gui_solver'));
fs.rmSync(work, { recursive: true, force: true });
fs.mkdirSync(work, { recursive: true });
let failures = 0;
const check = (ok, text) => { console.log((ok ? 'PASS ' : 'FAIL ') + text); if (!ok) failures++; };

function wrapper(name, env) {
  const p = path.join(work, name);
  fs.writeFileSync(p, '#!/bin/sh\n' + Object.entries({ FAKE_CCX_FIXTURES: path.resolve(dir), ...env }).map(([k, v]) => `export ${k}='${v}'`).join('\n') +
    `\nexec "${path.resolve(fake)}" "$@"\n`);
  fs.chmodSync(p, 0o755);
  return p;
}

(async () => {
  const browser = await chromium.connectOverCDP('http://127.0.0.1:9222');
  const page = browser.contexts()[0].pages().find((p) => p.url().startsWith('http://localhost'));
  if (!page) { console.error('GUI page not found'); process.exit(2); }
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  await page.waitForFunction(() => document.querySelectorAll('#mesher option').length > 0, null, { timeout: 15000 });
  const message = () => page.textContent('#result');
  const isBad = () => page.evaluate(() => document.getElementById('result').className === 'bad');
  const log = () => page.textContent('#solverLog');
  const waitIdle = async () => {
    await page.waitForFunction(() => document.querySelectorAll('button:not([disabled])').length > 3, null, { timeout: 60000 });
    await page.waitForTimeout(400);
  };
  const click = async (text) => { await page.click('aside button:has-text("' + text + '")'); await waitIdle(); };
  const waitLog = (re, ms = 30000) => page.waitForFunction((s) => new RegExp(s).test(document.getElementById('solverLog').textContent), re, { timeout: ms });
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };
  const viewerFields = () => page.evaluate(async () => {
    const w = document.getElementById('viewer').contentWindow;
    for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model); i++) await new Promise((r) => setTimeout(r, 100));
    await new Promise((r) => setTimeout(r, 300));
    const m = w.OpenModelViewer && w.OpenModelViewer.state.model;
    return m ? { fields: m.fields.length, modes: m.modes ? m.modes.length : 0, disp: !!m.disp } : null;
  });
  const script = '#e2e\nmaterial E=210000 nu=0.3 rho=7.85e-9\nfix plane:x=0\nforce plane:x=10 F=0,-100,0\nstep static\n';
  const setScript = (t) => page.fill('#anScript', t);

  // 1. Example script present at start; no mesh yet -> refusal.
  check((await page.inputValue('#anScript')).includes('material E='), 'the script box starts with the example');
  await setScript(script);
  await page.fill('#anJob', path.join(work, 'job1', 'job.inp'));
  check(await page.isDisabled('aside #btnWrite') && await page.isDisabled('aside #btnRun') && await page.isDisabled('aside #btnCancel'),
        'without a mesh Write, Run and Cancel are disabled');
  const noMesh = await page.evaluate(() => document.querySelector('#ribbon .rb[data-act="an-run"]').disabled);
  check(noMesh, 'the ribbon Run solver button is disabled too');

  // 2. Mesh, then write the input.
  await page.fill('#loadPath', path.resolve(dir, 'beam.inp'));
  await click('Load and display');
  check(/Mesh loaded/.test(await message()), 'beam.inp loads');
  await click('Write input');
  const wm = await message();
  check(!(await isBad()) && /Input del solutore scritto/.test(wm) && /480 volume elements/.test(wm) && /9 constrained nodes/.test(wm), 'input written: "' + wm.split('\n')[1] + '"');
  const inp = fs.readFileSync(path.join(work, 'job1', 'job.inp'), 'utf8');
  check(/\*STATIC/.test(inp) && /MG_LOAD1, 2, -11\.1111/.test(inp) && /\*BOUNDARY/.test(inp), 'job.inp holds the static step, the load and the boundary conditions');

  // 3. Errors reported with line numbers; nothing is written.
  await setScript('material E=210000 nu=0.3\nfix plane:q=0\nforce all F=1,2\n');
  await click('Write input');
  const em = await message();
  check((await isBad()) && /riga 2/.test(em) && /riga 3/.test(em), 'script errors carry line numbers: "' + em.split('\n').slice(1, 3).join(' | ') + '"');
  await setScript('material E=210000 nu=0.3\nfix plane:x=99\n');
  await click('Write input');
  check((await isBad()) && /No node lies on the plane x = 99/.test(await message()), 'an empty selection is reported with the model range');

  // 4. Successful run: log, results loaded automatically.
  await setScript(script);
  await page.fill('#anJob', path.join(work, 'job2'));
  await page.fill('#anSolver', wrapper('ccx_ok.sh', {}));
  await click('Run solver');
  await waitLog('terminato');
  await waitIdle();
  const ok = await message();
  check(!(await isBad()) && /Analysis completed/.test(ok) && /Results loaded from job\.frd \+ job\.dat/.test(ok), 'run finishes and loads job.frd + job.dat: "' + ok.split('\n')[0] + '"');
  check(/CalculiX Version fake/.test(await log()) && /Job finished/.test(await log()), 'the solver log is shown');
  const v = await viewerFields();
  check(v && v.fields >= 20 && v.disp, 'viewer shows the results (' + (v && v.fields) + ' fields, deformation: ' + (v && v.disp) + ')');
  check(await page.isEnabled('aside #btnRun') && await page.isDisabled('aside #btnCancel'), 'Run enabled and Cancel disabled once finished');
  await shot('solver_done.png');

  // 5. Frequency run -> modes.
  await setScript('material E=210000 nu=0.3 rho=7.85e-9\nfix plane:x=0\nstep frequency modes=5\n');
  await click('Run solver');
  await waitLog('terminato');
  await waitIdle();
  const fm = await message();
  check(/vibrare/.test(fm) && /1 = 8355/.test(fm), 'frequency run loads the modes: "' + (fm.split('\n')[1] || '').slice(0, 60) + '"');
  const v2 = await viewerFields();
  check(v2 && v2.modes === 5, 'viewer has 5 modes (' + (v2 && v2.modes) + ')');

  // 6. Cancel a running job.
  await setScript(script);
  await page.fill('#anSolver', wrapper('ccx_slow.sh', { FAKE_CCX_SLEEP: '60', FAKE_CCX_IGNORE_TERM: '1' }));
  await click('Run solver');
  await waitLog('increment 2');
  check(await page.isDisabled('aside #btnRun') && await page.isEnabled('aside #btnCancel'), 'while running: Run disabled, Cancel enabled');
  const ribbonState = await page.evaluate(() => ({ run: document.querySelector('#ribbon [data-act="an-run"]').disabled, cancel: document.querySelector('#ribbon [data-act="an-cancel"]').disabled }));
  check(ribbonState.run && !ribbonState.cancel, 'ribbon follows: Run disabled, Cancel enabled');
  const t0 = Date.now();
  await page.click('aside #btnCancel');
  await waitLog('annullato', 20000);
  const took = (Date.now() - t0) / 1000;
  check(/Analisi annullata/.test(await message()) && took < 10, 'cancel stops a solver that ignores SIGTERM in ' + took.toFixed(1) + ' s');
  check(await page.isEnabled('aside #btnRun') && await page.isDisabled('aside #btnCancel'), 'buttons restored after cancel');

  // 7. Solver failure and missing executable.
  await page.fill('#anSolver', wrapper('ccx_fail.sh', { FAKE_CCX_FAIL: '1' }));
  await click('Run solver');
  await waitLog('errore');
  const fl = await message();
  check((await isBad()) && /codice 1/.test(fl) && /nonpositive jacobian/.test(await log()), 'a failing solver is reported with exit code and log');
  await page.fill('#anSolver', path.join(work, 'no-such-ccx'));
  await click('Run solver');
  check((await isBad()) && /Avvio del solutore fallito/.test(await message()) && /Cannot start/.test(await message()), 'a missing executable is reported');
  await page.fill('#anSolver', wrapper('ccx_nofrd.sh', { FAKE_CCX_NOFRD: '1' }));
  await click('Run solver');
  await page.waitForFunction(() => /non ha scritto/.test(document.getElementById('result').textContent), null, { timeout: 30000 });
  await waitIdle();
  check((await isBad()) && /non ha scritto job\.frd/.test(await message()), 'a run without result file is reported');

  // 8. Ribbon tab.
  await page.click('#ribbon .tab[data-tab="solve"]');
  const labels = await page.$$eval('#ribbon .panel[data-panel="solve"] .rb', (b) => b.map((x) => x.textContent.trim()));
  check(labels.length === 13, 'the Analisi tab has 13 buttons: ' + labels.join(', '));
  await page.fill('#anSolver', wrapper('ccx_ok.sh', {}));
  await page.click('#ribbon .rb[data-act="an-run"]');
  await waitLog('terminato');
  await waitIdle();
  check(/Analysis completed/.test(await message()), 'the ribbon button runs the analysis');
  await page.fill('#anScript', 'zzz');
  await page.click('#ribbon .rb[data-act="an-example"]');
  await page.waitForFunction(() => document.getElementById('anScript').value.includes('material E='));
  check((await page.inputValue('#anScript')).includes('material E='), 'the ribbon "Example" button restores the example script');
  await shot('solver_ribbon.png');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems[0] : ''));
  console.log(failures ? failures + ' FAILED' : 'all passed');
  await browser.close();
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
