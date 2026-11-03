#include <OpenModel/CodeAster.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_map>

namespace OpenModel {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

std::string trimmedText(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string upperText(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::vector<std::string> splitCells(const std::string& line, char delimiter) {
    std::vector<std::string> cells;
    if (delimiter == ' ') {
        std::istringstream in(line);
        for (std::string t; in >> t;) cells.push_back(t);
        return cells;
    }
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, delimiter)) cells.push_back(trimmedText(cell));
    if (!line.empty() && line.back() == delimiter) cells.emplace_back();
    return cells;
}

char delimiterOf(const std::string& header) {
    const auto count = [&](char c) { return std::count(header.begin(), header.end(), c); };
    if (count('\t') > 0) return '\t';
    if (count(';') > 0) return ';';
    if (count(',') > 0) return ',';
    return ' ';
}

bool isKeyColumn(const std::string& u) {
    return u == "NOEUD" || u == "NODE" || u == "NUME_ORDRE" || u == "NUME_MODE" || u == "INST" || u == "FREQ";
}

bool numberOf(const std::string& text, double& value) {
    if (text.empty()) { value = kNaN; return true; }
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return false;
    if (!std::isfinite(value)) value = kNaN;
    return true;
}

/** Node id of a code_aster node name: the digits at its end (`N12`, `12`). */
bool nodeIdOf(const std::string& name, std::uint64_t& id) {
    std::size_t i = name.size();
    while (i > 0 && std::isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
    if (i == name.size() || name.size() - i > 18) return false;
    id = std::stoull(name.substr(i));
    return id != 0;
}

struct FieldSpec {
    std::string name;
    std::vector<std::string> sourceColumns;    ///< code_aster column names, in component order
    std::vector<std::string> components;
};

const std::vector<FieldSpec>& knownFields() {
    static const std::vector<FieldSpec> specs = {
        {"DISP", {"DX", "DY", "DZ"}, {"D1", "D2", "D3"}},
        {"STRESS", {"SIXX", "SIYY", "SIZZ", "SIXY", "SIYZ", "SIXZ"}, {"SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX"}},
        {"STRAIN", {"EPXX", "EPYY", "EPZZ", "EPXY", "EPYZ", "EPXZ"}, {"EXX", "EYY", "EZZ", "EXY", "EYZ", "EZX"}},
    };
    return specs;
}

} // namespace

std::string CodeAsterTableImporter::formatName() const { return "code_aster table"; }

bool CodeAsterTableImporter::canRead(const std::filesystem::path& path) const {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".tab";
}

ResultImportResult CodeAsterTableImporter::read(const std::filesystem::path& path, FemResults& destination) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ResultImportResult result;
        result.addError(ImportStatus::IoError, "IO_OPEN", "Cannot open " + path.string());
        return result;
    }
    const std::string stem = path.stem().string();
    const bool modal = stem.size() >= 6 && (stem.compare(stem.size() - 6, 6, "_modes") == 0 || stem.find("_modes_") != std::string::npos);
    return parse(in, destination, modal);
}

ResultImportResult CodeAsterTableImporter::parse(std::istream& input, FemResults& destination) const {
    return parse(input, destination, false);
}

