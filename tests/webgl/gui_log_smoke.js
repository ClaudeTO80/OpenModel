/**
 * @brief Smoke test of the Log section of the workbench (no backend needed).
 *
 * A stub bridge answers openmodel_log_since with four entries, one per level. The test checks that the page polls
 * with the last sequence number it has read, that the filter shows the levels up to the one chosen, and that Clear
 * empties the list without showing the same entries again.
 *
 * Usage: node tests/webgl/gui_log_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_log_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.addInitScript(() => {
      window.__logCalls = [];
      window.__logEntries = [
        { seq: 1, level: "error", source: "Gmsh", time: Date.now(), text: "Mesh generation failed" },
        { seq: 2, level: "warning", source: "OpenModel", time: Date.now(), text: "Surface 4 has no interior points" },
        { seq: 3, level: "info", source: "OpenModel", time: Date.now(), text: "Meshing in 3D" },
        { seq: 4, level: "debug", source: "Gmsh", time: Date.now(), text: "tolerance 1e-8" },
      ];
      window.webui = {
        isConnected: () => true,
        call: async (name, arg) => {
          if (name === "openmodel_log_since") {
            window.__logCalls.push(arg);
            const after = Number(arg) || 0;
            const entries = window.__logEntries.filter((entry) => entry.seq > after);
            return JSON.stringify({ ok: true, last: 4, entries });
          }
          if (name === "openmodel_about") return JSON.stringify({ ok: true, version: "0.1.5" });
          if (name === "openmodel_mesh_runs") return JSON.stringify({ ok: true, runs: [
            { index: 0, name: "bracket", nodes: 61436, elements: 194878, shown: true, part: "", groups: [] }] });
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(1500);

    const lines = () => page.evaluate(() => [...document.querySelectorAll("#progressLog > div")].map((d) => d.textContent));
    const setFilter = (value) => page.evaluate((v) => { const s = document.getElementById("logFilter"); s.value = v; s.dispatchEvent(new Event("change")); }, value);

    // The page reads from the start, then from the last sequence number it has seen.
    const calls = await page.evaluate(() => window.__logCalls.slice());
    check(calls.length >= 1 && calls[0] === "0", "first poll asks for every entry (since 0)");
    check(calls.some((c) => c === "4"), "a later poll asks for the entries after 4");

    // Information is the default: errors, warnings and information are shown, debug is not.
    let shown = await lines();
    check(shown.length === 3, "default filter shows three lines (errors, warnings, information)");
    check(shown.some((t) => t.includes("Mesh generation failed")) && !shown.some((t) => t.includes("tolerance")),
      "the error is shown and the debug line is hidden");
    check(shown.every((t) => /\s(ERROR|WARNING|INFO)\s/.test(t)), "every line carries its level");

    await setFilter("debug");
    check((await lines()).length === 4, "filter Debug shows all four lines");

    await setFilter("error");
    check((await lines()).length === 1, "filter Errors shows only the error");

    await setFilter("info");
    await page.click("#dockLog button:text-is(\"Clear\")");
    check((await lines()).length === 0, "Clear empties the list");

    const before = await page.evaluate(() => window.__logCalls.length);
    await page.waitForTimeout(1500);
    const after = await page.evaluate((from) => window.__logCalls.slice(from), before);
    check(after.length >= 1 && after.every((c) => c === "4"), "polling after Clear asks only for new entries");
    check((await lines()).length === 0, "entries already cleared are not shown again");

    // The dock: the log is the default tab, the solver tab shows its own output, the toggle hides the body.
    const dockState = () => page.evaluate(() => ({
      logVisible: !document.getElementById("dockLog").hidden,
      solverVisible: !document.getElementById("dockSolver").hidden,
      collapsed: document.getElementById("dock").classList.contains("collapsed"),
    }));
    let dock = await dockState();
    check(dock.logVisible && !dock.solverVisible && !dock.collapsed, "the dock opens on the Log tab");
    await page.click("#tabSolver");
    dock = await dockState();
    check(dock.solverVisible && !dock.logVisible, "the Solver tab shows the solver output");
    await page.click("#tabLog");
    await page.click("#dockToggle");
    check((await dockState()).collapsed, "the toggle hides the body of the dock");
    await page.click("#statusMessage");
    check(!(await dockState()).collapsed, "a click on the status message opens the log again");

    // The status bar: the counts of the shown mesh, the last message whatever its level, the version.
    await page.evaluate(() => refreshMeshRuns());
    const mesh = await page.textContent("#statusMesh");
    check(mesh.includes("61,436 nodes") && mesh.includes("194,878 elements"), "the status bar shows the counts of the shown mesh");
    check((await page.textContent("#statusMessage")) === "tolerance 1e-8", "the status bar shows the last message (debug included)");
    check((await page.textContent("#statusVersion")) === "OpenModel 0.1.5", "the status bar shows the version");

    check(errors.length === 0, "no page errors (" + errors.join(" | ") + ")");
    console.log("gui_log_smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message || e); process.exit(1); });
