// Headless test of the rotation axes of the left mouse button in examples/webgl-fem-viewer.
// Usage: node viewer_rotation_axes_smoke.js <viewer index.html> <model.mgwv>
//   Ctrl+Alt drag: roll (about the view axis), Ctrl+Shift drag: pitch (about the horizontal axis),
//   Shift+Alt drag: yaw (about the vertical axis). Plain drag still rotates, Shift drag still pans.
//   The oracle is the view matrix itself: a rotation about one view axis keeps the matching row of the matrix
//   (right = row 0, up = row 1, depth = row 2) and changes the other two, and the matrix stays orthonormal.
// Exit code 0 on success. Needs WebGL2 (SwiftShader is enough). The browser is taken from OPENMODEL_TEST_CHROMIUM when set.
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv] = process.argv;
if (!html || !mgwv) { console.error('usage: viewer_rotation_axes_smoke.js <index.html> <model.mgwv>'); process.exit(2); }
let failures = 0;
const check = (ok, message) => { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; };

const EPS = 1e-9;
const rowsOf = (R) => [R.slice(0, 3), R.slice(3, 6), R.slice(6, 9)];
const same = (a, b) => a.every((v, i) => Math.abs(v - b[i]) < EPS);
const rowChanged = (a, b, r) => !same(rowsOf(a)[r], rowsOf(b)[r]);
const orthonormal = (R) => {
  const [x, y, z] = rowsOf(R);
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  return Math.abs(dot(x, x) - 1) < 1e-6 && Math.abs(dot(y, y) - 1) < 1e-6 && Math.abs(dot(z, z) - 1) < 1e-6 &&
    Math.abs(dot(x, y)) < 1e-6 && Math.abs(dot(x, z)) < 1e-6 && Math.abs(dot(y, z)) < 1e-6;
};

(async () => {
  const browser = await chromium.launch({
    executablePath: process.env.OPENMODEL_TEST_CHROMIUM || undefined,
    args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  await page.goto('file://' + path.resolve(html));
  await page.evaluate((b64) => {
    const b = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    window.OpenModelViewer.load(b.buffer, 'washer.mgwv');
  }, fs.readFileSync(mgwv).toString('base64'));
  await page.waitForTimeout(300);

  const rot = () => page.evaluate(() => Array.from(window.OpenModelViewer.state.rot));
  const target = () => page.evaluate(() => Array.from(window.OpenModelViewer.state.target));
  const selection = () => page.evaluate(() => JSON.stringify(window.OpenModelViewer.pick.get()));

  // Drag with the given modifier keys held, from the centre of the canvas by (dx, dy) pixels in steps.
  const drag = async (keys, dx, dy, button = 'left') => {
    const box = await page.locator('canvas').first().boundingBox();
    const x0 = box.x + box.width / 2, y0 = box.y + box.height / 2;
    for (const k of keys) await page.keyboard.down(k);
    await page.mouse.move(x0, y0);
    await page.mouse.down({ button });
    const steps = 10;
    for (let i = 1; i <= steps; i++) await page.mouse.move(x0 + (dx * i) / steps, y0 + (dy * i) / steps);
    await page.mouse.up({ button });
    for (const k of keys.slice().reverse()) await page.keyboard.up(k);
    await page.waitForTimeout(50);
  };

  const R0 = await rot();

  // Plain drag still rotates about both screen axes.
  await drag([], 80, 40);
  let R = await rot();
  check(rowChanged(R0, R, 0) && rowChanged(R0, R, 1), 'plain drag still rotates (regression)');

  // Roll: Ctrl+Alt, horizontal drag: only the depth row (view axis) is kept, the rotation is about it.
  let before = await rot(); let t0 = await target();
  await drag(['Control', 'Alt'], 80, 0);
  R = await rot();
  check(!rowChanged(before, R, 2), 'Ctrl+Alt drag rolls: the view axis is kept');
  check(rowChanged(before, R, 0) && rowChanged(before, R, 1), 'Ctrl+Alt drag rolls: right and up turn in their plane');
  check(orthonormal(R), 'Ctrl+Alt roll keeps the view matrix orthonormal');
  check(same(t0, await target()), 'Ctrl+Alt drag does not pan the target');

  // Roll ignores vertical motion: a purely vertical Ctrl+Alt drag leaves the matrix alone.
  before = await rot();
  await drag(['Control', 'Alt'], 0, 80);
  check(same(before, await rot()), 'Ctrl+Alt vertical-only drag does not roll (roll uses the horizontal component)');

  // Pitch: Ctrl+Shift, vertical drag: the right row is kept.
  before = await rot(); t0 = await target();
  await drag(['Control', 'Shift'], 0, 80);
  R = await rot();
  check(!rowChanged(before, R, 0), 'Ctrl+Shift drag pitches: the right axis is kept');
  check(rowChanged(before, R, 1) && rowChanged(before, R, 2), 'Ctrl+Shift drag pitches: up and depth turn');
  check(orthonormal(R), 'Ctrl+Shift pitch keeps the view matrix orthonormal');
  check(same(t0, await target()), 'Ctrl+Shift drag does not pan the target');

  // Pitch ignores horizontal motion.
  before = await rot();
  await drag(['Control', 'Shift'], 80, 0);
  check(same(before, await rot()), 'Ctrl+Shift horizontal-only drag does not pitch (pitch uses the vertical component)');

  // Yaw: Shift+Alt, horizontal drag: the up row is kept.
  before = await rot(); t0 = await target();
  await drag(['Shift', 'Alt'], 80, 0);
  R = await rot();
  check(!rowChanged(before, R, 1), 'Shift+Alt drag yaws: the up axis is kept');
  check(rowChanged(before, R, 0) && rowChanged(before, R, 2), 'Shift+Alt drag yaws: right and depth turn');
  check(orthonormal(R), 'Shift+Alt yaw keeps the view matrix orthonormal');
  check(same(t0, await target()), 'Shift+Alt drag does not pan the target');

  // Shift alone still pans (the target moves, the rotation does not).
  before = await rot(); t0 = await target();
  await drag(['Shift'], 60, 30);
  check(same(before, await rot()) && !same(t0, await target()), 'Shift drag still pans (regression)');

  // A Ctrl+Alt click without motion is a rotation start, not a selection: the selection stays as it was.
  const sel0 = await selection();
  const box = await page.locator('canvas').first().boundingBox();
  await page.keyboard.down('Control'); await page.keyboard.down('Alt');
  await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  await page.keyboard.up('Alt'); await page.keyboard.up('Control');
  await page.waitForTimeout(50);
  check((await selection()) === sel0, 'Ctrl+Alt click does not change the selection');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems.join(' | ') : ''));
  await browser.close();
  console.log(failures ? failures + ' failure(s)' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
