#include <OpenModel/FemResults.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <istream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace OpenModel {

void FemResults::clear() noexcept {
    format.clear();
    steps.clear();
    fields.clear();
    eigenmodes.clear();
    modelNodeIds.clear();
    modelCoordinates.clear();
}

std::vector<std::size_t> FemResults::modeSteps() const {
    std::vector<std::size_t> modes;
    for (std::size_t i = 0; i < steps.size(); ++i)
        if (steps[i].kind == StepKind::Frequency) modes.push_back(i);
    return modes;
}

void ResultImportResult::addError(ImportStatus newStatus, std::string code, std::string message, std::size_t line) {
    if (status == ImportStatus::Success) status = newStatus;
    diagnostics.push_back({ImportDiagnosticLevel::Error, std::move(code), std::move(message), line});
}

void ResultImportResult::addWarning(std::string code, std::string message, std::size_t line) {
    diagnostics.push_back({ImportDiagnosticLevel::Warning, std::move(code), std::move(message), line});
}

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

std::string trimmed(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string lowerExtension(const std::filesystem::path& path) {
    return lowered(path.extension().string());
}

bool startsWith(const std::string& s, const char* prefix) {
    return s.compare(0, std::char_traits<char>::length(prefix), prefix) == 0;
}

/**
 * Fortran drops the exponent letter when the exponent needs three digits ("1.23456-101").
 * Re-insert it so strtod can read the number.
 */
std::string restoreExponentLetter(const std::string& text) {
    for (std::size_t i = text.size(); i-- > 1;) {
        if ((text[i] == '+' || text[i] == '-') && std::isdigit(static_cast<unsigned char>(text[i - 1]))) {
            return text.substr(0, i) + "E" + text.substr(i);
        }
        if (text[i] == 'E' || text[i] == 'e') break;
    }
    return text;
}

enum class Num { Ok, Blank, Bad };

/** Reads a fixed-width real field. Overflow stars and non-finite values become NaN. */
Num fixedReal(const std::string& line, std::size_t pos, std::size_t width, double& value) {
    if (pos >= line.size()) return Num::Blank;
    const std::string text = trimmed(line.substr(pos, width));
    if (text.empty()) return Num::Blank;
    if (text.find('*') != std::string::npos) { value = kNaN; return Num::Ok; }
    auto parse = [&](const std::string& candidate) {
        char* end = nullptr;
        const double v = std::strtod(candidate.c_str(), &end);
        if (end == candidate.c_str() || *end != '\0') return false;
        value = std::isfinite(v) ? v : kNaN;
        return true;
    };
    if (parse(text)) return Num::Ok;
    if (parse(restoreExponentLetter(text))) return Num::Ok;
    return Num::Bad;
}

/** Reads a fixed-width integer field. */
Num fixedInt(const std::string& line, std::size_t pos, std::size_t width, long long& value) {
    if (pos >= line.size()) return Num::Blank;
    const std::string text = trimmed(line.substr(pos, width));
    if (text.empty()) return Num::Blank;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) return Num::Bad;
    value = v;
    return Num::Ok;
}

/* ----------------------------------------------------------------------------- FRD */

class FrdParser {
public:
    FrdParser(std::istream& input, FemResults& out, ResultImportResult& result)
        : m_in(input), m_out(out), m_result(result) {}

    /** @return false when parsing had to stop because of an error. */
    bool run() {
        std::string line;
        while (next(line)) {
            const std::string key = ltrim(line);
            if (startsWith(key, "2C")) {
                if (!readNodes(line)) return false;
            } else if (startsWith(key, "3C")) {
                skipBlock();
            } else if (startsWith(key, "100C")) {
                if (!readResultBlock(line)) return false;
            } else if (startsWith(key, "9999")) {
                break;
            }
        }
        return true;
    }

private:
    std::istream& m_in;
    FemResults& m_out;
    ResultImportResult& m_result;
    std::size_t m_line = 0;

    struct Component { std::string name; bool stored = true; };

    static std::string ltrim(const std::string& s) {
        const auto first = s.find_first_not_of(' ');
        return first == std::string::npos ? std::string{} : s.substr(first);
    }

    bool next(std::string& line) {
        if (!std::getline(m_in, line)) return false;
        ++m_line;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    }

    static bool isRecord(const std::string& line, const char* key) {
        return startsWith(ltrim(line), key);
    }

    /** FORMAT digit of a block header: the last non-blank character (0 short, 1 long, 2 binary). */
    static int headerFormat(const std::string& line) {
        const auto last = line.find_last_not_of(" \t");
        if (last == std::string::npos || !std::isdigit(static_cast<unsigned char>(line[last]))) return -1;
        return line[last] - '0';
    }

    void skipBlock() {
        std::string line;
        while (next(line))
            if (isRecord(line, "-3")) return;
    }

