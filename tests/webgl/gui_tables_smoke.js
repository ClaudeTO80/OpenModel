/**
 * @brief Smoke test of the table editor of the analysis GUI (gui.html).
 *
 * Checks the pure table rules (tableLineFromRows, tableUsers), the list of tables with Edit and Remove, the dialog
 * for a new table and for an edit (rename refused when used, duplicate names refused), removal refused while a table
 * is used, and an amplitude that takes its points from a table by name.
 *
 * Usage: node tests/webgl/gui_tables_smoke.js <url of gui.html>
 * The page must be served over HTTP. Needs the `playwright` package (NODE_PATH must resolve it) and a Chromium.
 *
 * @param argv[2] URL of gui.html, e.g. http://localhost:8080/gui.html
 */
const { chromium } = require("playwright");

/** @brief Stops the test with a message when a check fails and logs the passing ones. @param ok result of the check @param msg description */
function check(ok, msg) { if (!ok) throw new Error("check failed: " + msg); console.log("ok  " + msg); }

const SCRIPT = [
  "material E=210000 nu=0.3 rho=7.85e-9 name=STEEL",
  "fix all",
  "table name=T1 t=0,0,1,1,2,0.5",
  "table name=T2 t=0,0,2,1",
  "amplitude name=A1 t=T1",
  "job IMPACT amp=A1:T2",
  "step dynamic dt=0.01 period=1",
].join("\n");

