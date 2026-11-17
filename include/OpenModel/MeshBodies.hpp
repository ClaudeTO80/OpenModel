#pragma once

#include <OpenModel/MeshData.hpp>

#include <unordered_set>

/**
 * @file MeshBodies.hpp
 * @brief Export rule of the bodies of a mesh: a body meshed in volume is written with its volume elements only.
 *
 * A mesh may hold several bodies (MeshData::getBodies()), each meshed in volume (dimension 3) or in surface (dimension 2).
 * An element is written when at least one body containing it has the element's dimension; an element that belongs to no
 * body is always written. So the boundary triangles of a volume body stay in memory (groups, loads and the viewer use
 * them) but are left out of the exported files, and a surface body writes its surface elements.
 */

namespace OpenModel {

/** @brief Ids of the elements that exporters leave out (see the file comment). */
std::unordered_set<ElementId> nonExportedElements(const MeshData& mesh);

/**
 * @brief Copy of `mesh` without the non-exported elements. Nodes, node sets, constraints and the bodies are kept (the
 * bodies without their non-exported elements); physical and generic groups lose the elements that were removed.
 */
MeshData exportableMesh(const MeshData& mesh);

} // namespace OpenModel