    bool readNodes(const std::string& header) {
        const int format = headerFormat(header);
        if (format == 2) {
            m_result.addError(ImportStatus::UnsupportedFeature, "FRD_BINARY",
                              "Binary .frd files are not supported; run CalculiX without the 'binary' option", m_line);
            return false;
        }
        const std::size_t nodeWidth = format == 0 ? 5 : 10;
        std::string line;
        while (next(line)) {
            if (isRecord(line, "-3")) return true;
            if (!isRecord(line, "-1")) continue;
            long long id = 0;
            double c[3] = {0, 0, 0};
            bool ok = fixedInt(line, 3, nodeWidth, id) == Num::Ok && id > 0;
            for (int k = 0; ok && k < 3; ++k) ok = fixedReal(line, 3 + nodeWidth + 12 * static_cast<std::size_t>(k), 12, c[k]) == Num::Ok;
            if (!ok) {
                m_result.addError(ImportStatus::InvalidInput, "FRD_BAD_NODE", "Malformed node record", m_line);
                return false;
            }
            m_out.modelNodeIds.push_back(static_cast<NodeId>(id));
            m_out.modelCoordinates.push_back({c[0], c[1], c[2]});
        }
        m_result.addWarning("FRD_UNTERMINATED", "Node block is not terminated by '-3'", m_line);
        return true;
    }

    struct Header {
        double value = 0.0;
        long long numNodes = -1;
        long long kind = 0;     // ICTYPE
        long long number = 0;   // NUMSTP
        int format = 1;
        std::string setName;
    };

    bool parseHeader(const std::string& line, Header& h) {
        // (1X,' 100','C',6A1,E12.5,I12,20A1,I2,I5,10A1,I2): columns 0-based.
        h.setName = trimmed(line.size() > 6 ? line.substr(6, 6) : std::string{});
        if (fixedReal(line, 12, 12, h.value) != Num::Ok) {
            m_result.addError(ImportStatus::InvalidInput, "FRD_BAD_HEADER", "Cannot read the step value of the result block", m_line);
            return false;
        }
        fixedInt(line, 24, 12, h.numNodes);
        fixedInt(line, 56, 2, h.kind);
        fixedInt(line, 58, 5, h.number);
        long long fmt = 1;
        if (fixedInt(line, 73, 2, fmt) != Num::Ok) {
            const int digit = headerFormat(line);
            fmt = digit < 0 ? 1 : digit;
        }
        h.format = static_cast<int>(fmt);
        return true;
    }

    /** Per-step bookkeeping needed to decide whether a block belongs to an existing step. */
    struct StepInfo {
        long long number = 0;
        std::unordered_set<std::string> datasets;
    };
    std::vector<StepInfo> m_info;

    std::size_t stepFor(const Header& h, const std::string& datasetName) {
        static const char* kinds[] = {"static", "time", "frequency", "load step", "user"};
        static const StepKind enums[] = {StepKind::Static, StepKind::Time, StepKind::Frequency, StepKind::LoadStep, StepKind::User};
        const std::size_t kindIndex = h.kind >= 0 && h.kind <= 4 ? static_cast<std::size_t>(h.kind) : 0;
        const StepKind kind = enums[kindIndex];
        // Several quantities written for the same increment share one ResultStep; a quantity that
        // appears twice (for instance the displacements of two modes with equal frequency) opens a new one.
        for (std::size_t i = 0; i < m_out.steps.size(); ++i) {
            const ResultStep& st = m_out.steps[i];
            if (st.kind == kind && st.value == h.value && m_info[i].number == h.number && !m_info[i].datasets.count(datasetName)) {
                m_info[i].datasets.insert(datasetName);
                return i;
            }
        }
        ResultStep step;
        step.kind = kind;
        step.value = h.value;
        std::ostringstream label;
        label.precision(6);
        if (kind == StepKind::Frequency) {
            std::size_t modes = 0;
            for (const auto& other : m_out.steps) modes += other.kind == StepKind::Frequency;
            step.mode = modes + 1;
            label << "mode " << step.mode << ": f = " << h.value;
        } else {
            label << kinds[kindIndex];
            if (h.kind != 0 || h.value != 0.0) label << " " << h.value;
            if (h.number > 0) label << " (step " << h.number << ")";
            if (!h.setName.empty() && h.kind == 4) label << " [" << h.setName << "]";
        }
        step.label = label.str();
        m_out.steps.push_back(std::move(step));
        m_info.push_back({h.number, {datasetName}});
        return m_out.steps.size() - 1;
    }

