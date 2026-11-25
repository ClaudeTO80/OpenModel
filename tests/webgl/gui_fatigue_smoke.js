/**
 * @brief Smoke test of the fatigue dialog: configuration, argument passing, result table and the no-results state.
 *
 * The bridge is a stub: openmodel_fatigue_options returns one material and one field, openmodel_fatigue_run returns
 * two rows. The test checks what the page sends and how it shows the reply; the analysis itself is tested in C++.
 *
 * Usage: node tests/webgl/gui_fatigue_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

/** @brief Builds a page stub whose fatigue options reply with the given resultsLoaded flag. */
function stubScript(resultsLoaded) {
  return (loaded) => {
    window.lastFatigueArgs = null;
    window.webui = {
      isConnected: () => true,
      call: async (name, ...args) => {
        if (name === "openmodel_fatigue_options") return JSON.stringify({ ok: true, message: "",
          resultsLoaded: loaded, resultsName: "run.frd",
          fields: loaded ? [{ name: "STRESS", components: 6, location: "node" }] : [],
          materials: [{ id: "steel", name: "Steel" }, { id: "alu", name: "Aluminium" }] });
        if (name === "openmodel_fatigue_run") {
          window.lastFatigueArgs = args;
          return JSON.stringify({ ok: true, message: "Fatigue analysis of STRESS: 2 of 2 position(s) analysed.",
            field: "STRESS", folder: "",
            rows: [
              { label: "node:1", ok: true, damage: 0.00125, life: 800, cycles: 12, diagnostic: "" },
              { label: "node:2", ok: false, damage: null, life: null, cycles: 0, diagnostic: "" },
            ] });
        }
        return JSON.stringify({ ok: false, message: "stub" });
      },
    };
  };
}

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_fatigue_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    // Case 1: results loaded, two materials, one field: run, then the result table.
    const page = await browser.newPage();
    await page.addInitScript(stubScript(true), true);
    await page.goto(url);
    await page.waitForTimeout(500);
    check(await page.locator('button[data-act="fatigue"]').count() === 1, "the Results tab has the Fatigue button");

    await page.evaluate(() => openFatigueDialog());
    await page.waitForTimeout(200);
    check(await page.locator("#fatigueDialog[open]").count() === 1, "the configuration dialog opens");
    check(await page.locator("#fatMaterial option").count() === 2, "both materials with Basquin constants are offered");
    check(await page.locator("#fatField option").count() === 1, "the loaded stress field is offered");
    check(await page.locator("#fatigueRun").isEnabled(), "the run button is enabled when results and materials exist");

    await page.locator("#fatMaterial").selectOption("alu");
    await page.locator("#fatPositions").fill("node:1; node:2");
    await page.locator("#fatProjection").fill("0,0,1");
    await page.locator("#fatigueRun").click();
    await page.waitForTimeout(400);

    const args = await page.evaluate(() => window.lastFatigueArgs);
    check(Array.isArray(args) && args.length === 8, "the run sends eight arguments");
    check(args[0] === "alu" && args[1] === "STRESS", "the material and the field are sent");
    check(args[2] === "node:1; node:2" && args[3] === "0,0,1", "the positions and the normal direction are sent");
    check(args[4] === "293.15" && args[5] === "MPa" && args[6] === "1000000", "temperature and MPa unit (scale 1e6) are sent");
    check(args[7] === "", "an empty report folder is sent as empty");

    const rows = await page.locator("#fatigueRows tr").count();
    check(rows === 3, "one header and one row per position are shown");
    const firstRow = await page.locator("#fatigueRows tr").nth(1).innerText();
    check(/node:1/.test(firstRow) && /1\.250e-3|0\.00125/.test(firstRow), "the first row shows the position and its damage");
    check(/failed/.test(await page.locator("#fatigueRows tr").nth(2).innerText()), "a failed position is marked as failed");
    check(/2 of 2/.test(await page.locator("#fatigueStatus").innerText()), "the status shows the message of the analysis");
    check(await page.locator("#fatigueRun").isEnabled(), "the run button is enabled again after the analysis");
    await page.close();

    // Case 2: no results loaded: the dialog says so and the run button stays disabled.
    const empty = await browser.newPage();
    await empty.addInitScript(stubScript(false), false);
    await empty.goto(url);
    await empty.waitForTimeout(500);
    await empty.evaluate(() => openFatigueDialog());
    await empty.waitForTimeout(200);
    check(await empty.locator("#fatigueRun").isDisabled(), "without results the run button is disabled");
    check(/Load the solver results/.test(await empty.locator("#fatigueStatus").innerText()),
      "without results the dialog asks to load them");
    await empty.close();

    console.log("gui_fatigue_smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
