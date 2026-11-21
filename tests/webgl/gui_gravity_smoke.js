/**
 * @brief Smoke test of the default gravity and of the Loads menu and detail panel.
 *
 * Checks that gravity is on the model by default, that it can be switched off (its line commented out) and on again,
 * that the Loads + menu lists the load types and adds gravity without duplicating it, that a load of the tree opens
 * its detail panel, and that the part-only gravity control is gone.
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

    const script = () => page.evaluate(() => $("anScript").value);
    check(/^gravity g=0,0,-9810$/m.test(await script()), "gravity is on the model by default");
    check(await page.locator("#asGravTarget").count() === 0, "the part-only gravity control is gone");

    // The Loads row lists gravity; switching it off comments the line out and the tree says so.
    await page.evaluate(() => setGravityOn(false));
    check(/^# gravity g=0,0,-9810$/m.test(await script()), "switching gravity off comments its line out");
    const offRow = await page.evaluate(() => [...document.querySelectorAll(".sbrow .sbname")].map((n) => n.textContent));
    check(offRow.includes("gravity · gravity (off)"), "the tree shows gravity as off");

    // The + of Loads opens the list of load types.
    await page.evaluate(() => {
      const row = [...document.querySelectorAll(".sbrow")].find((r) => r.querySelector(".sbname")?.textContent === "Loads");
      [...row.querySelectorAll("button")].find((b) => b.textContent.trim() === "+").click();
    });
    await page.waitForTimeout(100);
    const items = await page.evaluate(() => [...document.querySelectorAll("#loadMenu button")].map((b) => b.textContent));
    check(items.length === 3 && items.some((t) => t.startsWith("Gravity")) && items.some((t) => t.startsWith("Force")) && items.some((t) => t.startsWith("Pressure")),
      "the + of Loads lists gravity, force and pressure");
    await page.locator("#loadMenu button", { hasText: "Gravity" }).click();
    await page.waitForTimeout(100);
    check(/^gravity g=0,0,-9810$/m.test(await script()), "choosing gravity from the menu switches it back on");
    check((await script()).match(/^gravity\b/gm).length === 1, "gravity is not duplicated");

    // Choosing it again while it is on says so.
    await page.evaluate(() => addLoad("gravity"));
    const msg = await page.evaluate(() => $("result").textContent);
    check(/already on the model/.test(msg), "adding gravity while it is on says so");

    // A load of the tree opens its detail panel in the sidebar overlay.
    await page.evaluate(() => [...document.querySelectorAll(".sbname")].find((n) => n.textContent === "gravity · gravity").click());
    await page.waitForTimeout(100);
    const detail = await page.evaluate(() => ({ open: !document.getElementById("inspector").hidden, title: document.getElementById("inspTitle").textContent,
      g: document.getElementById("ldGravity")?.value }));
    check(detail.open && /Gravity/.test(detail.title) && detail.g === "0,0,-9810", "a load opens its values in the overlay");

    // Force needs a selection; without one the menu item says so.
    await page.evaluate(() => addLoad("force"));
    const msg2 = await page.evaluate(() => $("result").textContent);
    check(/Select nodes, elements or faces/.test(msg2), "a force needs a selection");

    check(errors.length === 0, "no page errors (" + errors.join(" | ") + ")");
    console.log("gui gravity smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
