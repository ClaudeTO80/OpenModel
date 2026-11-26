#pragma once

#include <OpenModel/Heat1D.hpp>

#include <string>
#include <vector>

/**
 * @file Heat1DScript.hpp
 * @brief Text script of a one-dimensional heat analysis (internal solver only) and its run.
 *
 * The segment is the axis of the body: the script gives its length and the number of elements, not a mesh. The
 * section of the body along the axis gives A(x) and the perimeter P(x) used for the lateral exchange. One command per
 * line, `#` starts a comment, values are numbers in the caller's units (the same set for all of them).
 *
 * @code
 * analysis heat 1d length=1.0 elements=50
 * section rect a=0.1 b=0.1                       # A = a b, P = 2 (a + b)
 * section circle r=0.01                          # A = pi r^2, P = 2 pi r
 * section tube ro=0.02 ri=0.01                   # A = pi (ro^2 - ri^2), P = 2 pi (ro + ri): both surfaces exposed
 * section polygon sides=6 r=0.05                 # regular polygon, r = circumradius: P = n side, A = n/2 r^2 sin(2 pi/n)
 * section table x=0,0.5,1 A=0.01,0.02,0.01 P=0.4,0.5,0.4   # A(x), P(x) piecewise linear; x from 0 to length
 * thermal k=50 rhocp=3.9e6                       # conductivity and volumetric heat capacity (constant)
 * initial T=20                                   # initial temperature (default 0)
 * temperature end=left T=100                     # Dirichlet end; end=left or end=right
 * flux end=right q=500                           # heat per unit area entering the domain; no line = insulated
 * convection end=right h=25 Tinf=20 [emissivity=0.8]
 * lateral h=10 Tinf=20 [emissivity=0]            # exchange along the perimeter P(x)
 * heat q=5000                                    # volumetric heat source
 * job MAIN [solver=internal]                     # optional; only the internal solver runs 1D models
 * step steady                                    # or: step transient dt=0.01 end=10 [theta=0.5] [capacity=consistent|lumped]
 * @endcode
 *
 * Each command appears once, except that each end takes one temperature, flux or convection line. Time dependence and
 * amplitudes are not available yet: every value is constant.
 */

namespace OpenModel::Heat1D {

/** @brief A parsed heat script, ready to run. */
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
 * @brief Results as CSV in long format: a header `time,x,temperature`, then one row per recorded time and node.
 *        Numbers use 10 significant digits.
 */
std::string resultToCsv(const Result& result);

} // namespace OpenModel::Heat1D