    bool readResultBlock(const std::string& headerLine) {
        Header h;
        if (!parseHeader(headerLine, h)) return false;
        if (h.format == 2) {
            m_result.addError(ImportStatus::UnsupportedFeature, "FRD_BINARY",
                              "Binary .frd files are not supported; run CalculiX without the 'binary' option", m_line);
            return false;
        }
        const std::size_t nodeWidth = h.format == 0 ? 5 : 10;
        const std::size_t firstValue = 3 + nodeWidth;

        std::string datasetName;
        long long irtype = 1;
        std::vector<Component> components;
        std::size_t stored = 0;
        bool haveDataset = false, skipData = false, started = false;

        ResultField field;
        std::vector<double> record;
        long long currentNode = 0;
        bool open = false;

        auto commit = [&]() -> bool {
            if (!open) return true;
            open = false;
            if (record.size() != stored) {
                m_result.addError(ImportStatus::InvalidInput, "FRD_TRUNCATED_RECORD",
                                  "Node " + std::to_string(currentNode) + " of '" + datasetName + "' has " +
                                  std::to_string(record.size()) + " values, expected " + std::to_string(stored), m_line);
                return false;
            }
            field.ids.push_back(static_cast<NodeId>(currentNode));
            field.values.insert(field.values.end(), record.begin(), record.end());
            return true;
        };

        auto takeValues = [&](const std::string& line) -> bool {
            for (std::size_t k = 0; k < 6 && record.size() < stored; ++k) {
                double v = 0;
                const Num r = fixedReal(line, firstValue + 12 * k, 12, v);
                if (r == Num::Blank) break;
                if (r == Num::Bad) {
                    m_result.addError(ImportStatus::InvalidInput, "FRD_BAD_VALUE", "Malformed number in result record", m_line);
                    return false;
                }
                record.push_back(v);
            }
            return true;
        };

        std::string line;
        while (next(line)) {
            if (isRecord(line, "-3")) {
                if (!commit()) return false;
                if (started) {
                    if (h.numNodes >= 0 && static_cast<long long>(field.ids.size()) != h.numNodes)
                        m_result.addWarning("FRD_NODE_COUNT", "'" + datasetName + "' has " + std::to_string(field.ids.size()) +
                                            " records but the block header announces " + std::to_string(h.numNodes), m_line);
                    m_out.fields.push_back(std::move(field));
                    ++m_result.fieldsRead;
                }
                return true;
            }
            if (isRecord(line, "-4")) {
                datasetName = trimmed(line.size() > 5 ? line.substr(5, 8) : std::string{});
                fixedInt(line, 13 + 5, 5, irtype);
                haveDataset = true;
                if (irtype != 1) {
                    skipData = true;
                    m_result.addWarning("FRD_DATASET_SKIPPED",
                                        "Dataset '" + datasetName + "' is not plain nodal data (IRTYPE " + std::to_string(irtype) +
                                        "); skipped", m_line);
                }
            } else if (isRecord(line, "-5")) {
                Component c;
                c.name = trimmed(line.size() > 5 ? line.substr(5, 8) : std::string{});
                long long exist = 0;
                fixedInt(line, 33, 5, exist);
                c.stored = exist == 0 || exist == 2;
                components.push_back(c);
            } else if (isRecord(line, "-1") || isRecord(line, "-2")) {
                if (!haveDataset || skipData) continue;
                if (!started) {
                    started = true;
                    stored = static_cast<std::size_t>(std::count_if(components.begin(), components.end(),
                                                                    [](const Component& c) { return c.stored; }));
                    if (stored == 0) {
                        m_result.addWarning("FRD_NO_COMPONENTS", "Dataset '" + datasetName + "' has no stored components; skipped", m_line);
                        skipData = true;
                        started = false;
                        continue;
                    }
                    field = ResultField{};
                    field.name = datasetName;
                    field.step = stepFor(h, datasetName);
                    for (const auto& c : components)
                        if (c.stored) field.components.push_back(c.name.empty() ? datasetName : c.name);
                }
                if (isRecord(line, "-1")) {
                    if (!commit()) return false;
                    long long id = 0;
                    if (fixedInt(line, 3, nodeWidth, id) != Num::Ok || id <= 0) {
                        m_result.addError(ImportStatus::InvalidInput, "FRD_BAD_NODE", "Malformed node number in result record", m_line);
                        return false;
                    }
                    currentNode = id;
                    record.clear();
                    open = true;
                    if (!takeValues(line)) return false;
                } else if (open) {
                    if (!takeValues(line)) return false;
                }
            }
        }
        m_result.addError(ImportStatus::InvalidInput, "FRD_UNTERMINATED", "Result block is not terminated by '-3'", m_line);
        return false;
    }
};


/* ----------------------------------------------------------------------------- DAT */

std::vector<std::string> splitWhitespace(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string token;
    while (in >> token) out.push_back(token);
    return out;
}

/** Free-format number of a .dat row; Fortran three-digit exponents lose the 'E'. */
bool datReal(const std::string& token, double& value) {
    auto parse = [&](const std::string& text) {
        char* end = nullptr;
        const double v = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || *end != '\0') return false;
        value = std::isfinite(v) ? v : kNaN;
        return true;
    };
    return parse(token) || parse(restoreExponentLetter(token));
}

bool datInteger(const std::string& token, std::uint64_t& value) {
    if (token.empty() || token.size() > 18) return false;
    for (const char c : token)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    value = std::stoull(token);
    return true;
}

/** Name used in the .frd for the quantities CalculiX prints under a long name in the .dat. */
std::string datFieldName(const std::string& label) {
    const std::string l = lowered(trimmed(label));
    if (l == "displacements") return "DISP";
    if (l == "stresses") return "STRESS";
    if (l == "strains") return "STRAIN";
    if (l == "mechanical strains") return "MESTRAIN";
    if (l == "temperatures") return "NDTEMP";
    std::string name;
    for (const char c : l) name += c == ' ' ? '_' : static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return name;
}

class DatParser {
public:
    DatParser(std::istream& input, FemResults& out, ResultImportResult& result) : m_in(input), m_out(out), m_result(result) {}

