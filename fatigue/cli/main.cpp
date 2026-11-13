/**
 * @file main.cpp
 * @brief openmodel-fatigue: runs the fatigue analysis of a material on FEM results, or of a load spectrum.
 *
 * Commands (exit code 0 on success, 1 when the analysis did not succeed everywhere, 2 on usage or input errors):
 *   analyze  --material FILE --field NAME --position SPEC... --results FILE... [--projection X,Y,Z]
 *            [--temperature K] [--out DIR]
 *   spectrum --spectrum FILE --material FILE [--out DIR]
 *   info     prints the version and the supported input forms
 *
 * With --out DIR the reports are written to DIR: summary.csv, cycles.csv and report.json for analyze;
 * spectrum_summary.csv, spectrum_blocks.csv and spectrum.json for spectrum.
 */

#include <OpenModel/Version.hpp>
#include <OpenModel/FemResults.hpp>
#include <OpenModel/Fatigue/FatigueAnalysis.hpp>
#include <OpenModel/Fatigue/FatigueReport.hpp>
#include <OpenModel/Fatigue/FatigueReportWriter.hpp>
#include <OpenModel/Fatigue/FatigueSpectrumReport.hpp>
#include <OpenModel/Fatigue/FatigueSpecification.hpp>
#include <OpenModel/Fatigue/Json.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <filesystem>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {

void usage(std::ostream& out) {
    out << "Usage:\n"
        << "  openmodel-fatigue analyze --material FILE.json --field NAME --position SPEC... --results FILE... \\\n"
        << "                            [--unit SYMBOL:SCALE] [--projection X,Y,Z] [--temperature K] [--out DIR]\n"
        << "  openmodel-fatigue spectrum --spectrum FILE.json --material FILE.json [--out DIR]\n"
        << "  openmodel-fatigue info\n"
        << "SPEC is node:ID, element:ID or ip:ELEMENT:N (1-based integration point).\n";
}

void printDiagnostics(const std::vector<ImportDiagnostic>& diagnostics) {
    for (const auto& d : diagnostics) {
        std::cerr << (d.level == ImportDiagnosticLevel::Error ? "error" : d.level == ImportDiagnosticLevel::Warning ? "warning" : "info")
                  << " " << d.code << ": " << d.message;
        if (d.line) std::cerr << " (line " << d.line << ")";
        std::cerr << "\n";
    }
}

/** Options of the command line, by name; repeated names accumulate in order. */
class Arguments {
public:
    Arguments(int argc, char** argv, int first) {
        for (int i = first; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg.size() < 3 || arg.compare(0, 2, "--") != 0) {
                error_ = "unexpected argument '" + arg + "'";
                return;
            }
            if (i + 1 >= argc) {
                error_ = "missing value for " + arg;
                return;
            }
            values_[arg].push_back(argv[++i]);
        }
    }
    const std::string& error() const { return error_; }
    std::string single(const std::string& name, const std::string& fallback = std::string()) const {
        const auto it = values_.find(name);
        return it == values_.end() ? fallback : it->second.back();
    }
    bool has(const std::string& name) const { return values_.count(name) != 0; }
    std::vector<std::string> all(const std::string& name) const {
        const auto it = values_.find(name);
        return it == values_.end() ? std::vector<std::string>() : it->second;
    }
    /** Names that the command does not know: they are usage errors. */
    std::string unknown(const std::vector<std::string>& known) const {
        for (const auto& kv : values_) {
            bool found = false;
            for (const auto& k : known) found = found || kv.first == k;
            if (!found) return kv.first;
        }
        return std::string();
    }

private:
    std::map<std::string, std::vector<std::string>> values_;
    std::string error_;
};

/** Reads and parses a JSON file; on failure prints the reason and returns false. */
bool readJson(const std::string& path, Json::Value& value) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "error: cannot read '" << path << "'\n";
        return false;
    }
    std::ostringstream text;
    text << in.rdbuf();
    const auto parsed = Json::parse(text.str());
    if (!parsed) {
        std::cerr << "error: " << path << ": " << parsed.error << "\n";
        return false;
    }
    value = parsed.value;
    return true;
}

bool readMaterialFile(const std::string& path, FatigueMaterial& material) {
    Json::Value root;
    if (!readJson(path, root)) return false;
    const auto result = readMaterial(root, material);
    if (!result) {
        std::cerr << "error: " << path << ": " << result.error << "\n";
        return false;
    }
    return true;
}

/** Reads the result files like the core tool does: merged into one set. */
bool readResultFiles(const std::vector<fs::path>& paths, FemResults& results) {
    const auto imported = readResultsMerged(paths, results);
    printDiagnostics(imported.diagnostics);
    return static_cast<bool>(imported);
}

bool writeFile(const fs::path& path, const std::function<void(std::ostream&)>& write) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::cerr << "error: cannot write '" << path.string() << "'\n";
        return false;
    }
    write(out);
    return static_cast<bool>(out);
}

