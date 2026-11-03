// GUI end-to-end test for CAD components/groups and interface continuity.
// Same setup as gui_e2e.js. Usage: node gui_assembly_e2e.js <assembly.step> <work-dir> [screenshot-dir]
// The STEP comes from build/make_assembly_fixture (Base red, Cover blue, two Pins, touching on z = 10).
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const [, , stepFile, workDir, shotDir] = process.argv;
if (!stepFile || !workDir) { console.error('usage: gui_assembly_e2e.js <assembly.step> <work-dir> [shots]'); process.exit(2); }
let failures = 0;
const check = (ok, text) => { console.log((ok ? 'PASS ' : 'FAIL ') + text); if (!ok) failures++; };

/** Count pixels of a PNG buffer for which pred(r,g,b) holds (8-bit RGB/RGBA, non-interlaced). */
function countPixels(png, pred) {
  let pos = 8, width = 0, height = 0, bpp = 4; const idat = [];
  while (pos < png.length) {
    const len = png.readUInt32BE(pos), type = png.toString('ascii', pos + 4, pos + 8);
    if (type === 'IHDR') { width = png.readUInt32BE(pos + 8); height = png.readUInt32BE(pos + 12); bpp = png[pos + 17] === 6 ? 4 : 3; }
    if (type === 'IDAT') idat.push(png.subarray(pos + 8, pos + 8 + len));
    pos += 12 + len;
  }
  const raw = zlib.inflateSync(Buffer.concat(idat)), stride = width * bpp, out = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const f = raw[y * (stride + 1)];
    for (let x = 0; x < stride; x++) {
      const v = raw[y * (stride + 1) + 1 + x];
      const a = x >= bpp ? out[y * stride + x - bpp] : 0, b = y ? out[(y - 1) * stride + x] : 0, c = x >= bpp && y ? out[(y - 1) * stride + x - bpp] : 0;
      let r = v;
      if (f === 1) r = v + a; else if (f === 2) r = v + b; else if (f === 3) r = v + ((a + b) >> 1);
      else if (f === 4) { const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c); r = v + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c); }
      out[y * stride + x] = r & 255;
    }
  }
  let n = 0;
  for (let i = 0; i < out.length; i += bpp) if (pred(out[i], out[i + 1], out[i + 2])) n++;
  return n;
}

