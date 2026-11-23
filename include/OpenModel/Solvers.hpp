#pragma once

#include <OpenModel/AnalysisSetup.hpp>

#include <map>
#include <string>
#include <vector>

/**
 * @file Solvers.hpp
 * @brief Solver registry: the solvers the user has configured (kind and executable path), the default solver of each
 *        analysis type, and the choice of the solver of a job.
 *
 * A **solver entry** has an id (letters, digits, `_`, `-`, unique), a kind (`calculix`, `code_aster`, `elmer`) and the
 * path of its executable (empty = the default name of the kind on PATH). The **defaults** give, for each analysis type
 * (`static`, `frequency`, `dynamic`), the id of the solver used when a job does not name one.
 *
 * A job chooses its solver with `job NAME solver=ID` in the analysis script. Without it the solver is the default of the
 * job's analysis type (the type of its first step). The built-in ids `calculix`, `code_aster` and `elmer` always exist, so a
 * script can name them even when no settings file is present; a settings entry with the same id replaces the built-in one.
 *
 * The settings are stored as flat string keys (see configFromFields()): `solver.<id>.kind`, `solver.<id>.path`,
 * `default.<type>`. A settings file of an older version with only a CalculiX path is read as the solver `calculix`.
 */

namespace OpenModel {

enum class SolverKind { CalculiX, CodeAster, Elmer };

/** @brief Text name of a kind: "calculix", "code_aster" or "elmer". */
std::string solverKindName(SolverKind kind);

/** @brief Kind from its text name (case-insensitive). Returns false when the name is unknown. */
bool parseSolverKind(const std::string& text, SolverKind& kind);

/** @brief Analysis type of a step as the settings name it: "static", "frequency" or "dynamic". */
std::string analysisTypeName(StepType type);

/** @brief One configured solver. */
struct SolverEntry {
    std::string id;                 ///< Name used in the settings and in `job NAME solver=ID`.
    SolverKind kind = SolverKind::CalculiX;
    std::string path;               ///< Executable (or launcher); empty = the default name of the kind on PATH.
};

/** @brief The configured solvers and the default solver of each analysis type. */
struct SolverConfig {
    std::vector<SolverEntry> solvers;
    /// Analysis type ("static", "frequency", "dynamic") -> solver id. A missing type falls back to "calculix".
    std::map<std::string, std::string> defaults;
};

/** @brief The configuration when nothing is set: the three built-in solvers and CalculiX as default for every type. */
SolverConfig defaultSolverConfig();

/** @brief The solver with id `id` (built-in ids included), or null. */
const SolverEntry* findSolver(const SolverConfig& config, const std::string& id);

/**
 * @brief Checks a configuration: ids are valid and unique, paths hold no line break, every default names a solver of the
 *        configuration and is a solver that can run that analysis type (code_aster and Elmer: static only for now).
 * @return an empty string when the configuration is valid, otherwise the first problem found.
 */
std::string validateSolverConfig(const SolverConfig& config);

/**
 * @brief The solver a job runs with: the `solver=` of the job when set, else the default of its analysis type.
 * @param config configuration in effect
 * @param job    the job (its first step gives the analysis type)
 * @param error  receives the reason when the job names an unknown solver or the type has no usable default
 * @return the solver, or null with `error` set
 */
const SolverEntry* solverForJob(const SolverConfig& config, const AnalysisJob& job, std::string& error);

/** @brief Flat string keys of a configuration, for the settings file (`solver.<id>.kind`, `solver.<id>.path`, `default.<type>`). */
std::map<std::string, std::string> configToFields(const SolverConfig& config);

/**
 * @brief Configuration read from flat string keys (the inverse of configToFields()).
 *
 * A key set that holds no `solver.` entry falls back to the defaults. An entry without kind, or with an unknown kind, is
 * skipped and named in `error`. `legacyCalculixPath` (the older `solverPath` key) becomes the solver `calculix` when no
 * solver is listed for it.
 */
SolverConfig configFromFields(const std::map<std::string, std::string>& fields, const std::string& legacyCalculixPath,
                              std::string& error);

} // namespace OpenModel
