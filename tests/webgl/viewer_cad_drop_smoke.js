// Headless test of the CAD drop in examples/webgl-fem-viewer (Playwright + Chromium): a CAD file dropped on the view is
// posted to the parent window (the OpenModel GUI adds it to the model), while an .mgwv file is still loaded in the viewer.
// Usage: node viewer_cad_drop_smoke.js <viewer index.html>
// Exit code 0 on success. Requires WebGL2 (SwiftShader is enough).
'use strict';
const { chromium } = require('playwright');
const path = require('path');
const http = require('http');
const fs = require('fs');

const [, , html] = process.argv;
if (!html) {
  console.error('usage: viewer_cad_drop_smoke.js <index.html>');
  process.exit(2);
}

let failures = 0;
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

// The viewer is embedded in a parent page of the same origin, as the GUI does: the drop must be forwarded from it.
const viewerDir = path.dirname(path.resolve(html));
const viewerFile = path.basename(path.resolve(html));
const parentPage = '<!doctype html><title>parent</title><iframe id="viewer" src="/' + viewerFile + '"></iframe>';
const server = http.createServer((req, res) => {
  if (req.url === '/') { res.writeHead(200, { 'content-type': 'text/html' }); res.end(parentPage); return; }
  const file = path.join(viewerDir, decodeURIComponent(req.url.split('?')[0]).replace(/^\//, ''));
  if (!file.startsWith(viewerDir) || !fs.existsSync(file)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'content-type': file.endsWith('.html') ? 'text/html' : 'application/octet-stream' });
  res.end(fs.readFileSync(file));
});

(async () => {
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const url = 'http://127.0.0.1:' + server.address().port + '/';
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
  const problems = [];
  page.on('pageerror', (e) => problems.push(e.message));
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById('viewer').contentWindow &&
    document.getElementById('viewer').contentWindow.document.readyState === 'complete');

  // Record the messages the parent receives from the viewer.
  await page.evaluate(() => {
    window.__drops = [];
    window.addEventListener('message', (ev) => {
      if (ev.data && ev.data.type === 'openmodel-cad-drop') window.__drops.push({ name: ev.data.file.name, size: ev.data.file.size });
    });
  });

  // Simulate a drop of a CAD file and of an .mgwv file on the view.
  const dropFile = (name, content) => page.evaluate(({ name, content }) => {
    const frame = document.getElementById('viewer').contentWindow;
    const view = frame.document.querySelector('canvas') || frame.document.body;
    const transfer = new frame.DataTransfer();
    transfer.items.add(new frame.File([content], name));
    view.dispatchEvent(new frame.DragEvent('drop', { dataTransfer: transfer, bubbles: true, cancelable: true }));
  }, { name, content: name.endsWith('.step') ? 'ISO-10303-21;\nEND-ISO-10303-21;\n' : 'MGWV' });

  await dropFile('bracket.step');
  await page.waitForFunction(() => window.__drops.length === 1, null, { timeout: 5000 }).catch(() => {});
  const drops = await page.evaluate(() => window.__drops);
  check(drops.length === 1 && drops[0].name === 'bracket.step', 'CAD drop is forwarded to the parent window');
  check(drops.length === 1 && drops[0].size > 0, 'the forwarded File has content');

  await dropFile('model.stp');
  await page.waitForFunction(() => window.__drops.length === 2, null, { timeout: 5000 }).catch(() => {});
  const after = await page.evaluate(() => window.__drops.map((d) => d.name));
  check(after.includes('model.stp'), 'a second CAD extension (.stp) is forwarded too');

  await dropFile('results.mgwv');
  await page.waitForTimeout(300);
  const countAfterMgwv = await page.evaluate(() => window.__drops.length);
  check(countAfterMgwv === 2, 'an .mgwv drop is not forwarded to the parent');

  check(problems.length === 0, 'no page errors (' + problems.join(' | ') + ')');
  await browser.close();
  server.close();
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.error(e); server.close(); process.exit(1); });
