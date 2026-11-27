/**
 * @brief GUI test of the 1D heat panel (Analysis tab, "Heat conduction 1D"): the template fills the script, a run shows
 * the time selector, the temperature plot and the CSV download, a script with errors lists them with their line
 * numbers, and the panel never calls the structural analysis handlers.
 *
 * Usage: node gui_heat1d_smoke.js <gui.html> <viewer.html>
 * A small HTTP server serves the two files, because the GUI reads the viewer frame, which a file:// page cannot do.
 * The WebUI bridge is a stub: openmodel_heat1d_run answers with a fixed result, or with errors when the script has the
 * word "bogus"; the real solver is covered by heat1d_script_test. Needs the `playwright` package (NODE_PATH must
 * resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const http = require('http');

const [, , guiPath, viewerPath] = process.argv;
if (!guiPath || !viewerPath) {
  console.error('usage: gui_heat1d_smoke.js <gui.html> <viewer.html>');
  process.exit(2);
}

let failures = 0;
/** @brief Logs one check as PASS or FAIL and counts the failures. @param ok result of the check @param message description */
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

const files = { '/gui.html': [guiPath, 'text/html'], '/viewer.html': [viewerPath, 'text/html'] };
const server = http.createServer((req, res) => {
  const entry = files[req.url.split('?')[0]];
  if (!entry) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': entry[1] });
  fs.createReadStream(entry[0]).pipe(res);
});