/** Text of a damage value with six significant digits. */
std::string numberText(double value) {
    std::ostringstream text;
    text << std::setprecision(6) << value;
    return text.str();
}

/** Life in blocks (1/D) as text; "-" when the damage is zero or unknown. */
std::string lifeText(bool hasDamage, double damage) {
    if (!hasDamage || !(damage > 0.0)) return "-";
    std::ostringstream text;
    text << std::setprecision(6) << (1.0 / damage);
    return text.str();
}

int runAnalyze(const Arguments& args) {
    const std::string bad = args.unknown({"--material", "--field", "--position", "--results", "--unit", "--projection", "--temperature", "--out"});
    if (!bad.empty()) { std::cerr << "error: unknown option " << bad << "\n"; usage(std::cerr); return 2; }
    if (!args.has("--material") || !args.has("--field") || !args.has("--position") || !args.has("--results")) {
        std::cerr << "error: analyze needs --material, --field, --position and --results\n";
        usage(std::cerr);
        return 2;
    }

    FatigueMaterial material;
    if (!readMaterialFile(args.single("--material"), material)) return 2;

    StressProjection projection;
    if (args.has("--projection")) {
        const auto result = parseProjection(args.single("--projection"), projection);
        if (!result) { std::cerr << "error: --projection " << result.error << "\n"; return 2; }
    }
    double temperature = 293.15;
    if (args.has("--temperature")) {
        std::istringstream text(args.single("--temperature"));
        if (!(text >> temperature) || !(temperature > 0.0)) { std::cerr << "error: --temperature must be a positive number of kelvin\n"; return 2; }
    }

    std::vector<FatiguePositionInput> positions;
    for (const auto& text : args.all("--position")) {
        FatiguePositionInput input;
        const auto result = parsePosition(text, input.position);
        if (!result) { std::cerr << "error: --position " << result.error << "\n"; return 2; }
        input.material = &material;
        input.projection = projection;
        input.label = text;
        positions.push_back(input);
    }

    const std::string field = args.single("--field");
    std::vector<fs::path> resultFiles;
    for (const auto& file : args.all("--results")) resultFiles.emplace_back(file);
    FemResults results;
    if (!readResultFiles(resultFiles, results)) return 2;
    if (args.has("--unit")) {
        PhysicalUnit unit;
        const auto parsedUnit = parseUnitDeclaration(args.single("--unit"), unit);
        if (!parsedUnit) { std::cerr << "error: --unit " << parsedUnit.error << "\n"; return 2; }
        std::string error;
        if (!declareFieldUnit(results, field, unit, error)) { std::cerr << "error: " << error << "\n"; return 2; }
    }
    const OpenModelFemResultsSource source(results);

    const FatigueReport report = FatigueReportBuilder::build(source, field, positions, temperature);

    std::cout << "field " << field << ", " << positions.size() << " position(s): " << (report ? "success" : report.diagnostic.empty() ? "failed" : report.diagnostic) << "\n";
    for (const auto& p : report.positions) {
        std::cout << "  " << p.label << "  " << (p.analysis ? "damage " + numberText(p.damage) : "failed: " + p.analysis.diagnostic)
                  << "  life(blocks) " << lifeText(p.hasDamage, p.damage) << "\n";
    }

    if (args.has("--out")) {
        const fs::path dir = args.single("--out");
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) { std::cerr << "error: cannot create '" << dir.string() << "': " << ec.message() << "\n"; return 2; }
        const bool written =
            writeFile(dir / "summary.csv", [&](std::ostream& o) { FatigueReportWriter::writeCsvSummary(report, o); }) &&
            writeFile(dir / "cycles.csv", [&](std::ostream& o) { FatigueReportWriter::writeCsvCycles(report, o); }) &&
            writeFile(dir / "report.json", [&](std::ostream& o) { FatigueReportWriter::writeJson(report, o, true); });
        if (!written) return 2;
        std::cout << "reports written to " << dir.string() << "\n";
    }
    return report ? 0 : 1;
}

struct ResultSet {
    std::unique_ptr<FemResults> results;
    std::unique_ptr<OpenModelFemResultsSource> source;
};