    bool run() {
        std::string line;
        while (next(line)) {
            const std::string t = trimmed(line);
            if (t.empty()) continue;
            std::string compact;
            for (const char c : t)
                if (c != ' ') compact += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (compact == "EIGENVALUEOUTPUT") {
                if (!readEigenvalues()) return false;
                continue;
            }
            Header h;
            if (parseHeader(t, h)) {
                if (!readBlock(h)) return false;
            }
        }
        return true;
    }

private:
    struct Header {
        std::string label, set;
        std::vector<std::string> components;
        double time = 0.0;
        bool element = false;   ///< integration-point data: rows start with element id and point number
    };

    std::istream& m_in;
    FemResults& m_out;
    ResultImportResult& m_result;
    std::size_t m_line = 0;
    std::vector<std::unordered_set<std::string>> m_stepFields;   // "NAME/location" keys per step

    bool next(std::string& line) {
        if (!std::getline(m_in, line)) return false;
        ++m_line;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    }

    /** `name (c1,c2,...) for set SET and time T`. */
    bool parseHeader(const std::string& t, Header& h) {
        const std::string l = lowered(t);
        const auto open = t.find('('), close = t.find(')');
        const auto forSet = l.find(" for set ");
        const auto andTime = l.find(" and time");
        if (open == std::string::npos || close == std::string::npos || open > close || forSet == std::string::npos ||
            andTime == std::string::npos || forSet < close || andTime < forSet)
            return false;
        h.label = trimmed(t.substr(0, open));
        if (h.label.empty()) return false;
        std::string inside = t.substr(open + 1, close - open - 1), cell;
        std::vector<std::string> parts;
        std::istringstream in(inside);
        while (std::getline(in, cell, ',')) parts.push_back(trimmed(cell));
        h.element = parts.size() >= 2 && lowered(parts[0]) == "elem" && startsWith(lowered(parts[1]), "integ");
        for (std::size_t i = h.element ? 2 : 0; i < parts.size(); ++i)
            h.components.push_back(parts[i].empty() ? "V" + std::to_string(i) : [&] {
                std::string up = parts[i];
                for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return up;
            }());
        if (h.components.empty()) return false;
        h.set = trimmed(t.substr(forSet + 9, andTime - forSet - 9));
        const std::string timeText = trimmed(t.substr(andTime + 9));
        const auto tokens = splitWhitespace(timeText);
        return !tokens.empty() && datReal(tokens[0], h.time);
    }

    std::size_t stepFor(double time, const std::string& key) {
        for (std::size_t i = 0; i < m_out.steps.size(); ++i) {
            const double a = m_out.steps[i].value;
            if (std::fabs(a - time) <= 1e-12 * std::max(1.0, std::fabs(time)) && !m_stepFields[i].count(key)) return i;
        }
        ResultStep step;
        step.kind = StepKind::Time;
        step.value = time;
        std::ostringstream label;
        label.precision(6);
        label << "time " << time;
        step.label = label.str();
        m_out.steps.push_back(std::move(step));
        m_stepFields.emplace_back();
        return m_out.steps.size() - 1;
    }

