#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <set>
#include <unordered_map>

namespace OpenModel {
namespace {
bool validConditionId(const std::string& id);

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool parseNumber(const std::string& text, double& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(value);
}

bool parseVector3(const std::string& text, std::array<double, 3>& out) {
    std::istringstream in(text);
    std::string cell;
    std::size_t n = 0;
    while (std::getline(in, cell, ',')) {
        if (n >= 3 || !parseNumber(cell, out[n])) return false;
        ++n;
    }
    return n == 3 && text.back() != ',';
}

struct Parser {
    AnalysisParseResult result;
    bool haveMaterial = false, haveStep = false;
    std::set<std::string> jobIds;
    std::size_t line = 0;
    std::map<std::string, std::size_t> idCounters;
    std::set<std::string> conditionIds;

    std::string conditionId(const std::string& kind, std::map<std::string, std::string>& keys) {
        std::string value;
        const auto it = keys.find("id");
        if (it != keys.end()) { value = it->second; keys.erase(it); }
        else {
            do { value = kind + "-" + std::to_string(++idCounters[kind]); }
            while (conditionIds.count(value) != 0);
        }
        const bool valid = !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
        });
        if (!valid) { error("SCRIPT_BAD_ID", "Condition id must contain only letters, digits, underscore, hyphen or dot"); return {}; }
        if (!conditionIds.insert(value).second) { error("SCRIPT_DUPLICATE_ID", "Condition id '" + value + "' is used more than once"); return {}; }
        return value;
    }

    void error(std::string code, std::string message) {
        result.diagnostics.push_back({ImportDiagnosticLevel::Error, std::move(code), std::move(message), line});
    }
    void warning(std::string code, std::string message) {
        result.diagnostics.push_back({ImportDiagnosticLevel::Warning, std::move(code), std::move(message), line});
    }

    bool selector(const std::string& token, NodeSelector& out) {
        if (lowered(token) == "all") { out.kind = SelectorKind::All; return true; }
        if (startsWith(token, "group:") && token.size() > 6) {
            out.kind = SelectorKind::Group;
            out.name = token.substr(6);
            return true;
        }
        if (startsWith(token, "plane:") && token.size() > 8 && token[7] == '=') {
            const char axis = static_cast<char>(std::tolower(static_cast<unsigned char>(token[6])));
            if (axis != 'x' && axis != 'y' && axis != 'z') return false;
            out.kind = SelectorKind::Plane;
            out.axis = axis - 'x';
            return parseNumber(token.substr(8), out.position);
        }
        return false;
    }

    void parseLine(const std::string& raw) {
        std::string text = raw;
        const auto hash = text.find('#');
        if (hash != std::string::npos) text.erase(hash);
        std::istringstream in(text);
        std::vector<std::string> tokens;
        for (std::string t; in >> t;) tokens.push_back(t);
        if (tokens.empty()) return;
        const std::string command = lowered(tokens[0]);

        std::map<std::string, std::string> keys;
        std::vector<std::string> positional;
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            const std::string& t = tokens[i];
            if (startsWith(t, "plane:") || startsWith(t, "group:") || lowered(t) == "all") { positional.push_back(t); continue; }
            const auto eq = t.find('=');
            if (eq == std::string::npos || eq == 0) { positional.push_back(t); continue; }
            const std::string key = lowered(t.substr(0, eq));
            if (keys.count(key)) { error("SCRIPT_DUPLICATE_KEY", "'" + key + "' is given twice"); return; }
            keys[key] = t.substr(eq + 1);
        }
        auto take = [&](const char* key, std::string& value) {
            const auto it = keys.find(key);
            if (it == keys.end()) return false;
            value = it->second;
            keys.erase(it);
            return true;
        };
        auto number = [&](const char* key, double& value, bool required) {
            std::string text;
            if (!take(key, text)) {
                if (required) error("SCRIPT_MISSING_KEY", "'" + command + "' needs " + key + "=");
                return false;
            }
            if (!parseNumber(text, value)) { error("SCRIPT_BAD_NUMBER", "'" + text + "' is not a number (" + key + ")"); return false; }
            return true;
        };
        auto vector3 = [&](const char* key, std::array<double, 3>& value) {
            std::string text;
            if (!take(key, text)) { error("SCRIPT_MISSING_KEY", "'" + command + "' needs " + key + "=x,y,z"); return false; }
            if (!parseVector3(text, value)) { error("SCRIPT_BAD_VECTOR", "'" + text + "' is not three comma separated numbers (" + key + ")"); return false; }
            return true;
        };
        auto leftovers = [&]() {
            for (const auto& k : keys) error("SCRIPT_UNKNOWN_KEY", "Unknown option '" + k.first + "' for '" + command + "'");
        };
        const std::size_t errorsBefore = result.diagnostics.size();

