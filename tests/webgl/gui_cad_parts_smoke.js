/**
 * @brief Smoke test of the CAD tree: checking parts to mesh (no backend needed).
 *
 * A stub bridge answers openmodel_cad_components with a small assembly: Frame holds Bracket and Shaft, and Plate is
 * apart. The test checks that checking a parent checks its whole subtree, that unchecking a child unchecks its
 * ancestors, and which solid ids the selection gives.
 *
 * Usage: node tests/webgl/gui_cad_parts_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_cad_parts_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    await page.addInitScript(() => {
      window.__calls = [];
      window.__generated = [];
      window.__components = [
        { id: 0, parent: -1, name: "Frame", path: "Frame", isAssembly: true, solidIds: [0, 1] },
        { id: 1, parent: 0, name: "Bracket", path: "Frame/Bracket", isAssembly: false, solidIds: [0] },
        { id: 2, parent: 0, name: "Shaft", path: "Frame/Shaft", isAssembly: false, solidIds: [1] },
        { id: 3, parent: -1, name: "Plate", path: "Plate", isAssembly: false, solidIds: [2] },
      ];
      window.webui = {
        isConnected: () => true,
        call: async (name, arg) => {
          window.__calls.push(name);
          if (name === "openmodel_cad_components") return JSON.stringify({ ok: true, message: "", components: window.__components });
          if (name === "openmodel_generate_volume") {
            window.__generated.push(JSON.parse(arg));
            return JSON.stringify({ ok: true, message: "" });
          }
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(400);
    await page.evaluate(() => refreshCadComponents());
    await page.waitForTimeout(200);

    const row = (text) => page.locator(".sbrow", { has: page.locator(".sbname", { hasText: new RegExp("^" + text + "$") }) }).first();
    const openCad = async () => page.evaluate(() => {
      const cat = document.querySelector('.sbname[data-cat="cad"]');
      const c = cat && cat.parentElement.querySelector(".sbcaret");
      if (c && c.getAttribute("aria-expanded") !== "true") c.click();
    });
    await openCad();
    await page.waitForTimeout(100);
    check((await row("Frame").count()) === 1 && (await row("Plate").count()) === 1, "the CAD tree lists the roots of the assembly");
    check((await page.evaluate(() => selectedSolidIds().length)) === 0, "nothing is checked at the start");

    // Open Frame so its parts are visible.
    await row("Frame").locator(".sbcaret").click();
    await page.waitForTimeout(100);
    check((await row("Bracket").count()) === 1 && (await row("Shaft").count()) === 1, "the parts of Frame are listed below it");

    // Checking the parent checks all of its subtree.
    await row("Frame").locator(".sbcheck").check();
    await page.waitForTimeout(100);
    check(await row("Bracket").locator(".sbcheck").isChecked() && await row("Shaft").locator(".sbcheck").isChecked(),
      "checking Frame checks Bracket and Shaft");
    check(JSON.stringify(await page.evaluate(() => selectedSolidIds())) === "[0,1]", "the selection of Frame gives its solids 0 and 1");

    // Unchecking one child unchecks the parent, and the solids follow.
    await row("Bracket").locator(".sbcheck").uncheck();
    await page.waitForTimeout(100);
    check(!(await row("Frame").locator(".sbcheck").isChecked()), "unchecking Bracket unchecks Frame");
    check(JSON.stringify(await page.evaluate(() => selectedSolidIds())) === "[1]", "the selection then holds only solid 1");

    // Another root is checked on its own.
    await row("Plate").locator(".sbcheck").check();
    await page.waitForTimeout(100);
    check(JSON.stringify(await page.evaluate(() => selectedSolidIds())) === "[1,2]", "Plate adds solid 2 to the selection");

    // Mesh checked parts: one generate request per checked solid, each named after its component.
    await page.evaluate(() => {
      const panel = document.getElementById("btnMeshParts").closest(".spanel");
      if (panel) showSidebarTab(panel.dataset.spanel);
      for (let el = document.getElementById("btnMeshParts"); el; el = el.parentElement) if (el.tagName === "DETAILS") el.open = true;
    });
    await page.locator("#btnMeshParts").click();
    await page.waitForTimeout(300);
    const sent = await page.evaluate(() => window.__generated.map((r) => [r.solids, r.solidName]));
    check(JSON.stringify(sent) === '[["1","Shaft"],["2","Plate"]]', "each checked part is meshed alone, named after its component");
    check((await page.evaluate(() => window.__generated[0].mesher)) !== undefined, "the request carries the parameters of the form");

    const calls = await page.evaluate(() => window.__calls.filter((c) => c === "openmodel_cad_components").length);
    check(calls >= 1, "the components are asked from the backend");
  } finally {
    await browser.close();
  }
  console.log("gui cad parts smoke: all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