    bool readBlock(const Header& h) {
        std::string line;
        // Rows start after the blank line that follows the header.
        std::vector<std::vector<std::string>> rows;
        bool started = false;
        const std::size_t headerLine = m_line;
        while (next(line)) {
            const std::string t = trimmed(line);
            if (t.empty()) {
                if (started) break;
                if (m_line - headerLine > 2) break;   // header without rows (for example TOTALS output)
                continue;
            }
            auto tokens = splitWhitespace(t);
            std::uint64_t first = 0;
            if (!datInteger(tokens[0], first)) {
                if (started) { m_result.addError(ImportStatus::InvalidInput, "DAT_BAD_ROW", "Unexpected text inside the data of '" + h.label + "'", m_line); return false; }
                // "total force ..." style output: values without a record id.
                m_result.addWarning("DAT_BLOCK_SKIPPED", "Block '" + h.label + "' has no record ids; skipped", m_line);
                return true;
            }
            started = true;
            rows.push_back(std::move(tokens));
            rowLines.push_back(m_line);
        }
        std::vector<std::size_t> lines;
        lines.swap(rowLines);
        if (rows.empty()) {
            m_result.addWarning("DAT_BLOCK_SKIPPED", "Block '" + h.label + "' for set " + h.set + " has no rows; skipped", headerLine);
            return true;
        }
        const std::size_t comps = h.components.size();
        const std::size_t lead = h.element ? 2 : 1;
        ResultField field;
        field.name = datFieldName(h.label);
        field.components = h.components;
        field.location = h.element ? ResultLocation::Element : ResultLocation::Node;
        std::vector<std::size_t> pointCount;           // element data: integration points seen per element
        std::unordered_map<std::uint64_t, std::size_t> position;
        for (std::size_t r = 0; r < rows.size(); ++r) {
            const auto& tok = rows[r];
            if (tok.size() != lead + comps) {
                m_result.addError(ImportStatus::InvalidInput, "DAT_ROW_WIDTH",
                                  "Row of '" + h.label + "' has " + std::to_string(tok.size()) + " values, the header announces " +
                                  std::to_string(lead + comps), lines[r]);
                return false;
            }
            std::uint64_t id = 0;
            datInteger(tok[0], id);
            if (id == 0) { m_result.addError(ImportStatus::InvalidInput, "DAT_BAD_ID", "Record id 0 in '" + h.label + "'", lines[r]); return false; }
            std::vector<double> v(comps);
            for (std::size_t c = 0; c < comps; ++c)
                if (!datReal(tok[lead + c], v[c])) {
                    m_result.addError(ImportStatus::InvalidInput, "DAT_BAD_VALUE", "'" + tok[lead + c] + "' is not a number", lines[r]);
                    return false;
                }
            const auto found = position.find(id);
            if (found == position.end()) {
                position.emplace(id, field.ids.size());
                field.ids.push_back(id);
                field.values.insert(field.values.end(), v.begin(), v.end());
                pointCount.push_back(1);
            } else if (h.element) {
                const std::size_t at = found->second;     // running sum; divided below
                for (std::size_t c = 0; c < comps; ++c) field.values[at * comps + c] += v[c];
                ++pointCount[at];
            } else {
                m_result.addError(ImportStatus::InvalidInput, "DAT_DUPLICATE_NODE", "Node " + std::to_string(id) + " appears twice in '" + h.label + "'", lines[r]);
                return false;
            }
        }
        if (h.element) {
            std::size_t maxPoints = 0;
            for (std::size_t e = 0; e < field.ids.size(); ++e) {
                maxPoints = std::max(maxPoints, pointCount[e]);
                for (std::size_t c = 0; c < comps; ++c) field.values[e * comps + c] /= static_cast<double>(pointCount[e]);
            }
            m_result.addWarning("DAT_IP_AVERAGED",
                                "'" + field.name + "' (set " + h.set + ") is given at integration points (up to " + std::to_string(maxPoints) +
                                " per element); each element shows the unweighted mean of its points", headerLine);
        }
        const std::string key = field.name + (h.element ? "/e" : "/n");
        // The same quantity printed for two sets in one increment is one field: union of the records.
        for (auto& existing : m_out.fields) {
            const double a = m_out.steps[existing.step].value;
            if (std::fabs(a - h.time) <= 1e-12 * std::max(1.0, std::fabs(h.time)) && existing.name == field.name &&
                existing.location == field.location) {
                std::unordered_set<std::uint64_t> have(existing.ids.begin(), existing.ids.end());
                for (std::size_t i = 0; i < field.ids.size(); ++i) {
                    if (!have.insert(field.ids[i]).second) continue;
                    existing.ids.push_back(field.ids[i]);
                    existing.values.insert(existing.values.end(), field.values.begin() + static_cast<std::ptrdiff_t>(i * comps),
                                           field.values.begin() + static_cast<std::ptrdiff_t>((i + 1) * comps));
                }
                return true;
            }
        }
        const std::size_t step = stepFor(h.time, key);
        m_stepFields[step].insert(key);
        field.step = step;
        m_out.fields.push_back(std::move(field));
        ++m_result.fieldsRead;
        return true;
    }

    std::vector<std::size_t> rowLines;

    bool readEigenvalues() {
        std::string line;
        bool started = false;
        while (next(line)) {
            const std::string t = trimmed(line);
            if (t.empty()) {
                if (started) return true;
                continue;
            }
            const auto tokens = splitWhitespace(t);
            std::uint64_t mode = 0;
            if (!datInteger(tokens[0], mode)) {
                if (started) return true;
                continue;       // the table has a three line caption
            }
            if (tokens.size() < 5) {
                m_result.addError(ImportStatus::InvalidInput, "DAT_EIGEN_ROW", "Eigenvalue row needs mode, eigenvalue, rad/time, cycles/time and imaginary part", m_line);
                return false;
            }
            EigenMode m;
            m.mode = static_cast<std::size_t>(mode);
            double v[4];
            for (int k = 0; k < 4; ++k)
                if (!datReal(tokens[1 + static_cast<std::size_t>(k)], v[k])) {
                    m_result.addError(ImportStatus::InvalidInput, "DAT_EIGEN_ROW", "'" + tokens[1 + static_cast<std::size_t>(k)] + "' is not a number", m_line);
                    return false;
                }
            m.eigenvalue = v[0]; m.omega = v[1]; m.frequency = v[2]; m.imaginary = v[3];
            if (m.eigenvalue < 0.0)
                m_result.addWarning("DAT_NEGATIVE_EIGENVALUE", "Mode " + std::to_string(m.mode) + " has a negative eigenvalue (rigid body motion or unstable model)", m_line);
            m_out.eigenmodes.push_back(m);
            started = true;
        }
        return true;
    }
};

/* ----------------------------------------------------------------------------- CSV */

struct CsvColumn {
    std::string name;
    std::vector<double> values;
};

std::vector<std::string> splitDelimited(const std::string& line, char delimiter) {
    std::vector<std::string> cells;
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') { cell += '"'; ++i; }
            else quoted = !quoted;
        } else if (!quoted && (delimiter == ' ' ? (c == ' ' || c == '\t') : c == delimiter)) {
            if (delimiter == ' ') {
                if (!cell.empty()) { cells.push_back(cell); cell.clear(); }
            } else {
                cells.push_back(trimmed(cell));
                cell.clear();
            }
        } else {
            cell += c;
        }
    }
    if (delimiter != ' ' || !cell.empty()) cells.push_back(trimmed(cell));
    return cells;
}

