#include <OpenModel/Heat1DScript.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

/**
 * @file Heat1DScript.cpp
 * @brief Parser of the one-dimensional heat script and its CSV output (see Heat1DScript.hpp).
 *
 * The parser reads every line before it decides: all problems are reported, each with its line number. The model is built
 * only when the script is complete and valid; the final check is Heat1D::validate().
 */

namespace OpenModel::Heat1D {
namespace {

constexpr double kPi = 3.14159265358979323846;

/** @brief One script line: the keyword, the words without `=`, and the `key=value` pairs. */
struct Command {
    int line = 0;
    std::string keyword;
    std::vector<std::string> bare;
    std::map<std::string, std::string> values;
};

/** @brief Splits the text into commands, dropping comments and blank lines. Malformed words are reported. */
std::vector<Command> tokenize(const std::string& text, std::vector<std::string>& errors) {
    std::vector<Command> commands;
    std::istringstream stream(text);
    std::string raw;
    int lineNumber = 0;
    while (std::getline(stream, raw)) {
        ++lineNumber;
        if (const auto hash = raw.find('#'); hash != std::string::npos) raw.erase(hash);
        std::istringstream words(raw);
        std::vector<std::string> tokens;
        std::string w;
        while (words >> w) tokens.push_back(w);
        if (tokens.empty()) continue;

        Command c;
        c.line = lineNumber;
        c.keyword = tokens.front();
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            const std::string& t = tokens[i];
            const auto eq = t.find('=');
            if (eq == std::string::npos) {
                c.bare.push_back(t);
            } else if (eq == 0 || eq + 1 == t.size()) {
                errors.push_back("line " + std::to_string(lineNumber) + ": malformed word '" + t + "'");
            } else if (!c.values.emplace(t.substr(0, eq), t.substr(eq + 1)).second) {
                errors.push_back("line " + std::to_string(lineNumber) + ": '" + t.substr(0, eq) + "' given twice");
            }
        }
        commands.push_back(std::move(c));
    }
    return commands;
}

std::string where(const Command& c) { return "line " + std::to_string(c.line) + " (" + c.keyword + "): "; }

/** @brief Number in full (no trailing text, finite). */
bool toNumber(const std::string& s, double& value) {
    try {
        std::size_t pos = 0;
        value = std::stod(s, &pos);
        return pos == s.size() && std::isfinite(value);
    } catch (const std::exception&) {
        return false;
    }
}

/** @brief Integer value (a number with no fractional part). */
bool toInteger(const std::string& s, long& value) {
    double v = 0.0;
    if (!toNumber(s, v) || v != std::floor(v)) return false;
    value = static_cast<long>(v);
    return true;
}

/** @brief Required number `key=`. */
bool requireNumber(const Command& c, const char* key, double& out, std::vector<std::string>& errs) {
    const auto it = c.values.find(key);
    if (it == c.values.end()) {
        errs.push_back(where(c) + "missing " + key + "=");
        return false;
    }
    if (!toNumber(it->second, out)) {
        errs.push_back(where(c) + key + " is not a number: " + it->second);
        return false;
    }
    return true;
}

/** @brief Optional number `key=`: `out` keeps its value when the key is absent. */
bool optionalNumber(const Command& c, const char* key, double& out, std::vector<std::string>& errs) {
    const auto it = c.values.find(key);
    if (it == c.values.end()) return true;
    if (!toNumber(it->second, out)) {
        errs.push_back(where(c) + key + " is not a number: " + it->second);
        return false;
    }
    return true;
}

/** @brief Reports keys the command does not know. */
bool checkKeys(const Command& c, const std::vector<std::string>& allowed, std::vector<std::string>& errs) {
    bool ok = true;
    for (const auto& kv : c.values) {
        if (std::find(allowed.begin(), allowed.end(), kv.first) == allowed.end()) {
            errs.push_back(where(c) + "unknown key '" + kv.first + "'");
            ok = false;
        }
    }
    return ok;
}

/** @brief Comma-separated list of numbers. */
bool toNumberList(const std::string& s, std::vector<double>& out) {
    out.clear();
    std::istringstream items(s);
    std::string item;
    while (std::getline(items, item, ',')) {
        double v = 0.0;
        if (!toNumber(item, v)) return false;
        out.push_back(v);
    }
    return !out.empty();
}

/**
 * @brief Parses a conductivity table `T1:k1,T2:k2,...`: temperatures strictly increasing, conductivities positive, at
 *        least two pairs. Errors are appended with the given prefix.
 */
bool parseConductivityTable(const std::string& text, std::vector<double>& temps, std::vector<double>& values,
                            std::vector<std::string>& errs, const std::string& prefix) {
    temps.clear();
    values.clear();
    if (!text.empty() && text.back() == ',') { // a trailing comma is a typo, not an empty pair
        errs.push_back(prefix + "kT must be pairs T:k, got '" + text + "'");
        return false;
    }
    std::istringstream items(text);
    std::string item;
    while (std::getline(items, item, ',')) {
        const auto colon = item.find(':');
        double t = 0.0;
        double kv = 0.0;
        if (colon == std::string::npos || !toNumber(item.substr(0, colon), t) || !toNumber(item.substr(colon + 1), kv)) {
            errs.push_back(prefix + "kT must be pairs T:k, got '" + item + "'");
            return false;
        }
        if (!temps.empty() && !(t > temps.back())) {
            errs.push_back(prefix + "kT temperatures must be strictly increasing");
            return false;
        }
        if (!(kv > 0.0)) {
            errs.push_back(prefix + "kT conductivities must be positive");
            return false;
        }
        temps.push_back(t);
        values.push_back(kv);
    }
    if (temps.size() < 2) {
        errs.push_back(prefix + "kT needs at least two pairs");
        return false;
    }
    return true;
}

/** @brief Piecewise-linear function through (xs, ys); constant beyond the ends. */
ScalarFn piecewiseLinear(std::vector<double> xs, std::vector<double> ys) {
    return [xs = std::move(xs), ys = std::move(ys)](double x) {
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        const auto it = std::upper_bound(xs.begin(), xs.end(), x);
        const std::size_t i = static_cast<std::size_t>(it - xs.begin()) - 1;
        const double s = (x - xs[i]) / (xs[i + 1] - xs[i]);
        return ys[i] + s * (ys[i + 1] - ys[i]);
    };
}

/**
 * @brief A material of the script: conductivity and volumetric heat capacity.
 *
 * The conductivity is the constant `k`, or the table `kTemperatures`/`kValues` (piecewise linear in T, constant beyond
 * its ends) when the table is not empty.
 */
struct MaterialDef {
    std::string id;
    double k = 0.0;
    double rhocp = 0.0;
    std::vector<double> kTemperatures;
    std::vector<double> kValues;
};

/** @brief A segment of the axis: a material over a length, meshed with a number of elements. */
struct Segment {
    std::string material;
    double length = 0.0;
    long elements = 0;
    int line = 0;
    bool hasShape = false; ///< the segment has its own section (shape= on the segment line)
    ScalarFn area;         ///< its area and perimeter when hasShape
    ScalarFn perimeter;
};

/**
 * @brief Index of the segment that holds x: the first one whose end lies beyond x (the last one at and past the end).
 * @param ends  end position of each segment, increasing
 */
std::size_t segmentIndex(const std::vector<double>& ends, double x) {
    const auto it = std::upper_bound(ends.begin(), ends.end(), x);
    const std::size_t i = static_cast<std::size_t>(it - ends.begin());
    return i < ends.size() ? i : ends.size() - 1;
}

TimeFn constantTime(double v) {
    return [v](double) { return v; };
}

ScalarFn constantScalar(double v) {
    return [v](double) { return v; };
}

/** @brief Conductivity of a material as a function of T: the constant k, or the tabulated values. */
ScalarFn conductivityOf(const MaterialDef& md) {
    if (md.kTemperatures.empty()) return constantScalar(md.k);
    return piecewiseLinear(md.kTemperatures, md.kValues);
}

/** @brief Area and perimeter of a simple section, both constant along the axis. */
struct ConstantSection {
    ScalarFn area;
    ScalarFn perimeter;
};

/** @brief Keys of a simple section: rect a b, circle r, tube ro ri, polygon sides r. Empty for any other kind. */
std::vector<std::string> sectionKeys(const std::string& kind) {
    if (kind == "rect") return {"a", "b"};
    if (kind == "circle") return {"r"};
    if (kind == "tube") return {"ro", "ri"};
    if (kind == "polygon") return {"sides", "r"};
    return {};
}

/**
 * @brief Reads a simple section (rect, circle, tube or polygon) from a command. The caller checks the keys with
 *        sectionKeys first.
 * @param c     the command holding the section values
 * @param kind  the section kind, one of rect, circle, tube, polygon
 * @param out   receives the area and the perimeter when the section is valid
 * @param errs  receives the problems, prefixed with the line number
 * @return true when the section is valid
 */
bool readConstantSection(const Command& c, const std::string& kind, ConstantSection& out, std::vector<std::string>& errs) {
    if (kind == "rect") {
        double a = 0.0, b = 0.0;
        if (!requireNumber(c, "a", a, errs) || !requireNumber(c, "b", b, errs)) return false;
        if (!(a > 0.0 && b > 0.0)) {
            errs.push_back(where(c) + "a and b must be positive");
            return false;
        }
        out.area = constantScalar(a * b);
        out.perimeter = constantScalar(2.0 * (a + b));
        return true;
    }
    if (kind == "circle") {
        double r = 0.0;
        if (!requireNumber(c, "r", r, errs)) return false;
        if (!(r > 0.0)) {
            errs.push_back(where(c) + "r must be positive");
            return false;
        }
        out.area = constantScalar(kPi * r * r);
        out.perimeter = constantScalar(2.0 * kPi * r);
        return true;
    }
    if (kind == "tube") {
        double ro = 0.0, ri = 0.0;
        if (!requireNumber(c, "ro", ro, errs) || !requireNumber(c, "ri", ri, errs)) return false;
        if (!(ri >= 0.0 && ro > ri)) {
            errs.push_back(where(c) + "need 0 <= ri < ro");
            return false;
        }
        out.area = constantScalar(kPi * (ro * ro - ri * ri));
        // Both surfaces (outer and inner) are exposed to the same lateral exchange.
        out.perimeter = constantScalar(2.0 * kPi * (ro + ri));
        return true;
    }
    if (kind == "polygon") {
        double r = 0.0;
        long sides = 0;
        const std::string sidesText = c.values.count("sides") ? c.values.at("sides") : std::string();
        bool ok = true;
        if (sidesText.empty()) {
            errs.push_back(where(c) + "missing sides=");
            ok = false;
        } else if (!toInteger(sidesText, sides) || sides < 3) {
            errs.push_back(where(c) + "sides must be an integer >= 3");
            ok = false;
        }
        if (!requireNumber(c, "r", r, errs)) return false;
        if (!(r > 0.0)) {
            errs.push_back(where(c) + "r must be positive");
            ok = false;
        }
        if (!ok) return false;
        const double n = static_cast<double>(sides);
        out.area = constantScalar(0.5 * n * r * r * std::sin(2.0 * kPi / n));
        out.perimeter = constantScalar(2.0 * n * r * std::sin(kPi / n));
        return true;
    }
    errs.push_back(where(c) + "unknown section '" + kind + "'");
    return false;
}

} // namespace

