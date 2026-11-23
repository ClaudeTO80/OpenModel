#include <OpenModel/Solvers.hpp>

#include <algorithm>
#include <cctype>

/**
 * @file Solvers.cpp
 * @brief Solver registry: built-in solvers, validation, per-job choice and the flat-key settings format.
 */
namespace OpenModel {
namespace {

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool validSolverId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

bool isAnalysisType(const std::string& type) { return type == "static" || type == "frequency" || type == "dynamic"; }

/** Which analysis types a kind can run. code_aster writes static and frequency; Elmer static only (for now). */
bool kindRuns(SolverKind kind, const std::string& type) {
    switch (kind) {
    case SolverKind::CalculiX: return true;
    case SolverKind::CodeAster: return type == "static" || type == "frequency";
    case SolverKind::Elmer: return type == "static";
    }
    return false;
}

std::string defaultIdOf(const SolverConfig& config, const std::string& type) {
    const auto it = config.defaults.find(type);
    return it == config.defaults.end() ? std::string("calculix") : it->second;
}

} // namespace

std::string solverKindName(SolverKind kind) {
    switch (kind) {
    case SolverKind::CalculiX: return "calculix";
    case SolverKind::CodeAster: return "code_aster";
    case SolverKind::Elmer: return "elmer";
    }
    return "calculix";
}

bool parseSolverKind(const std::string& text, SolverKind& kind) {
    const std::string name = lowered(text);
    if (name == "calculix" || name == "ccx") kind = SolverKind::CalculiX;
    else if (name == "code_aster" || name == "aster") kind = SolverKind::CodeAster;
    else if (name == "elmer") kind = SolverKind::Elmer;
    else return false;
    return true;
}

std::string analysisTypeName(StepType type) {
    switch (type) {
    case StepType::Static: return "static";
    case StepType::Frequency: return "frequency";
    case StepType::Dynamic: return "dynamic";
    }
    return "static";
}

SolverConfig defaultSolverConfig() {
    SolverConfig config;
    config.solvers = {{"calculix", SolverKind::CalculiX, {}}, {"code_aster", SolverKind::CodeAster, {}}, {"elmer", SolverKind::Elmer, {}}};
    config.defaults = {{"static", "calculix"}, {"frequency", "calculix"}, {"dynamic", "calculix"}};
    return config;
}

const SolverEntry* findSolver(const SolverConfig& config, const std::string& id) {
    for (const auto& s : config.solvers)
        if (s.id == id) return &s;
    return nullptr;
}

std::string validateSolverConfig(const SolverConfig& config) {
    for (std::size_t i = 0; i < config.solvers.size(); ++i) {
        const auto& s = config.solvers[i];
        if (!validSolverId(s.id)) return "solver id '" + s.id + "' must be 1-32 characters from letters, digits, '_' and '-'";
        for (std::size_t j = 0; j < i; ++j)
            if (config.solvers[j].id == s.id) return "solver id '" + s.id + "' is defined twice";
        if (s.path.find_first_of("\r\n") != std::string::npos) return "the path of solver '" + s.id + "' holds a line break";
    }
    for (const auto& [type, id] : config.defaults) {
        if (!isAnalysisType(type)) return "unknown analysis type '" + type + "' in the defaults (static, frequency, dynamic)";
        const SolverEntry* solver = findSolver(config, id);
        if (!solver) return "the default solver of " + type + " analysis, '" + id + "', is not configured";
        if (!kindRuns(solver->kind, type))
            return "solver '" + id + "' (" + solverKindName(solver->kind) + ") cannot run " + type + " analysis";
    }
    return {};
}

const SolverEntry* solverForJob(const SolverConfig& config, const AnalysisJob& job, std::string& error) {
    error.clear();
    const std::string type = job.steps.empty() ? std::string("static") : analysisTypeName(job.steps.front().type);
    const std::string id = job.solver.empty() ? defaultIdOf(config, type) : job.solver;
    const SolverEntry* solver = findSolver(config, id);
    if (!solver) {
        error = job.solver.empty() ? "the default solver of " + type + " analysis, '" + id + "', is not configured"
                                   : "job '" + job.id + "' names solver '" + id + "', which is not configured";
        return nullptr;
    }
    if (!kindRuns(solver->kind, type)) {
        error = "job '" + job.id + "': solver '" + id + "' (" + solverKindName(solver->kind) + ") cannot run " + type + " analysis";
        return nullptr;
    }
    return solver;
}

std::map<std::string, std::string> configToFields(const SolverConfig& config) {
    std::map<std::string, std::string> fields;
    for (const auto& s : config.solvers) {
        fields["solver." + s.id + ".kind"] = solverKindName(s.kind);
        fields["solver." + s.id + ".path"] = s.path;
    }
    for (const auto& [type, id] : config.defaults) fields["default." + type] = id;
    return fields;
}

SolverConfig configFromFields(const std::map<std::string, std::string>& fields, const std::string& legacyCalculixPath,
                              std::string& error) {
    error.clear();
    SolverConfig config = defaultSolverConfig();
    const std::string prefix = "solver.";
    // Entries listed in the fields replace the built-in solver of the same id, or are added.
    struct Listed { SolverEntry entry; bool hasKind = false; bool valid = true; };
    std::map<std::string, Listed> listed;
    for (const auto& [key, value] : fields) {
        if (key.rfind(prefix, 0) != 0) continue;
        const auto dot = key.rfind('.');
        if (dot == std::string::npos || dot <= prefix.size()) continue;
        const std::string id = key.substr(prefix.size(), dot - prefix.size());
        const std::string field = key.substr(dot + 1);
        Listed& item = listed[id];
        item.entry.id = id;
        if (field == "kind") {
            item.hasKind = true;
            if (!parseSolverKind(value, item.entry.kind)) {
                item.valid = false;
                error = "solver '" + id + "' has an unknown kind '" + value + "' (calculix, code_aster, elmer): skipped";
            }
        } else if (field == "path") {
            item.entry.path = value;
        }
    }
    for (const auto& [id, item] : listed) {
        if (!item.valid) continue;
        if (!item.hasKind) {
            if (error.empty()) error = "solver '" + id + "' has no kind: skipped";
            continue;
        }
        auto existing = std::find_if(config.solvers.begin(), config.solvers.end(), [&](const SolverEntry& s) { return s.id == id; });
        if (existing != config.solvers.end()) *existing = item.entry;
        else config.solvers.push_back(item.entry);
    }
    // The older settings held one CalculiX path: it becomes the solver `calculix` unless the fields list one.
    if (!legacyCalculixPath.empty() && !listed.count("calculix"))
        for (auto& s : config.solvers)
            if (s.id == "calculix") s.path = legacyCalculixPath;
    for (const auto& [key, value] : fields) {
        if (key.rfind("default.", 0) != 0) continue;
        const std::string type = key.substr(8);
        if (!value.empty()) config.defaults[type] = value;
    }
    return config;
}

} // namespace OpenModel
