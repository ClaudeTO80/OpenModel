/**
 * @brief Smoke test of the Mesh branch: selecting the mesh to show and removing one from the session (no backend needed).
 *
 * A stub bridge keeps the list of runs in the page: openmodel_mesh_runs lists them, openmodel_mesh_select marks one
 * as shown, openmodel_mesh_remove deletes one. The test checks the rows and the calls the page makes.
 *
 * Usage: node tests/webgl/gui_mesh_runs_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_mesh_runs_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    await page.addInitScript(() => {
      window.__calls = [];
      window.__runs = [
        { index: 0, name: "Bracket, 2 mm", nodes: 120, elements: 300, shown: true, groups: ["Bracket"] },
        { index: 1, name: "Shaft, 1 mm", nodes: 80, elements: 190, shown: false, groups: ["Shaft"] },
      ];
      window.webui = {
        isConnected: () => true,
        call: async (name, arg) => {
          window.__calls.push([name, arg]);
          const runs = window.__runs;
          if (name === "openmodel_mesh_runs") return JSON.stringify({ ok: true, message: "", runs });
          if (name === "openmodel_mesh_select") {
            const i = Number(arg);
            runs.forEach((r) => { r.shown = r.index === i; });
            return JSON.stringify({ ok: true, message: "" });
          }
          if (name === "openmodel_mesh_remove") {
            const i = Number(arg);
            const gone = runs.findIndex((r) => r.index === i);
            if (gone >= 0) runs.splice(gone, 1);
            runs.forEach((r, k) => { r.index = k; });
            return JSON.stringify({ ok: true, message: "" });
          }
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(400);

    // Opens a closed tree node by its label, as the user does with its caret.
    const openNode = async (label) => {
      await page.evaluate((text) => {
        const name = [...document.querySelectorAll(".sbname")].find((n) => n.textContent.trim() === text);
        const caret = name && name.parentElement.querySelector(".sbcaret");
        if (caret && caret.getAttribute("aria-expanded") !== "true") caret.click();
      }, label);
      await page.waitForTimeout(100);
    };

    // Open the Mesh branch, its Groups node and the parts, then load the runs.
    await page.evaluate(() => refreshMeshRuns());
    await page.waitForTimeout(200);
    await openNode("Mesh");
    await openNode("Groups");
    await openNode("Bracket");
    await openNode("Shaft");

    const rowOf = (text) => page.locator(".sbrow").filter({ hasText: text }).first();
    check((await rowOf("Bracket, 2 mm").count()) === 1, "the session meshes are listed under Mesh");
    check((await rowOf("Shaft, 1 mm").count()) === 1, "the second mesh is listed");
    const underPart = await page.evaluate(() => {
      const kids = (part) => [...document.querySelectorAll(".sbgroup")]
        .find((g) => g.firstElementChild && g.firstElementChild.querySelector(".sbname")?.textContent.trim() === part);
      return { bracket: kids("Bracket")?.textContent, shaft: kids("Shaft")?.textContent };
    });
    check(underPart.bracket.includes("Bracket, 2 mm") && !underPart.bracket.includes("Shaft, 1 mm"),
      "each mesh is attributed to its group under Groups");
    check(underPart.shaft.includes("Shaft, 1 mm"), "the Shaft group lists its mesh");
    const meshDirect = await page.evaluate(() => {
      const cat = document.querySelector('.sbname[data-cat="mesh"]');
      const box = cat.closest(".sbgroup").querySelector(":scope > .sbkids");
      return [...box.querySelectorAll(":scope > .sbgroup > .sbrow > .sbname")].map((n) => n.textContent.trim());
    });
    check(JSON.stringify(meshDirect) === '["Groups"]', "Mesh holds only Groups, no mesh row at its top level");
    check((await rowOf("Bracket, 2 mm").locator(".sbeye").textContent()).includes("👁"), "the shown mesh has an open eye");

    // Show the other mesh.
    await rowOf("Shaft, 1 mm").locator(".sbeye").click();
    await page.waitForTimeout(300);
    let calls = await page.evaluate(() => window.__calls.filter((c) => c[0] === "openmodel_mesh_select"));
    check(calls.length === 1 && calls[0][1] === "1", "the eye of Shaft asks the backend to show mesh 1");
    check((await rowOf("Shaft, 1 mm").locator(".sbeye").textContent()).includes("👁"), "Shaft is now the shown mesh");

    // Remove the first mesh.
    await rowOf("Bracket, 2 mm").locator(".sbdel").click();
    await page.waitForTimeout(300);
    calls = await page.evaluate(() => window.__calls.filter((c) => c[0] === "openmodel_mesh_remove"));
    check(calls.length === 1 && calls[0][1] === "0", "the × of Bracket asks the backend to remove mesh 0");
    check((await rowOf("Bracket, 2 mm").count()) === 0, "the removed mesh is no longer listed");
    check((await rowOf("Shaft, 1 mm").count()) === 1, "the other mesh stays");
  } finally {
    await browser.close();
  }
  console.log("gui mesh runs smoke: all checks passed");
})().catch((e) => { console.error(e.message); process.exit(1); });
