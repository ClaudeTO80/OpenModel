#pragma once

#include <OpenModel/Heat2DMesh.hpp>

#include <string>
#include <vector>

/**
 * @file Heat2DMeshScript.hpp
 * @brief Text script of a two-dimensional heat analysis on a CAD mesh (`analysis heat 2d mesh`). The parser is part of
 *        the core; running it needs the Gmsh backend (see Heat2DGmsh.hpp).
 *
 * One command per line, `#` starts a comment. Boundary conditions refer to the tags of the mesh: 1 is the outer contour,
 * 2 is the holes (see Heat2DGmsh.hpp).
 *
 * @code
 * analysis heat 2d mesh
 * geometry step file=plate.step size=0.05      # planar face of the STEP file, linear triangles of size 0.05
 * material k=1 rhocp=1 thickness=1             # constant properties; thickness optional (default 1)
 * boundary tag=1 temperature T=0               # outer contour
 * boundary tag=2 temperature T=50              # holes
 * heat q=1                                     # volumetric source
 * initial T=20                                 # initial temperature for a transient run (default 0)
 * job MAIN [solver=internal]
 * step steady                                  # or: step transient dt=0.01 end=10 [theta=0.5] [capacity=consistent|lumped]
 * @endcode
 *
 * Each tag takes one condition, given once. A tag without a line is insulated. `geometry`, `material`, `heat`, `initial`,
 * `job` and `step` appear once. Properties are constant. Errors are collected with their line numbers.
 */

namespace OpenModel::Heat2D {

/** @brief A parsed mesh script, ready to run. */
struct MeshScriptProblem {
    std::string stepFile;          ///< Path of the STEP file.
    double size = 0.0;             ///< Element size.
    MeshModel model;               ///< Properties and boundary conditions by tag.
    Options options;               ///< Steady or transient run.
    double initialT = 0.0;         ///< Constant initial temperature for a transient run.
    bool hasInitial = false;       ///< True when the script gave an initial temperature.
};

/**
 * @brief Parses a script. Errors are appended to `errors` with their line number; the vector is left unchanged on success.
 * @return true when the script is valid
 */
bool parseMeshScript(const std::string& text, MeshScriptProblem& problem, std::vector<std::string>& errors);

/**
 * @brief Results as CSV in long format: header `time,x,y,temperature`, then one row per recorded time and node.
 *        Numbers use 10 significant digits.
 */
std::string meshResultToCsv(const TriangleMesh& mesh, const MeshResult& result);

} // namespace OpenModel::Heat2D
