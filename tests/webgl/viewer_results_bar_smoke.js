/**
 * @brief Headless test of the results bar on the right of the viewer (the fields and the modes of the model) and of the
 * transparent colour bar. A click on a field switches to the scalar colouring of that field; a click on a mode selects it.
 *
 * Usage: node viewer_results_bar_smoke.js <index.html> <results.mgwv> [screenshot-dir]
 * The .mgwv must carry solver results. Checks on a model with several modes run only when the file has them.
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, shots] = process.argv;
if (!html || !mgwv) { console.error('usage: viewer_results_bar_smoke.js <index.html> <results.mgwv> [screenshot-dir]'); process.exit(2); }

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
    await page.goto('file://' + path.resolve(html));
    await page.waitForTimeout(300);
    await page.evaluate(() => localStorage.clear());
    await page.reload();
    await page.waitForTimeout(300);
    const b64 = fs.readFileSync(mgwv).toString('base64');
    await page.evaluate((b64) => {
      const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      window.OpenModelViewer.load(bytes.buffer, 'results.mgwv');
    }, b64);
    await page.waitForTimeout(300);
    await page.selectOption('#colorMode', 'scalar');
    await page.waitForTimeout(200);
    // Reloads the page and loads the results again: a reload empties the viewer.
    const reopen = async () => {
      await page.reload();
      await page.waitForTimeout(300);
      await page.evaluate((b64) => {
        const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
        window.OpenModelViewer.load(bytes.buffer, 'results.mgwv');
      }, b64);
      await page.waitForTimeout(300);
      await page.selectOption('#colorMode', 'scalar');
      await page.waitForTimeout(200);
    };
    const visible = (id) => page.evaluate((id) => {
      const el = document.getElementById(id);
      return !!el && !el.hidden && el.getClientRects().length > 0;
    }, id);
    const info = await page.evaluate(() => {
      const m = window.OpenModelViewer.state.model;
      return { fields: m.fields.length, modes: m.modes.length };
    });

    // 1. The bar lists the fields and the modes of the model; the field in use is pressed.
    check(await visible('resultsBar'), 'the results bar is shown for a model with results');
    const listed = await page.locator('#rbFields button').count();
    check(listed === info.fields, 'the bar lists every field of the model (' + listed + ' of ' + info.fields + ')');
    const pressedField = await page.evaluate(() => {
      const b = document.querySelector('#rbFields button[aria-pressed="true"]');
      return b ? +b.dataset.rbField : -1;
    });
    check(pressedField === 0, 'the field in use is the one pressed (first field at load)');
    if (shots) await page.screenshot({ path: path.join(shots, 'results_bar.png') });

    // 2. A field click switches to the scalar colouring of that field.
    await page.selectOption('#colorMode', 'block');
    await page.waitForTimeout(100);
    if (info.fields > 1) {
      await page.click('#rbFields button[data-rb-field="1"]');
      await page.waitForTimeout(100);
      const state = await page.evaluate(() => ({ idx: window.OpenModelViewer.state.fieldIndex, mode: window.OpenModelViewer.state.mode, pressed: +document.querySelector('#rbFields button[aria-pressed="true"]').dataset.rbField }));
      check(state.idx === 1 && state.mode === 'scalar', 'clicking a field shows it in scalar colouring');
      check(state.pressed === 1, 'the pressed field follows the click');
      check((await page.inputValue('#fieldSel')) === '1', 'the Field list of the sidebar follows the click');
    } else {
      await page.click('#rbFields button[data-rb-field="0"]');
      await page.waitForTimeout(100);
      check((await page.evaluate(() => window.OpenModelViewer.state.mode)) === 'scalar', 'clicking the only field shows it in scalar colouring');
      console.log('SKIP a second field: the file has one field');
    }

    // 3. A mode click selects that mode (only when the file has several modes).
    if (info.modes > 1) {
      await page.click('#rbModes button[data-rb-mode="1"]');
      await page.waitForTimeout(100);
      check((await page.evaluate(() => window.OpenModelViewer.state.modeIndex)) === 1, 'clicking a mode selects it');
      check((await page.evaluate(() => document.querySelector('#rbModes button[aria-pressed="true"]').dataset.rbMode)) === '1', 'the pressed mode follows the click');
    } else {
      console.log('SKIP mode clicks: the file has ' + info.modes + ' mode(s)');
    }

    // 4. The bar folds to its title and unfolds again; the choice is kept.
    await page.click('#rbToggle');
    check(await page.evaluate(() => document.getElementById('resultsBar').classList.contains('collapsed')), 'the toggle folds the bar');
    check(!(await page.isVisible('#rbFields')), 'a folded bar hides its lists');
    await reopen();
    check(await page.evaluate(() => document.getElementById('resultsBar').classList.contains('collapsed')), 'the folded choice is kept after reload');
    await page.click('#rbToggle');
    check(!(await page.evaluate(() => document.getElementById('resultsBar').classList.contains('collapsed'))), 'the bar unfolds again');

    // 5. The colour bar is transparent; its text has a halo.
    await reopen();
    const bg = await page.evaluate(() => getComputedStyle(document.getElementById('colorbar')).backgroundColor);
    check(bg === 'rgba(0, 0, 0, 0)', 'the colour bar background is transparent (' + bg + ')');

    // 6. Clearing the model hides the bar.
    await page.evaluate(() => window.OpenModelViewer.clear());
    await page.waitForTimeout(100);
    check(!(await visible('resultsBar')), 'the results bar is hidden after clear()');

    check(problems.length === 0, 'no page or console errors (' + problems.length + ')');
    if (problems.length) console.log(problems.join('\n'));
  } finally {
    await browser.close();
  }
  console.log(failures === 0 ? 'viewer_results_bar_smoke: all checks passed' : 'viewer_results_bar_smoke: ' + failures + ' check(s) failed');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
