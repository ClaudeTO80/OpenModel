#pragma once

#include "FlatJson.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>

/**
 * @file WorkbenchSettings.hpp
 * @brief Settings of the OpenModel Workbench: solver path, jobs folder and legend palette.
 *
 * The settings are kept in a small JSON object of strings, {"solverPath":"...","jobDir":"...","legendPalette":"..."},
 * in the user configuration folder. Header only and independent of the WebUI runtime, so the file handling can be
 * tested on its own.
 */
namespace OpenModel::Workbench {

/** @brief The settings of the workbench. Empty paths mean "not set": the program then uses its own default. */
struct Settings {
    std::string solverPath;                 ///< Solver executable; empty = "ccx" on PATH (code_aster: run_aster / as_run).
    std::string jobDir;                     ///< Base folder of the job files; empty = the folder the program runs in.
    std::string legendPalette = "Viridis";  ///< Colour map of the viewer's colour bar.

    bool operator==(const Settings& other) const {
        return solverPath == other.solverPath && jobDir == other.jobDir && legendPalette == other.legendPalette;
    }
};

/**
 * @brief Location of the settings file.
 * Windows: %APPDATA%\OpenModel\settings.json. Other systems: $XDG_CONFIG_HOME/openmodel/settings.json, or
 * ~/.config/openmodel/settings.json. Without a configuration folder the file goes next to the program's working folder.
 */
inline std::filesystem::path settingsFile() {
#ifdef _WIN32
    if (const char* appData = std::getenv("APPDATA"); appData && *appData)
        return std::filesystem::path(appData) / "OpenModel" / "settings.json";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return std::filesystem::path(xdg) / "openmodel" / "settings.json";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config" / "openmodel" / "settings.json";
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec) / "openmodel-settings.json";
}

/**
 * @brief Reads the settings file.
 *
 * A missing file gives the defaults without an error. A file that cannot be read or parsed also gives the defaults,
 * and the reason is written to @p error, so that a damaged file never stops the program.
 *
 * @param file  the settings file
 * @param error receives the reason when the file exists but cannot be used; left empty otherwise
 * @return the settings found in the file, or the defaults for each missing key
 */
inline Settings load(const std::filesystem::path& file, std::string& error) {
    Settings settings;
    error.clear();
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return settings;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot read " + file.string();
        return settings;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::map<std::string, std::string> fields;
    std::string parseError;
    if (!FlatJson::parseStringObject(buffer.str(), fields, parseError)) {
        error = "settings file ignored: " + parseError;
        return settings;
    }
    if (const auto it = fields.find("solverPath"); it != fields.end()) settings.solverPath = it->second;
    if (const auto it = fields.find("jobDir"); it != fields.end()) settings.jobDir = it->second;
    if (const auto it = fields.find("legendPalette"); it != fields.end() && !it->second.empty())
        settings.legendPalette = it->second;
    return settings;
}

/**
 * @brief Escapes a string for a JSON literal (quotes, backslashes and control characters).
 * @param text the text to escape
 * @return the escaped text, without the surrounding quotes
 */
inline std::string escapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                static const char* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
                out += hex[static_cast<unsigned char>(c) & 0xF];
            } else {
                out += c;
            }
        }
    }
    return out;
}

/**
 * @brief The settings as a JSON object, for the answers of the WebUI handlers.
 * @param settings the settings
 * @return {"solverPath":"...","jobDir":"...","legendPalette":"..."}
 */
inline std::string toJson(const Settings& settings) {
    return "{\"solverPath\":\"" + escapeJson(settings.solverPath) + "\",\"jobDir\":\"" + escapeJson(settings.jobDir) +
           "\",\"legendPalette\":\"" + escapeJson(settings.legendPalette) + "\"}";
}

/**
 * @brief Writes the settings file, creating its folder when needed.
 *
 * The text goes to a temporary file first and then replaces the settings file, so an interrupted write never leaves a
 * half-written file behind.
 *
 * @param file     the settings file
 * @param settings the settings to store
 * @param error    receives the reason when the file cannot be written
 * @return true when the file was written
 */
inline bool save(const std::filesystem::path& file, const Settings& settings, std::string& error) {
    error.clear();
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) {
        error = "cannot create " + file.parent_path().string() + ": " + ec.message();
        return false;
    }
    const std::filesystem::path temporary = file.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + temporary.string();
            return false;
        }
        out << toJson(settings) << '\n';
        out.flush();
        if (!out) {
            error = "cannot write " + temporary.string();
            return false;
        }
    }
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        // Some systems do not replace an existing file on rename: remove it and try once more.
        std::filesystem::remove(file, ec);
        ec.clear();
        std::filesystem::rename(temporary, file, ec);
    }
    if (ec) {
        error = "cannot replace " + file.string() + ": " + ec.message();
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

} // namespace OpenModel::Workbench