        if (command == "material") {
            if (haveMaterial) { error("SCRIPT_DUPLICATE", "Only one material is supported"); return; }
            AnalysisMaterial m;
            number("e", m.youngModulus, true);
            number("nu", m.poisson, true);
            number("rho", m.density, false);
            std::string name;
            if (take("name", name)) m.name = name;
            m.id = conditionId("material", keys);
            if (!positional.empty()) error("SCRIPT_UNEXPECTED", "Unexpected '" + positional[0] + "' after material");
            leftovers();
            if (result.diagnostics.size() == errorsBefore) { result.model.material = m; haveMaterial = true; }
        } else if (command == "fix") {
            FixedSupport f;
            if (positional.size() != 1 || !selector(positional[0], f.where)) { error("SCRIPT_SELECTOR", "fix needs one selector: all, group:NAME or plane:x=VALUE"); return; }
            std::string dofs, tol;
            if (take("dof", dofs) && lowered(dofs) != "all") {
                f.dof = {{false, false, false}};
                std::istringstream d(dofs);
                std::string cell;
                while (std::getline(d, cell, ',')) {
                    if (cell == "1" || cell == "2" || cell == "3") f.dof[static_cast<std::size_t>(cell[0] - '1')] = true;
                    else { error("SCRIPT_BAD_DOF", "dof must list 1, 2, 3 (x, y, z) separated by commas, or 'all'"); return; }
                }
            }
            std::string prescribed;
            if (take("value", prescribed) && !parseVector3(prescribed, f.displacement))
                error("SCRIPT_BAD_VECTOR", "'" + prescribed + "' is not three comma separated prescribed translations (value)");
            if (take("tol", tol) && !parseNumber(tol, f.where.tolerance)) error("SCRIPT_BAD_NUMBER", "'" + tol + "' is not a number (tol)");
            f.id = conditionId("fix", keys);
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.supports.push_back(f);
        } else if (command == "force") {
            NodalForce f;
            if (positional.size() != 1 || !selector(positional[0], f.where)) { error("SCRIPT_SELECTOR", "force needs one selector: all, group:NAME or plane:x=VALUE"); return; }
            vector3("f", f.total);
            take("amp", f.amplitude);
            std::string tol;
            if (take("tol", tol) && !parseNumber(tol, f.where.tolerance)) error("SCRIPT_BAD_NUMBER", "'" + tol + "' is not a number (tol)");
            f.id = conditionId("force", keys);
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.forces.push_back(f);
        } else if (command == "pressure") {
            SurfacePressure p;
            NodeSelector sel;
            if (positional.size() != 1 || !selector(positional[0], sel) || sel.kind != SelectorKind::Group) {
                error("SCRIPT_SELECTOR", "pressure needs a surface group: pressure group:NAME p=VALUE");
                return;
            }
            p.group = sel.name;
            number("p", p.pressure, true);
            take("amp", p.amplitude);
            p.id = conditionId("pressure", keys);
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.pressures.push_back(p);
        } else if (command == "gravity") {
            std::array<double, 3> g{};
            if (!positional.empty()) error("SCRIPT_UNEXPECTED", "Unexpected '" + positional[0] + "' after gravity");
            vector3("g", g);
            leftovers();
            if (result.diagnostics.size() == errorsBefore) {
                if (result.model.hasGravity) { error("SCRIPT_DUPLICATE", "Only one gravity line is supported"); return; }
                result.model.hasGravity = true;
                result.model.gravity = g;
            }
        } else if (command == "job") {
            if (positional.size() != 1 || !validConditionId(positional[0])) { error("SCRIPT_BAD_JOB", "job needs an id: job NAME (letters, digits, _ - .)"); return; }
            if (!jobIds.insert(positional[0]).second) { error("SCRIPT_DUPLICATE", "Job '" + positional[0] + "' is defined twice"); return; }
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.jobs.push_back(AnalysisJob{positional[0], {}});
        } else if (command == "step") {
            AnalysisStep step;
            if (positional.empty()) { error("SCRIPT_STEP", "step needs 'static', 'frequency' or 'dynamic'"); return; }
            const std::string kind = lowered(positional[0]);
            for (std::size_t i = 1; i < positional.size(); ++i) {
                if (lowered(positional[i]) == "reset") step.resetLoads = true;
                else { error("SCRIPT_UNEXPECTED", "Unexpected '" + positional[i] + "' after step " + kind); return; }
            }
            std::string reset;
            if (take("reset", reset)) {
                const std::string v = lowered(reset);
                if (v == "yes" || v == "true" || v == "1") step.resetLoads = true;
                else if (v == "no" || v == "false" || v == "0") step.resetLoads = false;
                else { error("SCRIPT_BAD_RESET", "reset takes yes or no"); return; }
            }
            std::string loads;
            if (take("loads", loads)) {
                if (lowered(loads) == "all") step.allLoads = true;
                else if (lowered(loads) == "none") step.allLoads = false;   // no load at all in this step
                else {
                    step.allLoads = false;
                    std::istringstream list(loads);
                    std::string id;
                    while (std::getline(list, id, ',')) {
                        if (!validConditionId(id)) { error("SCRIPT_BAD_ID", "loads= takes load ids separated by commas, got '" + id + "'"); return; }
                        step.loads.push_back(id);
                    }
                }
            }
            if (kind == "static") step.type = StepType::Static;
            else if (kind == "dynamic") {
                step.type = StepType::Dynamic;
                double increment = 0.0, period = 0.0, minimum = 0.0, maximum = 0.0;
                if (!number("dt", increment, true) || !number("period", period, true)) { leftovers(); return; }
                if (!(increment > 0.0) || !(period > 0.0) || increment > period) {
                    error("SCRIPT_BAD_DYNAMIC", "dynamic needs 0 < dt <= period");
                    return;
                }
                number("dtmin", minimum, false);
                number("dtmax", maximum, false);
                if (minimum < 0.0 || maximum < 0.0 || (maximum > 0.0 && maximum < increment)) {
                    error("SCRIPT_BAD_DYNAMIC", "dtmin and dtmax must be positive, with dtmax >= dt");
                    return;
                }
                step.dynamicIncrement = increment;
                step.dynamicPeriod = period;
                step.dynamicMinIncrement = minimum;
                step.dynamicMaxIncrement = maximum;
            }
            else if (kind == "frequency") {
                step.type = StepType::Frequency;
                double modes = 6;
                if (number("modes", modes, false)) {
                    if (modes < 1 || modes > 1000 || modes != std::floor(modes)) { error("SCRIPT_BAD_MODES", "modes must be an integer between 1 and 1000"); return; }
                    step.modes = static_cast<std::size_t>(modes);
                }
            } else { error("SCRIPT_STEP", "Unknown step '" + positional[0] + "' (static, frequency or dynamic)"); return; }
            leftovers();
            if (result.diagnostics.size() == errorsBefore) {
                // Steps belong to the last job; a script that starts with a step gets the default job.
                if (result.model.jobs.empty()) result.model.jobs.push_back(AnalysisJob{"job", {}});
                result.model.jobs.back().steps.push_back(step);
                haveStep = true;
            }
        } else if (command == "amplitude") {
            AnalysisAmplitude a;
            std::string name, values;
            if (!positional.empty()) { error("SCRIPT_UNEXPECTED", "Unexpected '" + positional[0] + "' after amplitude"); return; }
            if (!take("name", name) || !validConditionId(name)) { error("SCRIPT_BAD_NAME", "amplitude needs name=NAME (letters, digits, _ - .)"); return; }
            if (!take("t", values)) { error("SCRIPT_MISSING_KEY", "amplitude needs t=time,factor,time,factor,..."); return; }
            std::vector<double> numbers;
            {
                std::istringstream in(values);
                std::string cell;
                while (std::getline(in, cell, ',')) {
                    double v = 0.0;
                    if (!parseNumber(cell, v)) { error("SCRIPT_BAD_NUMBER", "'" + cell + "' is not a number (t)"); return; }
                    numbers.push_back(v);
                }
            }
            if (numbers.size() < 4 || numbers.size() % 2 != 0) { error("SCRIPT_BAD_AMPLITUDE", "amplitude needs at least two (time, factor) pairs"); return; }
            for (std::size_t i = 0; i < numbers.size(); i += 2) a.points.push_back({numbers[i], numbers[i + 1]});
            for (std::size_t i = 1; i < a.points.size(); ++i)
                if (!(a.points[i][0] > a.points[i - 1][0])) { error("SCRIPT_BAD_AMPLITUDE", "amplitude times must be strictly increasing"); return; }
            for (const auto& existing : result.model.amplitudes)
                if (existing.name == name) { error("SCRIPT_DUPLICATE", "amplitude '" + name + "' is defined twice"); return; }
            a.name = name;
            leftovers();
            if (result.diagnostics.size() == errorsBefore) result.model.amplitudes.push_back(a);
        } else if (command == "output") {
            std::string element;
            if (take("element", element)) {
                const std::string v = lowered(element);
                if (v == "on") result.model.elementPrint = 1;
                else if (v == "off") result.model.elementPrint = 0;
                else if (v == "auto") result.model.elementPrint = -1;
                else error("SCRIPT_BAD_OUTPUT", "output element= takes on, off or auto");
            }
            leftovers();
        } else {
            error("SCRIPT_UNKNOWN_COMMAND", "Unknown command '" + tokens[0] + "' (material, fix, force, pressure, gravity, amplitude, job, step, output)");
        }
    }
};

} // namespace

AnalysisParseResult parseAnalysisScript(const std::string& text) {
    Parser p;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        ++p.line;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        p.parseLine(raw);
    }
    p.line = 0;
    if (!p.haveMaterial) p.error("SCRIPT_NO_MATERIAL", "The script has no 'material' line");
    if (!p.haveStep) {
        p.warning("SCRIPT_DEFAULT_STEP", "No 'step' line: a static analysis is assumed");
        if (p.result.model.jobs.empty()) p.result.model.jobs.push_back(AnalysisJob{"job", {AnalysisStep{}}});
    }
    for (const auto& job : p.result.model.jobs)
        if (job.steps.empty()) p.error("SCRIPT_EMPTY_JOB", "Job '" + job.id + "' has no step");
    // Load ids named by a step must exist (checked here, because the loads may follow the job line).
    {
        std::set<std::string> loadIds;
        for (const auto& f : p.result.model.forces) loadIds.insert(f.id);
        for (const auto& q : p.result.model.pressures) loadIds.insert(q.id);
        if (p.result.model.hasGravity) loadIds.insert(kGravityLoadId);
        for (const auto& job : p.result.model.jobs)
            for (const auto& step : job.steps)
                for (const auto& id : step.loads)
                    if (!loadIds.count(id)) p.error("SCRIPT_UNKNOWN_LOAD", "Job '" + job.id + "' uses the load '" + id + "' that is not defined");
    }
    auto known = [&](const std::string& name) {
        return name.empty() || std::any_of(p.result.model.amplitudes.begin(), p.result.model.amplitudes.end(),
                                           [&](const AnalysisAmplitude& a) { return a.name == name; });
    };
    for (const auto& f : p.result.model.forces)
        if (!known(f.amplitude)) p.error("SCRIPT_UNKNOWN_AMPLITUDE", "force uses amplitude '" + f.amplitude + "' that is not defined");
    for (const auto& q : p.result.model.pressures)
        if (!known(q.amplitude)) p.error("SCRIPT_UNKNOWN_AMPLITUDE", "pressure uses amplitude '" + q.amplitude + "' that is not defined");
    p.result.success = std::none_of(p.result.diagnostics.begin(), p.result.diagnostics.end(),
                                    [](const ImportDiagnostic& d) { return d.level == ImportDiagnosticLevel::Error; });
    return std::move(p.result);
}