ResultImportResult CodeAsterTableImporter::parse(std::istream& input, FemResults& destination, bool modal) const {
    ResultImportResult result;
    std::string line;
    std::size_t lineNumber = 0;

    // Header: the first line (a leading '#' is dropped) that names a key column.
    std::vector<std::string> header;
    char delimiter = ' ';
    while (std::getline(input, line)) {
        ++lineNumber;
        std::string text = trimmedText(line);
        if (text.empty()) continue;
        if (text[0] == '#') text = trimmedText(text.substr(1));
        const char d = delimiterOf(text);
        auto cells = splitCells(text, d);
        bool isHeader = false;
        for (auto& c : cells) { c = upperText(c); isHeader = isHeader || isKeyColumn(c); }
        if (isHeader && cells.size() >= 2) { header = std::move(cells); delimiter = d; break; }
    }
    if (header.empty()) {
        result.addError(ImportStatus::InvalidInput, "ASTER_TAB_NO_HEADER", "No header line with NOEUD, NUME_ORDRE, FREQ or INST found (is this an IMPR_TABLE file?)", lineNumber);
        return result;
    }
    const auto column = [&](const char* name) -> int {
        for (std::size_t i = 0; i < header.size(); ++i) if (header[i] == name) return static_cast<int>(i);
        return -1;
    };
    int nodeCol = column("NOEUD");
    if (nodeCol < 0) nodeCol = column("NODE");
    const int orderCol = column("NUME_ORDRE"), modeCol = column("NUME_MODE"), instCol = column("INST"), freqCol = column("FREQ");

    struct Row { std::string node; std::vector<double> values; double order = kNaN, inst = kNaN, freq = kNaN; };
    std::vector<Row> rows;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string text = trimmedText(line);
        if (text.empty() || text[0] == '#' || text[0] == '!') continue;
        const auto cells = splitCells(text, delimiter);
        if (cells.size() < header.size()) {
            result.addError(ImportStatus::InvalidInput, "ASTER_TAB_SHORT_ROW", "Row has " + std::to_string(cells.size()) + " cells, the header " + std::to_string(header.size()), lineNumber);
            return result;
        }
        Row row;
        row.values.assign(header.size(), kNaN);
        for (std::size_t i = 0; i < header.size(); ++i) {
            if (static_cast<int>(i) == nodeCol) { row.node = cells[i]; continue; }
            if (!numberOf(cells[i], row.values[i])) {
                if (isKeyColumn(header[i]) || header[i].rfind("COOR_", 0) == 0) { row.values[i] = kNaN; continue; }
                result.addError(ImportStatus::InvalidInput, "ASTER_TAB_BAD_NUMBER", "'" + cells[i] + "' is not a number (column " + header[i] + ")", lineNumber);
                return result;
            }
        }
        if (orderCol >= 0) row.order = row.values[static_cast<std::size_t>(orderCol)];
        else if (modeCol >= 0) row.order = row.values[static_cast<std::size_t>(modeCol)];
        if (instCol >= 0) row.inst = row.values[static_cast<std::size_t>(instCol)];
        if (freqCol >= 0) row.freq = row.values[static_cast<std::size_t>(freqCol)];
        rows.push_back(std::move(row));
    }
    if (rows.empty()) { result.addError(ImportStatus::InvalidInput, "ASTER_TAB_EMPTY", "The table has no data row"); return result; }

    FemResults parsed;
    parsed.format = formatName();

    // Eigenvalue table: no node column.
    if (nodeCol < 0) {
        if (freqCol < 0) {
            result.addError(ImportStatus::InvalidInput, "ASTER_TAB_NO_NODE", "The table has neither a node column (NOEUD) nor a FREQ column");
            return result;
        }
        std::size_t sequence = 0;
        for (const auto& r : rows) {
            ++sequence;
            const std::size_t mode = std::isfinite(r.order) && r.order >= 1 ? static_cast<std::size_t>(r.order) : sequence;
            if (!std::isfinite(r.freq)) { result.addWarning("ASTER_TAB_NO_FREQUENCY", "Mode " + std::to_string(mode) + " has no frequency; skipped"); continue; }
            EigenMode m;
            m.mode = mode;
            m.frequency = r.freq;
            m.omega = 2.0 * kPi * r.freq;
            m.eigenvalue = m.omega * m.omega;
            parsed.eigenmodes.push_back(m);
            ResultStep step;
            step.kind = StepKind::Frequency;
            step.value = r.freq;
            step.mode = mode;
            std::ostringstream label;
            label << "mode " << mode << ": f = " << r.freq;
            step.label = label.str();
            parsed.steps.push_back(step);
        }
        if (parsed.eigenmodes.empty()) { result.addError(ImportStatus::InvalidInput, "ASTER_TAB_NO_FREQUENCY", "No valid frequency in the table"); return result; }
        result.stepsRead = parsed.steps.size();
        destination = std::move(parsed);
        return result;
    }

    // Nodal table: steps by NUME_ORDRE (or INST); one step when neither column exists.
    const bool haveOrder = orderCol >= 0 || modeCol >= 0;
    std::map<double, std::size_t> stepOfKey;   // key -> index in parsed.steps
    auto keyOf = [&](const Row& r) { return haveOrder ? r.order : (instCol >= 0 ? r.inst : 0.0); };
    std::vector<double> keys;
    for (const auto& r : rows) {
        const double k = keyOf(r);
        if (!std::isfinite(k)) { result.addError(ImportStatus::InvalidInput, "ASTER_TAB_BAD_STEP", "A row has no step number (NUME_ORDRE / INST)"); return result; }
        if (stepOfKey.emplace(k, 0).second) keys.push_back(k);
    }
    std::sort(keys.begin(), keys.end());
    for (const double k : keys) {
        ResultStep step;
        double firstInst = kNaN, firstFreq = kNaN;
        for (const auto& r : rows) if (keyOf(r) == k) { firstInst = r.inst; firstFreq = r.freq; break; }
        std::ostringstream label;
        if (modal) {
            step.kind = StepKind::Frequency;
            step.mode = static_cast<std::size_t>(std::max(1.0, k));
            step.value = std::isfinite(firstFreq) ? firstFreq : 0.0;
            label << "mode " << step.mode;
            if (std::isfinite(firstFreq)) label << ": f = " << firstFreq;
        } else if (keys.size() == 1) {
            step.kind = StepKind::Static;
            step.value = 0.0;
            label << "static";
        } else if (haveOrder) {
            step.kind = std::isfinite(firstInst) ? StepKind::Time : StepKind::LoadStep;
            step.value = std::isfinite(firstInst) ? firstInst : k;
            label << (std::isfinite(firstInst) ? "time " : "order ") << step.value << " (order " << k << ")";
        } else {
            step.kind = StepKind::Time;
            step.value = k;
            label << "time " << k;
        }
        step.label = label.str();
        stepOfKey[k] = parsed.steps.size();
        parsed.steps.push_back(step);
    }

    // Fields: the known tensors/vectors first, then every other numeric column as a scalar.
    std::vector<char> used(header.size(), 0);
    struct Plan { std::string name; std::vector<std::string> components; std::vector<int> columns; };
    std::vector<Plan> plans;
    for (const auto& spec : knownFields()) {
        Plan plan{spec.name, spec.components, {}};
        bool all = true;
        for (const auto& source : spec.sourceColumns) {
            const int c = column(source.c_str());
            if (c < 0) { all = false; break; }
            plan.columns.push_back(c);
        }
        if (!all) continue;
        for (const int c : plan.columns) used[static_cast<std::size_t>(c)] = 1;
        plans.push_back(std::move(plan));
    }
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (used[i] || static_cast<int>(i) == nodeCol || isKeyColumn(header[i]) || header[i].rfind("COOR_", 0) == 0) continue;
        plans.push_back({header[i], {header[i]}, {static_cast<int>(i)}});
    }
    if (plans.empty()) { result.addError(ImportStatus::InvalidInput, "ASTER_TAB_NO_FIELD", "The table has no result column"); return result; }

    std::size_t duplicates = 0, badNodes = 0;
    for (const auto& plan : plans) {
        std::map<std::size_t, ResultField> perStep;
        std::map<std::size_t, std::unordered_map<std::uint64_t, char>> seen;
        for (const auto& r : rows) {
            std::uint64_t id = 0;
            if (!nodeIdOf(r.node, id)) { ++badNodes; continue; }
            const std::size_t s = stepOfKey[keyOf(r)];
            auto& field = perStep[s];
            if (field.name.empty()) {
                field.name = plan.name;
                field.components = plan.components;
                field.step = s;
                field.location = ResultLocation::Node;
            }
            if (!seen[s].emplace(id, 1).second) { ++duplicates; continue; }
            field.ids.push_back(id);
            for (const int c : plan.columns) field.values.push_back(r.values[static_cast<std::size_t>(c)]);
        }
        for (auto& entry : perStep) parsed.fields.push_back(std::move(entry.second));
    }
    if (badNodes) result.addWarning("ASTER_TAB_BAD_NODE", std::to_string(badNodes / std::max<std::size_t>(plans.size(), 1)) + " row(s) with a node name that has no number were skipped");
    if (duplicates) result.addWarning("ASTER_TAB_DUPLICATE", "Duplicate node records in a step were ignored (first one kept)");
    if (parsed.fields.empty()) { result.addError(ImportStatus::InvalidInput, "ASTER_TAB_NO_RECORD", "No usable record in the table"); return result; }
    result.stepsRead = parsed.steps.size();
    result.fieldsRead = parsed.fields.size();
    destination = std::move(parsed);
    return result;
}

} // namespace OpenModel
