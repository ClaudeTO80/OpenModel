/**
 * @brief Smoke test of the workbench layout: the Selection category shown in the sidebar only when requested, and the status of the model tree.
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

    // Selection is hidden until requested: no Selection row in the tree, and its controls sit in a hidden sidebar panel.
    const before = await page.evaluate(() => {
      const sel = document.getElementById("selMode");
      const panel = document.querySelector('.spanel[data-spanel="sel"]');
      return {
        noOverlay: !document.getElementById("selectionOverlay"),
        inPanel: !!panel && panel.contains(sel) && panel.closest("#inspBody") !== null,
        panelHidden: !!panel && panel.hidden,
        rows: [...document.querySelectorAll(".sbrow")].map((row) => row.querySelector(".sbname")?.textContent),
      };
    });
    check(before.noOverlay && before.inPanel, "the Selection controls are in the sidebar panel, not in an overlay over the viewer");
    check(!before.rows.includes("Selection"), "the Selection category is not in the tree until it is requested");

    // Requested (ribbon Selection > Options): the category appears and its panel opens on the left.
    await page.evaluate(() => openSelectionPanel());
    await page.waitForTimeout(200);
    const after = await page.evaluate(() => {
      const insp = document.getElementById("inspector");
      const r = insp.getBoundingClientRect(), m = document.querySelector("main").getBoundingClientRect();
      return {
        open: !insp.hidden,
        onLeft: r.left - m.left < 5 && r.width < m.width,
        hasSel: document.getElementById("inspBody").contains(document.getElementById("selMode")),
        rows: [...document.querySelectorAll(".sbrow")].map((row) => [row.querySelector(".sbname")?.textContent, row.querySelector(".sbsub")?.textContent]),
      };
    });
    check(after.open && after.onLeft && after.hasSel, "requested, the Selection options open on the left, in the sidebar details");
    check(after.rows.some(([n]) => n === "Selection"), "requested, the Selection category appears in the tree");
    const layout = { mesh: after.rows };
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