(async () => {
  const browser = await chromium.connectOverCDP('http://127.0.0.1:9222');
  const page = browser.contexts()[0].pages().find((p) => p.url().startsWith('http://localhost'));
  if (!page) { console.error('GUI page not found'); process.exit(2); }
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  await page.waitForFunction(() => document.querySelectorAll('#mesher option').length > 0, null, { timeout: 15000 });

  const message = () => page.textContent('#result');
  const waitIdle = async () => { await page.waitForFunction(() => !document.querySelector('button[disabled]') || document.querySelectorAll('button:not([disabled])').length > 3, null, { timeout: 120000 }); await page.waitForTimeout(500); };
  const click = async (text) => { await page.click('aside button:has-text("' + text + '")'); await waitIdle(); };
  const frame = () => page.frameLocator('#viewer');
  const viewer = (fn, arg) => page.evaluate(async ([src, a]) => {
    const w = document.getElementById('viewer').contentWindow;
    for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model); i++) await new Promise((r) => setTimeout(r, 100));
    return new w.Function('V', 'arg', 'return (' + src + ')(V, arg)')(w.OpenModelViewer, a);
  }, [fn.toString(), arg]);
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };
  const redPixels = async () => { await page.waitForTimeout(300); return countPixels(await frame().locator('#gl').screenshot(), (r, g, b) => r > 60 && r > 2 * g && r > 2 * b); };
  const leaves = () => viewer((V) => [...V.state.model.meta.groups].filter((g) => g.dimension === 3).map((g) => g.name));

  // 1. Import: the structure stored in the file is reported and drives the viewer.
  await page.fill('#cadPath', stepFile);
  await click('Importa e triangola');
  const msg = await message();
  check(/Struttura CAD: 5 nodi di assieme \(4 componenti\), 2 gruppi di facce/.test(msg), 'import reports the CAD structure: ' + (msg.split('\n').find((l) => l.startsWith('Struttura')) || msg));
  await page.waitForFunction(() => document.querySelector('#nativeCadGrpList')?.textContent.includes('TopFace'), null, { timeout: 10000 });
  const nativeGroups = await page.textContent('#nativeCadGrpList');
  check(/TopFace/.test(nativeGroups) && /Facce/.test(nativeGroups), 'Workbench lists native CAD named groups with topological dimension: ' + nativeGroups);
  await page.waitForTimeout(800);
  const names = await frame().locator('#comps .nm').allTextContents();
  check(['Assy', 'Base', 'Cover', 'Pin', 'Pin #2'].every((n) => names.includes(n)), 'viewer lists the components as a tree: ' + names.join(' | '));
  const mode = await viewer((V) => V.state.mode);
  check(mode === 'region', 'viewer opens coloured by component (mode ' + mode + ')');
  const swatches = await frame().locator('#comps .sw').evaluateAll((el) => el.map((e) => getComputedStyle(e).backgroundColor));
  check(swatches.includes('rgb(255, 0, 0)') && swatches.includes('rgb(0, 0, 255)'), 'component colours come from the CAD file: ' + swatches.join(' '));
  const redBefore = await redPixels();
  await frame().locator('#comps label:has(.nm:text-is("Base")) input').uncheck();
  const redAfter = await redPixels();
  check(redBefore > 500 && redAfter === 0, 'unchecking Base hides it (red pixels ' + redBefore + ' -> ' + redAfter + ')');
  await shot('a1_cad_components.png');

  // 2. Conformal Gmsh mesh: groups, colours and shared interface.
  await page.selectOption('#mesher', 'gmsh');
  await page.selectOption('#iface', 'conformal');
  await page.fill('#size', '2');
  await click('Genera mesh volumetrica');
  const gmsg = await message();
  check(/validata/.test(gmsg) && /conformi/.test(gmsg), 'Gmsh conformal mesh generated');
  check(/CAD groups created: 4 components, 0 solid groups, 2 face groups/.test(gmsg), 'CAD groups transferred: ' + (gmsg.split('\n').find((l) => l.startsWith('CAD groups')) || ''));
  check(/fragmented into 4 volumes/.test(gmsg), 'solids were fragmented for conformity');
  await page.waitForTimeout(800);
  const vol = await leaves();
  check(['Assy/Base', 'Assy/Cover', 'Assy/Pin', 'Assy/Pin #2'].every((n) => vol.includes(n)), 'mesh groups carry the CAD paths: ' + vol.join(', '));
  await page.waitForFunction(() => document.querySelector('#genericMeshGrpList')?.textContent.includes('Assy/Base'), null, { timeout: 10000 });
  check(/CAD/.test(await page.textContent('#genericMeshGrpList')), 'Workbench lists backend-independent FEM groups and their source');
  const redVol = await redPixels();
  await frame().locator('#comps label:has(.nm:text-is("Base")) input').uncheck();
  const redVolHidden = await redPixels();
  check(redVol > 500 && redVolHidden === 0, 'hiding Base also hides its skin and wireframe (red ' + redVol + ' -> ' + redVolHidden + ')');
  // Base is still hidden, so "Assy" is indeterminate: one click on it shows everything, a second one hides everything.
  const indeterminate = await frame().locator('#comps label:has(.nm:text-is("Assy")) input').evaluate((el) => el.indeterminate);
  check(indeterminate, 'parent is indeterminate while one child is hidden');
  await frame().locator('#comps label:has(.nm:text-is("Assy")) input').click();
  await frame().locator('#comps label:has(.nm:text-is("Assy")) input').click();
  const allHidden = await viewer((V) => V.state.model.meta.groups.every((g, i) => g.dimension !== 3 || V.state.gvis[i + 1] === 0));
  check(allHidden, 'unchecking the parent "Assy" hides every component below it');
  await frame().locator('#comps label:has(.nm:text-is("Assy")) input').check();
  const shown = await viewer((V) => V.state.model.meta.groups.every((g, i) => g.dimension !== 3 || V.state.gvis[i + 1] === 1));
  check(shown, 'checking the parent shows them again');
  await viewer((V) => V.set({ mode: 'surface' }));
  const legend = await frame().locator('#legend .nm').allTextContents();
  check(legend.some((t) => t.startsWith('Interface')) && legend.some((t) => t.startsWith('TopFace')), 'face groups from the CAD (layer + named face) are listed: ' + legend.join(' | '));
  await shot('a2_conformal.png');

  // 3. Independent mode must say so; the groups stay.
  await page.selectOption('#iface', 'independent');
  await click('Genera mesh volumetrica');
  const imsg = await message();
  check(/indipendenti/.test(imsg) && !/fragmented/.test(imsg) && /4 components/.test(imsg), 'independent mode reported, no fragmentation');

  // 4. MGMB keeps the groups (names, hierarchy); colours are not stored there.
  const base = path.join(workDir, 'asm_out');
  await page.fill('#outBase', base);
  await click('Salva MGMB');
  await page.fill('#loadPath', base + '.mgmb');
  await click('Carica e visualizza');
  await page.waitForTimeout(800);
  const reloaded = await leaves();
  check(reloaded.length === 4 && reloaded.includes('Assy/Base'), 'groups survive MGMB save/load: ' + reloaded.join(', '));

  // 5. TetGen is honest about what it cannot do. Needs the CAD model: re-import.
  await page.fill('#cadPath', stepFile);
  await click('Importa e triangola');
  await page.selectOption('#mesher', 'tetgen');
  await page.fill('#size', '2');
  await click('Genera mesh volumetrica');
  const tmsg = await message();
  check(/TetGen/.test(tmsg) && /does not transfer CAD components/.test(tmsg), 'TetGen reports that it does not transfer CAD groups');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems[0] : ''));
  await browser.close();
  console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });
