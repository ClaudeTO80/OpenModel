#include <OpenModel/Heat2DScript.hpp>

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

/**
 * @file Heat2DScript.cpp
 * @brief Parser of the two-dimensional heat script and its CSV output (see Heat2DScript.hpp).
 *
 * The parser reads every line before it decides: all problems are reported, each with its line number. The model is
 * built only when the script is complete and valid; the final check is Heat2D::validate().
 */

namespace OpenModel::Heat2D {
namespace {

/** @brief One script line: the words without `=` and the `key=value` pairs. */
struct Command {
    int line = 0;
    std::vector<std::string> words;               ///< Keyword first, then bare words.
    std::map<std::string, std::string> values;    ///< key=value pairs.
};

/** @brief Splits the text into commands, dropping comments and blank lines. Malformed words are reported. */
std::vector<Command> tokenize(const std::string& text, std::vector<std::string>& errors) {
    std::vector<Command> commands;
    std::istringstream stream(text);
    std::string raw;
    int lineNumber = 0;
    while (std::getline(stream, raw)) {
        ++lineNumber;
        const std::size_t hash = raw.find('#');
        if (hash != std::string::npos) raw.erase(hash);
        std::istringstream words(raw);
        Command c;
        c.line = lineNumber;
        std::string w;
        bool bad = false;
        while (words >> w) {
            const std::size_t eq = w.find('=');
            if (eq == std::string::npos) {
                c.words.push_back(w);
            } else if (eq == 0 || eq + 1 == w.size()) {
                errors.push_back("line " + std::to_string(lineNumber) + ": malformed '" + w + "', expected key=value");
                bad = true;
            } else {
                const std::string key = w.substr(0, eq);
                if (c.values.count(key)) {
                    errors.push_back("line " + std::to_string(lineNumber) + ": repeated key '" + key + "'");
                    bad = true;
                }
                c.values[key] = w.substr(eq + 1);
            }
        }
        if (!bad && !c.words.empty()) commands.push_back(c);
    }
    return commands;
}

/** @brief Prefix for messages: the line of the command. */
std::string where(const Command& c) {
    return "line " + std::to_string(c.line) + ": ";
}

/** @brief Reads a number; reports and returns false when it is missing or malformed. */
bool number(const Command& c, const std::string& key, double& out, std::vector<std::string>& errors) {
    const auto it = c.values.find(key);
    if (it == c.values.end()) {
        errors.push_back(where(c) + "missing " + key + "=");
        return false;
    }
    try {
        std::size_t pos = 0;
        out = std::stod(it->second, &pos);
        if (pos != it->second.size() || !std::isfinite(out)) throw std::invalid_argument("partial");
    } catch (const std::exception&) {
        errors.push_back(where(c) + key + "= is not a number: '" + it->second + "'");
        return false;
    }
    return true;
}

/** @brief Reads an optional number with a default. */
bool optionalNumber(const Command& c, const std::string& key, double& out, std::vector<std::string>& errors) {
    if (!c.values.count(key)) return true;
    return number(c, key, out, errors);
}

/** @brief Reports every key of the command that is not in the allowed set. */
void checkKeys(const Command& c, const std::set<std::string>& allowed, std::vector<std::string>& errors) {
    for (const auto& kv : c.values) {
        if (!allowed.count(kv.first)) errors.push_back(where(c) + "unknown key '" + kv.first + "'");
    }
}

/** @brief Constant time function. */
Heat1D::TimeFn constant(double v) {
    return [v](double) { return v; };
}

/** @brief Constant field. */
Field2 constantField(double v) {
    return [v](double, double) { return v; };
}

/** @brief Builds a side condition from a command's values; the kind decides which keys are needed. */
bool readCondition(const Command& c, Heat1D::Boundary& b, const std::string& kind, std::vector<std::string>& errors) {
    if (kind == "temperature") {
        b.kind = BoundaryKind::Temperature;
        double T = 0.0;
        if (!number(c, "T", T, errors)) return false;
        b.value = constant(T);
        return true;
    }
    if (kind == "flux") {
        b.kind = BoundaryKind::HeatFlux;
        double q = 0.0;
        if (!number(c, "q", q, errors)) return false;
        b.value = constant(q);
        return true;
    }
    if (kind == "convection") {
        b.kind = BoundaryKind::Convection;
        double Tinf = 0.0;
        if (!number(c, "h", b.h, errors)) return false;
        if (!number(c, "Tinf", Tinf, errors)) return false;
        if (!optionalNumber(c, "emissivity", b.emissivity, errors)) return false;
        b.ambient = constant(Tinf);
        return true;
    }
    errors.push_back(where(c) + "unknown condition '" + kind + "'");
    return false;
}

} // namespace

bool parseScript(const std::string& text, ScriptProblem& problem, std::vector<std::string>& errors) {
    std::vector<std::string> errs;
    const std::vector<Command> commands = tokenize(text, errs);

    Model m;
    Options o;
    bool sawAnalysis = false;
    bool sawPlate = false;
    bool sawMaterial = false;
    bool sawStep = false;
    bool sawJob = false;
    bool sawHoleCondition = false;
    bool sawInitial = false;
    bool sawSource = false;
    std::set<std::string> sidesSeen;

    double thickness = 1.0;
    double rho = 0.0;
    double k = 0.0;

    for (const Command& c : commands) {
        const std::string& kw = c.words[0];
        // A known command takes no bare word after it, except analysis, hole, step and job. Unknown commands are
        // reported as such below.
        static const std::set<std::string> knownCommands = {"analysis", "plate", "material", "hole", "temperature",
                                                            "flux",     "convection", "heat", "initial", "job", "step"};
        static const std::set<std::string> takesWords = {"analysis", "hole", "step", "job"};
        if (knownCommands.count(kw) && !takesWords.count(kw) && c.words.size() > 1) {
            errs.push_back(where(c) + "unexpected word '" + c.words[1] + "'");
            continue;
        }

        if (kw == "analysis") {
            if (c.words.size() < 3 || c.words[1] != "heat" || c.words[2] != "2d") {
                errs.push_back(where(c) + "expected 'analysis heat 2d'");
                continue;
            }
            if (sawAnalysis) errs.push_back(where(c) + "analysis repeated");
            sawAnalysis = true;
        } else if (kw == "plate") {
            if (sawPlate) errs.push_back(where(c) + "plate repeated");
            sawPlate = true;
            checkKeys(c, {"lx", "ly", "nx", "ny", "thickness"}, errs);
            double lx = 0.0, ly = 0.0, nx = 0.0, ny = 0.0;
            number(c, "lx", lx, errs);
            number(c, "ly", ly, errs);
            number(c, "nx", nx, errs);
            number(c, "ny", ny, errs);
            if (!optionalNumber(c, "thickness", thickness, errs)) continue;
            m.lx = lx;
            m.ly = ly;
            if (nx != std::floor(nx) || ny != std::floor(ny) || nx < 1 || ny < 1) {
                errs.push_back(where(c) + "nx and ny must be whole numbers of at least 1");
            } else {
                m.nx = static_cast<int>(nx);
                m.ny = static_cast<int>(ny);
            }
            m.thickness = thickness;
        } else if (kw == "material") {
            if (sawMaterial) errs.push_back(where(c) + "material repeated");
            sawMaterial = true;
            checkKeys(c, {"k", "rhocp"}, errs);
            if (!number(c, "k", k, errs) || !number(c, "rhocp", rho, errs)) continue;
            if (!(k > 0.0)) errs.push_back(where(c) + "k must be positive");
            if (!(rho > 0.0)) errs.push_back(where(c) + "rhocp must be positive");
            m.conductivity = [k](double, double, double) { return k; };
            m.volumetricHeatCapacity = constantField(rho);
        } else if (kw == "hole") {
            if (c.words.size() < 2) {
                errs.push_back(where(c) + "hole needs a shape: circle or rect");
                continue;
            }
            const std::string& shape = c.words[1];
            // The hole condition line (hole temperature|flux|convection) is read after the loop.
            if (shape == "temperature" || shape == "flux" || shape == "convection") continue;
            Hole h;
            if (shape == "circle") {
                checkKeys(c, {"cx", "cy", "r"}, errs);
                h.shape = HoleShape::Circle;
                if (number(c, "cx", h.cx, errs) && number(c, "cy", h.cy, errs) && number(c, "r", h.r, errs))
                    m.holes.push_back(h);
            } else if (shape == "rect") {
                checkKeys(c, {"x0", "y0", "x1", "y1"}, errs);
                h.shape = HoleShape::Rectangle;
                if (number(c, "x0", h.x0, errs) && number(c, "y0", h.y0, errs) && number(c, "x1", h.x1, errs) &&
                    number(c, "y1", h.y1, errs))
                    m.holes.push_back(h);
            } else {
                errs.push_back(where(c) + "hole shape must be circle or rect");
            }
        } else if (kw == "temperature" || kw == "flux" || kw == "convection") {
            const auto side = c.values.find("side");
            if (side == c.values.end()) {
                errs.push_back(where(c) + "missing side=left|right|bottom|top");
                continue;
            }
            Heat1D::Boundary* target = nullptr;
            if (side->second == "left") target = &m.left;
            else if (side->second == "right") target = &m.right;
            else if (side->second == "bottom") target = &m.bottom;
            else if (side->second == "top") target = &m.top;
            else {
                errs.push_back(where(c) + "side must be left, right, bottom or top");
                continue;
            }
            if (sidesSeen.count(side->second)) {
                errs.push_back(where(c) + "side '" + side->second + "' already has a condition");
                continue;
            }
            sidesSeen.insert(side->second);
            if (kw == "temperature") checkKeys(c, {"side", "T"}, errs);
            if (kw == "flux") checkKeys(c, {"side", "q"}, errs);
            if (kw == "convection") checkKeys(c, {"side", "h", "Tinf", "emissivity"}, errs);
            readCondition(c, *target, kw, errs);
        } else if (kw == "heat") {
            if (sawSource) errs.push_back(where(c) + "heat repeated");
            sawSource = true;
            checkKeys(c, {"q"}, errs);
            double q = 0.0;
            if (number(c, "q", q, errs)) m.source = [q](double, double, double) { return q; };
        } else if (kw == "initial") {
            if (sawInitial) errs.push_back(where(c) + "initial repeated");
            sawInitial = true;
            checkKeys(c, {"T"}, errs);
            double T0 = 0.0;
            if (number(c, "T", T0, errs)) m.initialTemperature = constantField(T0);
        } else if (kw == "job") {
            if (sawJob) errs.push_back(where(c) + "job repeated");
            sawJob = true;
            checkKeys(c, {"solver"}, errs);
            if (c.words.size() < 2) errs.push_back(where(c) + "job needs a name");
            if (c.values.count("solver") && c.values.at("solver") != "internal")
                errs.push_back(where(c) + "only the internal solver runs 2D models");
        } else if (kw == "step") {
            if (sawStep) errs.push_back(where(c) + "step repeated");
            sawStep = true;
            if (c.words.size() < 2) {
                errs.push_back(where(c) + "step needs steady or transient");
                continue;
            }
            if (c.words[1] == "steady") {
                checkKeys(c, {}, errs);
                o.steady = true;
            } else if (c.words[1] == "transient") {
                checkKeys(c, {"dt", "end", "theta", "capacity"}, errs);
                double dt = 0.0, end = 0.0;
                if (!number(c, "dt", dt, errs) || !number(c, "end", end, errs)) continue;
                o.steady = false;
                o.timeStep = dt;
                o.endTime = end;
                if (!optionalNumber(c, "theta", o.theta, errs)) continue;
                if (c.values.count("capacity")) {
                    const std::string mode = c.values.at("capacity");
                    if (mode == "consistent") o.capacity = Capacity::Consistent;
                    else if (mode == "lumped") o.capacity = Capacity::Lumped;
                    else errs.push_back(where(c) + "capacity must be consistent or lumped");
                }
            } else {
                errs.push_back(where(c) + "step must be steady or transient");
            }
        } else {
            errs.push_back(where(c) + "unknown command '" + kw + "'");
        }
    }

    // Hole condition: one line, applied to every hole boundary. Parsed after the others so its line number is known.
    for (const Command& c : commands) {
        if (c.words[0] != "hole" || c.words.size() < 2 || (c.words[1] != "temperature" && c.words[1] != "flux" &&
                                                           c.words[1] != "convection"))
            continue;
        if (sawHoleCondition) {
            errs.push_back(where(c) + "hole condition repeated");
            continue;
        }
        sawHoleCondition = true;
        const std::string kind = c.words[1];
        if (kind == "temperature") checkKeys(c, {"T"}, errs);
        if (kind == "flux") checkKeys(c, {"q"}, errs);
        if (kind == "convection") checkKeys(c, {"h", "Tinf", "emissivity"}, errs);
        readCondition(c, m.hole, kind, errs);
    }

    if (!sawAnalysis) errs.push_back("missing 'analysis heat 2d'");
    if (!sawPlate) errs.push_back("missing 'plate' line");
    if (!sawMaterial) errs.push_back("missing 'material' line");
    if (!sawStep) errs.push_back("missing 'step' line");

    if (!errs.empty()) {
        errors.insert(errors.end(), errs.begin(), errs.end());
        return false;
    }

    // Final check of the complete model. Messages carry no line number here.
    const std::string modelError = validate(m, o);
    if (!modelError.empty()) {
        errors.push_back(modelError);
        return false;
    }

    problem.model = std::move(m);
    problem.options = o;
    return true;
}

Result runScript(const ScriptProblem& problem) {
    return solve(problem.model, problem.options);
}

std::string resultToCsv(const Result& result, const ScriptProblem& problem) {
    std::ostringstream os;
    os << std::setprecision(10);
    os << "time,x,y,temperature\n";
    const double hx = problem.model.lx / problem.model.nx;
    const double hy = problem.model.ly / problem.model.ny;
    for (std::size_t k = 0; k < result.times.size(); ++k) {
        for (int j = 0; j < result.nodesY; ++j) {
            for (int i = 0; i < result.nodesX; ++i) {
                const std::size_t id = static_cast<std::size_t>(j * result.nodesX + i);
                if (!result.active[id]) continue;
                os << result.times[k] << ',' << i * hx << ',' << j * hy << ',' << result.temperatures[k][id] << '\n';
            }
        }
    }
    return os.str();
}

} // namespace OpenModel::Heat2D