std::string serializeAnalysisScript(const AnalysisModel& model) {
    std::ostringstream out;
    out << std::setprecision(17);
    out << "material";
    if (!model.material.id.empty()) out << " id=" << model.material.id;
    out << " E=" << model.material.youngModulus << " nu=" << model.material.poisson
        << " rho=" << model.material.density << " name=" << model.material.name << '\n';
    auto selectorText = [](const NodeSelector& selector) {
        if (selector.kind == SelectorKind::All) return std::string("all");
        if (selector.kind == SelectorKind::Group) return std::string("group:") + selector.name;
        if (selector.kind == SelectorKind::Nodes) return std::string("all");   // never written: see withMeshConstraints()
        std::ostringstream value;
        value << "plane:" << "xyz"[std::max(0, std::min(2, selector.axis))] << '=' << selector.position;
        return value.str();
    };
    for (const auto& support : model.supports) {
        out << "fix";
        if (!support.id.empty()) out << " id=" << support.id;
        out << ' ' << selectorText(support.where) << " dof=";
        const bool all = support.dof[0] && support.dof[1] && support.dof[2];
        if (all) out << "all";
        else {
            bool first = true;
            for (std::size_t i = 0; i < 3; ++i) if (support.dof[i]) {
                if (!first) out << ',';
                out << (i + 1); first = false;
            }
        }
        out << " value=" << support.displacement[0] << ',' << support.displacement[1] << ',' << support.displacement[2];
        if (support.where.tolerance >= 0.0) out << " tol=" << support.where.tolerance;
        out << '\n';
    }
    for (const auto& a : model.amplitudes) {
        out << "amplitude name=" << a.name << " t=";
        for (std::size_t i = 0; i < a.points.size(); ++i) out << (i ? "," : "") << a.points[i][0] << ',' << a.points[i][1];
        out << '\n';
    }
    for (const auto& force : model.forces) {
        out << "force";
        if (!force.id.empty()) out << " id=" << force.id;
        out << ' ' << selectorText(force.where) << " F=" << force.total[0] << ',' << force.total[1] << ',' << force.total[2];
        if (!force.amplitude.empty()) out << " amp=" << force.amplitude;
        if (force.where.tolerance >= 0.0) out << " tol=" << force.where.tolerance;
        out << '\n';
    }
    for (const auto& pressure : model.pressures) {
        out << "pressure";
        if (!pressure.id.empty()) out << " id=" << pressure.id;
        out << " group:" << pressure.group << " p=" << pressure.pressure;
        if (!pressure.amplitude.empty()) out << " amp=" << pressure.amplitude;
        out << '\n';
    }
    if (model.hasGravity)
        out << "gravity g=" << model.gravity[0] << ',' << model.gravity[1] << ',' << model.gravity[2] << '\n';
    for (const auto& job : model.jobs) {
        out << "job " << job.id << '\n';
        for (const auto& step : job.steps) {
            out << "step " << (step.type == StepType::Static ? "static" : step.type == StepType::Frequency ? "frequency" : "dynamic");
            if (step.type == StepType::Frequency) out << " modes=" << step.modes;
            if (step.type == StepType::Dynamic) {
                out << " dt=" << step.dynamicIncrement << " period=" << step.dynamicPeriod;
                if (step.dynamicMinIncrement > 0.0) out << " dtmin=" << step.dynamicMinIncrement;
                if (step.dynamicMaxIncrement > 0.0) out << " dtmax=" << step.dynamicMaxIncrement;
            }
            if (!step.allLoads) {
                out << " loads=";
                if (step.loads.empty()) out << "none";
                for (std::size_t i = 0; i < step.loads.size(); ++i) out << (i ? "," : "") << step.loads[i];
            }
            if (step.resetLoads) out << " reset";
            out << '\n';
        }
    }
    out << "output element=" << (model.elementPrint > 0 ? "on" : model.elementPrint == 0 ? "off" : "auto") << '\n';
    return out.str();
}


namespace {
bool validConditionId(const std::string& id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '-' || c == '.';
    });
}
template<class T> T* findCondition(std::vector<T>& values, const std::string& id) {
    const auto it = std::find_if(values.begin(), values.end(), [&](const T& value) { return value.id == id; });
    return it == values.end() ? nullptr : &*it;
}
template<class T> const T* findCondition(const std::vector<T>& values, const std::string& id) {
    const auto it = std::find_if(values.begin(), values.end(), [&](const T& value) { return value.id == id; });
    return it == values.end() ? nullptr : &*it;
}
template<class T> bool eraseCondition(std::vector<T>& values, const std::string& id) {
    const auto it = std::find_if(values.begin(), values.end(), [&](const T& value) { return value.id == id; });
    if (it == values.end()) return false;
    values.erase(it);
    return true;
}
template<class T> bool replaceCondition(std::vector<T>& values, const std::string& id, const T& replacement, std::string& error) {
    auto* current = findCondition(values, id);
    if (!current) { error = "Condition not found: " + id; return false; }
    if (!validConditionId(id)) { error = "Invalid condition ID: " + id; return false; }
    T updated = replacement;
    updated.id = id;
    *current = std::move(updated);
    error.clear();
    return true;
}
}
const AnalysisJob* findJob(const AnalysisModel& m, const std::string& id) {
    if (id.empty()) return m.jobs.size() == 1 ? &m.jobs.front() : nullptr;
    for (const auto& job : m.jobs) if (job.id == id) return &job;
    return nullptr;
}

std::vector<std::string> activeLoadIds(const AnalysisModel& m, const AnalysisStep& step) {
    std::vector<std::string> all;
    if (step.allLoads) {
        for (const auto& f : m.forces) all.push_back(f.id);
        for (const auto& p : m.pressures) all.push_back(p.id);
        if (m.hasGravity) all.push_back(kGravityLoadId);
        return all;
    }
    return step.loads;
}

FixedSupport* findSupport(AnalysisModel& m, const std::string& id) { return findCondition(m.supports, id); }
NodalForce* findForce(AnalysisModel& m, const std::string& id) { return findCondition(m.forces, id); }
SurfacePressure* findPressure(AnalysisModel& m, const std::string& id) { return findCondition(m.pressures, id); }
const FixedSupport* findSupport(const AnalysisModel& m, const std::string& id) { return findCondition(m.supports, id); }
const NodalForce* findForce(const AnalysisModel& m, const std::string& id) { return findCondition(m.forces, id); }
const SurfacePressure* findPressure(const AnalysisModel& m, const std::string& id) { return findCondition(m.pressures, id); }
bool updateMaterial(AnalysisModel& m, const std::string& id, const AnalysisMaterial& r, std::string& e) {
    if (m.material.id != id) { e = "Material not found: " + id; return false; }
    if (!validConditionId(id)) { e = "Invalid condition ID: " + id; return false; }
    m.material = r; m.material.id = id; e.clear(); return true;
}
bool updateSupport(AnalysisModel& m, const std::string& id, const FixedSupport& r, std::string& e) { return replaceCondition(m.supports, id, r, e); }
bool updateForce(AnalysisModel& m, const std::string& id, const NodalForce& r, std::string& e) { return replaceCondition(m.forces, id, r, e); }
bool updatePressure(AnalysisModel& m, const std::string& id, const SurfacePressure& r, std::string& e) { return replaceCondition(m.pressures, id, r, e); }
bool eraseSupport(AnalysisModel& m, const std::string& id) { return eraseCondition(m.supports, id); }
bool eraseForce(AnalysisModel& m, const std::string& id) { return eraseCondition(m.forces, id); }
bool erasePressure(AnalysisModel& m, const std::string& id) { return eraseCondition(m.pressures, id); }

