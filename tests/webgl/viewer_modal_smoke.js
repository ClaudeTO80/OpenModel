// Headless test of modal results and per-element fields in examples/webgl-fem-viewer (Playwright + Chromium).
// Usage: node viewer_modal_smoke.js <viewer index.html> <modal.mgwv> <elements.mgwv> <static.mgwv> [screenshot-dir]
//   modal.mgwv:    written from a result file with vibration modes (5 modes)
//   elements.mgwv: written from a per-element table (element fields only)
//   static.mgwv:   written from a static result file with STRESS and TOSTRAIN
// Exit code 0 on success. Requires WebGL2 (SwiftShader is enough).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, modalFile, elementFile, staticFile, shotDir] = process.argv;
if (!html || !modalFile || !elementFile || !staticFile) {
  console.error('usage: viewer_modal_smoke.js <index.html> <modal.mgwv> <elements.mgwv> <static.mgwv> [dir]');
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

  // Renders and returns the canvas pixels as a string of RGB bytes (for exact comparisons) plus statistics.
  const snapshot = () => page.evaluate(() => {
    window.OpenModelViewer.set({});
    const c = document.getElementById('gl'), g = c.getContext('webgl2');
    const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
    g.readPixels(0, 0, w, h, g.RGBA, g.UNSIGNED_BYTE, px);
    const bg = [px[0], px[1], px[2]];
    let covered = 0, minY = h, maxY = -1;
    const colours = new Set();
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const i = (y * w + x) * 4;
      if (Math.abs(px[i] - bg[0]) + Math.abs(px[i + 1] - bg[1]) + Math.abs(px[i + 2] - bg[2]) > 12) {
        covered++; minY = Math.min(minY, y); maxY = Math.max(maxY, y);
        colours.add((px[i] >> 3 << 10) | (px[i + 1] >> 3 << 5) | (px[i + 2] >> 3));
      }
    }
    window.__prev = window.__last || null;
    window.__last = px;
    return { w, h, covered, height: maxY - minY, colours: colours.size, glError: g.getError() };
  });
  // Fraction of pixels that differ between the last two snapshots.
  const changedFraction = () => page.evaluate(() => {
    const a = window.__prev, b = window.__last;
    let n = 0;
    for (let i = 0; i < b.length; i += 4)
      if (Math.abs(a[i] - b[i]) + Math.abs(a[i + 1] - b[i + 1]) + Math.abs(a[i + 2] - b[i + 2]) > 24) n++;
    return n / (b.length / 4);
  });

  // ------------------------------------------------------------------ modes
  const msg = await load(modalFile);
  check(msg === '', 'modal file loads without error message' + (msg ? ': ' + msg : ''));
  const ui = await page.evaluate(() => {
    const S = window.OpenModelViewer.state, $ = (id) => document.getElementById(id);
    return {
      boxHidden: $('modeBox').hidden,
      options: [...$('modeSel').options].map((o) => o.textContent),
      freq: $('modeFreq').textContent,
      defHidden: $('defSec').hidden,
      firstField: S.model.fields[0].name,
      dynamic: !!S.model.fields[0].dynamic,
      colour: S.mode,
      info: $('resultsInfo').textContent,
      meta: S.model.meta.modes.map((m) => m.frequency),
      deformationName: S.model.meta.deformation.name,
      hasDisplacementChunk: !!S.model.meta.chunks.displacement,
    };
  });
  check(!ui.boxHidden && !ui.defHidden, 'the mode selector is shown inside the deformation panel');
  check(ui.options.length === 5 && ui.options[0].startsWith('mode 1') && ui.options[4].startsWith('mode 5'), 'five modes are listed: ' + ui.options.join(' | '));
  check(Math.abs(ui.meta[0] - 8355.17) < 0.5 && ui.meta.every((f, i) => i === 0 || f > ui.meta[i - 1]),
        'frequencies are stored in ascending order (' + ui.meta.map((f) => f.toFixed(0)).join(', ') + ')');
  check(Math.abs(+ui.freq - 8355.17) < 0.5, 'the frequency of mode 1 is displayed: ' + ui.freq);
  check(ui.firstField === '|u| (modo)' && ui.dynamic && ui.colour === 'scalar', 'the file opens coloured by the displacement magnitude of the mode');
  check(!ui.hasDisplacementChunk, 'a modal file has no single displacement chunk (one chunk per mode)');
  check(ui.info.includes('5 modes'), 'results label: "' + ui.info + '"');

  await page.evaluate(() => window.OpenModelViewer.set({ wire: 'none', clip: false, view: 'y', mode: 'scalar', field: 0, deform: { on: true } }));
  const range0 = await page.evaluate(() => window.OpenModelViewer.state.range.slice());
  const modeOne = await snapshot();
  check(modeOne.glError === 0 && modeOne.covered > modeOne.w * modeOne.h * 0.01, 'mode 1 renders');
  await shot('modal_mode1.png');

  // Selecting mode 3 must change displacement texture data, colour range and picture.
  const after = await page.evaluate(() => {
    const V = window.OpenModelViewer, S = V.state;
    const before = S.model.fields[0].max;
    V.set({ modeIndex: 2 });
    return { index: S.modeIndex, sameData: S.model.disp === S.model.modes[2].disp, before, after: S.model.fields[0].max,
             range: S.range.slice(), sel: document.getElementById('modeSel').value, freq: document.getElementById('modeFreq').textContent };
  });
  check(after.index === 2 && after.sameData && after.sel === '2', 'mode 3 becomes the active displacement');
  check(Math.abs(+after.freq - 146612) < 5, 'its frequency is displayed: ' + after.freq);
  check(after.range[1] === after.after && Math.abs(after.range[1] - range0[1]) > 1e-6, 'the colour range follows the mode (' + range0[1].toPrecision(4) + ' -> ' + after.range[1].toPrecision(4) + ')');
  const modeThree = await snapshot();
  const diffModes = await changedFraction();
  check(modeThree.glError === 0 && diffModes > 0.005, 'mode 3 looks different from mode 1 (' + (100 * diffModes).toFixed(1) + '% of pixels)');
  await shot('modal_mode3.png');

  // Animation: phase 0 is the undeformed shape, a quarter period the full shape.
  await page.evaluate(() => window.OpenModelViewer.set({ modeIndex: 0, animate: false, deform: { on: false } }));
  const rest = await snapshot();
  await page.evaluate(() => window.OpenModelViewer.set({ deform: { on: true }, animate: true, phase: 0 }));
  const at = async (phase) => {
    await page.evaluate((ph) => { const V = window.OpenModelViewer; V.state.animate = true; V.state.lastTick = performance.now() + 1e6; V.set({ phase: ph }); }, phase);
    return snapshot();
  };
  const a0 = await at(0);
  const aQuarter = await at(Math.PI / 2);
  const qDiff = await changedFraction();
  const aHalf = await at(Math.PI);
  const hDiffToQuarter = await changedFraction();
  const aThree = await at(1.5 * Math.PI);
  const t3 = await changedFraction();           // 3/2 pi against pi (rest)
  check(a0.glError === 0 && aQuarter.glError === 0 && aThree.glError === 0, 'no WebGL error while animating');
  check(Math.abs(a0.height - rest.height) <= 1 && Math.abs(a0.covered - rest.covered) < rest.covered * 0.002,
        'phase 0 equals the undeformed shape (height ' + rest.height + ' vs ' + a0.height + ' px)');
  check(qDiff > 0.003, 'a quarter period moves the shape (' + (100 * qDiff).toFixed(1) + '% of pixels)');
  check(Math.abs(aHalf.covered - a0.covered) < a0.covered * 0.01 && hDiffToQuarter > 0.003, 'half period returns to the rest position');
  check(t3 > 0.003, 'three quarters leaves the rest position (' + (100 * t3).toFixed(1) + '% of pixels)');
  // Mirror image: pi/2 and 3pi/2 are on opposite sides, so they differ by roughly twice as much as either from rest.
  await at(Math.PI / 2);
  await at(1.5 * Math.PI);
  const mirror = await changedFraction();
  check(mirror > 1.4 * Math.min(qDiff, t3), 'phases pi/2 and 3pi/2 are opposite shapes (' + (100 * mirror).toFixed(1) + '% vs ' + (100 * qDiff).toFixed(1) + '%)');
  await page.evaluate(() => window.OpenModelViewer.set({ animate: false }));

  // The real animation loop advances the phase on its own.
  const live = await page.evaluate(async () => {
    const V = window.OpenModelViewer, S = V.state;
    V.set({ animate: true, phase: 0 });
    S.lastTick = 0;
    const start = S.phase;
    await new Promise((r) => setTimeout(r, 600));
    const moved = S.phase;
    V.set({ animate: false });
    return { start, moved };
  });
  check(live.moved > live.start + 0.2, 'the animation loop advances the phase by itself (' + live.moved.toFixed(2) + ' rad after 0.6 s)');

  // The check box drives the same state.
  const box = await page.evaluate(() => { const c = document.getElementById('modeAnim'); c.checked = true; c.dispatchEvent(new Event('change')); const on = window.OpenModelViewer.state.animate; c.checked = false; c.dispatchEvent(new Event('change')); return [on, window.OpenModelViewer.state.animate]; });
  check(box[0] === true && box[1] === false, 'the "Anima" check box toggles the animation');

  // ---------------------------------------------------------- element fields
  const msgE = await load(elementFile);
  check(msgE === '', 'element file loads without error message' + (msgE ? ': ' + msgE : ''));
  const el = await page.evaluate(() => {
    const S = window.OpenModelViewer.state, m = S.model;
    return {
      names: [...document.getElementById('fieldSel').options].map((o) => o.textContent),
      lengths: m.fields.map((f) => f.values.length), elementFlags: m.fields.map((f) => f.element),
      tris: m.meta.counts.triangles, verts: m.meta.counts.vertices, modeBox: document.getElementById('modeBox').hidden,
      colour: S.mode,
    };
  });
  check(el.names.length >= 4 && el.names.every((n) => n.endsWith('[elemento]')), 'all fields are marked as per-element (' + el.names.length + ' fields)');
  check(el.elementFlags.every(Boolean) && el.lengths.every((n) => n === el.tris), 'element fields hold one value per triangle (' + el.tris + ')');
  check(el.modeBox && el.colour === 'scalar', 'no mode selector for a file without modes; opens coloured by the field');
  const names = el.names;
  const vm = names.indexOf('stress von Mises [elemento]');
  check(vm >= 0, 'derived von Mises is available for element tensors');
  await page.evaluate((i) => window.OpenModelViewer.set({ wire: 'none', view: 'y', mode: 'scalar', field: i }), Math.max(0, vm));
  const elShot = await snapshot();
  check(elShot.glError === 0 && elShot.colours >= 4, 'element field renders with several colours (' + elShot.colours + ' bins)');
  await shot('element_von_mises.png');
  // Per element means flat colours: far fewer distinct shades than the node-interpolated field would show.
  const part = names.indexOf('part [elemento]');
  await page.evaluate((i) => window.OpenModelViewer.set({ field: i }), part);
  const partShot = await snapshot();
  check(partShot.colours >= 2 && partShot.colours <= 6, 'a two-valued element field gives two flat colour regions (' + partShot.colours + ' bins incl. edges)');
  await shot('element_part.png');

  // ------------------------------------------------------------ static file
  const msgS = await load(staticFile);
  check(msgS === '', 'static file loads');
  const st = await page.evaluate(() => {
    const m = window.OpenModelViewer.state.model;
    const f = (n) => m.fields.find((x) => x.name === n);
    return {
      names: m.fields.map((x) => x.name),
      p1: f('STRESS principal 1'), p3: f('STRESS principal 3'), vm: f('STRESS von Mises'), tr: f('STRESS Tresca'),
      eq: f('TOSTRAIN equivalent'), ep1: f('TOSTRAIN principal 1'), sxy: f('STRESS.SXY'),
      modes: m.modes.length, modeBoxHidden: document.getElementById('modeBox').hidden,
    };
  });
  const need = ['STRESS von Mises', 'STRESS principal 1', 'STRESS principal 2', 'STRESS principal 3', 'STRESS Tresca', 'STRESS hydrostatic',
                'TOSTRAIN equivalent', 'TOSTRAIN principal 1', 'TOSTRAIN principal 3', 'TOSTRAIN max shear', 'TOSTRAIN volumetric'];
  check(need.every((n) => st.names.includes(n)), 'stress and strain invariants are listed');
  // Beam example: sigma_xx up to 400, shear 60: principal 1 >= von Mises/sqrt(3)... use exact relations instead.
  check(st.p1.max >= 400 && st.p1.max < 420 && st.p3.min <= -399 && st.p3.min > -420,
        'principal stresses bracket the bending stress (p1 max ' + st.p1.max.toFixed(1) + ', p3 min ' + st.p3.min.toFixed(1) + ')');
  check(st.tr.max >= Math.max(st.p1.max, -st.p3.min) - 1e-3 && st.tr.max >= st.vm.max, 'Tresca >= |principal| and >= von Mises (' + st.tr.max.toFixed(1) + ' >= ' + st.vm.max.toFixed(1) + ')');
  check(st.eq.max > 0 && st.ep1.max > 0 && st.ep1.max < 3e-3, 'strain invariants have plausible magnitude (principal 1 max ' + st.ep1.max.toExponential(2) + ')');
  check(st.modes === 0 && st.modeBoxHidden, 'a static file shows no mode selector');

  check(problems.length === 0, 'no console errors or warnings' + (problems.length ? ': ' + problems.slice(0, 3).join(' | ') : ''));
  await browser.close();
  if (failures) { console.log(failures + ' check(s) failed'); process.exit(1); }
  console.log('all checks passed');
})().catch((e) => { console.error(e); process.exit(1); });
