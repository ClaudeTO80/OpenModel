/**
 * @brief Smoke test of the workbench layout: the Selection card over the viewer and the status of the model tree.
 *
 * A stub bridge answers one shown mesh run. The test checks that the Selection controls moved from the sidebar into
 * the card over the viewer (same ids, still working) and that each tree category shows its state.
 *
 * Usage: node tests/webgl/gui_workbench_smoke.js <url of gui.html>
 * Serve src/webui over HTTP first; needs the `playwright` package (NODE_PATH) and a Chromium.
 *
 * @param argv[2] URL of gui.html
 */
const { chromium } = require("playwright");

/** @brief Throws with a message when a check fails, logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_workbench_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.addInitScript(() => {
      window.webui = { isConnected: () => true, call: async (name) => {
        if (name === "openmodel_mesh_runs") return JSON.stringify({ ok: true, runs: [
          { index: 0, name: "bracket", nodes: 61436, elements: 194878, shown: true, part: "", groups: [] }] });
        if (name === "openmodel_about") return JSON.stringify({ ok: true, version: "0.1.5" });
        return JSON.stringify({ ok: false, message: "stub" });
      } };
    });
    await page.goto(url);
    await page.evaluate(() => refreshMeshRuns());
    await page.waitForTimeout(800);

    const layout = await page.evaluate(() => {
      const card = document.getElementById("selectionOverlay");
      const sel = document.getElementById("selMode");
      const inCard = !!card && !!sel && card.contains(sel);
      const sidebar = document.querySelector("aside");
      return {
        inMain: !!card && card.parentElement.tagName === "MAIN",
        inCard,
        outsideSidebar: !!sidebar && !sidebar.contains(sel),
        mesh: [...document.querySelectorAll(".sbrow")].map((row) => [row.querySelector(".sbname")?.textContent, row.querySelector(".sbsub")?.textContent]),
      };
    });
    check(layout.inMain, "the Selection card sits over the viewer, inside the main area");
    check(layout.inCard && layout.outsideSidebar, "the Selection controls moved out of the sidebar into the card");
    const byName = Object.fromEntries(layout.mesh.filter(([n]) => n));
    check(byName["Mesh"] === "1 · 61,436 nodes", "the Mesh category shows its mesh count and nodes");
    check(byName["Constraints"] === "none" && byName["Jobs"] === "none", "empty categories say none");
    check(byName["CAD"] === "none", "CAD says none before an import");

    // Without a model the picking control stays disabled, as before the move; its value is the initial one.
    const picking = await page.evaluate(() => ({ value: document.getElementById("selMode").value, disabled: document.getElementById("selMode").disabled }));
    check(picking.value === "off" && picking.disabled, "the moved Selection control keeps its initial state (off, disabled without a model)");

    check(errors.length === 0, "no page errors (" + errors.join(" | ") + ")");
    console.log("gui_workbench_smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message || e); process.exit(1); });
