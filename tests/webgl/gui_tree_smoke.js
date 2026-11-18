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
    check(cats.join() === "cad,mesh,mat,cons,loads", "five categories in tree order");
    check((await page.locator("#sbTreeBody .sbadd").count()) === 5, "one '+' per category");
    const catNode = (key) => page.locator("#sbTreeBody > .sbgroup", { has: page.locator(`.sbname[data-cat="${key}"]`) }).first();

    const kids = catNode("mesh").locator(":scope > .sbkids");
    check(await kids.isVisible(), "mesh children shown by default");
    await catNode("mesh").locator(":scope > .sbrow > .sbcaret").click();
    check(!(await kids.isVisible()), "mesh collapses on caret click");
    await page.reload(); await page.waitForTimeout(800);
    check(!(await catNode("mesh").locator(":scope > .sbkids").isVisible()), "collapsed state kept after reload");

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
    await page.locator("#sbTreeBody .sbadd").nth(2).click();
    check(await page.locator('.spanel[data-spanel="mat"]').isVisible(), "'+' on Material opens the material panel");
  } finally {
    await browser.close();
  }
  console.log("gui tree smoke: all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