char detectDelimiter(const std::string& header) {
    const std::size_t commas = static_cast<std::size_t>(std::count(header.begin(), header.end(), ','));
    const std::size_t semis = static_cast<std::size_t>(std::count(header.begin(), header.end(), ';'));
    const std::size_t tabs = static_cast<std::size_t>(std::count(header.begin(), header.end(), '\t'));
    if (tabs >= commas && tabs >= semis && tabs > 0) return '\t';
    if (semis > commas) return ';';
    if (commas > 0) return ',';
    return ' ';
}

bool isNodeColumnName(const std::string& name) {
    const std::string n = lowered(name);
    return n == "node" || n == "node_id" || n == "nodeid" || n == "nid" || n == "id";
}

bool isElementColumnName(const std::string& name) {
    const std::string n = lowered(name);
    return n == "element" || n == "element_id" || n == "elementid" || n == "elem" || n == "elem_id" || n == "eid";
}

/** Parses a table cell; empty or non-finite becomes NaN. Returns false for text that is not a number. */
bool parseCell(std::string text, bool decimalComma, double& value) {
    text = trimmed(text);
    if (text.empty()) { value = kNaN; return true; }
    if (decimalComma && text.find('.') == std::string::npos) std::replace(text.begin(), text.end(), ',', '.');
    char* end = nullptr;
    const double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') {
        const std::string l = lowered(text);
        if (l == "nan" || l == "na" || l == "n/a" || l == "null" || l == "-") { value = kNaN; return true; }
        return false;
    }
    value = std::isfinite(v) ? v : kNaN;
    return true;
}

/** Splits "U_x" into ("U", "x") when the name ends with one of the given suffixes. */
bool splitSuffix(const std::string& name, const std::string& suffix, std::string& base) {
    const std::string l = lowered(name);
    const std::size_t n = suffix.size();
    if (l.size() <= n + 1) return false;
    if (l.compare(l.size() - n, n, suffix) != 0) return false;
    const char separator = l[l.size() - n - 1];
    if (separator != '_' && separator != '.') return false;
    base = name.substr(0, name.size() - n - 1);
    return !base.empty();
}

} // namespace

/* --------------------------------------------------------------------------- public */

std::string CalculixFrdImporter::formatName() const { return "CalculiX FRD"; }

bool CalculixFrdImporter::canRead(const std::filesystem::path& path) const { return lowerExtension(path) == ".frd"; }

ResultImportResult CalculixFrdImporter::parse(std::istream& input, FemResults& destination) const {
    ResultImportResult result;
    FemResults parsed;
    parsed.format = formatName();
    FrdParser parser(input, parsed, result);
    if (!parser.run()) return result;
    if (parsed.fields.empty()) {
        result.addError(ImportStatus::InvalidInput, "FRD_NO_RESULTS", "The file contains no nodal result block");
        return result;
    }
    result.stepsRead = parsed.steps.size();
    destination = std::move(parsed);
    return result;
}

ResultImportResult CalculixFrdImporter::read(const std::filesystem::path& path, FemResults& destination) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ResultImportResult result;
        result.addError(ImportStatus::IoError, "IO_OPEN", "Cannot open " + path.string());
        return result;
    }
    return parse(in, destination);
}

std::string CalculixDatImporter::formatName() const { return "CalculiX DAT"; }

bool CalculixDatImporter::canRead(const std::filesystem::path& path) const { return lowerExtension(path) == ".dat"; }

ResultImportResult CalculixDatImporter::parse(std::istream& input, FemResults& destination) const {
    ResultImportResult result;
    FemResults parsed;
    parsed.format = formatName();
    DatParser parser(input, parsed, result);
    if (!parser.run()) return result;
    if (parsed.fields.empty() && parsed.eigenmodes.empty()) {
        result.addError(ImportStatus::InvalidInput, "DAT_NO_RESULTS",
                        "No 'name (components) for set ... and time ...' block and no eigenvalue table found (is *NODE PRINT / *EL PRINT used?)");
        return result;
    }
    result.stepsRead = parsed.steps.size();
    destination = std::move(parsed);
    return result;
}

ResultImportResult CalculixDatImporter::read(const std::filesystem::path& path, FemResults& destination) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ResultImportResult result;
        result.addError(ImportStatus::IoError, "IO_OPEN", "Cannot open " + path.string());
        return result;
    }
    return parse(in, destination);
}

ResultImportResult mergeResults(FemResults& base, const FemResults& extra) {
    ResultImportResult result;
    if (base.steps.empty() && base.fields.empty()) {
        base = extra;
        return result;
    }
    // Step correspondence.
    std::vector<std::size_t> map(extra.steps.size());
    for (std::size_t i = 0; i < extra.steps.size(); ++i) {
        const ResultStep& e = extra.steps[i];
        std::size_t found = static_cast<std::size_t>(-1);
        for (std::size_t j = 0; j < base.steps.size() && found == static_cast<std::size_t>(-1); ++j) {
            const ResultStep& b = base.steps[j];
            if (e.kind == StepKind::Frequency || b.kind == StepKind::Frequency) {
                if (e.kind == b.kind && e.mode == b.mode) found = j;
            } else if (std::fabs(e.value - b.value) <= 1e-5 * std::max(std::fabs(e.value), std::fabs(b.value))) {
                found = j;
            }
        }
        if (found == static_cast<std::size_t>(-1)) {
            base.steps.push_back(e);
            found = base.steps.size() - 1;
            result.addWarning("MERGE_NEW_STEP", "No step of the first file matches '" + e.label + "'; it is added as a separate step");
        }
        map[i] = found;
    }
    for (const auto& f : extra.fields) {
        const std::size_t step = map[f.step];
        bool present = false;
        for (const auto& b : base.fields)
            if (b.step == step && b.name == f.name && b.location == f.location) { present = true; break; }
        if (present) {
            result.addWarning("MERGE_FIELD_PRESENT", "'" + f.name + "' already exists for step '" + base.steps[step].label + "'; the one of the later file is ignored");
            continue;
        }
        ResultField copy = f;
        copy.step = step;
        base.fields.push_back(std::move(copy));
    }
    if (base.modelNodeIds.empty()) {
        base.modelNodeIds = extra.modelNodeIds;
        base.modelCoordinates = extra.modelCoordinates;
    }
    if (base.eigenmodes.empty()) base.eigenmodes = extra.eigenmodes;
    if (base.format != extra.format) base.format += " + " + extra.format;
    return result;
}

