#pragma once

#include <OpenModel/CADModel.hpp>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshTopology.hpp>

#include <cstddef>
#include <vector>

/** @brief Boundary of a volume mesh that lies on a set of CAD faces. */
struct CADFaceMapping {
    std::vector<NodeIndex> nodes;                 ///< Sorted, unique nodes of the matched boundary faces (all nodes, mid-edge ones included).
    std::size_t boundaryFaceCount = 0;            ///< Boundary faces of the mesh that lie entirely on the requested faces.
    std::size_t boundaryFacesExamined = 0;        ///< Boundary faces of the mesh with at least one node near a requested face.
    std::vector<int> faceIdsWithoutMatch;         ///< Requested faces that no boundary face lies on.
};

/**
 * @brief Find the nodes of a mesh that belong to given CAD faces, without any mapping done by the mesher.
 *
 * This is the on-demand counterpart of the face audit a mesher can run after meshing: instead of mapping every
 * mesh surface to a CAD face, only the faces asked for are searched. A boundary face of the volume mesh
 * (a face with a single incident volume element) belongs to a requested CAD face when all of its corner nodes
 * lie on that face within @p tolerance. Nodes are tested once, and only against the requested faces, so the cost
 * grows with the number of requested faces and nodes, not with the number of CAD faces of the model.
 *
 * It works on any MeshData, whatever backend produced it, and needs only the geometric queries of the CAD model.
 * Limits: a mesh made of several solids with coincident faces (independent interfaces) matches the boundary faces
 * of every solid touching the requested face, because ownership is not examined; and corner nodes must lie on
 * the exact CAD surface, which holds for nodes created by a mesher on the CAD but not for a mesh moved or scaled
 * afterwards.
 *
 * @param cad       CAD model whose face ids are @p faceIds (numbered as RenderSubMesh::sourceFaceId).
 * @param mesh      Volume mesh in the same coordinate system as @p cad.
 * @param faceIds   CAD faces to search.
 * @param tolerance Absolute distance tolerance; a value around 1e-6 times the model diagonal suits Gmsh meshes.
 * @return Matched nodes and counts. Empty `nodes` means nothing matched.
 * @throws std::invalid_argument When @p tolerance is not finite and positive.
 */
CADFaceMapping mapCADFacesToMesh(const CADModel& cad, const MeshData& mesh, const std::vector<int>& faceIds,
                                 double tolerance);

/**
 * @brief Same as above for callers that map several face sets on one mesh: @p topology (built from @p mesh with
 *        MeshTopology::build) is reused instead of being rebuilt for every call. On a 418k-element tetrahedral
 *        mesh the topology takes about 2.7 s, more than the face search itself.
 */
CADFaceMapping mapCADFacesToMesh(const CADModel& cad, const MeshData& mesh, const MeshTopology& topology,
                                 const std::vector<int>& faceIds, double tolerance);
