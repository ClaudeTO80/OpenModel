/**
 * @brief Smoke test of the material per part list and the Material branch of the sidebar (no backend needed).
 *
 * A stub bridge echoes the script it is sent, so the test checks the text the GUI writes:
 * one assign line per part, the default material removes it, and the tree lists the parts.
 *
 * Usage: node tests/webgl/gui_part_materials_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_part_materials_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    // Stub bridge: analysis_load echoes the script it receives; other calls report no backend.
    await page.addInitScript(() => {
      window.__calls = [];
      window.webui = {
        isConnected: () => true,
        call: async (name, text) => {
          window.__calls.push(name);
          if (name === "openmodel_analysis_load") return JSON.stringify({ ok: true, message: text });
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(500);
    // Fake model: two volume parts and a surface block (not a part).
    await page.evaluate(() => {
      viewerModel = () => ({ meta: { groups: [], blocks: [
        { name: "Housing", dimension: 3, elements: 100 },
        { name: "Insert", dimension: 3, elements: 40 },
        { name: "Skin", dimension: 2, elements: 8 },
      ] } });
      $("anScript").value = "material id=STEEL E=210000 nu=0.3 rho=7.85e-9 name=STEEL\nmaterial id=ALU E=70000 nu=0.33 rho=2.7e-9 name=ALU\nfix all";
      renderAnalysisConditions();
    });

    await page.evaluate(() => focusSection("matPartsSection"));   // open the Material panel, as the user would
    const selects = page.locator("#condParts select");
    check((await selects.count()) === 2, "one material choice per volume part (the surface block is not a part)");
    check((await selects.nth(0).inputValue()) === "STEEL", "a part without assign takes the default material");

    await selects.nth(0).selectOption("ALU");
    await page.waitForTimeout(200);
    let script = await page.locator("#anScript").inputValue();
    check(/^assign Housing material=ALU$/m.test(script), "choosing ALU writes an assign line for Housing");
    check((await page.locator('#sbTreeBody').textContent()).includes("Housing : ALU"), "the tree shows Housing with ALU");

    await selects.nth(0).selectOption("STEEL");
    await page.waitForTimeout(200);
    script = await page.locator("#anScript").inputValue();
    check(!/assign Housing/.test(script), "choosing the default material removes the assign line");

    await selects.nth(1).selectOption("ALU");
    await page.waitForTimeout(200);
    await page.evaluate((text) => { const t = $("anScript"); t.value = text; t.dispatchEvent(new Event("input")); }, script + "\nassign Insert material=ALU");
    await page.waitForTimeout(200);
    check((await selects.nth(1).inputValue()) === "ALU", "the list follows an assign line typed in the script");

    // The form still shows the default material, not the last material line.
    check((await page.locator("#matId").inputValue()) === "STEEL", "the material form shows the default material");

    const calls = await page.evaluate(() => window.__calls.filter((c) => c === "openmodel_analysis_load").length);
    check(calls >= 2, "each choice reloads the script in the backend");
  } finally {
    await browser.close();
  }
  console.log("gui part materials smoke: all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
