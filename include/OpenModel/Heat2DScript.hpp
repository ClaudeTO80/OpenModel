#pragma once

#include <OpenModel/Heat2D.hpp>

#include <string>
#include <vector>

/**
 * @file Heat2DScript.hpp
 * @brief Text script of a two-dimensional (thin-plate) heat analysis with the internal solver, and its run.
 *
 * One command per line, `#` starts a comment, values are numbers in the caller's units (the same set for all).
 *
 * @code
 * analysis heat 2d
 * plate lx=0.2 ly=0.1 nx=40 ny=20 thickness=0.005   # rectangle [0, lx] x [0, ly], uniform grid
 * material k=50 rhocp=3.9e6                         # constant conductivity and volumetric heat capacity
 * hole circle cx=0.1 cy=0.05 r=0.02                 # removed from the plate; may repeat
 * hole rect x0=0.02 y0=0.02 x1=0.04 y1=0.04         # removed from the plate; may repeat
 * initial T=20                                      # initial temperature (default 0)
 * temperature side=left T=100                       # side = left | right | bottom | top
 * flux side=right q=500                             # heat per unit area entering; no line = insulated
 * convection side=top h=25 Tinf=20 [emissivity=0.8]
 * hole temperature T=50                             # every hole boundary; hole flux / hole convection likewise
 * heat q=5000                                       # volumetric source
 * job MAIN [solver=internal]                        # optional; only the internal solver runs 2D models
 * step steady                                       # or: step transient dt=0.01 end=10 [theta=0.5] [capacity=consistent|lumped]
 * @endcode
 *
 * Each side and the hole boundary take one condition. `plate`, `material`, `step` and `job` appear once; `hole` may
 * repeat for the geometry, but the hole condition line appears once. Properties are constant: temperature-dependent
 * conductivity and time-dependent values are not available in this script yet.
 */

namespace OpenModel::Heat2D {

/** @brief A parsed 2D heat script, ready to run. */
struct ScriptProblem {
    Model model;     ///< Geometry, properties and conditions.
    Options options; ///< Steady or transient run.
};

/**
 * @brief Parses a script. Errors are appended to `errors`, one per problem, each prefixed with its line number.
 * @param text     the whole script
 * @param problem  receives the model and the options when the script is valid
 * @param errors   receives the messages; the vector is left unchanged when the script is valid
 * @return true when the script is valid
 */
bool parseScript(const std::string& text, ScriptProblem& problem, std::vector<std::string>& errors);

/** @brief Runs a parsed problem with the internal solver. */
Result runScript(const ScriptProblem& problem);

/**
 * @brief Results as CSV in long format: a header `time,x,y,temperature`, then one row per recorded time and active
 *        node. Inactive nodes (inside a hole) are omitted. Numbers use 10 significant digits.
 */
std::string resultToCsv(const Result& result, const ScriptProblem& problem);

} // namespace OpenModel::Heat2D
