/**
 * @brief Smoke test of editing a load in the Loads detail panel: value, amplitude and name.
 *
 * Adds a pressure and a force line with explicit values, opens the pressure, changes its value and its name, and
 * checks that the line and the step that uses it follow the new name. Removing the force drops it from the step.
 *
 * Usage: node tests/webgl/gui_load_edit_smoke.js <url of gui.html>
 * Serve src/webui over HTTP first (with viewer.html); needs the `playwright` package (NODE_PATH) and a Chromium.
 *
 * @param argv[2] URL of gui.html
 */
const { chromium } = require("playwright");

/** @brief Throws with a message when a check fails, logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_load_edit_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.addInitScript(() => {
      window.webui = { isConnected: () => true, call: async () => JSON.stringify({ ok: false, message: "stub" }) };
    });
    await page.goto(url);
    await page.waitForTimeout(500);
    const script = () => page.evaluate(() => $("anScript").value);

    await page.evaluate(() => {
      $("anScript").value = "material E=210000 nu=0.3 rho=7.85e-9\nfix group:BASE\npressure id=P1 group:TOP p=1\nforce id=F1 group:TIP F=0,0,-100\nstep static loads=P1,F1\n";
      renderAnalysisConditions(); renderTree();
    });
    await page.evaluate(() => { const e = scriptLoadEntries().find((x) => x.id === "P1"); showLoadDetail(e); });
    await page.fill("#ldP", "2.5");
    await page.fill("#ldName", "END_PRESS");
    await page.click('[data-ld="apply"]');
    await page.waitForTimeout(100);
    let s = await script();
    check(/^pressure id=END_PRESS group:TOP p=2\.5$/m.test(s), "the pressure line takes the new value and name");
    check(/^step static loads=END_PRESS,F1$/m.test(s), "the step that uses it follows the new name");

    await page.evaluate(() => { const e = scriptLoadEntries().find((x) => x.id === "F1"); showLoadDetail(e); });
    await page.fill("#ldF", "0,-50,-200");
    await page.click('[data-ld="apply"]');
    s = await script();
    check(/^force id=F1 group:TIP F=0,-50,-200$/m.test(s), "the force takes the new total force");

    await page.evaluate(() => { const e = scriptLoadEntries().find((x) => x.id === "F1"); showLoadDetail(e); });
    await page.fill("#ldName", "END_PRESS");
    await page.click('[data-ld="apply"]');
    const msg = await page.evaluate(() => $("result").textContent);
    check(/already exists/.test(msg), "a name already used by another load is refused");

    await page.evaluate(() => { const e = scriptLoadEntries().find((x) => x.id === "F1"); showLoadDetail(e); });
    await page.click('[data-ld="remove"]');
    s = await script();
    check(!/force id=F1/.test(s) && /^step static loads=END_PRESS$/m.test(s), "removing the force also drops it from the step");

    check(errors.length === 0, "no page errors (" + errors.join(" | ") + ")");
    console.log("gui load edit smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