// Stub bridge: every call is recorded in window.__calls.
const bridge = () => {
  window.__calls = [];
  const reply = (o) => JSON.stringify(o);
  const heatOk = {
    ok: true, message: '3 nodes, 2 recorded times (transient)', errors: [], converged: true, maxPicardIterations: 1,
    nodes: [0, 0.1, 0.2], times: [0, 60],
    temperatures: [[100, 60, 20], [100, 55, 21]],
    csv: 'time,x,temperature\n0,0,100\n',
  };
  window.webui = {
    isConnected: () => true,
    call: async (name, ...args) => {
      window.__calls.push({ name, args });
      switch (name) {
        case 'openmodel_defaults':
          return reply({ ok: true, message: '', version: '0.1.5', cwd: '/work', outBase: '/work/openmodel-model', meshers: [] });
        case 'openmodel_settings_get':
          return reply({ ok: true, message: '', path: '/cfg/settings.json', settings: { solverPath: '', jobDir: '/work/jobs', legendPalette: 'Rosso' } });
        case 'openmodel_heat1d_run':
          if (String(args[0]).includes('bogus')) {
            return reply({ ok: false, message: 'line 3 (bogus): unknown command', errors: ['line 3 (bogus): unknown command'] });
          }
          return reply(heatOk);
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
    const context = await browser.newContext({ viewport: { width: 1400, height: 900 } });
    await context.addInitScript(bridge);
    const page = await context.newPage();
    page.on('pageerror', (e) => problems.push(String(e)));
    page.on('dialog', (d) => d.accept());
    await page.goto(base + '/gui.html');
    await page.waitForFunction(() => document.getElementById('viewer').contentWindow && document.getElementById('viewer').contentWindow.OpenModelViewer);
    await page.click('#ribbon .tab[data-tab="solve"]');
    await page.click('button[data-act="heat-focus"]');
    await page.waitForFunction(() => !document.getElementById('secHeat').closest('.spanel').hidden);
    const calls = (name) => page.evaluate((n) => window.__calls.filter((c) => c.name === n).length, name);

    // 1. The template fills the script and the run answers with the time selector, the plot and the CSV button.
    await page.click('button:has-text("Template")');
    check((await page.inputValue('#heatScript')).startsWith('analysis heat 1d'), 'the template fills the heat script');
    await page.click('button:has-text("Run heat analysis")');
    await page.waitForFunction(() => !document.getElementById('heatOut').hidden);
    check((await page.locator('#heatTime option').count()) === 2, 'the time selector lists the two recorded times');
    check((await page.inputValue('#heatTime')) === '1', 'the last recorded time is shown first');
    check((await page.locator('#heatPlot polyline').count()) === 1, 'the temperature plot is drawn');
    const points = await page.getAttribute('#heatPlot polyline', 'points');
    check(points.trim().split(' ').length === 3, 'the plot has one point per node (3)');
    check((await page.textContent('#heatSummary')).includes('3 nodes'), 'the summary reports the nodes and times');
    check((await page.textContent('#heatErrors')) === '', 'no error is shown for a converged run');
    await page.selectOption('#heatTime', '0');
    check((await page.getAttribute('#heatPlot polyline', 'points')) !== points, 'choosing another time redraws the plot');
    check((await page.locator('button:has-text("Download CSV")').count()) === 1, 'the CSV download button is there');

    // 1b. The primitive chain: a second primitive with a tube section, set in the script and sent to the backend.
    check((await page.locator('#heatPrims > div').count()) === 1, 'the chain starts with one primitive');
    await page.click('button:has-text("Add primitive")');
    check((await page.locator('#heatPrims > div').count()) === 2, 'Add primitive adds a row');
    const second = page.locator('#heatPrims > div').nth(1);
    await second.locator('select').nth(1).selectOption('tube');
    const inputs = second.locator('input');
    check((await inputs.count()) === 4, 'a tube shows ro, ri, length and elements');
    await inputs.nth(0).fill('0.05');
    await inputs.nth(1).fill('0.02');
    await inputs.nth(2).fill('0.3');
    await inputs.nth(3).fill('6');
    await page.click('button:has-text("Set chain in script")');
    const script = await page.inputValue('#heatScript');
    check(script.includes('segment material=STEEL length=0.5 elements=20 shape=rect a=0.02 b=0.02'), 'the chain writes the rectangle segment');
    check(script.includes('segment material=STEEL length=0.3 elements=6 shape=tube ro=0.05 ri=0.02'), 'the chain writes the tube segment');
    check(!/^\s*section\b/m.test(script), 'the section line is replaced by the shapes');
    check(script.includes('material id=STEEL k=50 rhocp=3.9e6'), 'the chain writes the material line');
    check(script.includes('temperature end=left'), 'the other lines of the script are kept');
    await page.click('button:has-text("Run heat analysis")');
    await page.waitForFunction(() => !document.getElementById('heatOut').hidden);
    const sent = await page.evaluate(() => window.__calls.filter((c) => c.name === 'openmodel_heat1d_run').pop().args[0]);
    check(sent.includes('shape=tube ro=0.05 ri=0.02'), 'the run sends the chain to the backend');

    // 2. A script with errors lists them with their line numbers and hides the previous result.
    await page.fill('#heatScript', 'analysis heat 1d\nbogus line\n');
    await page.click('button:has-text("Run heat analysis")');
    await page.waitForFunction(() => document.getElementById('heatErrors').textContent.length > 0);
    check((await page.textContent('#heatErrors')).includes('line 3 (bogus)'), 'the error keeps its line number');
    check(await page.isHidden('#heatOut'), 'the previous result is hidden after an error');

    // 3. The panel is independent of the structural analysis.
    check((await calls('openmodel_analysis_load')) === 0 && (await calls('openmodel_run_solver')) === 0, 'the heat panel calls no structural analysis handler');
    check((await calls('openmodel_heat1d_run')) === 3, 'each run calls the heat handler once (template, chain, bad script)');

    check(problems.length === 0, 'no page errors (' + problems.length + ')');
    if (problems.length) console.log(problems.join('\n'));
  } finally {
    await browser.close();
    server.close();
  }
  console.log(failures === 0 ? 'gui_heat1d_smoke: all checks passed' : 'gui_heat1d_smoke: ' + failures + ' check(s) failed');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); server.close(); process.exit(1); });
