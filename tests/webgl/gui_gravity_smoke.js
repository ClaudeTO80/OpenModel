/**
 * @brief Smoke test of the gravity and body-load controls of the Loads panel.
 *
 * Checks the target list (all parts, selected elements), that gravity for all parts is written once (a second write
 * replaces the line), that a targeted gravity line is numbered body-N like the solver numbers it, and that the
 * selected-elements target asks for a selection when none is made.
 *
 * Usage: node tests/webgl/gui_gravity_smoke.js <url of gui.html>
 * Serve src/webui over HTTP first (with viewer.html); needs the `playwright` package (NODE_PATH) and a Chromium.
 *
 * @param argv[2] URL of gui.html
 */
const { chromium } = require("playwright");

/** @brief Throws with a message when a check fails, logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_gravity_smoke.js <url of gui.html>");
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

    const targets = await page.evaluate(() => [...document.querySelectorAll("#asGravTarget option")].map((o) => o.value));
    check(targets.includes("all") && targets.includes("selection"), "the gravity target list offers all parts and the selected elements");

    await page.evaluate(() => { $("asGravity").value = "0,0,-9810"; $("asGravTarget").value = "all"; });
    await page.evaluate(() => addGravity());
    await page.waitForTimeout(100);
    let script = await page.evaluate(() => $("anScript").value);
    check((script.match(/^gravity\b/gm) || []).length === 1 && /^gravity g=0,0,-9810$/m.test(script), "gravity for all parts is written as one line");

    await page.evaluate(() => { $("asGravity").value = "0,0,-5"; });
    await page.evaluate(() => addGravity());
    script = await page.evaluate(() => $("anScript").value);
    check((script.match(/^gravity\b/gm) || []).length === 1 && /^gravity g=0,0,-5$/m.test(script), "a second all-parts gravity replaces the first line");

    const ids = await page.evaluate(() => parseScript("gravity g=0,0,-1 part=Housing\ngravity id=ZG g=0,0,-2 group=ZONE\ngravity g=0,0,-3\n").loads.map((l) => l.id));
    check(ids.join() === "body-1,ZG,gravity", "targeted gravity gets body-N ids, an explicit id is kept");

    await page.evaluate(() => { $("asGravTarget").value = "selection"; });
    await page.evaluate(() => addGravity());
    await page.waitForTimeout(100);
    const msg = await page.evaluate(() => $("result").textContent);
    check(/Select nodes, elements or faces/.test(msg), "the selected-elements target asks for a selection when none is made");

    await page.evaluate(() => setPickGrowth("single"));
    check(errors.length === 0, "no page errors (" + errors.join(" | ") + ")");
    console.log("gui gravity smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
