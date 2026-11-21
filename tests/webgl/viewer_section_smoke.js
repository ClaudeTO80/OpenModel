/**
 * @brief Headless smoke test of the section and display overlay on the viewer canvas, and of its colour bar.
 *
 * Without a model the overlay and the colour bar stay hidden and the page reports no errors. With a model file
 * (argv[3], an .mgwv that has at least one scalar field) the test drives the overlay and checks that the sidebar
 * inputs and the viewer state follow it, and that the colour bar shows the range in use.
 *
 * Usage: node viewer_section_smoke.js <index.html> [file.mgwv]
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium. Exit code 0 when every check passes.
 */
'use strict';
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const [, , html, mgwv] = process.argv;
if (!html) { console.error('usage: viewer_section_smoke.js <index.html> [file.mgwv]'); process.exit(2); }

let failures = 0;
/** @brief Logs one check as PASS or FAIL and counts the failures. @param ok result of the check @param message description */
function check(ok, message) { console.log((ok ? 'PASS ' : 'FAIL ') + message); if (!ok) failures++; }

(async () => {
  const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  try {
    const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
    const problems = [];
    page.on('pageerror', (e) => problems.push(e.message));
    page.on('console', (m) => { if (m.type() === 'error' && !/GPU stall|Performance/.test(m.text())) problems.push(m.text()); });
    await page.goto('file://' + path.resolve(html));

    const hidden = await page.evaluate(() => ({
      tools: document.getElementById('viewTools').hidden,
      colorbar: document.getElementById('colorbar').hidden,
    }));
    check(hidden.tools && hidden.colorbar, 'overlay and colour bar are hidden before a model is loaded');
  const panelHidden = await page.evaluate(() => document.getElementById('resultsPanel').hidden);
  check(panelHidden, 'results card is hidden before a model is loaded');

    if (!mgwv) {
      check(problems.length === 0, 'no page errors without a model' + (problems.length ? ': ' + problems.join(' | ') : ''));
      console.log(failures ? 'viewer_section_smoke: FAILED' : 'viewer_section_smoke: all checks passed (no model file: overlay checks skipped)');
      process.exit(failures ? 1 : 0);
    }

    const b64 = fs.readFileSync(mgwv).toString('base64');
    await page.evaluate((b64) => {
      const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
      window.OpenModelViewer.load(bytes.buffer, 'section.mgwv');
    }, b64);
    await page.waitForTimeout(300);

    const visible = await page.evaluate(() => !document.getElementById('viewTools').hidden);
    check(visible, 'overlay is shown once a model is loaded');

    // Section: the overlay switches the sidebar input and the viewer state together.
    await page.click('#vtSection');
    let st = await page.evaluate(() => ({ side: document.getElementById('clipOn').checked, state: window.OpenModelViewer.state.clipOn }));
    check(st.side && st.state, 'Section button turns the section on in the sidebar and in the viewer');
    await page.click('[data-vt-axis="0"]');
    st = await page.evaluate(() => ({ side: document.getElementById('clipAxis').value, state: window.OpenModelViewer.state.clipAxis, pressed: document.querySelector('[data-vt-axis="0"]').getAttribute('aria-pressed') }));
    check(st.side === '0' && st.state === 0 && st.pressed === 'true', 'axis X selects the plane normal to X');
    await page.click('#vtFlip');
    st = await page.evaluate(() => window.OpenModelViewer.state.clipFlip);
    check(st === true, 'Flip reverses the section side');
    await page.fill('#vtPos', '250');
    await page.dispatchEvent('#vtPos', 'input');
    st = await page.evaluate(() => ({ side: document.getElementById('clipPos').value, state: window.OpenModelViewer.state.clipPos }));
    check(st.side === '250' && Math.abs(st.state - 0.25) < 1e-9, 'position slider moves the section plane');
    await page.click('#vtSection');
    st = await page.evaluate(() => ({ dis: document.getElementById('vtFlip').disabled, state: window.OpenModelViewer.state.clipOn }));
    check(!st.state && st.dis, 'turning the section off disables its controls');

    // Display mode: the three buttons drive the Edges select of the sidebar.
    for (const v of ['none', 'all', 'skin']) {
      await page.click(`[data-vt-wire="${v}"]`);
      st = await page.evaluate(() => ({ side: document.getElementById('wire').value, state: window.OpenModelViewer.state.wire, pressed: document.querySelector('[data-vt-wire][aria-pressed="true"]').dataset.vtWire }));
      check(st.side === v && st.state === v && st.pressed === v, 'display mode ' + v + ' follows in the sidebar and the overlay');
    }

    // Colour bar: only for scalar colouring, with the range in use.
    const before = await page.evaluate(() => !document.getElementById('colorbar').hidden);
    await page.selectOption('#colorMode', 'scalar');
    const bar = await page.evaluate(() => ({
      shown: !document.getElementById('colorbar').hidden,
      min: document.getElementById('cbMin').textContent,
      max: document.getElementById('cbMax').textContent,
      title: document.getElementById('cbTitle').textContent,
      background: document.getElementById('cbBar').style.background,
    }));
    check(!before && bar.shown, 'colour bar appears for scalar colouring');
    check(bar.background.includes('linear-gradient'), 'colour bar uses the colour map gradient');
    check(bar.title.length > 0 && bar.min !== '' && bar.max !== '', 'colour bar shows the field name and the range (' + bar.title + ': ' + bar.min + ' .. ' + bar.max + ')');
    await page.fill('#smax', '12.5');
    await page.dispatchEvent('#smax', 'input');
    const max = await page.textContent('#cbMax');
    check(max === '12.5', 'colour bar follows the Max input (' + max + ')');
    await page.selectOption('#colorMode', 'block');
    check(await page.evaluate(() => document.getElementById('colorbar').hidden), 'colour bar hides when the colouring is not scalar');

    // Results card: shown for a file with results, with the load case and the deformation scale as values.
    const card = await page.evaluate(() => ({
      hidden: document.getElementById('resultsPanel').hidden,
      label: window.OpenModelViewer.state.model.meta.results ? window.OpenModelViewer.state.model.meta.results.label : null,
      text: document.getElementById('rpList').textContent,
    }));
    if (card.label) {
      check(!card.hidden && card.text.includes(card.label), 'results card shows the load case (' + card.label + ')');
      await page.fill('#defFactor', '2');
      await page.dispatchEvent('#defFactor', 'input');
      const factor = await page.evaluate(() => document.getElementById('rpList').textContent);
      check(factor.includes('Deformation x') && /Deformation x\s*2/.test(factor), 'results card follows the deformation factor');
    } else {
      check(card.hidden, 'results card stays hidden for a file without results');
    }

    check(problems.length === 0, 'no page errors' + (problems.length ? ': ' + problems.join(' | ') : ''));
    console.log(failures ? 'viewer_section_smoke: FAILED (' + failures + ')' : 'viewer_section_smoke: all checks passed');
    process.exit(failures ? 1 : 0);
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message || e); process.exit(1); });
