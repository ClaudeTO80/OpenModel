/**
 * @brief Regression test: Remove on a condition of the analysis script edits the script, even when the backend holds no
 * analysis model (the example shown in the box is not loaded into the backend).
 *
 * Usage: node tests/webgl/gui_condition_remove_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_condition_remove_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    page.on("dialog", (d) => d.accept());
    // Stub bridge: the backend has no analysis model, so deleting by id reports that; loading echoes the script.
    await page.addInitScript(() => {
      window.webui = {
        isConnected: () => true,
        call: async (name, text) => {
          if (name === "openmodel_analysis_load") return JSON.stringify({ ok: true, message: text });
          if (name === "openmodel_analysis_delete") return JSON.stringify({ ok: false, message: "No analysis model loaded." });
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(500);
    await page.evaluate(() => {
      $("anScript").value = "material E=210000 nu=0.3 name=MATERIAL\nfix id=fix-1 plane:x=0 dof=all value=0,0,0\nstep static";
      renderAnalysisConditions();
    });
    await page.evaluate(() => focusSection("condFix"));   // shows the panel and opens its details, as the user would
    await page.locator("#condFix li", { hasText: "fix-1" }).getByRole("button", { name: "Remove" }).click();
    await page.waitForTimeout(300);
    const script = await page.locator("#anScript").inputValue();
    check(!/^fix\b/m.test(script), "the fix line is removed from the script");
    check(/^material E=210000/m.test(script) && /^step static$/m.test(script), "the other lines are kept");
  } finally {
    await browser.close();
  }
  console.log("all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
