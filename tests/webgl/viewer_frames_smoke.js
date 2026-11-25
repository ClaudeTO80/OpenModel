// Headless test of the time-history controls of examples/webgl-fem-viewer (Playwright + Chromium).
// Usage: node viewer_frames_smoke.js <viewer index.html> <frames.mgwv> <plain.mgwv> [screenshot-dir]
//   frames.mgwv: written from a result file with two or more increments (the --results export of a multi-step FRD)
//   plain.mgwv:  an ordinary MGWV without results (the increment controls must stay hidden)
// Exit code 0 on success. Requires WebGL2 (SwiftShader is enough).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, framesFile, plainFile, shotDir] = process.argv;
if (!html || !framesFile || !plainFile) {
  console.error('usage: viewer_frames_smoke.js <index.html> <frames.mgwv> <plain.mgwv> [dir]');
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

  // A checksum of the rendered canvas: it changes when the deformed shape changes.
  const checksum = () => page.evaluate(() => {
    window.OpenModelViewer.set({});
    const c = document.getElementById('gl'), g = c.getContext('webgl2');
    const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
    g.readPixels(0, 0, w, h, g.RGBA, g.UNSIGNED_BYTE, px);
    let sum = 0;
    for (let i = 0; i < px.length; i += 4) sum = (sum * 31 + px[i] * 3 + px[i + 1] * 5 + px[i + 2] * 7) % 4294967291;
    return sum;
  });

  // 1. A file with increments: the controls are shown and the last increment is the default one.
  await load(framesFile);
  const frameCount = await page.evaluate(() => window.OpenModelViewer.state.model.frames.length);
  check(frameCount >= 2, `the file has ${frameCount} increments`);
  const shownBox = await page.evaluate(() => !document.getElementById('frameBox').hidden);
  check(shownBox, 'the increment controls are shown');
  const defaults = await page.evaluate(() => ({ index: window.OpenModelViewer.state.frameIndex, max: document.getElementById('frameSel').max,
    value: document.getElementById('frameSel').value, label: document.getElementById('frameVal').textContent }));
  check(defaults.index === frameCount - 1 && defaults.value === String(frameCount - 1), 'the last increment is shown first');
  check(defaults.max === String(frameCount - 1) && defaults.label.includes('time') && defaults.label.includes(`(${frameCount} / ${frameCount})`),
    `slider and label match the increments ("${defaults.label}")`);
  const panel = await page.evaluate(() => document.getElementById('resultsPanel').innerText);
  check(/Time/.test(panel), 'the results card shows the increment');
  const lastSum = await checksum();
  await shot('frames_last.png');

  // 2. Another increment draws another deformed shape.
  await page.evaluate(() => { const s = document.getElementById('frameSel'); s.value = '0'; s.dispatchEvent(new Event('input')); });
  const firstSum = await checksum();
  const firstIndex = await page.evaluate(() => window.OpenModelViewer.state.frameIndex);
  check(firstIndex === 0, 'the slider moves to the first increment');
  check(firstSum !== lastSum, 'the first increment draws a different deformed shape');
  await shot('frames_first.png');

  // 3. Play runs through the increments and wraps to the first one.
  await page.evaluate(() => { const c = document.getElementById('framePlay'); c.checked = true; c.dispatchEvent(new Event('change')); });
  const seen = new Set();
  for (let i = 0; i < 30; i++) {
    await page.waitForTimeout(100);
    seen.add(await page.evaluate(() => window.OpenModelViewer.state.frameIndex));
  }
  await page.evaluate(() => { const c = document.getElementById('framePlay'); c.checked = false; c.dispatchEvent(new Event('change')); });
  check(seen.size >= 2, `play visits several increments (${[...seen].sort().join(', ')})`);
  check([...seen].every((i) => i >= 0 && i < frameCount), 'play stays inside the increments');

  // 4. A file without results: no increment controls.
  await load(plainFile);
  const plainHidden = await page.evaluate(() => document.getElementById('frameBox').hidden && window.OpenModelViewer.state.model.frames.length === 0);
  check(plainHidden, 'a file without increments hides the controls');

  check(problems.length === 0, 'no page errors or console warnings' + (problems.length ? ': ' + problems.join(' | ') : ''));
  await browser.close();
  if (failures) { console.error(failures + ' check(s) failed'); process.exit(1); }
  console.log('viewer_frames_smoke: all checks passed');
})().catch((e) => { console.error(e); process.exit(1); });
