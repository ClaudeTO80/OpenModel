/**
 * @brief Headless test of the legend options of the colour bar and of the viewer's session API: the options can be
 * hidden and shown (and the choice is kept), setPalette / palette / palettes, and clear() that empties the viewer so that
 * a new model can be loaded.
 *
 * Usage: node viewer_legend_smoke.js <index.html> <results.mgwv> [screenshot-dir]
 * The .mgwv must carry solver results (scalar colouring), e.g. from `openmodel-cli export-web --results`.
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, shots] = process.argv;
if (!html || !mgwv) { console.error('usage: viewer_legend_smoke.js <index.html> <results.mgwv> [screenshot-dir]'); process.exit(2); }

let failures = 0;
/** @brief Logs one check as PASS or FAIL and counts the failures. @param ok result of the check @param message description */
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  try {
    const context = await browser.newContext({ viewport: { width: 1100, height: 700 } });
    const page = await context.newPage();
    const problems = [];
    page.on('pageerror', (e) => problems.push(String(e)));
    page.on('console', (m) => { if (m.type() === 'error') problems.push(m.text()); });
    const url = 'file://' + path.resolve(html);
    const b64 = fs.readFileSync(mgwv).toString('base64');
    const load = () => page.evaluate((b64) => {
      const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      window.OpenModelViewer.load(bytes.buffer, 'legend.mgwv');
    }, b64);
    const visible = (id) => page.evaluate((id) => {
      const el = document.getElementById(id);
      return !!el && !el.hidden && el.getClientRects().length > 0;
    }, id);
    // The state of the options as set on the element (independent of whether a model is shown).
    const optionsShown = () => page.evaluate(() => !document.getElementById('cbOptions').hidden);
    // Reloads the viewer page and loads the results again, so that the colour bar can be used.
    const reopen = async () => {
      await page.reload();
      await page.waitForTimeout(300);
      await load();
      await page.waitForTimeout(300);
      await page.selectOption('#colorMode', 'scalar');
      await page.waitForTimeout(200);
    };

    await page.goto(url);
    await page.waitForTimeout(300);
    await page.evaluate(() => localStorage.clear());
    await page.reload();
    await page.waitForTimeout(300);
    await load();
    await page.waitForTimeout(300);
    await page.selectOption('#colorMode', 'scalar');
    await page.waitForTimeout(200);

    // 1. The options are shown by default and the button hides and shows them.
    check(await visible('colorbar'), 'colour bar is shown for the scalar colouring');
    check(await optionsShown(), 'legend options are shown by default');
    await page.click('#cbOptsBtn');
    check(!(await optionsShown()), 'the options button hides the legend options');
    check(await visible('colorbar'), 'the colour bar itself stays visible when the options are hidden');
    check((await page.getAttribute('#cbOptsBtn', 'aria-expanded')) === 'false', 'the button reports the collapsed state');
    check((await page.textContent('#cbOptsBtn')) === 'Show options', 'the button offers to show the options');
    if (shots) await page.screenshot({ path: path.join(shots, 'legend_hidden.png') });

    // 2. The choice is kept when the page is opened again.
    await reopen();
    check(!(await optionsShown()), 'a hidden choice is kept after reloading the viewer');
    await page.click('#cbOptsBtn');
    check(await visible('cbOptions'), 'the options come back when shown again');
    check((await page.getAttribute('#cbOptsBtn', 'aria-expanded')) === 'true', 'the button reports the expanded state');
    await reopen();
    check(await optionsShown(), 'a shown choice is kept after reloading the viewer');

    // 3. Palette API: the names, the active one, and refusal of an unknown name.
    const names = await page.evaluate(() => window.OpenModelViewer.palettes());
    check(JSON.stringify(names) === JSON.stringify(['Viridis', 'Rosso', 'Ocra', 'Blu', 'Arcobaleno']), 'palettes() lists the five colour maps in order');
    check((await page.evaluate(() => window.OpenModelViewer.palette())) === 'Viridis', 'Viridis is the default palette');
    const applied = await page.evaluate(() => window.OpenModelViewer.setPalette('Rosso'));
    check(applied === true, 'setPalette accepts a known name');
    check((await page.evaluate(() => window.OpenModelViewer.palette())) === 'Rosso', 'palette() reports the new palette');
    check((await page.inputValue('#palSel')) === 'Rosso', 'the Palette list of the colour bar follows setPalette');
    const refused = await page.evaluate(() => window.OpenModelViewer.setPalette('NoSuchMap'));
    check(refused === false, 'setPalette refuses an unknown name');
    check((await page.evaluate(() => window.OpenModelViewer.palette())) === 'Rosso', 'an unknown name leaves the palette unchanged');
    if (shots) await page.screenshot({ path: path.join(shots, 'legend_rosso.png') });

    // 4. clear() empties the viewer as at start.
    await page.evaluate(() => window.OpenModelViewer.clear());
    await page.waitForTimeout(200);
    check((await page.evaluate(() => window.OpenModelViewer.state.model)) === null, 'clear() removes the model');
    check(await visible('drop'), 'the empty-state message is back after clear()');
    check(!(await visible('colorbar')), 'the colour bar is hidden after clear()');
    check(!(await visible('blocksSec')), 'the block list is hidden after clear()');
    check((await page.textContent('#fileName')) === 'No file loaded', 'the file name is reset after clear()');
    check((await page.evaluate(() => document.getElementById('stats').children.length)) === 0, 'the statistics are emptied after clear()');
    check((await page.evaluate(() => window.OpenModelViewer.palette())) === 'Rosso', 'clear() keeps the palette of the session');

    // 5. A new model loads into the emptied viewer.
    await load();
    await page.waitForTimeout(300);
    check((await page.evaluate(() => window.OpenModelViewer.state.model !== null)), 'a new model loads after clear()');
    check(!(await visible('drop')), 'the empty-state message is hidden again once a model is loaded');
    check((await page.evaluate(() => document.getElementById('fileName').textContent)) === 'legend.mgwv', 'the new model is named after its file');

    check(problems.length === 0, 'no page or console errors (' + problems.length + ')');
    if (problems.length) console.log(problems.join('\n'));
  } finally {
    await browser.close();
  }
  console.log(failures === 0 ? 'viewer_legend_smoke: all checks passed' : 'viewer_legend_smoke: ' + failures + ' check(s) failed');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
