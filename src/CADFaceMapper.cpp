#include <OpenModel/CADFaceMapper.hpp>

#include <OpenModel/MeshTopology.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <unordered_map>

CADFaceMapping mapCADFacesToMesh(const CADModel& cad, const MeshData& mesh, const std::vector<int>& faceIds,
                                 double tolerance) {
    if (!std::isfinite(tolerance) || tolerance <= 0.0)
        throw std::invalid_argument("mapCADFacesToMesh: the tolerance must be finite and positive");
    if (faceIds.empty() || mesh.isEmpty()) return mapCADFacesToMesh(cad, mesh, MeshTopology{}, faceIds, tolerance);
    MeshTopology topology;
    topology.build(mesh);
    return mapCADFacesToMesh(cad, mesh, topology, faceIds, tolerance);
}

CADFaceMapping mapCADFacesToMesh(const CADModel& cad, const MeshData& mesh, const MeshTopology& topology,
                                 const std::vector<int>& faceIds, double tolerance) {
    if (!std::isfinite(tolerance) || tolerance <= 0.0)
        throw std::invalid_argument("mapCADFacesToMesh: the tolerance must be finite and positive");

    CADFaceMapping result;
    const std::vector<int> faces = [&] {
        std::set<int> unique(faceIds.begin(), faceIds.end());
        return std::vector<int>(unique.begin(), unique.end());
    }();
    if (faces.empty() || mesh.isEmpty()) {
        result.faceIdsWithoutMatch = faces;
        return result;
    }

    std::set<NodeIndex> nodes;
    // Faces are handled in chunks of 64: bit k of a node's mask says it lies on chunk[k].
    for (std::size_t first = 0; first < faces.size(); first += 64) {
        const std::size_t count = std::min<std::size_t>(64, faces.size() - first);
        std::unordered_map<NodeIndex, std::uint64_t> maskOfNode;
        const auto maskOf = [&](NodeIndex node) {
            const auto found = maskOfNode.find(node);
            if (found != maskOfNode.end()) return found->second;
            const auto p = mesh.nodeCoordinates(node);
            std::uint64_t mask = 0;
            for (std::size_t k = 0; k < count; ++k)
                if (cad.faceContainsPoint(faces[first + k], p, tolerance)) mask |= std::uint64_t{1} << k;
            maskOfNode.emplace(node, mask);
            return mask;
        };
        std::vector<bool> matched(count, false);
        for (const MeshFace& face : topology.faces()) {
            if (face.classification != MeshFaceClass::Boundary) continue;
            std::uint64_t common = ~std::uint64_t{0};
            bool any = false;
            for (std::size_t i = 0; i < face.cornerCount && i < face.nodes.size(); ++i) {
                const std::uint64_t mask = maskOf(face.nodes[i]);
                any = any || mask != 0;
                common &= mask;
            }
            if (any) ++result.boundaryFacesExamined;
            if (common == 0) continue;
            ++result.boundaryFaceCount;
            for (const NodeIndex n : face.nodes) nodes.insert(n);
            for (std::size_t k = 0; k < count; ++k)
                if (common & (std::uint64_t{1} << k)) matched[k] = true;
        }
        for (std::size_t k = 0; k < count; ++k)
            if (!matched[k]) result.faceIdsWithoutMatch.push_back(faces[first + k]);
    }
    result.nodes.assign(nodes.begin(), nodes.end());
    return result;
}