std::string exampleAnalysisScript() {
    return "# Cantilever: clamped at x = 0, load at x = 10 (units: mm, N, tonne, s)\n"
           "material E=210000 nu=0.3 rho=7.85e-9\n"
           "fix plane:x=0\n"
           "force plane:x=10 F=0,-100,0\n"
           "step static\n"
           "# step frequency modes=6\n";
}

std::string PrescribedDisplacementTracker::add(std::size_t supportIndex, const FixedSupport& support,
                                               const std::vector<NodeIndex>& nodes, const MeshData& mesh) {
    for (std::size_t d = 0; d < 3; ++d)
        if (support.dof[d] && !std::isfinite(support.displacement[d]))
            return "fix #" + std::to_string(supportIndex + 1) + " has a non-finite prescribed displacement";
    for (const NodeIndex n : nodes) {
        auto& entries = m_entries[n];
        for (std::size_t d = 0; d < 3; ++d) {
            if (!support.dof[d]) continue;
            const double value = support.displacement[d];
            Entry& entry = entries[d];
            if (entry.support == static_cast<std::size_t>(-1)) {
                entry.support = supportIndex;
                entry.value = value;
                continue;
            }
            const double scale = std::max({1.0, std::fabs(value), std::fabs(entry.value)});
            if (std::fabs(value - entry.value) > 1e-12 * scale)
                return "conflicting prescribed displacements: fix #" + std::to_string(entry.support + 1) +
                       " and fix #" + std::to_string(supportIndex + 1) + " assign different values to node " +
                       std::to_string(mesh.nodeId(n)) + " degree of freedom " + std::to_string(d + 1);
        }
    }
    return {};
}

AnalysisModel withMeshConstraints(const MeshData& mesh, const AnalysisModel& model, std::vector<ImportDiagnostic>& diagnostics) {
    AnalysisModel out = model;
    if (mesh.constrainedNodeCount() == 0) return out;
    // One support per distinct (translations, values): the nodes are listed in node order inside it.
    std::map<std::pair<DofMask, std::array<double, 3>>, std::vector<NodeIndex>> classes;
    std::size_t withRotation = 0, taken = 0;
    for (const auto& entry : mesh.getConstraints()) {
        const auto& c = entry.second;
        if (c.dofs & kDofRotations) ++withRotation;
        const DofMask translations = static_cast<DofMask>(c.dofs & kDofTranslations);
        if (translations == 0) continue;
        classes[{translations, {c.values[0], c.values[1], c.values[2]}}].push_back(c.node);
        ++taken;
    }
    if (withRotation > 0)
        diagnostics.push_back({ImportDiagnosticLevel::Warning, "CONSTRAINT_ROTATION_IGNORED",
                               std::to_string(withRotation) + " node(s) of the mesh constrain a rotation, which solid elements do not have: the rotations are not written", 0});
    std::size_t index = 0;
    for (auto& entry : classes) {
        FixedSupport support;
        support.id = "mesh-constraint-" + std::to_string(++index);
        support.where.kind = SelectorKind::Nodes;
        support.where.nodes = std::move(entry.second);
        for (std::size_t d = 0; d < 3; ++d) {
            support.dof[d] = (entry.first.first >> d) & 1u;
            support.displacement[d] = entry.first.second[d];
        }
        out.supports.push_back(std::move(support));
    }
    if (taken > 0)
        diagnostics.push_back({ImportDiagnosticLevel::Info, "CONSTRAINT_FROM_MESH",
                               std::to_string(taken) + " node(s) constrained in the mesh are added to the supports of the script (fix #" +
                               std::to_string(model.supports.size() + 1) + (index > 1 ? " to #" + std::to_string(model.supports.size() + index) : std::string()) + ")", 0});
    return out;
}

std::vector<NodeConstraint> supportConstraints(const MeshData& mesh, const AnalysisModel& model,
                                               std::vector<ImportDiagnostic>& diagnostics) {
    std::map<NodeIndex, NodeConstraint> merged;
    PrescribedDisplacementTracker tracker;
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        const auto& support = model.supports[i];
        const std::string label = "fix #" + std::to_string(i + 1);
        DofMask dofs = 0;
        for (std::size_t d = 0; d < 3; ++d) if (support.dof[d]) dofs = static_cast<DofMask>(dofs | (1u << d));
        if (dofs == 0) { diagnostics.push_back({ImportDiagnosticLevel::Warning, "SUPPORT_NO_DOF", label + " constrains no direction", 0}); continue; }
        std::string error;
        const auto nodes = selectNodes(mesh, support.where, error);
        if (!error.empty()) { diagnostics.push_back({ImportDiagnosticLevel::Warning, "SUPPORT_SELECTION", label + ": " + error, 0}); continue; }
        const auto conflict = tracker.add(i, support, nodes, mesh);
        if (!conflict.empty()) { diagnostics.push_back({ImportDiagnosticLevel::Warning, "SUPPORT_CONFLICT", conflict, 0}); continue; }
        for (const NodeIndex n : nodes) {
            auto& c = merged[n];
            c.node = n;
            for (std::size_t d = 0; d < 3; ++d)
                if (support.dof[d]) c.values[d] = support.displacement[d];
            c.dofs = static_cast<DofMask>(c.dofs | dofs);
        }
    }
    std::vector<NodeConstraint> out;
    out.reserve(merged.size());
    for (const auto& entry : merged) out.push_back(entry.second);
    return out;
}

void applySupports(MeshData& mesh, const AnalysisModel& model, std::vector<ImportDiagnostic>& diagnostics) {
    for (const auto& c : supportConstraints(mesh, model, diagnostics)) mesh.constrainNode(c.node, c.dofs, c.values);
}

