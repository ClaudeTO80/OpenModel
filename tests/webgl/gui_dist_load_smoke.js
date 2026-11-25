/**
 * @brief Smoke test of a distributed load in the GUI: listed under Loads, shown in the detail panel and edited there.
 *
 * Writes a `distributed` line and a step that uses it into the script, checks that the Loads list shows it as a
 * distributed load, opens its detail, changes its total force and checks that the line and the step follow.
 *
 * Usage: node tests/webgl/gui_dist_load_smoke.js <url of gui.html>
 * Serve src/webui over HTTP first; needs the `playwright` package (NODE_PATH) and a Chromium.
 *
 * @param argv[2] URL of gui.html
 */
const { chromium } = require("playwright");

/** @brief Throws with a message when a check fails, logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_dist_load_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    const errors = [];
    page.on("pageerror", (e) => errors.push(e.message));
    await page.addInitScript(() => {
      window.webui = { isConnected: () => true, call: async () => JSON.stringify({ ok: false, message: "stub" }) };
    });
    await page.goto(url);
    await page.waitForTimeout(500);

    const script = () => page.evaluate(() => $("anScript").value);
    await page.evaluate(() => {
      $("anScript").value = "material E=210000 nu=0.3\nfix plane:x=0\ndistributed id=shear group:Top F=10,0,-40\nstep static loads=shear\n";
      renderAnalysisConditions(); renderTree();
    });

    const listed = await page.evaluate(() => $("condLoads") ? $("condLoads").textContent : "");
    check(listed.includes("shear"), "the distributed load is listed under Loads");

    const parsedKind = await page.evaluate(() => scriptLoadEntries().find((e) => e.id === "shear") || null);
    check(parsedKind && parsedKind.kind === "distributed", "its kind is distributed");

    await page.evaluate(() => showLoadDetail(scriptLoadEntries().find((e) => e.id === "shear")));
    const detail = await page.evaluate(() => document.querySelector('#inspBody .spanel[data-spanel="loaddetail"]').textContent);
    check(detail.includes("Distributed on faces"), "the detail panel names it a distributed load");
    check(await page.evaluate(() => !!$("ldF")), "its total force is editable in the detail panel");

    await page.evaluate(() => { $("ldF").value = "5,5,-10"; });
    await page.evaluate(() => [...document.querySelectorAll('#inspBody [data-ld="apply"]')][0].click());
    await page.waitForTimeout(200);
    const after = await script();
    check(after.includes("distributed id=shear group:Top F=5,5,-10"), "applying the edit rewrites the distributed line");
    check(after.includes("step static loads=shear"), "the step keeps using the load");

    check(errors.length === 0, "no page errors (" + errors.join("; ") + ")");
    console.log("gui distributed load smoke: all checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
