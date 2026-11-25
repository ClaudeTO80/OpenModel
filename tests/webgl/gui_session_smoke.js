/**
 * @brief GUI test of the session: the Settings dialog (solver path, jobs folder with the folder chooser, legend
 * palette), the palette applied to the viewer, and New model (confirmation, closing on the server, emptied viewer).
 *
 * Usage: node gui_session_smoke.js <gui.html> <viewer.html> <results.mgwv>
 * A small HTTP server serves the three files, because the GUI reads the viewer frame, which a file:// page cannot do.
 * The WebUI bridge is a stub that records the calls; the server side is not needed.
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const http = require('http');

const [, , guiPath, viewerPath, mgwvPath] = process.argv;
if (!guiPath || !viewerPath || !mgwvPath) {
  console.error('usage: gui_session_smoke.js <gui.html> <viewer.html> <results.mgwv>');
  process.exit(2);
}

let failures = 0;
/** @brief Logs one check as PASS or FAIL and counts the failures. @param ok result of the check @param message description */
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

const files = { '/gui.html': [guiPath, 'text/html'], '/viewer.html': [viewerPath, 'text/html'], '/model.mgwv': [mgwvPath, 'application/octet-stream'] };
const server = http.createServer((req, res) => {
  const entry = files[req.url.split('?')[0]];
  if (!entry) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': entry[1] });
  fs.createReadStream(entry[0]).pipe(res);
});

// Stub bridge: every call is recorded in window.__calls. Only the answers the session needs are given.
const bridge = () => {
  window.__calls = [];
  const reply = (o) => JSON.stringify(o);
  const settings = { solverPath: '/opt/ccx', jobDir: '/work/jobs', legendPalette: 'Rosso' };
  window.webui = {
    isConnected: () => true,
    call: async (name, ...args) => {
      window.__calls.push({ name, args });
      switch (name) {
        case 'openmodel_defaults':
          return reply({ ok: true, message: '', version: '0.1.5', cwd: '/work', outBase: '/work/openmodel-model', meshers: [] });
        case 'openmodel_settings_get':
          return reply({ ok: true, message: '', path: '/cfg/settings.json', settings });
        case 'openmodel_settings_set': {
          const next = JSON.parse(args[0]);
          if (next.jobDir === '/nope') return reply({ ok: false, message: 'Settings not saved: the jobs folder cannot be used: /nope', path: '/cfg/settings.json', settings });
          return reply({ ok: true, message: 'Settings saved.', path: '/cfg/settings.json', settings: next });
        }
        case 'openmodel_list_dir':
          return reply({ ok: true, message: '', path: args[0] || '/work', parent: '/', sep: '/', entries: [{ name: 'sub', dir: true }] });
        case 'openmodel_close_model':
          return reply({ ok: true, message: 'Model closed.', view: 7, hasView: false, viewLabel: '' });
        default:
          return reply({ ok: true, message: '', groups: [], constraints: [], components: [], runs: [], cadGroups: [], meshGroups: [] });
      }
    },
  };
};

