#pragma once

#include <OpenModel/Heat1D.hpp>

#include <functional>
#include <string>
#include <vector>

/**
 * @file Heat2D.hpp
 * @brief Two-dimensional heat conduction on a rectangular plate with circular or rectangular holes: bilinear
 *        quadrilateral elements on a structured grid, theta time integration and Picard iteration for the
 *        non-linear terms (temperature-dependent conductivity, radiation).
 *
 * Plane (thin-plate) model: the plate has a uniform thickness t along z and the temperature does not vary through it.
 * The complete model solved is
 * \f[
 *   t\,\rho c_p\,\frac{\partial T}{\partial t}
 *   = \nabla\cdot\big(t\,k(x,y,T)\,\nabla T\big) + t\,q(x,y,t)
 * \f]
 * on the plate minus the holes, with the outer sides and the hole boundaries each given by a temperature, a heat flux
 * or a convection-radiation exchange (the same kinds as Heat1D::Boundary). The boundary flux is per unit area of the
 * edge and is multiplied by the thickness and by the edge length.
 *
 * Geometry. The plate [0, Lx] x [0, Ly] is divided into nx x ny uniform elements. An element belongs to the solid when
 * its centroid lies outside every hole: the boundary of a hole is therefore a staircase whose accuracy is first order
 * in the element size. Curved boundaries with a conforming mesh are the job of the Gmsh path (not in this file).
 *
 * The derivation, the choice of the options and the list of the simplifications are in docs/HEAT_2D.md.
 */

namespace OpenModel::Heat2D {

using Heat1D::Boundary;
using Heat1D::BoundaryKind;
using Heat1D::Capacity;
using Heat1D::Options;

using Field2 = std::function<double(double x, double y)>;
using Conductivity2 = std::function<double(double x, double y, double T)>;
using Source2 = std::function<double(double x, double y, double t)>;

/** @brief Shape of a hole. */
enum class HoleShape {
    Circle,   ///< Disc of centre (cx, cy) and radius r.
    Rectangle ///< Axis-aligned rectangle [x0, x1] x [y0, y1].
};

/** @brief A hole fully inside the plate. */
struct Hole {
    HoleShape shape = HoleShape::Circle;
    double cx = 0.0, cy = 0.0, r = 0.0;   ///< Circle.
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0; ///< Rectangle.
};

/** @brief The model: geometry, properties, sources and boundary conditions. */
struct Model {
    double lx = 1.0;               ///< Plate width along x, > 0.
    double ly = 1.0;               ///< Plate height along y, > 0.
    int nx = 10;                   ///< Elements along x, >= 1.
    int ny = 10;                   ///< Elements along y, >= 1.
    double thickness = 1.0;        ///< Thickness along z, > 0. Multiplies every term.
    std::vector<Hole> holes;       ///< Holes. Empty = a plain plate.

    Conductivity2 conductivity;            ///< k(x, y, T) > 0. Required.
    Field2 volumetricHeatCapacity;         ///< rho * c_p(x, y) > 0. Required for a transient run.
    Source2 source;                        ///< Volumetric source q(x, y, t). Empty = none.

    Boundary left;                 ///< Side x = 0.
    Boundary right;                ///< Side x = Lx.
    Boundary bottom;               ///< Side y = 0.
    Boundary top;                  ///< Side y = Ly.
    Boundary hole;                 ///< Every boundary of every hole. Empty flux value = insulated hole.

    Field2 initialTemperature;     ///< T(x, y) at t = 0. Empty = 0 everywhere.

    double stefanBoltzmann = 5.670374419e-8; ///< sigma, in the unit system of the model.
};

/** @brief Result of a run. */
struct Result {
    std::string error;                       ///< Empty on success; otherwise the first problem found.
    bool converged = false;                  ///< True when every step reached the Picard tolerance.
    int maxPicardIterations = 0;             ///< Largest number of Picard iterations used by one step.
    int nodesX = 0;                          ///< Nodes along x = nx + 1.
    int nodesY = 0;                          ///< Nodes along y = ny + 1.
    std::vector<bool> active;                ///< active[id] is true for nodes that belong to the solid.
    std::vector<double> times;               ///< Recorded times (a single 0 for a steady run).
    /// temperatures[k][id] = T at node id at time times[k]; node id = j * nodesX + i. Inactive nodes are 0.
    std::vector<std::vector<double>> temperatures;
};

/**
 * @brief Checks a model and its options.
 * @return an empty string when the input is usable, otherwise the first problem found.
 */
std::string validate(const Model& model, const Options& options);

/** @brief Result of the check that a 3D prismatic body can be modelled as this thin plate. */
struct ThinPlateAssessment {
    std::string error;             ///< Non-empty when the input is invalid (the other fields are then not meaningful).
    bool applicable = false;       ///< True when the Biot number on the faces normal to z is below the limit.
    double biot = 0.0;             ///< Bi = h_z (t / 2) / k, with t / 2 the characteristic length of the two-sided exchange.
    double biotLimit = 0.0;        ///< The limit used.
    std::vector<std::string> notes; ///< What the user must still assert (the check cannot see them).
};

/**
 * @brief Checks whether a 3D prismatic body of thickness t along z can be solved as this thin plate.
 *
 * Two conditions are needed. (1) The boundary conditions and the sources do not depend on z: this is not checked, it is
 * listed in the notes. (2) The exchange on the faces normal to z is weak: with the effective coefficient h_z on those
 * faces (convection plus linearised radiation, computed by the caller), the Biot number Bi = h_z (t/2) / k must not exceed
 * biotLimit. A zero h_z (adiabatic faces) gives Bi = 0 and the reduction is exact under condition (1).
 *
 * @param thickness  t > 0, the extent along z
 * @param conductivity  k > 0 at the operating temperature
 * @param faceCoefficient  h_z >= 0, effective coefficient on the faces normal to z
 * @param biotLimit  limit on Bi, 0 < biotLimit (0.1 is the usual choice)
 */
ThinPlateAssessment assessThinPlate(double thickness, double conductivity, double faceCoefficient, double biotLimit = 0.1);

/**
 * @brief Solves the model.
 *
 * Invalid input is reported in Result::error (no exception). A failed Picard iteration or a failed linear solve is
 * reported the same way, with Result::converged false.
 */
Result solve(const Model& model, const Options& options);

} // namespace OpenModel::Heat2D