std::string CsvResultImporter::formatName() const { return "Delimited text table"; }

bool CsvResultImporter::canRead(const std::filesystem::path& path) const {
    const auto ext = lowerExtension(path);
    return ext == ".csv" || ext == ".tsv" || ext == ".txt";
}

ResultImportResult CsvResultImporter::read(const std::filesystem::path& path, FemResults& destination) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ResultImportResult result;
        result.addError(ImportStatus::IoError, "IO_OPEN", "Cannot open " + path.string());
        return result;
    }
    return parse(in, destination);
}

ResultImportResult CsvResultImporter::parse(std::istream& input, FemResults& destination) const {
    ResultImportResult result;
    std::string line;
    std::size_t lineNo = 0;
    std::vector<std::string> header;
    char delimiter = ',';
    bool haveHeader = false;
    std::size_t nodeColumn = 0;
    std::vector<CsvColumn> columns;       // all columns, including the node one
    std::vector<std::uint64_t> ids;
    std::unordered_set<std::uint64_t> seen;
    ResultLocation location = ResultLocation::Node;
    bool decimalComma = false;
    bool sawFirstLine = true;

    while (std::getline(input, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (sawFirstLine) {
            sawFirstLine = false;
            if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) line.erase(0, 3);
        }
        const std::string t = trimmed(line);
        if (t.empty() || t[0] == '#' || startsWith(t, "//")) continue;

        if (!haveHeader) {
            delimiter = detectDelimiter(t);
            decimalComma = delimiter != ',';
            header = splitDelimited(t, delimiter);
            if (header.size() < 2) {
                result.addError(ImportStatus::InvalidInput, "CSV_HEADER", "The header needs a node column and at least one value column", lineNo);
                return result;
            }
            bool found = false, foundElement = false;
            std::unordered_set<std::string> names;
            for (std::size_t i = 0; i < header.size(); ++i) {
                if (header[i].empty()) {
                    result.addError(ImportStatus::InvalidInput, "CSV_HEADER", "Empty column name", lineNo);
                    return result;
                }
                if (!names.insert(lowered(header[i])).second) {
                    result.addError(ImportStatus::InvalidInput, "CSV_HEADER", "Duplicate column name '" + header[i] + "'", lineNo);
                    return result;
                }
                if (isNodeColumnName(header[i]) && !found) { nodeColumn = i; found = true; }
                else if (isElementColumnName(header[i]) && !foundElement) { if (!found) nodeColumn = i; foundElement = true; }
            }
            if (found && foundElement) {
                result.addError(ImportStatus::InvalidInput, "CSV_NODE_AND_ELEMENT",
                                "The table has both a node and an element column; use one table per location", lineNo);
                return result;
            }
            if (!found && !foundElement) {
                result.addError(ImportStatus::InvalidInput, "CSV_NO_NODE_COLUMN",
                                "No id column: name one column node/node_id/nid/id (nodal) or element/element_id/elem/eid (per element)", lineNo);
                return result;
            }
            if (foundElement) location = ResultLocation::Element;
            columns.resize(header.size());
            for (std::size_t i = 0; i < header.size(); ++i) columns[i].name = header[i];
            haveHeader = true;
            continue;
        }

        auto cells = splitDelimited(t, delimiter);
        if (cells.size() != header.size()) {
            result.addError(ImportStatus::InvalidInput, "CSV_ROW_WIDTH",
                            "Row has " + std::to_string(cells.size()) + " cells, the header has " + std::to_string(header.size()), lineNo);
            return result;
        }
        double idValue = 0;
        if (!parseCell(cells[nodeColumn], decimalComma, idValue) || !std::isfinite(idValue) || idValue < 1 ||
            idValue != std::floor(idValue) || idValue > 9.0e15) {
            result.addError(ImportStatus::InvalidInput, "CSV_BAD_NODE", std::string(location == ResultLocation::Node ? "Node" : "Element") + " id '" + cells[nodeColumn] + "' is not a positive integer", lineNo);
            return result;
        }
        const std::uint64_t id = static_cast<std::uint64_t>(idValue);
        if (!seen.insert(id).second) {
            result.addError(ImportStatus::InvalidInput, "CSV_DUPLICATE_NODE",
                            std::string(location == ResultLocation::Node ? "Node " : "Element ") + std::to_string(id) + " appears twice", lineNo);
            return result;
        }
        ids.push_back(id);
        for (std::size_t i = 0; i < cells.size(); ++i) {
            if (i == nodeColumn) continue;
            double v = 0;
            if (!parseCell(cells[i], decimalComma, v)) {
                result.addError(ImportStatus::InvalidInput, "CSV_BAD_VALUE",
                                "'" + cells[i] + "' in column '" + header[i] + "' is not a number", lineNo);
                return result;
            }
            columns[i].values.push_back(v);
        }
    }
    if (!haveHeader) {
        result.addError(ImportStatus::InvalidInput, "CSV_EMPTY", "The file has no header line");
        return result;
    }
    if (ids.empty()) {
        result.addError(ImportStatus::InvalidInput, "CSV_NO_ROWS", "The table has a header but no data rows");
        return result;
    }

    FemResults parsed;
    parsed.format = formatName();
    parsed.steps.push_back({"table", 0.0});

    // Group <name>_x/_y/_z and <name>_xx/_yy/_zz/_xy/_yz/_zx columns into vector and tensor fields.
    std::vector<bool> consumed(columns.size(), false);
    consumed[nodeColumn] = true;
    auto findColumn = [&](const std::string& base, const std::string& suffix) -> int {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            if (consumed[i]) continue;
            std::string b;
            if (splitSuffix(columns[i].name, suffix, b) && lowered(b) == lowered(base)) return static_cast<int>(i);
        }
        return -1;
    };
    auto makeField = [&](const std::string& name, const std::vector<int>& idx, const std::vector<std::string>& labels) {
        ResultField f;
        f.name = name;
        f.components = labels;
        f.step = 0;
        f.location = location;
        f.ids = ids;
        f.values.resize(ids.size() * idx.size());
        for (std::size_t c = 0; c < idx.size(); ++c) {
            for (std::size_t r = 0; r < ids.size(); ++r) f.values[r * idx.size() + c] = columns[static_cast<std::size_t>(idx[c])].values[r];
            consumed[static_cast<std::size_t>(idx[c])] = true;
        }
        parsed.fields.push_back(std::move(f));
    };
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (consumed[i]) continue;
        std::string base;
        if (splitSuffix(columns[i].name, "xx", base)) {
            const int a = findColumn(base, "xx"), b = findColumn(base, "yy"), c = findColumn(base, "zz"),
                      d = findColumn(base, "xy"), e = findColumn(base, "yz");
            int f = findColumn(base, "zx");
            if (f < 0) f = findColumn(base, "xz");
            if (a >= 0 && b >= 0 && c >= 0 && d >= 0 && e >= 0 && f >= 0) {
                makeField(base, {a, b, c, d, e, f}, {"XX", "YY", "ZZ", "XY", "YZ", "ZX"});
                continue;
            }
        }
        if (splitSuffix(columns[i].name, "x", base)) {
            const int a = findColumn(base, "x"), b = findColumn(base, "y"), c = findColumn(base, "z");
            if (a >= 0 && b >= 0 && c >= 0) {
                makeField(base, {a, b, c}, {"X", "Y", "Z"});
                continue;
            }
        }
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (consumed[i]) continue;
        makeField(columns[i].name, {static_cast<int>(i)}, {columns[i].name});
    }
    if (parsed.fields.empty()) {
        result.addError(ImportStatus::InvalidInput, "CSV_NO_FIELDS", "The table has no value column");
        return result;
    }
    result.stepsRead = 1;
    result.fieldsRead = parsed.fields.size();
    destination = std::move(parsed);
    return result;
}