(async () => {
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + server.address().port;
  const browser = await chromium.launch({ args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const problems = [];
  try {
    const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
    await context.addInitScript(bridge);
    const page = await context.newPage();
    page.on('pageerror', (e) => problems.push(String(e)));
    let dialogAnswer = 'accept';
    page.on('dialog', (d) => (dialogAnswer === 'accept' ? d.accept() : d.dismiss()));
    await page.goto(base + '/gui.html');
    await page.waitForFunction(() => document.getElementById('viewer').contentWindow && document.getElementById('viewer').contentWindow.OpenModelViewer);
    await page.waitForTimeout(400);
    const calls = (name) => page.evaluate((n) => window.__calls.filter((c) => c.name === n), name);

    // 1. Settings at start: the Analysis placeholders show them and the viewer uses the saved palette.
    check((await page.getAttribute('#anSolver', 'placeholder')) === '/opt/ccx', 'the solver path of the settings is the Executable placeholder');
    check((await page.getAttribute('#anJob', 'placeholder')) === '/work/jobs/openmodel_job/job.inp', 'the jobs folder of the settings is the job placeholder');
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.palette())) === 'Rosso', 'the viewer starts with the saved palette');

    // 1b. The left panel folds away and comes back; the choice is kept between sessions.
    check(await page.isVisible('aside'), 'the left panel is shown at start');
    await page.click('#sideToggle');
    check(!(await page.isVisible('aside')), 'the toggle folds the left panel away');
    check((await page.getAttribute('#sideToggle', 'aria-expanded')) === 'false', 'the toggle reports the folded state');
    await page.reload();
    await page.waitForFunction(() => document.getElementById('viewer').contentWindow && document.getElementById('viewer').contentWindow.OpenModelViewer);
    await page.waitForTimeout(400);
    check(!(await page.isVisible('aside')), 'the folded choice is kept after reload');
    await page.click('#sideToggle');
    check(await page.isVisible('aside'), 'the left panel comes back');

    // 2. Settings dialog: palettes come from the viewer; saving sends the three values and applies them.
    await page.click('#ribbon [data-act="settings"]');
    check(await page.isVisible('#settingsModal'), 'Settings opens its dialog');
    const options = await page.$$eval('#setPalette option', (os) => os.map((o) => o.value));
    check(JSON.stringify(options) === JSON.stringify(['Viridis', 'Rosso', 'Ocra', 'Blu', 'Arcobaleno']), 'the palette list has the five viewer palettes');
    check((await page.inputValue('#setPalette')) === 'Rosso', 'the dialog shows the current palette');
    check((await page.inputValue('#setSolver')) === '/opt/ccx' && (await page.inputValue('#setJobDir')) === '/work/jobs', 'the dialog shows the current solver path and jobs folder');

    // The folder chooser picks the jobs folder: its Use this folder button is shown, and it closes the chooser.
    const browseButtons = await page.$$('#settingsModal button:has-text("Browse…")');
    await browseButtons[1].click();
    check(await page.isVisible('#modal'), 'Browse opens the file chooser for the jobs folder');
    check(await page.isVisible('#useFolder'), 'the chooser offers Use this folder for a folder');
    await page.click('#useFolder');
    check(!(await page.isVisible('#modal')), 'Use this folder closes the chooser');
    check((await page.inputValue('#setJobDir')) === '/work/jobs', 'the chosen folder is written into the jobs folder field');

    await page.fill('#setSolver', '/opt/ccx2');
    await page.selectOption('#setPalette', 'Blu');
    await page.click('#settingsModal button:has-text("Save")');
    await page.waitForTimeout(300);
    const saved = await calls('openmodel_settings_set');
    const sent = saved.length ? JSON.parse(saved[saved.length - 1].args[0]) : {};
    check(sent.solverPath === '/opt/ccx2' && sent.jobDir === '/work/jobs' && sent.legendPalette === 'Blu', 'Save sends the solver path, jobs folder and palette');
    check(!(await page.isVisible('#settingsModal')), 'a saved dialog closes');
    check((await page.getAttribute('#anSolver', 'placeholder')) === '/opt/ccx2', 'the saved solver path is shown at once');
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.palette())) === 'Blu', 'the saved palette is applied to the viewer at once');

    // A refused save keeps the dialog open and shows the reason.
    await page.click('#ribbon [data-act="settings"]');
    await page.fill('#setJobDir', '/nope');
    await page.click('#settingsModal button:has-text("Save")');
    await page.waitForTimeout(300);
    check(await page.isVisible('#settingsModal'), 'a refused save keeps the dialog open');
    check((await page.textContent('#setNote')).includes('cannot be used'), 'a refused save shows the reason');
    await page.click('#settingsModal button:has-text("Cancel")');

    // 3. New model. First a model is loaded into the viewer.
    const b64 = fs.readFileSync(mgwvPath).toString('base64');
    await page.evaluate((b64) => {
      const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      document.getElementById('viewer').contentWindow.OpenModelViewer.load(bytes.buffer, 'beam.mgwv');
    }, b64);
    await page.waitForTimeout(300);
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.state.model !== null)), 'a model is loaded in the viewer');
    // The Analysis script sits in the Analysis tab, which is not the current one: set its value directly.
    await page.evaluate(() => { document.getElementById('anScript').value = 'material E=210000 nu=0.3\nfix plane:x=0'; });

    // Declining the confirmation keeps the model.
    dialogAnswer = 'dismiss';
    await page.click('#ribbon [data-act="new-model"]');
    await page.waitForTimeout(300);
    check((await calls('openmodel_close_model')).length === 0, 'declining the confirmation does not close the model');
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.state.model !== null)), 'the model stays after declining');

    // Accepting closes the model on the server and empties the viewer and the Analysis script.
    dialogAnswer = 'accept';
    await page.click('#ribbon [data-act="new-model"]');
    await page.waitForTimeout(500);
    check((await calls('openmodel_close_model')).length === 1, 'accepting asks the server to close the model');
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.state.model)) === null, 'the viewer is emptied');
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.document.getElementById('drop').hidden)) === false, 'the empty-state message is shown');
    check((await page.inputValue('#anScript')) === '', 'the Analysis script is emptied with the model');
    check((await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.palette())) === 'Blu', 'the palette survives the new model');

    check(problems.length === 0, 'no page errors (' + problems.length + ')');
    if (problems.length) console.log(problems.join('\n'));
  } finally {
    await browser.close();
    server.close();
  }
  console.log(failures === 0 ? 'gui_session_smoke: all checks passed' : 'gui_session_smoke: ' + failures + ' check(s) failed');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); server.close(); process.exit(1); });
