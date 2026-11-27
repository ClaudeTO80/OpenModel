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
 * analysis heat 1d                              # length and elements follow from the segments
 * material id=ALU k=200 rhocp=2.4e6             # one or more materials, each with an id; k= constant, or
 *                                                # kT=300:200,400:190 (conductivity tabulated in T, linear, held at the ends)
 * material id=STEEL k=50 rhocp=3.9e6
 * segment material=ALU length=0.02 elements=20   # segments follow the axis from x = 0 (interfaces are nodes)
 * segment material=STEEL length=0.05 elements=40 shape=circle r=0.01   # optional own section of the segment
 * section rect a=0.1 b=0.1                       # A = a b, P = 2 (a + b)
 * section circle r=0.01                          # A = pi r^2, P = 2 pi r
 * section tube ro=0.02 ri=0.01                   # A = pi (ro^2 - ri^2), P = 2 pi (ro + ri): both surfaces exposed
 * section polygon sides=6 r=0.05                 # regular polygon, r = circumradius: P = n side, A = n/2 r^2 sin(2 pi/n)
 * section table x=0,0.5,1 A=0.01,0.02,0.01 P=0.4,0.5,0.4   # A(x), P(x) piecewise linear; x from 0 to length
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
 * `material` and `segment` may repeat; every other command appears once, and each end takes one temperature, flux or
 * convection line. A segment may carry its own simple section (`shape=rect|circle|tube|polygon` with that shape's keys,
 * the same values as the section line): a chain of primitives is then a list of segments, and the section line may be
 * left out when every segment has a shape. Segments without a shape take the section line, which may be a table. The shorthand `thermal k=... rhocp=...` with `analysis heat 1d length=L elements=N` gives one
 * material over the whole length and cannot be mixed with material lines. Time dependence and amplitudes are not
 * available yet: every value is constant.
 */

namespace OpenModel::Heat1D {

/** @brief A simple section as written in the script: its kind and values. Unused values are zero. */
struct SectionSpec {
    std::string kind; ///< rect, circle, tube, polygon, table; empty when there is none
    double a = 0.0;   ///< rect: side along y
    double b = 0.0;   ///< rect: side along z
    double r = 0.0;   ///< circle radius, polygon circumradius
    double ro = 0.0;  ///< tube outer radius
    double ri = 0.0;  ///< tube inner radius
    int sides = 0;    ///< polygon sides
};

/** @brief One segment of the axis as written in the script. */
struct SegmentSpec {
    std::string material;  ///< material id
    double length = 0.0;   ///< length along the axis
    int elements = 0;      ///< number of elements
    SectionSpec section;   ///< the segment's own shape (shape=); empty kind when it takes the global section
};

/** @brief A parsed heat script, ready to run. */
struct ScriptProblem {
    Model model;     ///< Geometry, properties and conditions.
    Options options; ///< Steady or transient run.
    std::vector<SegmentSpec> segments; ///< The axis segments in order (the thermal shorthand gives one segment).
    SectionSpec globalSection;         ///< The section line; empty kind when the script has none.
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