ResultImportResult readResults(const std::filesystem::path& path, FemResults& destination) {
    const CalculixFrdImporter frd;
    const CsvResultImporter csv;
    const CalculixDatImporter dat;
    const ResultImporter* importers[] = {&frd, &dat, &csv};
    for (const ResultImporter* importer : importers)
        if (importer->canRead(path)) return importer->read(path, destination);
    ResultImportResult result;
    result.addError(ImportStatus::UnsupportedFormat, "FORMAT",
                    "Unsupported result file '" + path.filename().string() + "' (expected .frd, .dat, .csv, .tsv or .txt)");
    return result;
}

ResultImportResult readResultsMerged(const std::vector<std::filesystem::path>& paths, FemResults& destination) {
    ResultImportResult total;
    if (paths.empty()) {
        total.addError(ImportStatus::InvalidInput, "NO_FILES", "No result file given");
        return total;
    }
    FemResults merged;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        FemResults one;
        ResultImportResult r = readResults(paths[i], one);
        for (auto& d : r.diagnostics) {
            if (paths.size() > 1) d.message = paths[i].filename().string() + ": " + d.message;
            total.diagnostics.push_back(d);
        }
        if (!r) {
            total.status = r.status;
            return total;
        }
        if (i == 0) {
            merged = std::move(one);
        } else {
            ResultImportResult m = mergeResults(merged, one);
            for (auto& d : m.diagnostics) total.diagnostics.push_back(d);
        }
    }
    total.stepsRead = merged.steps.size();
    total.fieldsRead = merged.fields.size();
    destination = std::move(merged);
    return total;
}

} // namespace OpenModel
