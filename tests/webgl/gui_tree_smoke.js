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

    await page.locator("#sbTreeBody .sbadd").nth(2).click();
    check(await page.locator('.spanel[data-spanel="mat"]').isVisible(), "'+' on Material opens the material panel");
  } finally {
    await browser.close();
  }
  console.log("gui tree smoke: all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
