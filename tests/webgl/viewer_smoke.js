// Headless smoke test of examples/webgl-fem-viewer using Playwright + Chromium.
// Usage: node viewer_smoke.js <viewer index.html> <file.mgwv> [screenshot-dir]
// Exit code 0 on success. Requires a WebGL2 context (SwiftShader is sufficient).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv, shotDir] = process.argv;
if (!html || !mgwv) { console.error('usage: viewer_smoke.js <index.html> <file.mgwv> [dir]'); process.exit(2); }

let failures = 0;
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  // ReadPixels used by this test triggers a driver performance note; it is not a viewer problem.
  page.on('console', (m) => {
    if ((m.type() === 'error' || m.type() === 'warning') && !/GPU stall|Performance/.test(m.text())) problems.push(m.text());
  });
  await page.goto('file://' + path.resolve(html));
  const b64 = fs.readFileSync(mgwv).toString('base64');

  const load = await page.evaluate((b64) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    window.OpenModelViewer.load(bytes.buffer, 'smoke.mgwv');
    return { msg: document.getElementById('msg').textContent, blocks: window.OpenModelViewer.state.model.meta.blocks.length };
  }, b64);
  check(load.msg === '', 'file loads without error message' + (load.msg ? ': ' + load.msg : ''));

  // Count pixels that differ from the clear colour, read back from the canvas.
  const coverage = () => page.evaluate(() => {
    window.OpenModelViewer.set({});
    const c = document.getElementById('gl'), g = c.getContext('webgl2');
    const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
    g.readPixels(0, 0, w, h, g.RGBA, g.UNSIGNED_BYTE, px);
    const bg = [px[0], px[1], px[2]];
    let n = 0, colours = new Set();
    for (let i = 0; i < px.length; i += 4) {
      if (Math.abs(px[i] - bg[0]) + Math.abs(px[i + 1] - bg[1]) + Math.abs(px[i + 2] - bg[2]) > 12) { n++; if (colours.size < 64) colours.add(px[i] >> 4 << 8 | px[i + 1] >> 4 << 4 | px[i + 2] >> 4); }
    }
    return { covered: n, total: w * h, colours: colours.size, glError: g.getError() };
  });
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };

  await page.evaluate(() => window.OpenModelViewer.set({ mode: 'block', wire: 'none', clip: false, view: 'iso' }));
  const full = await coverage();
  check(full.glError === 0, 'no WebGL error after first render');
  check(full.covered > full.total * 0.03, 'model covers a visible part of the canvas (' + (100 * full.covered / full.total).toFixed(1) + '%)');
  await shot('block.png');

  const modes = await page.evaluate(() => {
    const out = {};
    for (const m of ['region', 'surface', 'flat']) { window.OpenModelViewer.set({ mode: m }); out[m] = true; }
    return out;
  });
  check(modes.region && modes.surface && modes.flat, 'region / surface / flat modes render');
  await shot('flat.png');

  const hasField = await page.evaluate(() => window.OpenModelViewer.state.model.fields.length > 0);
  if (hasField) {
    await page.evaluate(() => window.OpenModelViewer.set({ mode: 'scalar' }));
    const sc = await coverage();
    check(sc.colours > 4, 'scalar mode produces a colour gradient (' + sc.colours + ' colour bins)');
    await shot('scalar.png');
  }

  await page.evaluate(() => window.OpenModelViewer.set({ mode: 'block', wire: 'none', clip: false }));
  const before = await coverage();
  await page.evaluate(() => window.OpenModelViewer.set({ clip: true, clipAxis: 2, clipPos: 0.3, view: 'iso' }));
  const clipped = await coverage();
  check(clipped.covered < before.covered, 'section removes part of the model (' + before.covered + ' -> ' + clipped.covered + ' px)');
  await shot('clip.png');

  // Hiding every block (through the UI checkboxes) must leave only the background.
  await page.evaluate(() => {
    window.OpenModelViewer.set({ clip: false, wire: 'skin' });
    for (const box of document.querySelectorAll('#blocks input')) {
      box.checked = false;
      box.dispatchEvent(new Event('change', { bubbles: true }));
    }
  });
  const hidden = await coverage();
  check(hidden.covered === 0, 'hiding all blocks hides faces and edges (' + hidden.covered + ' px left)');

  check(problems.length === 0, 'no console errors or warnings' + (problems.length ? ': ' + problems[0] : ''));
  await browser.close();
  console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
