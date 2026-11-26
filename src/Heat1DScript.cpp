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

TimeFn constantTime(double v) {
    return [v](double) { return v; };
}

ScalarFn constantScalar(double v) {
    return [v](double) { return v; };
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
                errs.push_back(where(c) + "expected 'analysis heat 1d length=L elements=N'");
                continue;
            }
            checkKeys(c, {"length", "elements"}, errs);
            requireNumber(c, "length", length, errs);
            if (c.values.count("length") && !(length > 0.0)) errs.push_back(where(c) + "length must be positive");
            std::string elementsText = c.values.count("elements") ? c.values.at("elements") : std::string();
            if (elementsText.empty()) {
                errs.push_back(where(c) + "missing elements=");
            } else if (!toInteger(elementsText, elements) || elements < 1) {
                errs.push_back(where(c) + "elements must be an integer >= 1");
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
            if (kind == "rect") {
                checkKeys(c, {"a", "b"}, errs);
                double a = 0.0, b = 0.0;
                if (requireNumber(c, "a", a, errs) && requireNumber(c, "b", b, errs)) {
                    if (!(a > 0.0 && b > 0.0)) errs.push_back(where(c) + "a and b must be positive");
                    else {
                        area = constantScalar(a * b);
                        perimeter = constantScalar(2.0 * (a + b));
                    }
                }
            } else if (kind == "circle") {
                checkKeys(c, {"r"}, errs);
                double r = 0.0;
                if (requireNumber(c, "r", r, errs)) {
                    if (!(r > 0.0)) errs.push_back(where(c) + "r must be positive");
                    else {
                        area = constantScalar(kPi * r * r);
                        perimeter = constantScalar(2.0 * kPi * r);
                    }
                }
            } else if (kind == "tube") {
                checkKeys(c, {"ro", "ri"}, errs);
                double ro = 0.0, ri = 0.0;
                if (requireNumber(c, "ro", ro, errs) && requireNumber(c, "ri", ri, errs)) {
                    if (!(ri >= 0.0 && ro > ri)) errs.push_back(where(c) + "need 0 <= ri < ro");
                    else {
                        area = constantScalar(kPi * (ro * ro - ri * ri));
                        // Both surfaces (outer and inner) are exposed to the same lateral exchange.
                        perimeter = constantScalar(2.0 * kPi * (ro + ri));
                    }
                }
            } else if (kind == "polygon") {
                checkKeys(c, {"sides", "r"}, errs);
                double r = 0.0;
                long sides = 0;
                const std::string sidesText = c.values.count("sides") ? c.values.at("sides") : std::string();
                if (sidesText.empty()) errs.push_back(where(c) + "missing sides=");
                else if (!toInteger(sidesText, sides) || sides < 3) errs.push_back(where(c) + "sides must be an integer >= 3");
                if (requireNumber(c, "r", r, errs) && !(r > 0.0)) errs.push_back(where(c) + "r must be positive");
                if (sides >= 3 && r > 0.0) {
                    const double n = static_cast<double>(sides);
                    area = constantScalar(0.5 * n * r * r * std::sin(2.0 * kPi / n));
                    perimeter = constantScalar(2.0 * n * r * std::sin(kPi / n));
                }
            } else if (kind == "table") {
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
            } else {
                errs.push_back(where(c) + "unknown section '" + kind + "' (rect, circle, tube, polygon, table)");
            }
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

    if (!sawAnalysis) errs.push_back("missing the analysis line 'analysis heat 1d length=L elements=N'");
    if (!sawSection) errs.push_back("missing a section line (rect, circle, tube, polygon or table)");
    if (!sawThermal) errs.push_back("missing the thermal line 'thermal k=... rhocp=...'");
    if (!sawStep) errs.push_back("missing a step line ('step steady' or 'step transient ...')");

    if (sawAnalysis && length > 0.0 && tableSection) {
        if (tableX.front() != 0.0 || std::fabs(tableX.back() - length) > 1e-12 * length) {
            errs.push_back("line " + std::to_string(tableLine) + " (section): table x must go from 0 to the length");
        } else {
            area = piecewiseLinear(tableX, tableA);
            perimeter = piecewiseLinear(tableX, tableP);
        }
    }

    if (!errs.empty() || !sawAnalysis || !sawSection || !sawThermal || !sawStep) {
        errors.insert(errors.end(), errs.begin(), errs.end());
        return false;
    }

    Model m;
    m.length = length;
    m.elements = static_cast<int>(elements);
    m.conductivity = [k](double, double) { return k; };
    m.volumetricHeatCapacity = constantScalar(rhocp);
    m.area = area;
    if (sawHeat) m.source = [q](double, double) { return q; };
    m.lateral.perimeter = perimeter;
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