bool parseScript(const std::string& text, ScriptProblem& problem, std::vector<std::string>& errors) {
    std::vector<std::string> errs;
    const std::vector<Command> commands = tokenize(text, errs);

    bool sawAnalysis = false;
    bool sawSection = false;
    bool sawThermal = false;
    bool sawInitial = false;
    bool sawLateral = false;
    bool sawHeat = false;
    bool sawJob = false;
    bool sawStep = false;
    bool sawGeometry = false; ///< length= or elements= on the analysis line

    std::vector<MaterialDef> materials;
    std::vector<Segment> segments;

    double length = 0.0;
    long elements = 0;
    double k = 0.0;
    double rhocp = 0.0;
    double T0 = 0.0;
    double q = 0.0;
    double latH = 0.0;
    double latEps = 0.0;
    double latTinf = 0.0;
    std::array<Boundary, 2> ends{}; // [0] = left, [1] = right
    std::array<bool, 2> endSet{false, false};

    ScalarFn area;
    ScalarFn perimeter;
    bool tableSection = false;
    std::vector<double> tableX;
    std::vector<double> tableA;
    std::vector<double> tableP;
    int tableLine = 0;

    Options options;

    for (std::size_t index = 0; index < commands.size(); ++index) {
        const Command& c = commands[index];

        if (c.keyword == "analysis") {
            if (index != 0) {
                errs.push_back(where(c) + "must be the first command");
                continue;
            }
            sawAnalysis = true;
            if (c.bare.size() != 2 || c.bare[0] != "heat" || c.bare[1] != "1d") {
                errs.push_back(where(c) + "expected 'analysis heat 1d [length=L elements=N]'");
                continue;
            }
            checkKeys(c, {"length", "elements"}, errs);
            // length and elements are needed only by the thermal line; with material and segment lines they follow from the segments.
            sawGeometry = c.values.count("length") > 0 || c.values.count("elements") > 0;
            if (sawGeometry) {
                requireNumber(c, "length", length, errs);
                if (c.values.count("length") && !(length > 0.0)) errs.push_back(where(c) + "length must be positive");
                const std::string elementsText = c.values.count("elements") ? c.values.at("elements") : std::string();
                if (elementsText.empty()) {
                    errs.push_back(where(c) + "missing elements=");
                } else if (!toInteger(elementsText, elements) || elements < 1) {
                    errs.push_back(where(c) + "elements must be an integer >= 1");
                }
            }
        } else if (!sawAnalysis) {
            errs.push_back(where(c) + "the analysis line 'analysis heat 1d ...' must come first");
        } else if (c.keyword == "section") {
            if (sawSection) {
                errs.push_back(where(c) + "only one section is allowed");
                continue;
            }
            sawSection = true;
            const std::string kind = c.bare.size() == 1 ? c.bare[0] : std::string();
            if (kind == "table") {
                checkKeys(c, {"x", "A", "P"}, errs);
                const auto xs = c.values.count("x") ? c.values.at("x") : std::string();
                const auto as = c.values.count("A") ? c.values.at("A") : std::string();
                const auto ps = c.values.count("P") ? c.values.at("P") : std::string();
                if (xs.empty() || as.empty() || ps.empty()) {
                    errs.push_back(where(c) + "a table needs x=, A= and P=");
                } else if (!toNumberList(xs, tableX) || !toNumberList(as, tableA) || !toNumberList(ps, tableP)) {
                    errs.push_back(where(c) + "x, A and P must be comma-separated numbers");
                } else if (tableX.size() != tableA.size() || tableX.size() != tableP.size() || tableX.size() < 2) {
                    errs.push_back(where(c) + "x, A and P need the same number of values (at least 2)");
                } else {
                    tableSection = true;
                    tableLine = c.line;
                    for (std::size_t i = 0; i < tableX.size(); ++i) {
                        if (i > 0 && !(tableX[i] > tableX[i - 1])) {
                            errs.push_back(where(c) + "x must be strictly increasing");
                            break;
                        }
                        if (!(tableA[i] > 0.0)) {
                            errs.push_back(where(c) + "A must be positive");
                            break;
                        }
                        if (!(tableP[i] >= 0.0)) {
                            errs.push_back(where(c) + "P must be >= 0");
                            break;
                        }
                    }
                }
            } else if (!sectionKeys(kind).empty()) {
                checkKeys(c, sectionKeys(kind), errs);
                ConstantSection cs;
                if (readConstantSection(c, kind, cs, errs)) {
                    area = cs.area;
                    perimeter = cs.perimeter;
                }
            } else {
                errs.push_back(where(c) + "unknown section '" + kind + "' (rect, circle, tube, polygon, table)");
            }
        } else if (c.keyword == "material") {
            checkKeys(c, {"id", "k", "kT", "rhocp"}, errs);
            const std::string id = c.values.count("id") ? c.values.at("id") : std::string();
            double mk = 0.0;
            double mr = 0.0;
            std::vector<double> kTemps;
            std::vector<double> kVals;
            if (id.empty()) {
                errs.push_back(where(c) + "missing id=");
            } else if (std::any_of(materials.begin(), materials.end(), [&](const MaterialDef& md) { return md.id == id; })) {
                errs.push_back(where(c) + "material '" + id + "' is defined twice");
            }
            bool okK = false;
            if (c.values.count("k") && c.values.count("kT")) {
                errs.push_back(where(c) + "give k= or kT=, not both");
            } else if (c.values.count("kT")) {
                okK = parseConductivityTable(c.values.at("kT"), kTemps, kVals, errs, where(c));
            } else if (c.values.count("k")) {
                okK = requireNumber(c, "k", mk, errs);
                if (okK && !(mk > 0.0)) {
                    errs.push_back(where(c) + "k must be positive");
                    okK = false;
                }
            } else {
                errs.push_back(where(c) + "missing k= or kT=");
            }
            const bool okR = requireNumber(c, "rhocp", mr, errs);
            if (okR && !(mr > 0.0)) errs.push_back(where(c) + "rhocp must be positive");
            if (!id.empty() && okK && okR && mr > 0.0) {
                MaterialDef md;
                md.id = id;
                md.k = mk;
                md.rhocp = mr;
                md.kTemperatures = kTemps;
                md.kValues = kVals;
                materials.push_back(md);
            }
        } else if (c.keyword == "segment") {
            const std::string shapeKind = c.values.count("shape") ? c.values.at("shape") : std::string();
            std::vector<std::string> allowed{"material", "length", "elements", "shape"};
            if (!shapeKind.empty()) {
                const auto keys = sectionKeys(shapeKind);
                allowed.insert(allowed.end(), keys.begin(), keys.end());
            }
            checkKeys(c, allowed, errs);
            const std::string id = c.values.count("material") ? c.values.at("material") : std::string();
            bool ok = true;
            if (id.empty()) {
                errs.push_back(where(c) + "missing material=");
                ok = false;
            } else if (!std::any_of(materials.begin(), materials.end(), [&](const MaterialDef& md) { return md.id == id; })) {
                errs.push_back(where(c) + "unknown material '" + id + "' (define it with a material line before)");
                ok = false;
            }
            double segLength = 0.0;
            if (!requireNumber(c, "length", segLength, errs)) {
                ok = false;
            } else if (!(segLength > 0.0)) {
                errs.push_back(where(c) + "length must be positive");
                ok = false;
            }
            long segElements = 0;
            const std::string elementsText = c.values.count("elements") ? c.values.at("elements") : std::string();
            if (elementsText.empty()) {
                errs.push_back(where(c) + "missing elements=");
                ok = false;
            } else if (!toInteger(elementsText, segElements) || segElements < 1) {
                errs.push_back(where(c) + "elements must be an integer >= 1");
                ok = false;
            }
            Segment seg;
            seg.material = id;
            seg.length = segLength;
            seg.elements = segElements;
            seg.line = c.line;
            if (!shapeKind.empty()) {
                ConstantSection cs;
                if (sectionKeys(shapeKind).empty()) {
                    errs.push_back(where(c) + "unknown shape '" + shapeKind + "' (rect, circle, tube, polygon)");
                    ok = false;
                } else if (readConstantSection(c, shapeKind, cs, errs)) {
                    seg.hasShape = true;
                    seg.area = cs.area;
                    seg.perimeter = cs.perimeter;
                } else {
                    ok = false;
                }
            }
            if (ok) segments.push_back(seg);
        } else if (c.keyword == "thermal") {
            if (sawThermal) {
                errs.push_back(where(c) + "only one thermal line is allowed");
                continue;
            }
            sawThermal = true;
            checkKeys(c, {"k", "rhocp"}, errs);
            if (requireNumber(c, "k", k, errs) && !(k > 0.0)) errs.push_back(where(c) + "k must be positive");
            if (requireNumber(c, "rhocp", rhocp, errs) && !(rhocp > 0.0)) errs.push_back(where(c) + "rhocp must be positive");
        } else if (c.keyword == "initial") {
            if (sawInitial) {
                errs.push_back(where(c) + "only one initial line is allowed");
                continue;
            }
            sawInitial = true;
            checkKeys(c, {"T"}, errs);
            requireNumber(c, "T", T0, errs);
        } else if (c.keyword == "temperature" || c.keyword == "flux" || c.keyword == "convection") {
            checkKeys(c, c.keyword == "temperature" ? std::vector<std::string>{"end", "T"}
                          : c.keyword == "flux"     ? std::vector<std::string>{"end", "q"}
                                                    : std::vector<std::string>{"end", "h", "Tinf", "emissivity"},
                      errs);
            const auto endIt = c.values.find("end");
            if (endIt == c.values.end() || (endIt->second != "left" && endIt->second != "right")) {
                errs.push_back(where(c) + "end must be left or right");
                continue;
            }
            const std::size_t e = endIt->second == "left" ? 0 : 1;
            if (endSet[e]) {
                errs.push_back(where(c) + "the " + endIt->second + " end already has a condition");
                continue;
            }
            Boundary b;
            if (c.keyword == "temperature") {
                double value = 0.0;
                if (!requireNumber(c, "T", value, errs)) continue;
                b.kind = BoundaryKind::Temperature;
                b.value = constantTime(value);
            } else if (c.keyword == "flux") {
                double value = 0.0;
                if (!requireNumber(c, "q", value, errs)) continue;
                b.kind = BoundaryKind::HeatFlux;
                b.value = constantTime(value);
            } else {
                double h = 0.0;
                double eps = 0.0;
                double Tinf = 0.0;
                if (!requireNumber(c, "h", h, errs) || !requireNumber(c, "Tinf", Tinf, errs)) continue;
                if (!optionalNumber(c, "emissivity", eps, errs)) continue;
                b.kind = BoundaryKind::Convection;
                b.h = h;
                b.emissivity = eps;
                b.ambient = constantTime(Tinf);
            }
            endSet[e] = true;
            ends[e] = b;
        } else if (c.keyword == "lateral") {
            if (sawLateral) {
                errs.push_back(where(c) + "only one lateral line is allowed");
                continue;
            }
            sawLateral = true;
            checkKeys(c, {"h", "Tinf", "emissivity"}, errs);
            requireNumber(c, "Tinf", latTinf, errs);
            if (!optionalNumber(c, "h", latH, errs) || !optionalNumber(c, "emissivity", latEps, errs)) continue;
            if (!(latH >= 0.0)) errs.push_back(where(c) + "h must be >= 0");
            if (!(latEps >= 0.0 && latEps <= 1.0)) errs.push_back(where(c) + "emissivity must be in [0, 1]");
        } else if (c.keyword == "heat") {
            if (sawHeat) {
                errs.push_back(where(c) + "only one heat line is allowed");
                continue;
            }
            sawHeat = true;
            checkKeys(c, {"q"}, errs);
            requireNumber(c, "q", q, errs);
        } else if (c.keyword == "job") {
            if (sawJob) {
                errs.push_back(where(c) + "only one job line is allowed");
                continue;
            }
            sawJob = true;
            checkKeys(c, {"solver"}, errs);
            if (c.values.count("solver") && c.values.at("solver") != "internal") {
                errs.push_back(where(c) + "a 1D heat model runs only with solver=internal");
            }
        } else if (c.keyword == "step") {
            if (sawStep) {
                errs.push_back(where(c) + "only one step is allowed for now");
                continue;
            }
            sawStep = true;
            const std::string kind = c.bare.size() == 1 ? c.bare[0] : std::string();
            if (kind == "steady") {
                checkKeys(c, {}, errs);
                options.steady = true;
            } else if (kind == "transient") {
                checkKeys(c, {"dt", "end", "theta", "capacity"}, errs);
                double dt = 0.0;
                double endTime = 0.0;
                if (requireNumber(c, "dt", dt, errs) && !(dt > 0.0)) errs.push_back(where(c) + "dt must be positive");
                if (requireNumber(c, "end", endTime, errs) && !(endTime > 0.0)) errs.push_back(where(c) + "end must be positive");
                options.timeStep = dt;
                options.endTime = endTime;
                if (!optionalNumber(c, "theta", options.theta, errs)) continue;
                if (c.values.count("capacity")) {
                    const std::string mode = c.values.at("capacity");
                    if (mode == "consistent") options.capacity = Capacity::Consistent;
                    else if (mode == "lumped") options.capacity = Capacity::Lumped;
                    else errs.push_back(where(c) + "capacity must be consistent or lumped");
                }
            } else {
                errs.push_back(where(c) + "expected 'step steady' or 'step transient dt=... end=...'");
            }
        } else {
            errs.push_back(where(c) + "unknown command");
        }
    }

    if (!sawAnalysis) errs.push_back("missing the analysis line 'analysis heat 1d ...'");
    const bool everySegmentShaped = !segments.empty() && std::all_of(segments.begin(), segments.end(),
                                                                     [](const Segment& sg) { return sg.hasShape; });
    if (!sawSection && !everySegmentShaped) {
        errs.push_back("missing a section line (rect, circle, tube, polygon or table), or a shape= on every segment");
    }
    if (!sawStep) errs.push_back("missing a step line ('step steady' or 'step transient ...')");

    // One material over the whole length (thermal line), or material and segment lines.
    if (sawThermal) {
        if (!materials.empty() || !segments.empty()) {
            errs.push_back("the thermal line cannot be combined with material and segment lines");
        } else if (!sawGeometry) {
            errs.push_back("the thermal line needs length= and elements= on the analysis line");
        } else {
            MaterialDef md;
            md.id = "default";
            md.k = k;
            md.rhocp = rhocp;
            materials.push_back(md);
            Segment seg;
            seg.material = "default";
            seg.length = length;
            seg.elements = elements;
            segments.push_back(seg);
        }
    }
    if (segments.empty() && !sawThermal) {
        errs.push_back("missing the material data: a thermal line, or material and segment lines");
    }
    if (sawGeometry && !segments.empty() && !sawThermal) {
        errs.push_back("length and elements follow from the segment lines: remove them from the analysis line");
    }

    // Nodes: each segment is split into its elements; the interfaces between segments are nodes.
    std::vector<double> nodes{0.0};
    std::vector<double> segEnds;
    std::vector<ScalarFn> segK; // conductivity of each segment, as a function of T
    std::vector<double> segRhoc;
    std::vector<ScalarFn> segArea;      // section of each segment: its own, or the global one (filled below)
    std::vector<ScalarFn> segPerimeter;
    bool anyShape = false;
    double totalLength = 0.0;
    for (const Segment& s : segments) {
        const auto mat = std::find_if(materials.begin(), materials.end(), [&](const MaterialDef& md) { return md.id == s.material; });
        if (mat == materials.end()) continue; // checked when the segment line was read
        const double start = totalLength;
        totalLength += s.length;
        for (long j = 1; j <= s.elements; ++j) {
            nodes.push_back(start + s.length * static_cast<double>(j) / static_cast<double>(s.elements));
        }
        nodes.back() = totalLength;
        segEnds.push_back(totalLength);
        segK.push_back(conductivityOf(*mat));
        segRhoc.push_back(mat->rhocp);
        anyShape = anyShape || s.hasShape;
        segArea.push_back(s.area);
        segPerimeter.push_back(s.perimeter);
    }

    if (sawAnalysis && totalLength > 0.0 && tableSection) {
        if (tableX.front() != 0.0 || std::fabs(tableX.back() - totalLength) > 1e-12 * totalLength) {
            errs.push_back("line " + std::to_string(tableLine) + " (section): table x must go from 0 to the length");
        } else {
            area = piecewiseLinear(tableX, tableA);
            perimeter = piecewiseLinear(tableX, tableP);
        }
    }

    if (!errs.empty() || !sawAnalysis || segments.empty() || !sawStep || (!sawSection && !everySegmentShaped)) {
        errors.insert(errors.end(), errs.begin(), errs.end());
        return false;
    }

    Model m;
    m.length = totalLength;
    m.nodes = nodes;
    m.elements = static_cast<int>(nodes.size() - 1);
    m.conductivity = [segEnds, segK](double x, double T) { return segK[segmentIndex(segEnds, x)](T); };
    m.volumetricHeatCapacity = [segEnds, segRhoc](double x) { return segRhoc[segmentIndex(segEnds, x)]; };
    if (anyShape) {
        // Segments without their own section take the global one (a section line, possibly a table over the length).
        for (std::size_t i = 0; i < segArea.size(); ++i) {
            if (!segArea[i]) {
                segArea[i] = area;
                segPerimeter[i] = perimeter;
            }
        }
        m.area = [segEnds, segArea](double x) { return segArea[segmentIndex(segEnds, x)](x); };
        m.lateral.perimeter = [segEnds, segPerimeter](double x) { return segPerimeter[segmentIndex(segEnds, x)](x); };
    } else {
        m.area = area;
        m.lateral.perimeter = perimeter;
    }
    if (sawHeat) m.source = [q](double, double) { return q; };
    m.lateral.h = latH;
    m.lateral.emissivity = latEps;
    m.lateral.ambient = constantTime(latTinf);
    m.left = ends[0];
    m.right = ends[1];
    m.initialTemperature = constantScalar(T0);

    const std::string problemError = validate(m, options);
    if (!problemError.empty()) {
        errors.push_back(problemError);
        return false;
    }
    problem.model = std::move(m);
    problem.options = options;
    return true;
}

Result runScript(const ScriptProblem& problem) {
    return solve(problem.model, problem.options);
}

std::string resultToCsv(const Result& result) {
    std::ostringstream out;
    out << std::setprecision(10);
    out << "time,x,temperature\n";
    for (std::size_t k = 0; k < result.times.size(); ++k) {
        for (std::size_t i = 0; i < result.nodes.size(); ++i) {
            out << result.times[k] << ',' << result.nodes[i] << ',' << result.temperatures[k][i] << '\n';
        }
    }
    return out.str();
}

} // namespace OpenModel::Heat1D
