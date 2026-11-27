#pragma once

#include <OpenModel/Heat1DScript.hpp>
#include <OpenModel/MeshData.hpp>

#include <string>
#include <vector>

/**
 * @file Heat1DDisplay.hpp
 * @brief Display mesh of a one-dimensional heat problem: the real cross-sections of its segments and the axis.
 *
 * The mesh is for viewing only, not a solver mesh. Each segment is drawn as the surface of its cross-section extruded
 * along the axis (rectangle, circle, tube, regular polygon; a circle is a regular polygon with `ringPoints` sides), with
 * one physical group per material, named after the material id. The axis of the analysis is a tube along the nodes of
 * the solver, of radius a percentage of the largest extent of the model, in the group AXIS. The tube has a radius
 * because the viewer draws lines one pixel wide: a line would not be visible.
 *
 * With a temperature result, every element gets the temperature of the 1D solution at its position along the axis
 * (linear between the nodes). This is the one-dimensional assumption made visible: the section is at one temperature.
 *
 * The same chain is the starting point of the two- and three-dimensional meshes, where the groups will be regions of a
 * volume mesh rather than extruded sections.
 */

namespace OpenModel::Heat1D {

/** @brief Options of the display mesh. */
struct DisplayOptions {
    int ringPoints = 32;            ///< Points of a circle outline (at least 3).
    int axisSides = 8;              ///< Sides of the axis tube (at least 3).
    double axisRadiusPercent = 2.0; ///< Radius of the axis tube, in percent of the largest extent of the model.
};

/** @brief A display mesh, or the reason it cannot be drawn. */
struct DisplayResult {
    MeshData mesh;               ///< Surfaces of the segments (groups per material) and the axis (group AXIS).
    std::string error;           ///< Empty when the mesh is valid; a tabulated section cannot be drawn.
    /**
     * @brief One value per element of the mesh, in the order the viewer reads the blocks; empty when no temperature
     *        was given. The value is the temperature of the 1D solution at the element's position along the axis.
     */
    std::vector<double> values;
};

/**
 * @brief Builds the display mesh of a parsed heat problem.
 * @param problem       a problem from parseScript
 * @param options       display options
 * @param temperatures  the temperature of each node of the model (problem.model.nodes), or nullptr for none
 * @return the mesh, or an error when a section cannot be drawn or the temperatures do not match the nodes
 */
DisplayResult displayMesh(const ScriptProblem& problem, const DisplayOptions& options = {},
                          const std::vector<double>* temperatures = nullptr);

} // namespace OpenModel::Heat1D
