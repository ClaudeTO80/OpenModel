// End-to-end test of the OpenModel Workbench GUI (src/webui) on the real WebUI runtime.
// Needs: openmodel-webui running with a Chromium reachable through CDP on port 9222
// (see tests/webgl/README.md), Node.js with Playwright, and a STEP file.
// Usage: node gui_e2e.js <model.step> <work-dir> [screenshot-dir]
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , stepFile, workDir, shotDir] = process.argv;
if (!stepFile || !workDir) { console.error('usage: gui_e2e.js <model.step> <work-dir> [shots]'); process.exit(2); }
let failures = 0;
const check = (ok, text) => { console.log((ok ? 'PASS ' : 'FAIL ') + text); if (!ok) failures++; };

(async () => {
  const browser = await chromium.connectOverCDP('http://127.0.0.1:9222');
  const page = browser.contexts()[0].pages().find((p) => p.url().startsWith('http://localhost'));
  if (!page) { console.error('GUI page not found'); process.exit(2); }
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  await page.waitForFunction(() => document.querySelectorAll('#mesher option').length > 0, null, { timeout: 15000 });

  const message = () => page.textContent('#result');
  const waitIdle = async () => { await page.waitForFunction(() => !document.querySelector('button[disabled]') || document.querySelectorAll('button:not([disabled])').length > 3, null, { timeout: 120000 }); await page.waitForTimeout(400); };
  const viewerInfo = () => page.evaluate(async () => {
    const w = document.getElementById('viewer').contentWindow;
    for (let i = 0; i < 40 && !(w.OpenModelViewer && w.OpenModelViewer.state.model); i++) await new Promise((r) => setTimeout(r, 100));
    const m = w.OpenModelViewer.state.model;
    return m ? { elements: m.meta.counts.elements, blocks: m.meta.blocks.length, tris: m.meta.counts.triangles, types: [...new Set(m.meta.blocks.map((b) => b.type))].join(',') } : null;
  });
  const click = async (text) => { await page.click('aside button:has-text("' + text + '")'); await waitIdle(); };
  const shot = async (name) => { if (shotDir) { fs.mkdirSync(shotDir, { recursive: true }); await page.screenshot({ path: path.join(shotDir, name) }); } };

  const options = await page.$$eval('#mesher option', (o) => o.map((x) => x.value + (x.disabled ? ':disabled' : '')));
  check(options.includes('gmsh') && options.includes('tetgen'), 'mesher selector lists Gmsh and TetGen (' + options.join(', ') + ')');

  // 1. Import and triangulate.
  await page.fill('#cadPath', cadFixture);
  await click('Importa e triangola');
  check(/importato/.test(await message()), 'CAD import reports success');
  const surf = await viewerInfo();
  check(surf && surf.tris > 0 && /Triangle3/.test(surf.types), 'viewer shows the CAD surface (' + (surf && surf.tris) + ' triangles)');

  // CAD surface picking must return the original OCC face id, not a generated triangle id.
  await page.selectOption('#selMode', 'face');
  await page.fill('#selAngle', '0'); await page.dispatchEvent('#selAngle', 'change');
  const cadHit = await page.evaluate(() => {
    const V = document.getElementById('viewer').contentWindow.OpenModelViewer;
    const m = V.state.model;
    for (let t = 0; t < m.meta.counts.triangles; ++t) {
      const xy = [0, 1, 2].map((k) => V.pick.project(m.vertexNodeId[m.triIndices[3 * t + k]]));
      if (xy.some((p) => !p)) continue;
      const x = xy.reduce((a, p) => a + p[0], 0) / 3, y = xy.reduce((a, p) => a + p[1], 0) / 3;
      if (V.pick.at(x, y).tri >= 0) return [x, y];
    }
    return null;
  });
  if (cadHit) {
    await page.mouse.click(cadHit[0], cadHit[1]);
    await page.waitForTimeout(300);
  }
  const cadSelection = await page.evaluate(() => document.getElementById('viewer').contentWindow.OpenModelViewer.pick.get());
  check(cadHit && cadSelection.faces.length > 0 &&
        cadSelection.faces.every((face) => Number.isInteger(face.cadFaceId) && face.cadFaceId >= 0),
        'CAD face picking returns sourceFaceId values (' + JSON.stringify(cadSelection.faces.slice(0, 3)) + ')');
  check(/Facce CAD selezionate:/.test(await page.textContent('#cadFaceInfo')),
        'the workbench displays the selected CAD face identifiers');

  // Create a named CAD group, then reimport the same STEP to verify sidecar persistence.
  await page.fill('#cadGrpName', 'BladeRoot');
  await page.click('#btnCadGrpCreate');
  await waitIdle();
  check(/Gruppo CAD/.test(await message()) && fs.existsSync(cadSidecar),
        'CAD face group is created and persisted beside the STEP file');
  await page.selectOption('#selMode', 'off');
  await page.fill('#cadPath', cadFixture);
  await click('Importa e triangola');
  await page.waitForFunction(() => document.querySelector('#cadGrpList').textContent.includes('BladeRoot'), null, { timeout: 10000 });
  check(/Gruppi CAD utente: 1/.test(await message()) || (await page.textContent('#cadGrpList')).includes('BladeRoot'),
        'CAD face group is restored after reimporting the source STEP');
  const cadGroupDelete = page.locator('#cadGrpList button[data-cad-delete]').first();
  page.once('dialog', (dialog) => dialog.accept());
  await cadGroupDelete.click();
  await waitIdle();
  check(!(await page.textContent('#cadGrpList')).includes('BladeRoot'),
        'CAD face group can be deleted and the sidecar is updated');
  await shot('1_import.png');

  // 2. Gmsh volume mesh must change the viewer content to tetrahedra.
  await page.selectOption('#mesher', 'gmsh');
  await page.fill('#size', '0.8');
  await click('Genera mesh volumetrica');
  check(/Gmsh/.test(await message()) && /validata/.test(await message()), 'Gmsh generates and validates a volume mesh');
  const gm = await viewerInfo();
  check(gm && /Tetrahedron4/.test(gm.types) && gm.elements > 1000, 'viewer shows the Gmsh tetrahedra (' + (gm && gm.elements) + ' elements)');
  await shot('2_gmsh.png');

  // 3. Save / export in every format.
  const base = path.join(workDir, 'gui_out');
  await page.fill('#outBase', base);
  for (const [label, ext] of [['Salva MGMB', '.mgmb'], ['Esporta INP', '.inp'], ['Esporta MGWV', '.mgwv'], ['Esporta GLB', '.glb']]) {
    await click(label);
    check(fs.existsSync(base + ext) && fs.statSync(base + ext).size > 100, label + ' writes ' + path.basename(base + ext));
  }

  // 4. TetGen from the same CAD model.
  await page.selectOption('#mesher', 'tetgen');
  await page.fill('#size', '0.8');
  await click('Genera mesh volumetrica');
  check(/TetGen/.test(await message()) && /validata/.test(await message()), 'TetGen generates and validates a volume mesh: ' + (await message()).split('\n')[1]);
  const tg = await viewerInfo();
  check(tg && /Tetrahedron4/.test(tg.types), 'viewer shows the TetGen tetrahedra (' + (tg && tg.elements) + ' elements)');

  // 5. Reload the saved MGMB (loads a mesh without CAD) and compare counts with the Gmsh mesh.
  await page.fill('#loadPath', base + '.mgmb');
  await click('Carica e visualizza');
  check(/caricata/.test(await message()), 'saved MGMB reloads');
  const back = await viewerInfo();
  check(back && gm && back.elements === gm.elements, 'reloaded mesh has the same element count as the saved one (' + (back && back.elements) + ')');
  await page.fill('#loadPath', base + '.inp');
  await click('Carica e visualizza');
  const inp = await viewerInfo();
  check(inp && gm && inp.elements === gm.elements, 'exported INP reloads with the same element count (' + (inp && inp.elements) + ')');
  await shot('3_reloaded.png');

  // 6. Server-side file browser.
  await page.fill('#cadPath', stepFile);
  await page.click('aside button:has-text("Sfoglia")');
  await page.waitForSelector('#entries li');
  const names = await page.$$eval('#entries li', (l) => l.map((x) => x.textContent));
  check(names.includes(path.basename(stepFile)), 'file browser lists ' + path.basename(stepFile));
  await page.keyboard.press('Escape');

  // 7. Errors are reported, not swallowed.
  await page.fill('#loadPath', path.join(workDir, 'missing.mgmb'));
  await click('Carica e visualizza');
  check(/non trovato/.test(await message()), 'missing file is reported');

  check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems[0] : ''));
  await browser.close().catch(() => {});
  try { fs.rmSync(cadSidecar, { force: true }); } catch (_) {}
  console.log(failures ? failures + ' check(s) failed' : 'all checks passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
