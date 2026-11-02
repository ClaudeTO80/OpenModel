#pragma once

#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>

#include <array>
#include <string>
#include <vector>

/**
 * @file MeshSelection.hpp
 * @brief Turn a selection made in the viewer (nodes, elements or faces, by external id) into named groups of a MeshData.
 *
 * The groups are the handles used by the analysis script (`fix group:NAME`, `force group:NAME`, `pressure group:NAME`):
 *  - node set: the picked nodes, or all nodes of the picked elements / faces;
 *  - element group: a physical group of dimension 3 with the picked elements (or the owners of picked faces);
 *  - face group: a physical group of dimension 2 whose members are new surface elements (Triangle3 / Quadrilateral4 on
 *    the corner nodes, outward oriented) coincident with the picked faces. Pressure loads need this kind.
 */

namespace OpenModel {

/** @brief A picked face: the volume element it belongs to and three of its corner nodes (external ids). */
struct PickedFace {
    ElementId element = 0;
    std::array<NodeId, 3> nodes{{0, 0, 0}};
};

/** @brief What the viewer reports: only the part named by `mode` is filled ("node", "element" or "face"). */
struct ViewerSelection {
    std::string mode;
    std::vector<NodeId> nodes;
    std::vector<ElementId> elements;
    std::vector<PickedFace> faces;
};

enum class GroupKind { NodeSet, ElementGroup, FaceGroup };

const char* groupKindName(GroupKind kind) noexcept;   ///< "node", "element" or "face".

struct GroupInfo {
    std::string name;
    GroupKind kind = GroupKind::NodeSet;
    std::size_t count = 0;   ///< Nodes, elements or faces.
};

struct GroupResult {
    bool success = false;
    std::string error;
    std::string name;
    GroupKind kind = GroupKind::NodeSet;
    std::size_t count = 0;   ///< Members of the new group.
    std::vector<ImportDiagnostic> diagnostics;
};

/** @brief Valid group names: 1..64 characters from A-Z a-z 0-9 `_` `-` `.`, so that script lines stay unambiguous. */
bool isValidGroupName(const std::string& name);

/** @brief True if a node set, a physical group or an element block already uses `name` (case-insensitive). */
bool groupNameInUse(const MeshData& mesh, const std::string& name);

/** @brief `prefix` followed by the smallest number that makes an unused name (FIX1, FIX2, ...). */
std::string uniqueGroupName(const MeshData& mesh, const std::string& prefix);

/**
 * @brief Create a group from a selection.
 *
 * Fails, leaving the mesh untouched, on an invalid or used name, an empty selection, ids that are not in the mesh, a
 * selection that cannot give that kind of group (a face group needs faces; an element group needs elements or faces)
 * or a face whose three nodes are not a face of the element.
 */
GroupResult createGroup(MeshData& mesh, const std::string& name, GroupKind kind, const ViewerSelection& selection);

/** @brief Node sets, volume groups and surface groups of the mesh (other physical groups are not listed). */
std::vector<GroupInfo> listGroups(const MeshData& mesh);

/**
 * @brief The members of a group as a viewer selection (to show it): node ids, element ids, or faces.
 *
 * Surface elements that are not a face of a volume element are skipped and counted in `skipped`.
 */
bool groupAsSelection(const MeshData& mesh, const std::string& name, ViewerSelection& out, std::size_t& skipped, std::string& error);

} // namespace OpenModel