namespace {

/** @brief Eigenvector of the smallest eigenvalue of a symmetric 3x3 matrix, by cyclic Jacobi rotations. */
std::array<double, 3> smallestEigenvector(std::array<std::array<double, 3>, 3> a) {
    std::array<std::array<double, 3>, 3> v{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};
    for (int sweep = 0; sweep < 64; ++sweep) {
        const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
        const double scale = std::fabs(a[0][0]) + std::fabs(a[1][1]) + std::fabs(a[2][2]);
        if (off <= 1e-15 * scale || off == 0.0) break;
        for (int p = 0; p < 2; ++p) {
            for (int q = p + 1; q < 3; ++q) {
                if (a[p][q] == 0.0) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0);
                const double s = t * c;
                for (int k = 0; k < 3; ++k) { // A <- A J
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - s * akq;
                    a[k][q] = s * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) { // A <- J^T A
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - s * aqk;
                    a[q][k] = s * apk + c * aqk;
                }
                for (int k = 0; k < 3; ++k) { // V <- V J
                    const double vkp = v[k][p], vkq = v[k][q];
                    v[k][p] = c * vkp - s * vkq;
                    v[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (a[i][i] < a[best][best]) best = i;
    return {v[0][best], v[1][best], v[2][best]};
}

} // namespace

bool fitHoleCircle(const MeshData& mesh, const std::vector<NodeIndex>& edge, HoleFit& fit) {
    if (edge.size() < 3) return false;
    const double count = static_cast<double>(edge.size());

    std::array<double, 3> centre{{0.0, 0.0, 0.0}};
    for (const NodeIndex n : edge) {
        const auto p = mesh.nodeCoordinates(n);
        for (std::size_t k = 0; k < 3; ++k) centre[k] += p[k] / count;
    }

    // Covariance of the offsets from the centroid: its smallest-spread direction is the normal of the edge plane.
    std::array<std::array<double, 3>, 3> cov{};
    for (const NodeIndex n : edge) {
        const auto p = mesh.nodeCoordinates(n);
        std::array<double, 3> d{};
        for (std::size_t k = 0; k < 3; ++k) d[k] = p[k] - centre[k];
        for (std::size_t i = 0; i < 3; ++i)
            for (std::size_t j = 0; j < 3; ++j) cov[i][j] += d[i] * d[j] / count;
    }
    std::array<double, 3> axis = smallestEigenvector(cov);
    const double axisLength = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!(axisLength > 0.0) || !std::isfinite(axisLength)) return false;
    for (double& a : axis) a /= axisLength;

    // Radius: mean in-plane distance. Deviation: largest radial or axial excursion relative to that radius.
    std::vector<double> radial(edge.size()), axial(edge.size());
    double sumRadial = 0.0;
    for (std::size_t i = 0; i < edge.size(); ++i) {
        const auto p = mesh.nodeCoordinates(edge[i]);
        std::array<double, 3> d{};
        for (std::size_t k = 0; k < 3; ++k) d[k] = p[k] - centre[k];
        axial[i] = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
        std::array<double, 3> perp{};
        for (std::size_t k = 0; k < 3; ++k) perp[k] = d[k] - axial[i] * axis[k];
        radial[i] = std::sqrt(perp[0] * perp[0] + perp[1] * perp[1] + perp[2] * perp[2]);
        sumRadial += radial[i];
    }
    const double radius = sumRadial / count;
    if (!(radius > 0.0) || !std::isfinite(radius)) return false;

    double maxDeviation = 0.0;
    for (std::size_t i = 0; i < edge.size(); ++i)
        maxDeviation = std::max(maxDeviation, std::max(std::fabs(radial[i] - radius), std::fabs(axial[i])) / radius);

    fit.centre = centre;
    fit.axis = axis;
    fit.radius = radius;
    fit.maxDeviation = maxDeviation;
    return true;
}

std::vector<RigidLink> holeRigidLinks(const MeshData& mesh, const std::vector<std::vector<NodeIndex>>& holes,
                                      const std::array<double, 3>& reference, std::vector<ImportDiagnostic>& diagnostics,
                                      std::string& error) {
    std::vector<RigidLink> links;
    for (std::size_t h = 0; h < holes.size(); ++h) {
        const std::string label = "rbe2-" + std::to_string(h + 1);
        HoleFit fit;
        if (!fitHoleCircle(mesh, holes[h], fit)) {
            error = "Hole " + std::to_string(h + 1) + " cannot be fitted: it needs at least three nodes that are not all the same point";
            return {};
        }
        // Distance of the reference from the hole axis (not from the centre along the axis).
        std::array<double, 3> d{};
        for (std::size_t k = 0; k < 3; ++k) d[k] = reference[k] - fit.centre[k];
        const double along = d[0] * fit.axis[0] + d[1] * fit.axis[1] + d[2] * fit.axis[2];
        double offAxis2 = 0.0;
        for (std::size_t k = 0; k < 3; ++k) {
            const double perp = d[k] - along * fit.axis[k];
            offAxis2 += perp * perp;
        }
        if (std::sqrt(offAxis2) > 0.05 * fit.radius)
            diagnostics.push_back({ImportDiagnosticLevel::Warning, "RBE2_OFF_AXIS",
                                   label + ": the reference point is off the hole axis by more than 5 % of the radius", 0});
        if (fit.maxDeviation > 0.05)
            diagnostics.push_back({ImportDiagnosticLevel::Warning, "RBE2_NOT_CIRCULAR",
                                   label + ": the hole nodes deviate more than 5 % from a circle", 0});

        RigidLink link;
        link.id = label;
        link.reference = reference;
        link.slaves = holes[h];
        links.push_back(std::move(link));
    }
    return links;
}

std::vector<NodeIndex> selectNodes(const MeshData& mesh, const NodeSelector& selector, std::string& error) {
    std::vector<NodeIndex> out;
    error.clear();
    const std::size_t n = mesh.getNumNodes();
    if (selector.kind == SelectorKind::All) {
        out.resize(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<NodeIndex>(i);
        return out;
    }
    if (selector.kind == SelectorKind::Nodes) {
        for (const NodeIndex node : selector.nodes)
            if (node >= n) { error = "A node index of the selection is not in the mesh"; return out; }
        out = selector.nodes;
        return out;
    }
    if (selector.kind == SelectorKind::Plane) {
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (std::size_t i = 0; i < n; ++i) {
            const auto c = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
            for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], c[k]); hi[k] = std::max(hi[k], c[k]); }
        }
        const double diag = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        const double tol = selector.tolerance >= 0.0 ? selector.tolerance : 1e-6 * std::max(diag, 1e-300);
        for (std::size_t i = 0; i < n; ++i)
            if (std::fabs(mesh.nodeCoordinates(static_cast<NodeIndex>(i))[static_cast<std::size_t>(selector.axis)] - selector.position) <= tol)
                out.push_back(static_cast<NodeIndex>(i));
        if (out.empty()) {
            std::ostringstream m;
            m << "No node lies on the plane " << "xyz"[selector.axis] << " = " << selector.position << " (tolerance " << tol << "; the model spans "
              << lo[selector.axis] << " .. " << hi[selector.axis] << " in that direction)";
            error = m.str();
        }
        return out;
    }
    // Group: prefer an explicit node set, then a generic mesh group, then
    // legacy physical groups. Generic node memberships are authoritative: they
    // preserve many-to-many CAD membership without reconstructing it from elements.
    const auto& sets = mesh.getNodeSets();
    auto set = sets.find(selector.name);
    if (set == sets.end())
        for (auto it = sets.begin(); it != sets.end(); ++it)
            if (lowered(it->first) == lowered(selector.name)) { set = it; break; }
    if (set != sets.end()) {
        out = set->second;
    } else {
        const MeshGroup* generic = mesh.getMeshGroupRegistry().find(selector.name);
        if (!generic)
            for (const auto& g : mesh.getMeshGroupRegistry().groups())
                if (lowered(g.name) == lowered(selector.name)) { generic = &g; break; }
        if (generic) {
            out = generic->nodeIndices;
            if (out.empty() && !generic->elementIds.empty()) {
                std::unordered_map<ElementId, char> ids;
                for (const ElementId id : generic->elementIds) ids.emplace(id, 1);
                for (const auto& block : mesh.getElementBlocks())
                    for (std::size_t e = 0; e < block.size(); ++e)
                        if (ids.count(block.ids[e]))
                            for (std::size_t k = 0; k < block.nodesPerElement; ++k)
                                out.push_back(block.connectivity[e * block.nodesPerElement + k]);
            }
        } else {
            const MeshPhysicalGroup* group = nullptr;
            for (const auto& g : mesh.getPhysicalGroups()) if (g.name == selector.name) { group = &g; break; }
            if (!group)
                for (const auto& g : mesh.getPhysicalGroups()) if (lowered(g.name) == lowered(selector.name)) { group = &g; break; }
            if (!group) {
                std::string known;
                for (const auto& g : mesh.getPhysicalGroups()) known += (known.empty() ? "" : ", ") + g.name;
                for (const auto& g : mesh.getMeshGroupRegistry().groups()) known += (known.empty() ? "" : ", ") + g.name;
                for (const auto& s : sets) known += (known.empty() ? "" : ", ") + s.first;
                error = "Unknown group or node set '" + selector.name + "'" + (known.empty() ? " (the mesh has none)" : " (available: " + known + ")");
                return out;
            }
            std::unordered_map<ElementId, char> ids;
            for (const ElementId id : group->elementIds) ids.emplace(id, 1);
            for (const auto& block : mesh.getElementBlocks())
                for (std::size_t e = 0; e < block.size(); ++e)
                    if (ids.count(block.ids[e]))
                        for (std::size_t k = 0; k < block.nodesPerElement; ++k)
                            out.push_back(block.connectivity[e * block.nodesPerElement + k]);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (out.empty() && error.empty()) error = "The group or node set '" + selector.name + "' contains no node";
    return out;
}

namespace {

/** Corner positions of the faces of an element type in CalculiX numbering (0-based); empty = pressure unsupported. */
std::vector<std::vector<int>> faceTable(ElementType type) {
    switch (type) {
    case ElementType::Tetrahedron4:
    case ElementType::Tetrahedron10:
        return {{0, 1, 2}, {0, 3, 1}, {1, 3, 2}, {2, 3, 0}};
    case ElementType::Hexahedron8:
    case ElementType::Hexahedron20:
        return {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1}, {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}};
    default:
        return {};
    }
}

int surfaceCorners(ElementType type) {
    switch (type) {
    case ElementType::Triangle3: case ElementType::Triangle6: return 3;
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8: case ElementType::Quadrilateral9: return 4;
    default: return 0;
    }
}

std::string idList(const MeshData& mesh, const std::vector<NodeIndex>& nodes) {
    std::ostringstream out;
    std::size_t count = 0;
    for (const NodeIndex n : nodes) {
        if (count) out << ", ";
        out << mesh.nodeId(n);
        if (++count == 16) { out << "\n"; count = 0; }
    }
    if (count) out << "\n";
    return out.str();
}

void note(SolverInputResult& r, ImportDiagnosticLevel level, std::string code, std::string message) {
    r.diagnostics.push_back({level, std::move(code), std::move(message), 0});
}

SolverInputResult failed(SolverInputResult r, std::string message) {
    r.success = false;
    r.error = std::move(message);
    return r;
}

} // namespace

SolverInputResult writeSolverInput(const MeshData& mesh, const AnalysisModel& scriptModel, const std::filesystem::path& path,
                                   const std::string& jobId) {
    SolverInputResult r;
    const AnalysisModel model = withMeshConstraints(mesh, scriptModel, r.diagnostics);
    const AnalysisJob* job = findJob(model, jobId);
    if (!job) {
        if (jobId.empty() && model.jobs.size() > 1) return failed(r, "The model has several jobs: name the job to write");
        return failed(r, jobId.empty() ? std::string("The model has no job") : "No job '" + jobId + "'");
    }
    if (job->steps.empty()) return failed(r, "Job '" + job->id + "' has no step");
    const auto& m = model.material;
    if (!(m.youngModulus > 0.0) || !std::isfinite(m.youngModulus)) return failed(r, "Young's modulus must be positive");
    if (!(m.poisson > -1.0 && m.poisson < 0.5)) return failed(r, "Poisson's ratio must lie between -1 and 0.5 (exclusive)");
    bool needDensity = false;
    for (const auto& step : job->steps) {
        const auto active = activeLoadIds(model, step);
        if (step.type != StepType::Static) needDensity = true;
        if (model.hasGravity && std::find(active.begin(), active.end(), std::string(kGravityLoadId)) != active.end())
            needDensity = true;
    }
    if (needDensity && !(m.density > 0.0)) return failed(r, "A frequency analysis and gravity need the density (rho=)");
    for (const auto& f : model.forces)
        if (!f.amplitude.empty() && !std::any_of(model.amplitudes.begin(), model.amplitudes.end(), [&](const AnalysisAmplitude& a) { return a.name == f.amplitude; }))
            return failed(r, "force uses the undefined amplitude '" + f.amplitude + "'");
    for (const auto& p : model.pressures)
        if (!p.amplitude.empty() && !std::any_of(model.amplitudes.begin(), model.amplitudes.end(), [&](const AnalysisAmplitude& a) { return a.name == p.amplitude; }))
            return failed(r, "pressure uses the undefined amplitude '" + p.amplitude + "'");
    bool anyNonFrequency = false;
    for (std::size_t s = 0; s < job->steps.size(); ++s) {
        const auto& step = job->steps[s];
        const std::string label = "step " + std::to_string(s + 1) + " of job '" + job->id + "'";
        const auto active = activeLoadIds(model, step);
        const bool gravityActive = model.hasGravity && std::find(active.begin(), active.end(), std::string(kGravityLoadId)) != active.end();
        if (step.type != StepType::Frequency) anyNonFrequency = true;
        if (step.type == StepType::Frequency && gravityActive)
            note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_GRAVITY_IGNORED", label + ": loads (gravity included) have no effect in a frequency step");
        if (step.type == StepType::Frequency && (!model.forces.empty() || !model.pressures.empty()) && !active.empty())
            note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_LOADS_IGNORED", label + ": forces and pressures have no effect in a frequency step");
        if (step.type == StepType::Static && active.empty())
            note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_NO_LOAD", label + " has no active load: its results will be zero (apart from the carried loads)");
        if (step.type == StepType::Frequency && model.supports.empty())
            note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_FREE_BODY", label + ": frequency analysis without supports: the first modes are rigid body motions with zero frequency");
    }
    if (anyNonFrequency && model.supports.empty())
        return failed(r, "A static or dynamic analysis needs at least one support (fix ...): without it the body can move freely and the solver cannot converge");

    // Rigid links: every slave is a mesh node, and a node belongs to one link only (CalculiX rejects overlaps).
    {
        std::vector<char> coupled(mesh.getNumNodes(), 0);
        for (const auto& link : model.rigidLinks) {
            if (link.slaves.empty()) return failed(r, "Rigid link " + link.id + " has no nodes");
            for (const NodeIndex n : link.slaves) {
                if (n >= mesh.getNumNodes()) return failed(r, "Rigid link " + link.id + " uses a node that is not in the mesh");
                if (coupled[n]) return failed(r, "Node " + std::to_string(mesh.nodeId(n)) + " belongs to two rigid links");
                coupled[n] = 1;
            }
        }
    }

    // Volume part of the mesh only.
    MeshData solid;
    std::vector<std::string> volumeSets;
    NodeId lastNodeId = 0;
    for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
        const auto c = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
        solid.addNode(mesh.nodeId(static_cast<NodeIndex>(i)), c[0], c[1], c[2]);
        lastNodeId = std::max(lastNodeId, mesh.nodeId(static_cast<NodeIndex>(i)));
    }
    // Reference nodes of the rigid links: not mesh nodes, so they take the ids after the largest mesh id.
    std::vector<NodeId> rigidMasterIds;
    for (const auto& link : model.rigidLinks) {
        rigidMasterIds.push_back(++lastNodeId);
        solid.addNode(lastNodeId, link.reference[0], link.reference[1], link.reference[2]);
    }
    std::vector<ElementId> volumeIds;
    std::unordered_map<std::string, std::pair<ElementType, std::size_t>> blockSeen;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 3) continue;
        if (AbaqusInpWriter::abaqusElementType(block.type).empty())
            return failed(r, "Element type of block '" + block.name + "' cannot be written for CalculiX");
        for (std::size_t e = 0; e < block.size(); ++e) {
            std::vector<NodeIndex> conn(block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * block.nodesPerElement),
                                        block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * block.nodesPerElement));
            solid.addElement(block.name, block.type, block.ids[e], conn);
            volumeIds.push_back(block.ids[e]);
        }
        volumeSets.push_back(AbaqusInpWriter::sanitizeName(block.name));
    }
    if (volumeIds.empty()) return failed(r, "The mesh has no volume elements: generate a volume mesh first (surface meshes cannot be analysed here)");
    {
        std::unordered_map<ElementId, char> inVolume;
        for (const ElementId id : volumeIds) inVolume.emplace(id, 1);
        for (const auto& g : mesh.getPhysicalGroups()) {
            if (g.dimension != 3) continue;
            std::vector<ElementId> ids;
            for (const ElementId id : g.elementIds) if (inVolume.count(id)) ids.push_back(id);
            solid.addPhysicalGroup(g.name, g.dimension, g.tag, ids, g.materialName);
        }
    }
    // Preserve user/imported node sets in the analysis deck. The volume-only
    // mesh retains all original nodes, so their node-index memberships remain valid.
    for (const auto& nodeSet : mesh.getNodeSets())
        solid.addNodeSet(nodeSet.first, nodeSet.second);
    r.nodes = solid.getNumNodes();
    r.elements = volumeIds.size();

    // Resolve conditions. Every load of the model is resolved once; the steps only choose which ones act.
    std::string error;
    std::vector<std::vector<NodeIndex>> supportNodes, forceNodes;
    PrescribedDisplacementTracker displacementTracker;
    std::vector<char> constrained(mesh.getNumNodes(), 0);
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        supportNodes.push_back(selectNodes(mesh, model.supports[i].where, error));
        if (!error.empty()) return failed(r, "fix #" + std::to_string(i + 1) + ": " + error);
        if (!model.supports[i].dof[0] && !model.supports[i].dof[1] && !model.supports[i].dof[2])
            return failed(r, "fix #" + std::to_string(i + 1) + " constrains no direction");
        const auto conflict = displacementTracker.add(i, model.supports[i], supportNodes.back(), mesh);
        if (!conflict.empty()) return failed(r, conflict);
        for (const NodeIndex n : supportNodes.back()) constrained[n] = 1;
    }
    for (const char c : constrained) r.fixedNodes += c ? 1 : 0;
    std::size_t quadratic = 0;
    for (const auto& b : solid.getElementBlocks()) quadratic += (b.type == ElementType::Tetrahedron10 || b.type == ElementType::Hexahedron20) ? 1 : 0;
    for (std::size_t i = 0; i < model.forces.size(); ++i) {
        forceNodes.push_back(selectNodes(mesh, model.forces[i].where, error));
        if (!error.empty()) return failed(r, "force #" + std::to_string(i + 1) + ": " + error);
        r.loadedNodes += forceNodes.back().size();
    }
    if (quadratic && !model.forces.empty())
        note(r, ImportDiagnosticLevel::Warning, "ANALYSIS_FORCE_QUADRATIC",
             "A force shared equally by all nodes of a quadratic mesh is not the consistent load (corner nodes should carry less than mid-side nodes); use 'pressure' on a surface group for accurate results");

    // Pressure: faces of volume elements that coincide with the surface elements of the group.
    struct FaceLoad { ElementId element; int face; std::size_t pressure; };
    std::vector<FaceLoad> faceLoads;
    std::vector<std::vector<FaceLoad>> faceLoadsOf(model.pressures.size());   // by pressure index
    if (!model.pressures.empty()) {
        std::map<std::array<NodeIndex, 4>, std::vector<std::pair<ElementId, int>>> faces;
        for (const auto& block : solid.getElementBlocks()) {
            const auto table = faceTable(block.type);
            if (table.empty()) continue;
            for (std::size_t e = 0; e < block.size(); ++e)
                for (std::size_t f = 0; f < table.size(); ++f) {
                    std::array<NodeIndex, 4> key{{~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0)}};
                    for (std::size_t k = 0; k < table[f].size(); ++k) key[k] = block.connectivity[e * block.nodesPerElement + static_cast<std::size_t>(table[f][k])];
                    std::sort(key.begin(), key.begin() + static_cast<std::ptrdiff_t>(table[f].size()));
                    faces[key].push_back({block.ids[e], static_cast<int>(f) + 1});
                }
        }
        std::unordered_map<ElementId, std::pair<const ElementBlock*, std::size_t>> location;
        for (const auto& block : mesh.getElementBlocks())
            for (std::size_t e = 0; e < block.size(); ++e) location.emplace(block.ids[e], std::make_pair(&block, e));
        for (std::size_t pi = 0; pi < model.pressures.size(); ++pi) {
            const auto& p = model.pressures[pi];
            const std::vector<ElementId>* groupElements = nullptr;
            int groupDimension = -1;
            for (const auto& g : mesh.getPhysicalGroups())
                if (g.name == p.group) { groupElements = &g.elementIds; groupDimension = g.dimension; break; }
            if (!groupElements)
                for (const auto& g : mesh.getPhysicalGroups())
                    if (lowered(g.name) == lowered(p.group)) { groupElements = &g.elementIds; groupDimension = g.dimension; break; }
            if (!groupElements) {
                const MeshGroup* generic = mesh.getMeshGroupRegistry().find(p.group);
                if (!generic)
                    for (const auto& g : mesh.getMeshGroupRegistry().groups())
                        if (lowered(g.name) == lowered(p.group)) { generic = &g; break; }
                if (generic) { groupElements = &generic->elementIds; groupDimension = generic->dimension; }
            }
            if (!groupElements) return failed(r, "pressure: unknown surface group '" + p.group + "'");
            if (groupDimension != 2) return failed(r, "pressure: group '" + p.group + "' is not a surface group (dimension " + std::to_string(groupDimension) + ")");
            if (groupElements->empty()) return failed(r, "pressure: surface group '" + p.group + "' has no surface elements");
            std::size_t unmatched = 0, shared = 0;
            for (const ElementId id : *groupElements) {
                const auto at = location.find(id);
                const int corners = at == location.end() ? 0 : surfaceCorners(at->second.first->type);
                if (corners == 0) { ++unmatched; continue; }
                const auto& block = *at->second.first;
                std::array<NodeIndex, 4> key{{~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0), ~NodeIndex(0)}};
                for (int k = 0; k < corners; ++k) key[static_cast<std::size_t>(k)] = block.connectivity[at->second.second * block.nodesPerElement + static_cast<std::size_t>(k)];
                std::sort(key.begin(), key.begin() + corners);
                const auto hit = faces.find(key);
                if (hit == faces.end()) { ++unmatched; continue; }
                if (hit->second.size() != 1) { ++shared; continue; }
                const FaceLoad load{hit->second[0].first, hit->second[0].second, pi};
                faceLoads.push_back(load);
                faceLoadsOf[pi].push_back(load);
            }
            if (unmatched) return failed(r, "pressure: " + std::to_string(unmatched) + " surface element(s) of '" + p.group +
                                              "' are not faces of a tetrahedron/hexahedron (other element types are not supported for pressure)");
            if (shared) return failed(r, "pressure: " + std::to_string(shared) + " surface element(s) of '" + p.group +
                                           "' lie between two volume elements; a pressure needs a boundary surface");
        }
        r.pressureFaces = faceLoads.size();
    }

    // Write: the mesh through the existing writer, then the model.
    {
        AbaqusWriteOptions options;
        options.writeHeading = false;
        const auto w = AbaqusInpWriter::writeFile(path.string(), solid, options);
        if (!w.success) return failed(r, w.errors.empty() ? "Cannot write the mesh" : w.errors.front());
    }
    std::ostringstream out;
    out << std::setprecision(10);
    out << "** OpenModel job " << job->id << "\n";
    out << "*NSET, NSET=MG_NALL\n";
    {
        std::vector<NodeIndex> all(mesh.getNumNodes());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<NodeIndex>(i);
        out << idList(mesh, all);
    }
    out << "*ELSET, ELSET=MG_EALL\n";
    for (std::size_t i = 0; i < volumeSets.size(); ++i) out << volumeSets[i] << (i + 1 < volumeSets.size() ? ",\n" : "\n");
    out << "*MATERIAL, NAME=" << AbaqusInpWriter::sanitizeName(m.name.empty() ? "MATERIAL" : m.name) << "\n*ELASTIC\n"
        << m.youngModulus << ", " << m.poisson << "\n";
    if (m.density > 0.0) out << "*DENSITY\n" << m.density << "\n";
    const std::string materialName = AbaqusInpWriter::sanitizeName(m.name.empty() ? "MATERIAL" : m.name);
    for (const auto& setName : volumeSets) out << "*SOLID SECTION, ELSET=" << setName << ", MATERIAL=" << materialName << "\n";
    for (std::size_t i = 0; i < model.supports.size(); ++i) {
        out << "*NSET, NSET=MG_FIX" << i + 1 << "\n" << idList(mesh, supportNodes[i]);
    }
    for (std::size_t i = 0; i < model.forces.size(); ++i) out << "*NSET, NSET=MG_LOAD" << i + 1 << "\n" << idList(mesh, forceNodes[i]);
    // Supports hold in every step: CalculiX keeps *BOUNDARY from the first step on, so they are written once here.
    if (!model.supports.empty()) {
        out << "*BOUNDARY\n";
        for (std::size_t i = 0; i < model.supports.size(); ++i)
            for (int d = 0; d < 3; ++d)
                if (model.supports[i].dof[static_cast<std::size_t>(d)])
                    out << "MG_FIX" << i + 1 << ", " << d + 1 << ", " << d + 1 << ", "
                        << model.supports[i].displacement[static_cast<std::size_t>(d)] << "\n";
    }
    // Rigid links are model data too, written with the supports and before the first step.
    for (std::size_t i = 0; i < model.rigidLinks.size(); ++i)
        out << "*NSET, NSET=MG_RBS" << i + 1 << "\n" << idList(mesh, model.rigidLinks[i].slaves);
    for (std::size_t i = 0; i < model.rigidLinks.size(); ++i)
        out << "** " << model.rigidLinks[i].id << "\n*RIGID BODY, NSET=MG_RBS" << i + 1
            << ", REF NODE=" << rigidMasterIds[i] << "\n";
    // Amplitudes are model data: they must precede the steps.
    for (const auto& a : model.amplitudes) {
        out << "*AMPLITUDE, NAME=" << a.name << "\n";
        for (std::size_t i = 0; i < a.points.size(); ++i)
            out << a.points[i][0] << ", " << a.points[i][1] << (i % 4 == 3 || i + 1 == a.points.size() ? "\n" : ", ");
    }

    // Loads that acted in the earlier steps of this job (their last written value is non-zero).
    std::set<std::string> applied;
    bool printedNote = false;
    for (std::size_t s = 0; s < job->steps.size(); ++s) {
        const auto& step = job->steps[s];
        const auto active = activeLoadIds(model, step);
        const std::set<std::string> activeSet(active.begin(), active.end());
        const bool reset = step.resetLoads;
        // A load is written when it is active here, or zeroed when it acted before and the step resets the loads.
        auto isWritten = [&](const std::string& id) { return activeSet.count(id) != 0; };
        auto isZeroed = [&](const std::string& id) { return reset && !activeSet.count(id) && applied.count(id) != 0; };

        out << "*STEP\n";
        if (step.type == StepType::Frequency) {
            out << "*FREQUENCY\n" << step.modes << "\n";
        } else {
            if (step.type == StepType::Static) {
                out << "*STATIC\n";
            } else {
                // CalculiX: initial increment, step period, minimum and maximum increment (defaults as in its manual).
                const double minimum = step.dynamicMinIncrement > 0.0 ? step.dynamicMinIncrement
                                                                      : std::min(step.dynamicIncrement, 1.0e-5 * step.dynamicPeriod);
                const double maximum = step.dynamicMaxIncrement > 0.0 ? step.dynamicMaxIncrement : 1.0e30;
                out << "*DYNAMIC\n" << step.dynamicIncrement << ", " << step.dynamicPeriod << ", " << minimum << ", " << maximum << "\n";
            }
            // *CLOAD: one card per amplitude (empty name = constant load), in order of first appearance in the model.
            std::vector<std::string> amplitudeNames{""};
            for (const auto& f : model.forces)
                if (std::find(amplitudeNames.begin(), amplitudeNames.end(), f.amplitude) == amplitudeNames.end()) amplitudeNames.push_back(f.amplitude);
            for (const auto& name : amplitudeNames) {
                std::ostringstream lines;
                for (std::size_t i = 0; i < model.forces.size(); ++i) {
                    const auto& f = model.forces[i];
                    if (f.amplitude != name) continue;
                    if (isWritten(f.id)) {
                        for (int d = 0; d < 3; ++d) {
                            const double share = f.total[static_cast<std::size_t>(d)] / static_cast<double>(forceNodes[i].size());
                            if (share != 0.0) lines << "MG_LOAD" << i + 1 << ", " << d + 1 << ", " << share << "\n";
                        }
                    } else if (isZeroed(f.id)) {
                        for (int d = 0; d < 3; ++d) lines << "MG_LOAD" << i + 1 << ", " << d + 1 << ", 0\n";
                    }
                }
                if (lines.str().empty()) continue;
                out << "*CLOAD" << (name.empty() ? "" : ", AMPLITUDE=" + name) << "\n" << lines.str();
            }
            // *DLOAD: one card per amplitude: the pressures, then the gravity (constant, no amplitude).
            std::vector<std::string> pressureNames{""};
            for (const auto& p : model.pressures)
                if (std::find(pressureNames.begin(), pressureNames.end(), p.amplitude) == pressureNames.end()) pressureNames.push_back(p.amplitude);
            for (const auto& name : pressureNames) {
                std::ostringstream lines;
                for (std::size_t pi = 0; pi < model.pressures.size(); ++pi) {
                    const auto& p = model.pressures[pi];
                    if (p.amplitude != name) continue;
                    for (const auto& f : faceLoadsOf[pi]) {
                        if (isWritten(p.id)) lines << f.element << ", P" << f.face << ", " << p.pressure << "\n";
                        else if (isZeroed(p.id)) lines << f.element << ", P" << f.face << ", 0\n";
                    }
                }
                if (name.empty() && model.hasGravity) {
                    const auto& g = model.gravity;
                    const double magnitude = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
                    if (isWritten(kGravityLoadId) && magnitude > 0.0)
                        lines << "MG_EALL, GRAV, " << magnitude << ", " << g[0] / magnitude << ", " << g[1] / magnitude << ", " << g[2] / magnitude << "\n";
                    else if (isZeroed(kGravityLoadId))
                        lines << "MG_EALL, GRAV, 0, 0, 0, 1\n";
                }
                if (lines.str().empty()) continue;
                out << "*DLOAD" << (name.empty() ? "" : ", AMPLITUDE=" + name) << "\n" << lines.str();
            }
        }
        out << "*NODE FILE\nU" << (step.type != StepType::Frequency ? ", S, E" : "") << "\n";
        const bool print = step.type == StepType::Static && (model.elementPrint == 1 || (model.elementPrint < 0 && volumeIds.size() <= 200000));
        if (step.type == StepType::Static && !print && !printedNote) {
            printedNote = true;
            note(r, ImportDiagnosticLevel::Info, "ANALYSIS_NO_ELEMENT_PRINT",
                 "Element stresses are not written to the .dat (" + std::to_string(volumeIds.size()) + " elements; use 'output element=on' to force); the nodal ones are in the .frd");
        }
        if (print) out << "*EL PRINT, ELSET=MG_EALL\nS, E\n";
        out << "*END STEP\n";

        // The loads written in this step are the ones that act from now on (a reset drops the rest).
        // A frequency step ignores the loads, so it neither applies nor resets them.
        if (step.type == StepType::Frequency) continue;
        if (reset) applied.clear();
        for (const auto& id : active) applied.insert(id);
    }

    {
        std::ofstream file(path, std::ios::binary | std::ios::app);
        if (!file) return failed(r, "Cannot append to " + path.string());
        const std::string text = out.str();
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) return failed(r, "Failed while writing " + path.string());
    }
    std::ostringstream summary;
    summary << "Job " << job->id << ": " << job->steps.size() << " step(s) of " << r.elements << " volume elements, " << r.nodes
            << " nodes; E = " << m.youngModulus << ", nu = " << m.poisson;
    if (r.fixedNodes) summary << "; " << r.fixedNodes << " constrained nodes";
    if (r.loadedNodes) summary << "; force on " << r.loadedNodes << " nodes";
    if (r.pressureFaces) summary << "; pressure on " << r.pressureFaces << " faces";
    if (model.hasGravity) summary << "; gravity";
    if (!model.amplitudes.empty()) summary << "; " << model.amplitudes.size() << " amplitude(s)";
    r.summary = summary.str();
    r.success = true;
    return r;
}

} // namespace OpenModel
