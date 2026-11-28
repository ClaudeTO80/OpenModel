#include <OpenModel/Heat2DMeshScript.hpp>

#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

/**
 * @file Heat2DMeshScript.cpp
 * @brief Parser of the mesh heat script and its CSV output (see Heat2DMeshScript.hpp).
 *
 * The script is read to the end before any decision: every problem is reported with its line number. The final model
 * check is validateMesh(), which needs the mesh and therefore runs in the Gmsh runner; here only the script is checked.
 */

namespace OpenModel::Heat2D {
namespace {

/** @brief One script line: the words without `=`, and the `key=value` pairs. */
struct Command {
    int line = 0;
    std::vector<std::string> words;
    std::map<std::string, std::string> values;
};

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

std::string where(const Command& c) {
    return "line " + std::to_string(c.line) + ": ";
}

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

bool optionalNumber(const Command& c, const std::string& key, double& out, std::vector<std::string>& errors) {
    if (!c.values.count(key)) return true;
    return number(c, key, out, errors);
}

void checkKeys(const Command& c, const std::set<std::string>& allowed, std::vector<std::string>& errors) {
    for (const auto& kv : c.values) {
        if (!allowed.count(kv.first)) errors.push_back(where(c) + "unknown key '" + kv.first + "'");
    }
}

Heat1D::TimeFn constantTime(double v) {
    return [v](double) { return v; };
}

} // namespace

bool parseMeshScript(const std::string& text, MeshScriptProblem& problem, std::vector<std::string>& errors) {
    std::vector<std::string> errs;
    const std::vector<Command> commands = tokenize(text, errs);

    MeshScriptProblem p;
    Options o;
    bool sawAnalysis = false, sawGeometry = false, sawMaterial = false, sawStep = false, sawHeat = false;
    bool sawInitial = false, sawJob = false;
    std::set<int> tagsSeen;
    double k = 0.0, rho = 0.0;

    static const std::set<std::string> known = {"analysis", "geometry", "material", "boundary", "heat", "initial", "job", "step"};
    static const std::set<std::string> takesWords = {"analysis", "geometry", "step", "job", "boundary"};

    for (const Command& c : commands) {
        const std::string& kw = c.words[0];
        if (known.count(kw) && !takesWords.count(kw) && c.words.size() > 1) {
            errs.push_back(where(c) + "unexpected word '" + c.words[1] + "'");
            continue;
        }

        if (kw == "analysis") {
            if (c.words.size() < 4 || c.words[1] != "heat" || c.words[2] != "2d" || c.words[3] != "mesh") {
                errs.push_back(where(c) + "expected 'analysis heat 2d mesh'");
                continue;
            }
            if (sawAnalysis) errs.push_back(where(c) + "analysis repeated");
            sawAnalysis = true;
        } else if (kw == "geometry") {
            if (sawGeometry) errs.push_back(where(c) + "geometry repeated");
            sawGeometry = true;
            if (c.words.size() < 2 || c.words[1] != "step")
                errs.push_back(where(c) + "expected 'geometry step file=... size=...'");
            checkKeys(c, {"file", "size"}, errs);
            const auto f = c.values.find("file");
            if (f == c.values.end()) errs.push_back(where(c) + "missing file=");
            else p.stepFile = f->second;
            double size = 0.0;
            if (number(c, "size", size, errs)) {
                if (!(size > 0.0)) errs.push_back(where(c) + "size must be positive");
                p.size = size;
            }
        } else if (kw == "material") {
            if (sawMaterial) errs.push_back(where(c) + "material repeated");
            sawMaterial = true;
            checkKeys(c, {"k", "rhocp", "thickness"}, errs);
            if (!number(c, "k", k, errs) || !number(c, "rhocp", rho, errs)) continue;
            if (!(k > 0.0)) errs.push_back(where(c) + "k must be positive");
            if (!(rho > 0.0)) errs.push_back(where(c) + "rhocp must be positive");
            double t = 1.0;
            if (!optionalNumber(c, "thickness", t, errs)) continue;
            if (!(t > 0.0)) errs.push_back(where(c) + "thickness must be positive");
            p.model.thickness = t;
            p.model.conductivity = [k](double, double, double) { return k; };
            p.model.volumetricHeatCapacity = [rho](double, double) { return rho; };
        } else if (kw == "boundary") {
            if (c.words.size() < 2) {
                errs.push_back(where(c) + "boundary needs a kind: temperature, flux or convection");
                continue;
            }
            const std::string& kind = c.words[1];
            double tagValue = 0.0;
            if (!number(c, "tag", tagValue, errs)) continue;
            if (tagValue != std::floor(tagValue) || tagValue < 1) {
                errs.push_back(where(c) + "tag must be a whole number of at least 1");
                continue;
            }
            const int tag = static_cast<int>(tagValue);
            if (tagsSeen.count(tag)) {
                errs.push_back(where(c) + "tag " + std::to_string(tag) + " already has a condition");
                continue;
            }
            tagsSeen.insert(tag);
            OpenModel::Heat1D::Boundary b;
            if (kind == "temperature") {
                checkKeys(c, {"tag", "T"}, errs);
                double T = 0.0;
                if (!number(c, "T", T, errs)) continue;
                b.kind = BoundaryKind::Temperature;
                b.value = constantTime(T);
            } else if (kind == "flux") {
                checkKeys(c, {"tag", "q"}, errs);
                double q = 0.0;
                if (!number(c, "q", q, errs)) continue;
                b.kind = BoundaryKind::HeatFlux;
                b.value = constantTime(q);
            } else if (kind == "convection") {
                checkKeys(c, {"tag", "h", "Tinf", "emissivity"}, errs);
                double Tinf = 0.0;
                if (!number(c, "h", b.h, errs) || !number(c, "Tinf", Tinf, errs)) continue;
                if (!optionalNumber(c, "emissivity", b.emissivity, errs)) continue;
                b.kind = BoundaryKind::Convection;
                b.ambient = constantTime(Tinf);
            } else {
                errs.push_back(where(c) + "boundary kind must be temperature, flux or convection");
                continue;
            }
            p.model.boundaries[tag] = b;
        } else if (kw == "heat") {
            if (sawHeat) errs.push_back(where(c) + "heat repeated");
            sawHeat = true;
            checkKeys(c, {"q"}, errs);
            double q = 0.0;
            if (number(c, "q", q, errs)) p.model.source = [q](double, double, double) { return q; };
        } else if (kw == "initial") {
            if (sawInitial) errs.push_back(where(c) + "initial repeated");
            sawInitial = true;
            checkKeys(c, {"T"}, errs);
            double T0 = 0.0;
            if (number(c, "T", T0, errs)) {
                p.initialT = T0;
                p.hasInitial = true;
            }
        } else if (kw == "job") {
            if (sawJob) errs.push_back(where(c) + "job repeated");
            sawJob = true;
            checkKeys(c, {"solver"}, errs);
            if (c.words.size() < 2) errs.push_back(where(c) + "job needs a name");
            if (c.values.count("solver") && c.values.at("solver") != "internal")
                errs.push_back(where(c) + "only the internal solver runs mesh models");
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
                    if (mode == "consistent") o.capacity = OpenModel::Heat1D::Capacity::Consistent;
                    else if (mode == "lumped") o.capacity = OpenModel::Heat1D::Capacity::Lumped;
                    else errs.push_back(where(c) + "capacity must be consistent or lumped");
                }
            } else {
                errs.push_back(where(c) + "step must be steady or transient");
            }
        } else {
            errs.push_back(where(c) + "unknown command '" + kw + "'");
        }
    }

    if (!sawAnalysis) errs.push_back("missing 'analysis heat 2d mesh'");
    if (!sawGeometry) errs.push_back("missing 'geometry' line");
    if (!sawMaterial) errs.push_back("missing 'material' line");
    if (!sawStep) errs.push_back("missing 'step' line");

    if (!errs.empty()) {
        errors.insert(errors.end(), errs.begin(), errs.end());
        return false;
    }
    p.options = o;
    problem = std::move(p);
    return true;
}

std::string meshResultToCsv(const TriangleMesh& mesh, const MeshResult& result) {
    std::ostringstream os;
    os << std::setprecision(10);
    os << "time,x,y,temperature\n";
    for (std::size_t k = 0; k < result.times.size() && k < result.temperatures.size(); ++k) {
        for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
            os << result.times[k] << ',' << mesh.nodes[i][0] << ',' << mesh.nodes[i][1] << ','
               << result.temperatures[k][i] << '\n';
        }
    }
    return os.str();
}

} // namespace OpenModel::Heat2D
