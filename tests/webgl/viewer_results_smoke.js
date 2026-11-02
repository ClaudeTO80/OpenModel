// Headless test of the solver-results features of examples/webgl-fem-viewer (Playwright + Chromium).
// Usage: node viewer_results_smoke.js <viewer index.html> <results.mgwv> <plain.mgwv> [screenshot-dir]
//   results.mgwv: written with --results (fields + displacement)
//   plain.mgwv:   an ordinary MGWV without results (backward compatibility)
// Exit code 0 on success. Requires WebGL2 (SwiftShader is enough).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, resultsFile, plainFile, shotDir] = process.argv;
if (!html || !resultsFile || !plainFile) {
  console.error('usage: viewer_results_smoke.js <index.html> <results.mgwv> <plain.mgwv> [dir]');
  process.exit(2);
}

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
  page.on('console', (m) => {
    if ((m.type() === 'error' || m.type() === 'warning') && !/GPU stall|Performance/.test(m.text())) problems.push(m.text());
  });
  await page.goto('file://' + path.resolve(html));
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };

  const load = (file) => page.evaluate((b64) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    window.OpenModelViewer.load(bytes.buffer, 'test.mgwv');
    return document.getElementById('msg').textContent;
  }, fs.readFileSync(file).toString('base64'));

  // Snapshot of the canvas: RGBA bytes plus size.
  const snapshot = () => page.evaluate(() => {
    window.OpenModelViewer.set({});
    const c = document.getElementById('gl'), g = c.getContext('webgl2');
    const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
    g.readPixels(0, 0, w, h, g.RGBA, g.UNSIGNED_BYTE, px);
    const bg = [px[0], px[1], px[2]];
    let minX = w, maxX = -1, minY = h, maxY = -1, covered = 0;
    const colours = new Set();
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      if (Math.abs(px[i] - bg[0]) + Math.abs(px[i + 1] - bg[1]) + Math.abs(px[i + 2] - bg[2]) > 12) {
        covered++; minX = Math.min(minX, x); maxX = Math.max(maxX, x); minY = Math.min(minY, y); maxY = Math.max(maxY, y);
        if (colours.size < 200) colours.add((px[i] >> 3 << 10) | (px[i + 1] >> 3 << 5) | (px[i + 2] >> 3));
      }
    }
    window.__last = px;
    return { w, h, covered, bbox: [minX, minY, maxX, maxY], colours: colours.size, glError: g.getError() };
  });
  const differing = (before) => page.evaluate((prevB64) => {
    const prev = Uint8Array.from(atob(prevB64), (c) => c.charCodeAt(0)), cur = window.__last;
    let n = 0;
    for (let i = 0; i < cur.length; i += 4)
      if (Math.abs(cur[i] - prev[i]) + Math.abs(cur[i + 1] - prev[i + 1]) + Math.abs(cur[i + 2] - prev[i + 2]) > 24) n++;
    return n / (cur.length / 4);
  }, before);
  const lastPixelsB64 = () => page.evaluate(() => { let s = ''; const p = window.__last; for (let i = 0; i < p.length; i += 0x8000) s += String.fromCharCode.apply(null, p.subarray(i, i + 0x8000)); return btoa(s); });

  // ---------------------------------------------------------------- results file
  const msg = await load(resultsFile);
  check(msg === '', 'results file loads without error message' + (msg ? ': ' + msg : ''));
  const ui = await page.evaluate(() => ({
    mode: window.OpenModelViewer.state.mode,
    colorMode: document.getElementById('colorMode').value,
    fields: [...document.getElementById('fieldSel').options].map((o) => o.textContent),
    info: document.getElementById('resultsInfo').textContent,
    infoHidden: document.getElementById('resultsInfo').hidden,
    defHidden: document.getElementById('defSec').hidden,
    defName: document.getElementById('defName').textContent,
    factor: +document.getElementById('defFactor').value,
    hasDisp: !!window.OpenModelViewer.state.model.disp,
  }));
  check(ui.mode === 'scalar' && ui.colorMode === 'scalar', 'a results file opens coloured by the scalar field');
  check(ui.fields.includes('DISP magnitude') && ui.fields.includes('STRESS von Mises') && ui.fields.includes('DISP.D2'),
        'field selector lists the derived and component fields (' + ui.fields.length + ' fields)');
  check(!ui.infoHidden && ui.info.includes('beam.frd') && ui.info.includes('step 2'), 'results label is shown: "' + ui.info + '"');
  check(!ui.defHidden && ui.hasDisp && ui.defName === 'DISP' && ui.factor > 0, 'deformation panel is available (factor ' + ui.factor + ')');

  await page.evaluate(() => window.OpenModelViewer.set({ wire: 'none', clip: false, view: 'z', mode: 'scalar', field: 0 }));
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: false } }));
  const flat = await snapshot();
  check(flat.glError === 0, 'no WebGL error');
  check(flat.covered > flat.w * flat.h * 0.02, 'undeformed model is visible (' + (100 * flat.covered / (flat.w * flat.h)).toFixed(1) + '% of the canvas)');
  check(flat.colours >= 8, 'scalar colour map produces a gradient (' + flat.colours + ' colour bins)');
  const flatPixels = await lastPixelsB64();
  await shot('results_undeformed.png');

  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: true } }));
  const bent = await snapshot();
  const changed = await differing(flatPixels);
  check(bent.glError === 0, 'no WebGL error with deformation');
  check(changed > 0.01, 'the deformed shape differs from the undeformed one (' + (100 * changed).toFixed(1) + '% of pixels)');
  await shot('results_deformed.png');

  // Exaggerated factor must move the shape further than the suggested one.
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { factor: window.OpenModelViewer.state.defFactor * 4 } }));
  const bent4 = await snapshot();
  const vertical = (s) => s.bbox[3] - s.bbox[1];
  check(vertical(bent4) > vertical(bent) && vertical(bent) > vertical(flat),
        'a larger scale factor changes the silhouette (height ' + vertical(flat) + ' -> ' + vertical(bent) + ' -> ' + vertical(bent4) + ' px)');
  // Factor 0 equals the undeformed picture.
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { factor: 0 } }));
  await snapshot();
  const zero = await differing(flatPixels);
  check(zero < 0.001, 'factor 0 reproduces the undeformed picture (' + (100 * zero).toFixed(3) + '% differ)');

  // Slider and number input stay in sync.
  const sync = await page.evaluate(() => {
    const s = document.getElementById('defScale'), n = document.getElementById('defFactor');
    s.value = 200; s.dispatchEvent(new Event('input'));
    const afterSlider = +n.value;
    const suggested = window.OpenModelViewer.state.defSuggested;
    n.value = String(suggested); n.dispatchEvent(new Event('input'));
    return { afterSlider, suggested, sliderBack: +s.value };
  });
  check(Math.abs(sync.afterSlider - 2 * sync.suggested) < 1e-3 * sync.suggested * 2 + 1e-9, 'slider at 200 sets twice the suggested factor');
  check(Math.abs(sync.sliderBack - 100) < 0.5, 'typing the suggested factor moves the slider back to 100');

  // Switching field changes the colour range to the field's own limits.
  const field = await page.evaluate(() => {
    const V = window.OpenModelViewer, S = V.state, names = S.model.fields.map((f) => f.name);
    const i = names.indexOf('STRESS von Mises');
    V.set({ field: i });
    const f = S.model.fields[i];
    return { range: S.range.slice(), min: f.min, max: f.max, name: f.name };
  });
  check(field.name === 'STRESS von Mises' && field.range[0] === field.min && field.range[1] === field.max && field.max > field.min,
        'selecting a field applies its range [' + field.min.toPrecision(4) + ', ' + field.max.toPrecision(4) + ']');

  // Section plane and wireframe on the deformed shape.
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { factor: window.OpenModelViewer.state.defSuggested }, clip: true, clipAxis: 0, clipPos: 0.5, wire: 'all' }));
  const cut = await snapshot();
  check(cut.glError === 0 && cut.covered > 0 && cut.covered < bent.covered, 'section of the deformed shape renders and removes part of it');
  await shot('results_section.png');

  // ------------------------------------------------------------- plain file
  const plainMsg = await load(plainFile);
  const plain = await page.evaluate(() => ({
    mode: window.OpenModelViewer.state.mode,
    defHidden: document.getElementById('defSec').hidden,
    infoHidden: document.getElementById('resultsInfo').hidden,
    disp: window.OpenModelViewer.state.model.disp,
  }));
  check(plainMsg === '' && plain.defHidden && plain.infoHidden && plain.disp === null,
        'a file without results hides the deformation panel and the results label');
  check(plain.mode !== 'scalar', 'a file without results keeps the old default colour mode (' + plain.mode + ')');
  const plainShot = await snapshot();
  check(plainShot.glError === 0 && plainShot.covered > 0, 'plain file still renders');

  check(problems.length === 0, 'no console errors or warnings' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  if (failures) { console.log(failures + ' check(s) failed'); process.exit(1); }
  console.log('all checks passed');
})().catch((e) => { console.error(e); process.exit(1); });
