/**
 * @brief Smoke test of the single model tree in the GUI sidebar (no backend needed).
 *
 * Checks the category nodes, that a node collapses and its state survives a reload,
 * that a viewer eye toggles its flag, and that "+" opens the right panel.
 *
 * Usage: node tests/webgl/gui_tree_smoke.js <url of gui.html>
 * The page must be served over HTTP: from file:// the viewer iframe throws cross-origin errors.
 * Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_tree_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    await page.goto(url);
    await page.waitForTimeout(800);
    const cats = await page.$$eval("#sbTreeBody > .sbgroup > .sbrow > .sbname[data-cat]", (n) => n.map((x) => x.dataset.cat));
    check(cats.join() === "cad,mesh,mat,cons,loads,jobs", "six categories in tree order");
    check((await page.locator("#sbTreeBody .sbadd").count()) === 6, "one '+' per category");
    // Details overlay: closed on load, opened by a category name, closed by × and by Escape.
    check(!(await page.locator("#inspector").isVisible()), "details overlay closed on load");
    check((await page.locator("aside .spanel").count()) === 0, "details are not under the tree");
    await page.locator('#sbTreeBody .sbname[data-cat="loads"]').click();
    check(await page.locator("#inspector").isVisible(), "clicking a category opens the overlay");
    check((await page.locator("#inspTitle").textContent()) === "Loads", "overlay title is the category");
    await page.locator("#inspClose").click();
    check(!(await page.locator("#inspector").isVisible()), "× closes the overlay");
    await page.locator('#sbTreeBody .sbname[data-cat="cons"]').click();
    await page.keyboard.press("Escape");
    check(!(await page.locator("#inspector").isVisible()), "Escape closes the overlay");
    const catNode = (key) => page.locator("#sbTreeBody > .sbgroup", { has: page.locator(`.sbname[data-cat="${key}"]`) }).first();
    /** Opens a category node if it is closed, so that its children can be clicked. */
    const openNode = async (key) => {
      if (!(await catNode(key).locator(":scope > .sbkids").isVisible())) await catNode(key).locator(":scope > .sbrow > .sbcaret").click();
    };

    // Constraints needs a fix line in the script to have children to open.
    await page.evaluate(() => {
      document.getElementById("anScript").value = "fix plane:x=0\n";
      renderTree();
    });
    const kids = catNode("cons").locator(":scope > .sbkids");
    check(!(await kids.isVisible()), "every node starts closed");
    await catNode("cons").locator(":scope > .sbrow > .sbcaret").click();
    check(await kids.isVisible(), "constraints opens on caret click");
    await page.reload(); await page.waitForTimeout(800);
    await page.evaluate(() => { document.getElementById("anScript").value = "fix plane:x=0\n"; renderTree(); });
    check(!(await catNode("cons").locator(":scope > .sbkids").isVisible()), "closed again after reload (nothing is kept)");

    const eye = () => catNode("cons").locator(":scope > .sbrow > .sbeye");
    check((await eye().getAttribute("aria-pressed")) === "true", "constraints eye on by default");
    await eye().click();
    check((await eye().getAttribute("aria-pressed")) === "false", "constraints eye toggles off");

    // Material eyes: with a fake model in the viewer metadata, one row per material hides its groups.
    await page.evaluate(() => {
      viewerModel = () => ({ meta: { groups: [
        { name: "Bracket", dimension: 3, tag: 1, material: "steel" },
        { name: "Bolt", dimension: 3, tag: 2, material: "steel" },
        { name: "Plate", dimension: 3, tag: 3, material: "alu" },
        { name: "Free", dimension: 3, tag: 4, material: "" },
      ], blocks: [] } });
      renderTree();
    });
    const matRow = (mat) => catNode("mat").locator(".sbrow").filter({ has: page.locator(".sbname", { hasText: new RegExp("^" + mat + "$") }) });
    check((await matRow("steel").count()) === 1 && (await matRow("alu").count()) === 1, "one row per material, none for groups without one");
    // Scoped to the Material node: the Mesh node lists every group, including "Free".
    const matNode = catNode("mat");
    check((await matNode.locator(".sbrow").filter({ has: page.locator(".sbname", { hasText: /^Free$/ }) }).count()) === 0, "group without material has no material row");
    await openNode("mat");
    await matRow("steel").locator(".sbeye").click();
    const hidden = await page.evaluate(() => [...viewerVis.hidden].sort().join());
    check(hidden === "Bolt,Bracket", "steel eye hides the groups of steel only");
    // CAD eye: hides the surface blocks (CAD geometry) and leaves the volume block alone.
    await page.evaluate(() => {
      // A fake viewer in the iframe window: the GUI reads its state and records what it is told to draw.
      const frame = document.getElementById("viewer").contentWindow;
      frame.OpenModelViewer = {
        state: { model: { meta: { groups: [], blocks: [
          { name: "Surface", dimension: 2, elements: 10 },
          { name: "Volume", dimension: 3, elements: 50 },
          { name: "Face", dimension: 2, elements: 4 },
        ] } }, vis: new Uint32Array([1, 1, 1]), gvis: new Uint32Array(1).fill(1) },
        set: (o) => { window.__lastSet = o; },
      };
      viewerModel = () => frame.OpenModelViewer.state.model;  // undo the material fake above
      viewerVis.geometry = true;
      renderTree();
    });
    const cadEye = () => catNode("cad").locator(":scope > .sbrow > .sbeye");
    check((await cadEye().getAttribute("aria-pressed")) === "true", "CAD eye on by default");
    await cadEye().click();
    check((await cadEye().getAttribute("aria-pressed")) === "false", "CAD eye toggles off");
    const blockOn = await page.evaluate(() => window.__lastSet.blockOn);
    check(JSON.stringify(blockOn) === JSON.stringify({ 0: false, 2: false }), "CAD eye hides the surface blocks only");
    await cadEye().click();
    const again = await page.evaluate(() => window.__lastSet.blockOn);
    check(JSON.stringify(again) === JSON.stringify({ 0: true, 2: true }), "CAD eye shows them again");
    // Script tree: jobs, steps and the loads each step uses, read the way the solver reads the script.
    const parsed = await page.evaluate(() => {
      const text = "fix plane:x=0\nforce all F=1,0,0  # first load\njob SETTLE\nstep static loads=force-1\nstep static reset\n";
      return parseScript(text);
    });
    check(JSON.stringify(parsed.fixes.map((f) => f.id)) === '["fix-1"]', "fix line gets a generated id");
    check(JSON.stringify(parsed.loads.map((l) => l.id)) === '["force-1"]', "force line gets a generated id");
    check(parsed.jobs.length === 1 && parsed.jobs[0].name === "SETTLE", "job line starts a job");
    check(JSON.stringify(parsed.jobs[0].steps.map((s) => [s.kind, s.loads, s.reset])) ===
      '[["static",["force-1"],false],["static",null,true]]', "steps keep their loads= and reset");
    await page.evaluate(() => {
      document.getElementById("anScript").value = "fix plane:x=0\nforce all F=1,0,0\njob SETTLE\nstep static loads=force-1\nstep static reset\n";
      renderTree();
    });
    // Mesh branch: the rows come from the list of the session that the backend sends (meshRuns).
    await page.evaluate(() => {
      meshRuns = [{index: 0, name: "Plate", nodes: 10, elements: 20, shown: true},
                  {index: 1, name: "Plate, coarse", nodes: 11, elements: 20, shown: false}];
      renderTree();
    });
    await catNode("mesh").locator(":scope > .sbrow > .sbcaret").click();
    // The runs have no physical group here, so they are listed under Groups: open it, then check the row.
    await catNode("mesh").locator(":scope > .sbkids > .sbgroup", { has: page.locator(".sbname", { hasText: /^Groups$/ }) })
      .locator(":scope > .sbrow > .sbcaret").click();
    check(await page.locator("#sbTreeBody").getByText("Plate, coarse · 11 nodes · 20 elements").isVisible(), "Mesh branch lists the session meshes under Groups");
    await catNode("jobs").locator(":scope > .sbrow > .sbcaret").click();
    check(await page.locator("#sbTreeBody").getByText("job SETTLE", { exact: true }).isVisible(), "job row shows under Jobs");

    await page.locator("#sbTreeBody .sbadd").nth(2).click();
    check(await page.locator('.spanel[data-spanel="mat"]').isVisible(), "'+' on Material opens the material panel");
    const flush = await page.evaluate(() => {
      const r = document.getElementById("inspector").getBoundingClientRect();
      const a = document.querySelector("aside").getBoundingClientRect();
      return Math.abs(r.left - a.right) < 1 && r.left >= a.right;
    });
    check(flush, "the material panel sits on the left, against the sidebar");
  } finally {
    await browser.close();
  }
  console.log("gui tree smoke: all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
