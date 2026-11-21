/**
 * @brief Smoke test of the Constraints branch of the sidebar tree and of the Selection ribbon tab order.
 *
 * Checks that the tab Selection comes before Analysis, that the Constraints branch lists the fix lines of the script,
 * that a constraint can be renamed by double click (the name becomes the id= of its fix line), that the + button of
 * Constraints asks for a selection, and that the symbol size field exists.
 *
 * Usage: node tests/webgl/gui_constraints_tree_smoke.js <url of gui.html>
 * Serve src/webui over HTTP first (with viewer.html); needs the `playwright` package (NODE_PATH) and a Chromium.
 *
 * @param argv[2] URL of gui.html
 */
const { chromium } = require("playwright");

/** @brief Throws with a message when a check fails, logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_constraints_tree_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.addInitScript(() => {
      window.webui = { isConnected: () => true, call: async (name) => {
        if (name === "openmodel_about") return JSON.stringify({ ok: true, version: "0.1.5" });
        return JSON.stringify({ ok: false, message: "stub" });
      } };
    });
    await page.goto(url);
    await page.waitForTimeout(500);

    const tabs = await page.evaluate(() => [...document.querySelectorAll("#ribbon .tab")].map((t) => t.dataset.tab));
    check(tabs.indexOf("sel") === tabs.indexOf("mesh") + 1 && tabs.indexOf("sel") < tabs.indexOf("solve"), "the Selection tab comes before Analysis");

    // Two fix lines: one named by id=, one without (the solver gives it a generated id).
    await page.evaluate(() => {
      $("anScript").value = "fix plane:x=0 id=wall\nfix plane:x=10 dof=1\n";
      renderTree();
    });
    const rows = await page.evaluate(() => {
      const row = [...document.querySelectorAll(".sbrow")].find((r) => r.querySelector(".sbname")?.textContent === "Constraints");
      return { caret: !!row && !!row.querySelector(".sbcaret"), names: [...document.querySelectorAll(".sbrow .sbname")].map((n) => n.textContent) };
    });
    check(rows.caret, "the Constraints branch has children (caret) when the script has fix lines");
    check(rows.names.includes("wall") && rows.names.some((n) => n.startsWith("fix-")), "the children are the fix lines, named by their ids");

    // Expand, then rename "wall" by double click.
    await page.evaluate(() => {
      const row = [...document.querySelectorAll(".sbrow")].find((r) => r.querySelector(".sbname")?.textContent === "Constraints");
      row.querySelector(".sbcaret").click();
    });
    await page.locator(".sbname", { hasText: /^wall$/ }).dblclick();
    await page.locator("input.sbedit").fill("inlet");
    await page.keyboard.press("Enter");
    await page.waitForTimeout(100);
    const script = await page.evaluate(() => $("anScript").value);
    check(/^fix plane:x=0 id=inlet$/m.test(script), "renaming writes id=inlet on the fix line");
    const names2 = await page.evaluate(() => [...document.querySelectorAll(".sbrow .sbname")].map((n) => n.textContent));
    check(names2.includes("inlet") && !names2.includes("wall"), "the tree shows the new name");

    // An invalid name is refused and the script is unchanged.
    await page.locator(".sbname", { hasText: /^inlet$/ }).dblclick();
    await page.locator("input.sbedit").fill("bad name");
    await page.keyboard.press("Enter");
    await page.waitForTimeout(100);
    const script2 = await page.evaluate(() => $("anScript").value);
    check(script2 === script, "an invalid name leaves the script unchanged");

    // The + of Constraints starts the Fix flow, which needs a selection.
    await page.evaluate(() => {
      const row = [...document.querySelectorAll(".sbrow")].find((r) => r.querySelector(".sbname")?.textContent === "Constraints");
      [...row.querySelectorAll("button")].find((b) => b.textContent.trim() === "+").click();
    });
    await page.waitForTimeout(150);
    const msg = await page.evaluate(() => $("result").textContent);
    check(/Select nodes, elements or faces/.test(msg), "+ on Constraints asks for a selection when none is made");

    // Constraints stored in the mesh (Constrain) appear as one child per class of degrees of freedom.
    await page.evaluate(() => renderConstraints([{dofs: "ux,uy,uz", mask: 7, nodes: 12, values: [0, 0, 0, 0, 0, 0]}]));
    await page.waitForTimeout(100);
    const meshRow = await page.evaluate(() => [...document.querySelectorAll(".sbrow .sbname")].map((n) => n.textContent).includes("mesh · ux,uy,uz"));
    check(meshRow, "a mesh constraint class is a child of Constraints");

    check(!!(await page.$("#cdSymbolSize")), "the constraint symbol size field is in the Constraints panel");
    check(errors.length === 0, "no page errors (" + errors.join(" | ") + ")");
    console.log("gui constraints tree smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
