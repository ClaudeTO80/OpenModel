/**
 * @brief Viewer test of the opacity of the geometry: the slider sets the level and its label, and a translucent
 * surface shows the edges behind it. With the section plane on and all edges drawn, the internal edges (the
 * line elements of the axis) are counted as dark pixels of the screenshot: there are more of them at 30 % opacity
 * than at 100 %.
 *
 * Usage: node viewer_opacity_smoke.js <index.html> <file.mgwv>
 * The model must have line elements inside a solid, for example the display mesh of a 1D heat chain.
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv] = process.argv;
if (!html || !mgwv) {
  console.error('usage: viewer_opacity_smoke.js <index.html> <file.mgwv>');
  process.exit(2);
}

let failures = 0;
/** @brief Logs one check as PASS or FAIL and counts the failures. @param ok result of the check @param message description */
function check(ok, message) {
  console.log((ok ? 'PASS ' : 'FAIL ') + message);
  if (!ok) failures++;
}

(async () => {
  const browser = await chromium.launch({ args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const problems = [];
  try {
    const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
    page.on('pageerror', (e) => problems.push(String(e)));
    // Driver performance messages (GL Driver Message, GPU stall) are not errors of the page.
    page.on('console', (m) => { if ((m.type() === 'error' || m.type() === 'warning') && !m.text().startsWith('[.WebGL') ) problems.push(m.text()); });
    await page.goto('file://' + path.resolve(html));
    const b64 = fs.readFileSync(mgwv).toString('base64');
    await page.evaluate((b) => {
      const bin = atob(b);
      const u = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i);
      window.OpenModelViewer.load(u.buffer, 'opacity.mgwv');
    }, b64);
    await page.waitForTimeout(600);
    await page.selectOption('#wire', 'all');
    await page.check('text=Enable section');
    await page.waitForTimeout(300);

    /** @brief Number of dark pixels in the drawing area of the screenshot (the sidebar is excluded). */
    const darkPixels = async () => {
      const data = (await page.screenshot({ clip: { x: 320, y: 110, width: 760, height: 540 } })).toString('base64');
      return page.evaluate(async (d) => {
        const img = new Image();
        img.src = 'data:image/png;base64,' + d;
        await img.decode();
        const c = document.createElement('canvas');
        c.width = img.width; c.height = img.height;
        const ctx = c.getContext('2d');
        ctx.drawImage(img, 0, 0);
        const px = ctx.getImageData(0, 0, c.width, c.height).data;
        let dark = 0;
        for (let i = 0; i < px.length; i += 4) if (px[i] < 30 && px[i + 1] < 30 && px[i + 2] < 30) dark++;
        return dark;
      }, data);
    };

    const opaque = await darkPixels();

    // The slider sets the level and its label; the value is kept in the viewer state.
    await page.evaluate(() => {
      const el = document.getElementById('opacity');
      el.value = '30';
      el.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await page.waitForTimeout(300);
    check((await page.textContent('#opacityValue')) === '30 %', 'the opacity label follows the slider (30 %)');
    const translucent = await darkPixels();
    console.log('dark pixels: opaque ' + opaque + ', 30 % opacity ' + translucent);
    check(translucent > opaque, 'more edges are visible through the translucent surface (' + translucent + ' > ' + opaque + ')');

    // The API sets the same level, and 100 % goes back to the opaque rendering.
    await page.evaluate(() => window.OpenModelViewer.set({ opacity: 1 }));
    await page.waitForTimeout(300);
    check((await page.textContent('#opacityValue')) === '100 %', 'set({opacity: 1}) returns to 100 %');
    const again = await darkPixels();
    check(Math.abs(again - opaque) <= Math.max(5, opaque * 0.01), 'the opaque rendering is restored (' + again + ' vs ' + opaque + ')');

    check(problems.length === 0, 'no page or console errors (' + problems.length + ')');
    if (problems.length) console.log(problems.join('\n'));
  } finally {
    await browser.close();
  }
  console.log(failures === 0 ? 'viewer_opacity_smoke: all checks passed' : 'viewer_opacity_smoke: ' + failures + ' check(s) failed');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.error(e); process.exit(1); });
