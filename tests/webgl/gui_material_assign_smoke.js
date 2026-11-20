/**
 * @brief Regression test: assigning a known material to a group when the script's default material has no id=.
 *
 * The first material line of the script is the default material and may have no id=. Assigning ABS (database id
 * "abs") must write a material line that carries id=abs and an assign line that refers to it, so the script parses.
 *
 * Usage: node tests/webgl/gui_material_assign_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_material_assign_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    // Stub bridge: analysis_load echoes the script it receives; the database holds ABS under the id "abs".
    await page.addInitScript(() => {
      window.webui = {
        isConnected: () => true,
        call: async (name, text) => {
          if (name === "openmodel_analysis_load") return JSON.stringify({ ok: true, message: text });
          if (name === "openmodel_material_list") return JSON.stringify({ ok: true, message: "", path: "test",
            classes: [{ key: "polymer", name: "Polymer" }],
            materials: [
              { id: "abs", name: "ABS", builtIn: true, className: "Polymer", classKey: "polymer", E: "2300000000", nu: "0.35", rho: "1050", note: "" },
            ] });
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(500);
    // Fake model: one volume part named part1; the script has a default material written without id=.
    await page.evaluate(() => {
      viewerModel = () => ({ meta: { groups: [
        { name: "part1", dimension: 3, tag: 1, material: "" },
      ], blocks: [
        { name: "part1", dimension: 3, elements: 100 },
      ] } });
      $("anScript").value = "material E=210000 nu=0.3 rho=7.85e-9 name=STEEL\nfix all";
      renderAnalysisConditions();
    });
    await page.evaluate(() => focusSection("matPartsSection"));
    await page.evaluate(() => selectMaterialGroup("part1"));
    await page.waitForTimeout(200);
    await page.locator("#matAssignSel").selectOption("abs");
    await page.locator("#btnMatAssign").click();
    await page.waitForTimeout(300);

    const script = await page.locator("#anScript").inputValue();
    check(/^material id=abs E=2300000000 nu=0\.35 rho=1050 name=abs$/m.test(script), "a material line with id=abs is written");
    check(/^assign part1 material=abs$/m.test(script), "the assign line refers to abs");
    check(/^material E=210000/m.test(script), "the existing default material line is kept unchanged");

    // A part whose name has blanks: its assign line is written in double quotes, and assigning again replaces it.
    await page.evaluate(() => {
      viewerModel = () => ({ meta: { groups: [
        { name: "part1", dimension: 3, tag: 1, material: "" },
        { name: "Housing B", dimension: 3, tag: 2, material: "" },
      ], blocks: [
        { name: "part1", dimension: 3, elements: 100 },
        { name: "Housing B", dimension: 3, elements: 50 },
      ] } });
      renderMaterialAssign();
    });
    await page.evaluate(() => selectMaterialGroup("Housing B"));
    await page.waitForTimeout(200);
    await page.locator("#matAssignSel").selectOption("abs");
    await page.locator("#btnMatAssign").click();
    await page.waitForTimeout(300);
    let blank = await page.locator("#anScript").inputValue();
    check(/^assign "Housing B" material=abs$/m.test(blank), "a part name with blanks is written in double quotes");
    check(/^assign part1 material=abs$/m.test(blank), "the other part keeps its assign line");
    await page.evaluate(() => selectMaterialGroup("Housing B"));
    await page.locator("#matAssignSel").selectOption("abs");
    await page.locator("#btnMatAssign").click();
    await page.waitForTimeout(300);
    blank = await page.locator("#anScript").inputValue();
    check((blank.match(/Housing B/g) || []).length === 1, "assigning again replaces the quoted assign line");
  } finally {
    await browser.close();
  }
  console.log("all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
