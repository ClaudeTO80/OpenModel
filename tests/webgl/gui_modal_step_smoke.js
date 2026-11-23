/**
 * @brief Regression test: the Analysis panel can set a frequency (modal) step.
 *
 * Choosing "Frequency" shows the number-of-modes field, and "Set step in script" writes `step frequency modes=N`
 * (replacing the existing step line). An out-of-range or non-integer mode count is refused before the script changes.
 *
 * Usage: node tests/webgl/gui_modal_step_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_modal_step_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    // Stub bridge: analysis_load echoes the script it receives, so the script textarea is the observable result.
    await page.addInitScript(() => {
      window.webui = {
        isConnected: () => true,
        call: async (name, text) => {
          if (name === "openmodel_analysis_load") return JSON.stringify({ ok: true, message: text });
          return JSON.stringify({ ok: true, message: "" });
        },
      };
    });
    await page.goto(url);

    // The Step controls live in the "loads" details panel, which the app shows through its own tree navigation
    // (showSidebarTab). Open that panel the same way, then open the Step <details>.
    await page.evaluate(() => {
      showSidebarTab("loads");
      for (let n = document.getElementById("asStepKind"); n; n = n.parentElement) if (n.tagName === "DETAILS") n.open = true;
    });
    const kind = page.locator("#asStepKind");
    const modesBox = page.locator("#asModeFields");
    const modes = page.locator("#asModes");
    const script = page.locator("#anScript");

    // The frequency option exists and reveals the mode count only for itself.
    check(await kind.locator('option[value="frequency"]').count() === 1, "Analysis selector offers Frequency");
    await kind.selectOption("static");
    check(!(await modesBox.isVisible()), "mode count hidden for a static step");
    await kind.selectOption("frequency");
    check(await modesBox.isVisible(), "mode count shown for a frequency step");
    await kind.selectOption("dynamic");
    check(!(await modesBox.isVisible()), "mode count hidden again for a dynamic step");

    // A valid count writes the step line, replacing the existing one.
    await script.fill("material E=210000 nu=0.3 rho=7.85e-9\nfix plane:x=0\nstep static\n");
    await kind.selectOption("frequency");
    await modes.fill("8");
    await page.evaluate(() => setStep());
    const afterValid = await script.inputValue();
    check(afterValid.includes("step frequency modes=8"), "valid count writes 'step frequency modes=8'");
    check(!afterValid.includes("step static"), "the previous step line is replaced, not duplicated");

    // Invalid counts are refused and leave the script unchanged.
    for (const bad of ["0", "1001", "2.5", ""]) {
      await script.fill("material E=210000 nu=0.3 rho=7.85e-9\nfix plane:x=0\nstep static\n");
      const before = await script.inputValue();
      await modes.fill(bad);
      await page.evaluate(() => setStep());
      check((await script.inputValue()) === before, "mode count '" + bad + "' is refused, script unchanged");
    }
  } finally {
    await browser.close();
  }
  console.log("all modal step checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