int runSpectrum(const Arguments& args) {
    const std::string bad = args.unknown({"--spectrum", "--material", "--out"});
    if (!bad.empty()) { std::cerr << "error: unknown option " << bad << "\n"; usage(std::cerr); return 2; }
    if (!args.has("--spectrum") || !args.has("--material")) {
        std::cerr << "error: spectrum needs --spectrum and --material\n";
        usage(std::cerr);
        return 2;
    }

    FatigueMaterial material;
    if (!readMaterialFile(args.single("--material"), material)) return 2;

    const fs::path spectrumPath = args.single("--spectrum");
    Json::Value root;
    if (!readJson(spectrumPath.string(), root)) return 2;
    SpectrumSpecification spectrum;
    if (const auto r = readSpectrum(root, spectrum); !r) {
        std::cerr << "error: " << spectrumPath.string() << ": " << r.error << "\n";
        return 2;
    }

    // Result files are named relative to the spectrum file. Each distinct set of files is read once. The map keeps
    // the results and their sources at stable addresses, because the blocks point to the sources.
    const fs::path base = spectrumPath.parent_path();
    std::map<std::string, ResultSet> resultSets;
    std::vector<FatigueSpectrumPositionInput> positions;
    for (const auto& pos : spectrum.positions) {
        FatigueSpectrumPositionInput input;
        input.position = pos.position;
        input.material = &material;
        input.label = pos.label;
        for (const auto& block : pos.blocks) {
            std::vector<fs::path> files;
            std::string key;
            for (const auto& name : block.results) {
                const fs::path file = fs::path(name).is_absolute() ? fs::path(name) : base / name;
                files.push_back(file);
                key += file.generic_string() + "\n";
            }
            // A set is identified by its files, its field and its declared unit: the declaration changes the set.
            key += "|" + block.fieldName + "|" + (block.hasResultUnit ? block.resultUnit.symbol + ":" + std::to_string(block.resultUnit.scaleToSI) : std::string("-"));
            auto it = resultSets.find(key);
            if (it == resultSets.end()) {
                ResultSet set;
                set.results = std::make_unique<FemResults>();
                if (!readResultFiles(files, *set.results)) return 2;
                if (block.hasResultUnit) {
                    std::string error;
                    if (!declareFieldUnit(*set.results, block.fieldName, block.resultUnit, error)) {
                        std::cerr << "error: " << spectrumPath.string() << ": positions[" << positions.size() << "] block '" << block.label << "': " << error << "\n";
                        return 2;
                    }
                }
                set.source = std::make_unique<OpenModelFemResultsSource>(*set.results);
                it = resultSets.emplace(key, std::move(set)).first;
            }
            FatigueLoadBlockInput load;
            load.label = block.label;
            load.source = it->second.source.get();
            load.fieldName = block.fieldName;
            load.position = pos.position;
            load.projection = block.projection;
            load.repetitions = block.repetitions;
            input.blocks.push_back(load);
        }
        positions.push_back(std::move(input));
    }

    const FatigueSpectrumReport report = FatigueSpectrumReportBuilder::build(positions, spectrum.temperatureKelvin);

    std::cout << "spectrum: " << (report ? "success" : report.diagnostic.empty() ? "failed" : report.diagnostic) << "\n";
    for (const auto& p : report.positions) {
        std::cout << "  " << p.label << "  ";
        if (p.spectrum.status != FatigueSpectrumStatus::Success) {
            std::cout << "failed: " << p.spectrum.diagnostic << "\n";
            continue;
        }
        std::cout << "total damage " << numberText(p.spectrum.totalDamage) << "  spectrum repetitions to failure "
                  << lifeText(p.spectrum.totalDamage > 0.0, p.spectrum.totalDamage) << "\n";
    }

    if (args.has("--out")) {
        const fs::path dir = args.single("--out");
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) { std::cerr << "error: cannot create '" << dir.string() << "': " << ec.message() << "\n"; return 2; }
        const bool written =
            writeFile(dir / "spectrum_summary.csv", [&](std::ostream& o) { FatigueReportWriter::writeCsvSpectrumSummary(report, o); }) &&
            writeFile(dir / "spectrum_blocks.csv", [&](std::ostream& o) { FatigueReportWriter::writeCsvSpectrumBlocks(report, o); }) &&
            writeFile(dir / "spectrum.json", [&](std::ostream& o) { FatigueReportWriter::writeSpectrumJson(report, o, true); });
        if (!written) return 2;
        std::cout << "reports written to " << dir.string() << "\n";
    }
    return report ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(std::cerr);
        return 2;
    }
    const std::string command = argv[1];
    if (command == "--version" || command == "version") {
        std::cout << "OpenModel fatigue " << OpenModel::versionString() << "\n";
        return 0;
    }
    if (command == "help" || command == "--help") {
        usage(std::cout);
        return 0;
    }
    if (command == "info") {
        std::cout << "openmodel-fatigue " << OpenModel::versionString() << "\n"
                  << "curves: stressLifeSN (stress amplitude), strainLifeEN (strain amplitude)\n"
                  << "positions: node:ID, element:ID, ip:ELEMENT:N\n";
        return 0;
    }
    const Arguments args(argc, argv, 2);
    if (!args.error().empty()) {
        std::cerr << "error: " << args.error() << "\n";
        usage(std::cerr);
        return 2;
    }
    if (command == "analyze") return runAnalyze(args);
    if (command == "spectrum") return runSpectrum(args);
    std::cerr << "error: unknown command '" << command << "'\n";
    usage(std::cerr);
    return 2;
}
