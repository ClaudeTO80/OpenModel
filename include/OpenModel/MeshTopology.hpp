#pragma once

#include <OpenModel/MeshData.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

// Derived topology for supported 3D volume elements. Face node ordering is
// corners first, then edge nodes in perimeter order, then an optional face node.
enum class MeshFaceType { Triangle3, Quadrilateral4, Triangle6, Quadrilateral8, Quadrilateral9 };
enum class MeshFaceClass { Boundary, Internal, NonManifold };

struct MeshFace {
    MeshFaceType type = MeshFaceType::Triangle3;
    std::vector<NodeIndex> nodes;
    std::size_t cornerCount = 3;
    std::vector<ElementId> incidentElements;
    std::vector<std::string> physicalGroups; // Dimension-2 groups matched to explicit surface elements.
    MeshFaceClass classification = MeshFaceClass::Boundary;
};

struct MeshEdge {
    NodeIndex first = 0;
    NodeIndex second = 0; // first <= second
    std::vector<NodeIndex> intermediateNodes; // ordered from first to second
    std::vector<ElementId> incidentElements;
    std::size_t incidentFaces = 0;
    std::size_t incidentBoundaryFaces = 0;
    bool boundary = false;
    bool nonManifold = false;
};

struct MeshTopologyReport {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::size_t boundaryFaces = 0;
    std::size_t internalFaces = 0;
    std::size_t nonManifoldFaces = 0;
    std::size_t boundaryEdges = 0; // Unique edges belonging to at least one boundary face.
    std::size_t openBoundaryEdges = 0; // Boundary-surface edges incident to exactly one boundary face.
    std::size_t nonManifoldEdges = 0; // Boundary-surface edges incident to more than two boundary faces.
    std::size_t orientationConflicts = 0;
    bool success() const noexcept;
};

class MeshTopology {
public:
    MeshTopologyReport build(const MeshData& mesh);
    const std::vector<MeshFace>& faces() const noexcept;
    const std::vector<MeshEdge>& edges() const noexcept;
    static bool isMaterialInterface(const MeshFace& face, const MeshData& mesh);

private:
    struct FaceTemplate {
        std::vector<std::size_t> nodes;
        std::size_t cornerCount = 3;
    };
    static const std::vector<FaceTemplate>& facesFor(ElementType type);
    std::vector<MeshFace> m_faces;
    std::vector<MeshEdge> m_edges;
};
