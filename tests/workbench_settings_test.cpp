/**
 * @file workbench_settings_test.cpp
 * @brief The workbench settings file: round trip, defaults for missing keys, damaged files, escapes and folder creation.
 */
#include "../src/webui/WorkbenchSettings.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace OpenModel::Workbench;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";        \
        }                                                                                  \
    } while (0)

/** @brief Writes a text file, creating its folder. */
void writeText(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

} // namespace

int main() {
    // A fresh folder for every run, so that no earlier file can interfere.
    const fs::path root = fs::temp_directory_path() / "openmodel_workbench_settings_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path file = root / "config" / "settings.json";

    // 1. A missing file gives the defaults and no error.
    {
        std::string error = "stale";
        const Settings s = load(file, error);
        CHECK(error.empty());
        CHECK(s.jobDir.empty());
        CHECK(s.solvers.defaults.at("static") == "calculix");
        CHECK(s.legendPalette == "Viridis");
    }

    // 2. Save, then load: every field comes back unchanged, including characters that need escaping.
    {
        Settings out;
        out.solvers.solvers[0].path = "C:\\Program Files\\CalculiX\\ccx \"beta\".exe";
        out.solvers.solvers.push_back({"elmer-dev", OpenModel::SolverKind::Elmer, "/opt/elmer \"dev\"/ElmerSolver"});
        out.solvers.defaults["dynamic"] = "calculix";
        out.jobDir = "/home/user/jobs\twith tab";
        out.legendPalette = "Arcobaleno";
        std::string error;
        CHECK(save(file, out, error));
        CHECK(error.empty());
        CHECK(!fs::exists(file.string() + ".tmp"));   // the temporary file is renamed, not left behind
        std::string loadError = "stale";
        const Settings in = load(file, loadError);
        CHECK(loadError.empty());
        CHECK(in == out);
        CHECK(in.solvers.solvers.size() == 4);
        CHECK(in.solvers.solvers[3].path == "/opt/elmer \"dev\"/ElmerSolver");
    }

    // 3. Save replaces an existing file.
    {
        Settings first;
        first.legendPalette = "Blu";
        std::string error;
        CHECK(save(file, first, error));
        Settings second;
        second.legendPalette = "Rosso";
        CHECK(save(file, second, error));
        std::string loadError;
        CHECK(load(file, loadError).legendPalette == "Rosso");
    }

    // 4. A file with only some keys keeps the defaults for the others.
    {
        const fs::path partial = root / "partial.json";
        writeText(partial, "{\"jobDir\":\"/work\"}");
        std::string error;
        const Settings s = load(partial, error);
        CHECK(error.empty());
        CHECK(s.jobDir == "/work");
        CHECK(s.solvers.defaults.at("static") == "calculix");
        CHECK(s.legendPalette == "Viridis");
    }

    // 5. An empty palette name in the file falls back to the default.
    {
        const fs::path emptyPalette = root / "empty_palette.json";
        writeText(emptyPalette, "{\"legendPalette\":\"\"}");
        std::string error;
        CHECK(load(emptyPalette, error).legendPalette == "Viridis");
    }

    // 6. A damaged file gives the defaults and says why; it never stops the program.
    {
        const fs::path damaged = root / "damaged.json";
        writeText(damaged, "{\"solverPath\": 42}");   // numbers are not accepted by the flat reader
        std::string error;
        const Settings s = load(damaged, error);
        CHECK(!error.empty());
        CHECK(s.jobDir.empty() && s.legendPalette == "Viridis");

        const fs::path notJson = root / "not_json.json";
        writeText(notJson, "solverPath=/usr/bin/ccx");
        std::string error2;
        load(notJson, error2);
        CHECK(!error2.empty());
    }

    // 7. Escapes: quotes, backslashes and control characters are written as valid JSON strings.
    {
        CHECK(escapeJson("a\"b") == "a\\\"b");
        CHECK(escapeJson("a\\b") == "a\\\\b");
        CHECK(escapeJson("line\nend") == "line\\nend");
        CHECK(escapeJson(std::string("x\x01y", 3)) == "x\\u0001y");
        Settings s;
        s.jobDir = "p\"q";
        const std::string json = toJson(s);
        CHECK(json.find("\"jobDir\":\"p\\\"q\"") != std::string::npos);
    }

    // 9. A settings file of the earlier version (one CalculiX path) is read as the solver `calculix`.
    {
        const fs::path legacy = root / "legacy.json";
        writeText(legacy, "{\"solverPath\":\"/usr/bin/ccx\",\"jobDir\":\"/jobs\"}");
        std::string error;
        const Settings s = load(legacy, error);
        CHECK(error.empty());
        CHECK(s.jobDir == "/jobs");
        CHECK(s.solvers.solvers.size() == 3);
        CHECK(OpenModel::findSolver(s.solvers, "calculix")->path == "/usr/bin/ccx");
    }

    // 10. A solver entry with an unknown kind is reported; the valid entries are kept.
    {
        const fs::path weird = root / "weird.json";
        writeText(weird, "{\"solver.bad.kind\":\"fluent\",\"solver.ok.kind\":\"elmer\",\"solver.ok.path\":\"/e\"}");
        std::string error;
        const Settings s = load(weird, error);
        CHECK(!error.empty());
        CHECK(OpenModel::findSolver(s.solvers, "bad") == nullptr);
        CHECK(OpenModel::findSolver(s.solvers, "ok") && OpenModel::findSolver(s.solvers, "ok")->kind == OpenModel::SolverKind::Elmer);
    }

    // 8. Saving creates the folder of the file when it does not exist.
    {
        const fs::path deep = root / "a" / "b" / "c" / "settings.json";
        std::string error;
        CHECK(save(deep, Settings{}, error));
        CHECK(fs::exists(deep));
    }

    fs::remove_all(root, ec);
    if (g_failures == 0) std::cout << "workbench_settings_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
