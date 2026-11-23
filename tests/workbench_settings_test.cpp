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
        CHECK(s.solverPath.empty() && s.jobDir.empty());
        CHECK(s.legendPalette == "Viridis");
    }

    // 2. Save, then load: every field comes back unchanged, including characters that need escaping.
    {
        Settings out;
        out.solverPath = "C:\\Program Files\\CalculiX\\ccx \"beta\".exe";
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
        CHECK(s.solverPath.empty());
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
        CHECK(s.solverPath.empty() && s.legendPalette == "Viridis");

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
        s.solverPath = "p\"q";
        const std::string json = toJson(s);
        CHECK(json.find("\"solverPath\":\"p\\\"q\"") != std::string::npos);
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
