/**
 * @brief Headless smoke test of the colour bar of the viewer: its place on the left, the palette and scale
 * selectors, the editable range ends and the ticks. Takes a screenshot for a visual check.
 *
 * Usage: node viewer_colorbar_smoke.js <index.html> <file.mgwv> [screenshot-dir]
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, shots] = process.argv;
if (!html || !mgwv) { console.error('usage: viewer_colorbar_smoke.js <index.html> <file.mgwv> [screenshot-dir]'); process.exit(2); }

let failures = 0;
/** @brief Logs one check as PASS or FAIL and counts the failures. @param ok result of the check @param message description */
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  try {
    const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
    const problems = [];
    page.on('pageerror', (e) => problems.push(String(e)));
    page.on('console', (m) => { if (m.type() === 'error') problems.push(m.text()); });
    await page.goto('file://' + path.resolve(html));
    await page.waitForTimeout(300);

    const b64 = fs.readFileSync(mgwv).toString('base64');
    await page.evaluate((b64) => {
      const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      window.OpenModelViewer.load(bytes.buffer, 'colorbar.mgwv');
    }, b64);
    await page.waitForTimeout(300);
    await page.selectOption('#colorMode', 'scalar');
    await page.waitForTimeout(200);

    const box = await page.evaluate(() => {
      const r = document.getElementById('colorbar').getBoundingClientRect();
      const c = document.querySelector('canvas').getBoundingClientRect();
      return { left: r.left - c.left, centre: (r.top + r.bottom) / 2 - c.top, height: c.height, shown: !document.getElementById('colorbar').hidden };
    });
    check(box.shown && box.left < 120, 'colour bar is on the left of the viewer (left ' + Math.round(box.left) + ' px)');
    check(Math.abs(box.centre - box.height / 2) < box.height * 0.3, 'colour bar is vertically centred');
    if (shots) {
      fs.mkdirSync(shots, { recursive: true });
      await page.screenshot({ path: path.join(shots, 'colorbar.png') });
    }

    const palettes = await page.evaluate(() => Array.from(document.getElementById('palSel').options).map((o) => o.value));
    check(palettes.length === 5 && palettes.includes('Rosso') && palettes.includes('Ocra'), 'palette list: ' + palettes.join(', '));

    await page.selectOption('#palSel', 'Rosso');
    const rosso = await page.evaluate(() => ({
      bar: document.getElementById('cbBar').style.background,
      side: document.getElementById('bar').style.background,
      state: window.OpenModelViewer.state.range,
    }));
    // Chrome reports the stops as rgb(), so compare the darkest stop of Rosso (#67000d) and the lightest (#fff5f0).
    check(rosso.bar.includes('rgb(103, 0, 13)') && rosso.side.includes('rgb(255, 245, 240)'), 'palette Rosso is used by the colour bar and the sidebar bar');

    // Tick labels: three between the ends.
    const ticks = await page.evaluate(() => document.querySelectorAll('#cbMid span').length);
    check(ticks === 3, 'colour bar has three intermediate ticks (' + ticks + ')');

    // Range ends: the colour bar inputs drive the state and the sidebar Min/Max.
    const max0 = rosso.state[1];
    await page.fill('#cbMaxIn', String(12.5));
    await page.dispatchEvent('#cbMaxIn', 'change');
    const sideMax = await page.inputValue('#smax');
    const r1 = await page.evaluate(() => window.OpenModelViewer.state.range[1]);
    check(Math.abs(r1 - 12.5) < 1e-9 && sideMax === '12.5', 'Max in the colour bar sets the range and the sidebar (was ' + max0 + ')');

    // Logarithmic scale needs a positive minimum: with zero the linear scale is shown and a note says so.
    await page.selectOption('#scaleSel', 'log');
    const zeroMin = await page.evaluate(() => ({ note: !document.getElementById('cbNote').hidden, log: window.OpenModelViewer.state.logScale }));
    check(zeroMin.log && zeroMin.note, 'logarithmic with minimum 0 shows the note');

    await page.fill('#cbMinIn', '0.01');
    await page.dispatchEvent('#cbMinIn', 'change');
    const logOk = await page.evaluate(() => ({ note: !document.getElementById('cbNote').hidden, min: window.OpenModelViewer.state.range[0], side: document.getElementById('smin').value }));
    check(!logOk.note && logOk.min === 0.01 && logOk.side === '0.01', 'logarithmic with minimum 0.01 is applied and the note is hidden');
    if (shots) await page.screenshot({ path: path.join(shots, 'colorbar-log.png') });

    await page.selectOption('#scaleSel', 'lin');
    const linear = await page.evaluate(() => window.OpenModelViewer.state.logScale === false && !document.getElementById('cbNote').hidden === false);
    check(linear, 'switching back to linear clears the logarithmic scale');

    check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems.join(' | ') : ''));
  } finally {
    await browser.close();
  }
  console.log(failures ? 'viewer_colorbar_smoke: FAILED (' + failures + ')' : 'viewer_colorbar_smoke: all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