(async () => {
  const url = process.argv[2];
  if (!url) throw new Error("usage: node gui_tables_smoke.js <url of gui.html>");
  const browser = await chromium.launch({ args: ["--no-sandbox"] });
  try {
    const page = await browser.newPage();
    page.on("dialog", (d) => d.accept());
    // Stub bridge: analysis_load echoes the script it receives, so the script text is the state under test.
    await page.addInitScript(() => {
      window.webui = {
        isConnected: () => true,
        call: async (name, text) => {
          if (name === "openmodel_analysis_load") return JSON.stringify({ ok: true, message: text });
          return JSON.stringify({ ok: false, message: "stub" });
        },
      };
    });
    await page.goto(url);
    await page.waitForTimeout(500);
    await page.evaluate((script) => {
      viewerModel = () => ({ meta: { groups: [{ name: "part1", dimension: 3, tag: 1, material: "" }],
        blocks: [{ name: "part1", dimension: 3, elements: 100 }] } });
      $("anScript").value = script;
      renderAnalysisConditions();
    }, SCRIPT);
    await page.evaluate(() => focusSection("asAmpName"));
    await page.waitForTimeout(200);

    // Pure rules.
    const rules = await page.evaluate(() => ({
      good: tableLineFromRows("T3", [["0", "0"], ["1", "1"]]),
      digitName: tableLineFromRows("1T", [["0", "0"], ["1", "1"]]),
      oneRow: tableLineFromRows("T3", [["0", "0"]]),
      notIncreasing: tableLineFromRows("T3", [["0", "0"], ["1", "1"], ["1", "2"]]),
      notNumber: tableLineFromRows("T3", [["0", "0"], ["x", "1"]]),
      empty: tableLineFromRows("T3", [["0", "0"], ["1", ""]]),
      users: tableUsers("T2", $("anScript").value),
      usersT1: tableUsers("T1", $("anScript").value),
      free: tableUsers("T9", $("anScript").value),
    }));
    check(rules.good.ok && rules.good.line === "table name=T3 t=0,0,1,1", "a valid table builds its line");
    check(!rules.digitName.ok, "a name starting with a digit is refused");
    check(!rules.oneRow.ok, "a table with one row is refused");
    check(!rules.notIncreasing.ok, "non-increasing times are refused");
    check(!rules.notNumber.ok, "a non-number is refused");
    check(!rules.empty.ok, "an empty factor is refused");
    check(JSON.stringify(rules.users) === "[6]", "T2 is used by the job override on line 6");
    check(JSON.stringify(rules.usersT1) === "[5]", "T1 is used by the amplitude on line 5");
    check(rules.free.length === 0, "an unused name has no users");

    // The list of tables.
    const listed = await page.locator("#condTables li code").allInnerTexts();
    check(listed.length === 2 && listed[0].includes("table name=T1") && listed[1].includes("table name=T2"),
      "both tables are listed in the Tables list");

    // Removal is refused while a table is used.
    await page.locator("#condTables li").filter({ hasText: "table name=T1" }).getByRole("button", { name: "Remove" }).click();
    await page.waitForTimeout(200);
    const refusal = await page.locator("#result").innerText();
    check(refusal.includes("T1") && refusal.includes("line 5"), "removing a used table is refused with the line that uses it");
    check((await page.locator("#anScript").inputValue()).includes("table name=T1 t=0,0,1,1,2,0.5"), "the refused removal keeps the table");

    // Edit T2: add a row through the dialog.
    await page.locator("#condTables li").filter({ hasText: "table name=T2" }).getByRole("button", { name: "Edit" }).click();
    await page.waitForSelector("#tableDialog[open]");
    check((await page.locator("#tableName").inputValue()) === "T2", "the edit dialog opens with the table name");
    check((await page.locator("#tableRows > div").count()) === 2, "the edit dialog shows the two rows of T2");
    await page.locator("#tableAddRow").click();
    const rows = page.locator("#tableRows > div");
    await rows.nth(2).locator("input").nth(0).fill("3");
    await rows.nth(2).locator("input").nth(1).fill("2");
    await page.waitForTimeout(100);
    check((await page.locator("#tableStatus").innerText()) === "Ready to apply.", "a valid edit shows Ready to apply");
    await page.locator("#tableApply").click();
    await page.waitForTimeout(300);
    const afterEdit = await page.locator("#anScript").inputValue();
    check(afterEdit.includes("table name=T2 t=0,0,2,1,3,2"), "applying the edit replaces the table line in place");
    check(/^job IMPACT amp=A1:T2$/m.test(afterEdit), "the job line is unchanged by the table edit");

    // A duplicate name and a rename of a used table are refused in the dialog.
    await page.getByRole("button", { name: "New table" }).click();
    await page.waitForSelector("#tableDialog[open]");
    await page.locator("#tableName").fill("T1");
    await page.waitForTimeout(100);
    check((await page.locator("#tableStatus").innerText()).includes("already exists"), "a duplicate name is refused");
    check(await page.locator("#tableApply").isDisabled(), "Apply is disabled for a duplicate name");
    await page.locator("#tableCancel").click();

    await page.locator("#condTables li").filter({ hasText: "table name=T1" }).getByRole("button", { name: "Edit" }).click();
    await page.waitForSelector("#tableDialog[open]");
    await page.locator("#tableName").fill("T1b");
    await page.waitForTimeout(100);
    check((await page.locator("#tableStatus").innerText()).includes("cannot be renamed"), "renaming a used table is refused");
    await page.locator("#tableCancel").click();

    // A new table, then an amplitude that takes it by name.
    await page.getByRole("button", { name: "New table" }).click();
    await page.waitForSelector("#tableDialog[open]");
    await page.locator("#tableName").fill("T3");
    await page.waitForTimeout(100);
    await page.locator("#tableApply").click();
    await page.waitForTimeout(300);
    check((await page.locator("#anScript").inputValue()).includes("table name=T3 t=0,0,1,1"), "a new table is appended to the script");

    await page.locator("#asAmpName").fill("A2");
    await page.locator("#asAmpPoints").fill("T9");
    await page.getByRole("button", { name: "Add amplitude to script" }).click();
    await page.waitForTimeout(200);
    check((await page.locator("#result").innerText()).includes("No table named 'T9'"), "an amplitude naming an unknown table is refused");
    await page.locator("#asAmpPoints").fill("T3");
    await page.getByRole("button", { name: "Add amplitude to script" }).click();
    await page.waitForTimeout(300);
    check((await page.locator("#anScript").inputValue()).includes("amplitude name=A2 t=T3"), "an amplitude takes the points of table T3 by name");

    // T3 is used by amplitude A2 now: its removal is refused, and a table nothing uses can be removed.
    await page.locator("#condTables li").filter({ hasText: "table name=T3" }).getByRole("button", { name: "Remove" }).click();
    await page.waitForTimeout(200);
    check((await page.locator("#anScript").inputValue()).includes("table name=T3 t=0,0,1,1"), "removal of T3 is refused while A2 uses it");

    await page.getByRole("button", { name: "New table" }).click();
    await page.waitForSelector("#tableDialog[open]");
    await page.locator("#tableName").fill("T4");
    // Paste from a text file with mixed delimiters (blank, semicolon, comma, tab, line break).
    const pasteInto = (text) => page.locator("#tableRows > div").nth(0).locator("input").nth(0).evaluate((el, value) => {
      const dt = new DataTransfer(); dt.setData("text", value);
      el.dispatchEvent(new ClipboardEvent("paste", { clipboardData: dt, bubbles: true, cancelable: true }));
    }, text);
    await pasteInto("0;0\n1 1,2\t3");
    await page.waitForTimeout(100);
    check((await page.locator("#tableRows > div").count()) === 3, "a multi-value paste with mixed delimiters makes three rows");
    check((await page.locator("#tableStatus").innerText()) === "Ready to apply.", "the pasted table is ready to apply");
    await pasteInto("0 0 1");
    await page.waitForTimeout(100);
    check((await page.locator("#result").innerText()).includes("one value is missing"), "an odd number of pasted values is refused");
    await pasteInto("0;0\n1 1,2\t3");
    await page.waitForTimeout(100);
    await page.locator("#tableApply").click();
    await page.waitForTimeout(300);
    check((await page.locator("#anScript").inputValue()).includes("table name=T4 t=0,0,1,1,2,3"), "the pasted rows are written as the table line");
    await page.locator("#condTables li").filter({ hasText: "table name=T4" }).getByRole("button", { name: "Remove" }).click();
    await page.waitForTimeout(300);
    check(!(await page.locator("#anScript").inputValue()).includes("table name=T4"), "an unused table is removed");

    console.log("all table editor checks passed");
  } finally {
    await browser.close();
  }
})().catch((e) => { console.error(e.message); process.exit(1); });
