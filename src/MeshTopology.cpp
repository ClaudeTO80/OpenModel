#include <OpenModel/MeshTopology.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <sstream>
#include <utility>

namespace {
// Faces in the supported element catalog have at most nine nodes. Keeping the
// canonical key inline avoids one heap allocation for every face lookup.
struct FaceKey {
    std::array<NodeIndex, 9> nodes{};
    std::uint8_t count = 0;
    bool operator==(const FaceKey& other) const noexcept {
        if (count != other.count) return false;
        return std::equal(nodes.begin(), nodes.begin() + count, other.nodes.begin());
    }
};
struct FaceKeyHash {
    std::size_t operator()(const FaceKey& key) const noexcept {
        std::size_t seed = key.count;
        for (std::size_t i = 0; i < key.count; ++i) {
            const auto value = std::hash<NodeIndex>{}(key.nodes[i]);
            seed ^= value + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        }
        return seed;
    }
};
struct EdgeKeyHash {
    std::size_t operator()(const std::pair<NodeIndex, NodeIndex>& key) const noexcept {
        std::size_t seed = std::hash<NodeIndex>{}(key.first);
        const auto value = std::hash<NodeIndex>{}(key.second);
        seed ^= value + static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        return seed;
    }
};
FaceKey sortedKey(const NodeIndex* nodes, std::size_t count) {
    if (count > FaceKey{}.nodes.size())
        throw std::length_error("OpenModel topology face exceeds the supported inline key size");
    FaceKey key;
    key.count = static_cast<std::uint8_t>(count);
    std::copy(nodes, nodes + count, key.nodes.begin());
    std::sort(key.nodes.begin(), key.nodes.begin() + key.count);
    return key;
}
FaceKey sortedKey(const std::vector<NodeIndex>& nodes) {
    return sortedKey(nodes.data(), nodes.size());
}

bool sameCyclicOrientation(const std::vector<NodeIndex>& a, const NodeIndex* b,
                           std::size_t bCount, std::size_t corners) {
    if (a.size() < corners || bCount < corners || corners == 0) return false;
    for (std::size_t start = 0; start < corners; ++start) {
        bool same = true;
        for (std::size_t i = 0; i < corners; ++i)
            if (a[i] != b[(start + i) % corners]) { same = false; break; }
        if (same) return true;
    }
    return false;
}


}

bool MeshTopologyReport::success() const noexcept { return errors.empty(); }

MeshTopologyReport MeshTopology::build(const MeshData& mesh) {
    m_faces.clear();
    m_edges.clear();
    MeshTopologyReport report;
    const auto validation = mesh.validate();
    if (!validation.empty()) { report.errors = validation; return report; }

    // Dimension-2 physical groups are linked to explicit surface elements by element ID.
    std::unordered_map<ElementId, std::vector<std::string>> surfaceNames;
    for (const auto& group : mesh.getPhysicalGroups()) {
        if (group.dimension != 2) continue;
        for (const auto id : group.elementIds) surfaceNames[id].push_back(group.name);
    }
    // Build the explicit surface-face lookup once. This avoids scanning every
    // surface element for every volume face on large meshes.
    std::unordered_map<FaceKey, std::vector<std::string>, FaceKeyHash> surfaceGroupLookup;
    std::size_t faceOccurrenceUpperBound = 0;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) == 3)
            faceOccurrenceUpperBound += block.size() * facesFor(block.type).size();
    }
    // Only surface elements referenced by dimension-2 physical groups can
    // contribute entries to this lookup. Reserving for every surface element
    // wastes buckets on meshes where physical groups cover only a small subset.
    std::size_t groupedSurfaceElementCount = 0;
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 2) continue;
        for (const auto id : block.ids)
            if (surfaceNames.find(id) != surfaceNames.end()) ++groupedSurfaceElementCount;
    }
    surfaceGroupLookup.max_load_factor(1.0f);
    surfaceGroupLookup.reserve(groupedSurfaceElementCount);
    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 2) continue;
        for (std::size_t e = 0; e < block.size(); ++e) {
            const auto names = surfaceNames.find(block.ids[e]);
            if (names == surfaceNames.end()) continue;
            const auto offset = e * block.nodesPerElement;
            const auto* nodes = block.connectivity.data() + offset;
            auto& target = surfaceGroupLookup[sortedKey(nodes, block.nodesPerElement)];
            for (const auto& name : names->second)
                if (std::find(target.begin(), target.end(), name) == target.end()) target.push_back(name);
        }
    }

    std::unordered_map<FaceKey, std::size_t, FaceKeyHash> faceLookup;
    std::unordered_map<std::pair<NodeIndex, NodeIndex>, std::size_t, EdgeKeyHash> edgeLookup;
    // On conforming volume meshes, unique faces are substantially fewer than
    // face occurrences. Reserving the full occurrence upper bound in the hash
    // table retains a large amount of unused bucket/node capacity. Use a
    // conservative linear estimate and let the table grow when needed.
    const std::size_t elementCount = mesh.getNumElements();
    const std::size_t faceReserve = elementCount > (std::numeric_limits<std::size_t>::max() / 3)
        ? faceOccurrenceUpperBound : std::min(faceOccurrenceUpperBound, elementCount * 3);
    faceLookup.reserve(faceReserve);
    m_faces.reserve(faceReserve);
    const std::size_t edgeReserve = elementCount > (std::numeric_limits<std::size_t>::max() / 2)
        ? faceOccurrenceUpperBound : std::min(faceOccurrenceUpperBound, elementCount * 2);
    edgeLookup.reserve(edgeReserve);
    m_edges.reserve(edgeReserve);

    for (const auto& block : mesh.getElementBlocks()) {
        if (elementDimension(block.type) != 3) continue;
        const auto templates = facesFor(block.type);
        if (templates.empty()) {
            report.errors.push_back("Unsupported volume topology for face extraction in block '" + block.name + "'");
            continue;
        }
        for (std::size_t e = 0; e < block.size(); ++e) {
            const auto offset = e * block.nodesPerElement;
            for (const auto& ft : templates) {
                // Keep each face occurrence on the stack. Only a genuinely new
                // unique face materializes its public std::vector connectivity.
                std::array<NodeIndex, 9> ordered{};
                const std::size_t orderedCount = ft.nodes.size();
                if (orderedCount > ordered.size()) {
                    report.errors.push_back("Unsupported face with more than nine nodes");
                    continue;
                }
                for (std::size_t i = 0; i < orderedCount; ++i)
                    ordered[i] = block.connectivity[offset + ft.nodes[i]];
                const auto key = sortedKey(ordered.data(), orderedCount);
                const auto found = faceLookup.find(key);
                if (found == faceLookup.end()) {
                    MeshFace face;
                    face.cornerCount = ft.cornerCount;
                    face.nodes.assign(ordered.begin(), ordered.begin() + orderedCount);
                    face.incidentElements.reserve(2);
                    face.incidentElements.push_back(block.ids[e]);
                    if (ft.cornerCount == 3) face.type = orderedCount == 3 ? MeshFaceType::Triangle3 : MeshFaceType::Triangle6;
                    else if (orderedCount == 4) face.type = MeshFaceType::Quadrilateral4;
                    else if (orderedCount == 8) face.type = MeshFaceType::Quadrilateral8;
                    else face.type = MeshFaceType::Quadrilateral9;
                    if (const auto groups = surfaceGroupLookup.find(key); groups != surfaceGroupLookup.end())
                        face.physicalGroups = groups->second;
                    faceLookup.emplace(key, m_faces.size());
                    m_faces.push_back(std::move(face));
                } else {
                    auto& face = m_faces[found->second];
                    if (face.incidentElements.size() < 2) {
                        if (sameCyclicOrientation(face.nodes, ordered.data(), orderedCount, ft.cornerCount)) {
                            ++report.orientationConflicts;
                            report.warnings.push_back("Adjacent elements use the same orientation on a shared face");
                        }
                    }
                    if (std::find(face.incidentElements.begin(), face.incidentElements.end(), block.ids[e]) == face.incidentElements.end())
                        face.incidentElements.push_back(block.ids[e]);
                }
            }
        }
    }
    if (!report.errors.empty()) { m_faces.clear(); return report; }

    // Match explicit surface elements to the derived volume faces. A surface group is
    // attached only when its element connectivity exactly matches the face node set.
    for (auto& face : m_faces) {
        if (face.incidentElements.size() == 1) { face.classification = MeshFaceClass::Boundary; ++report.boundaryFaces; }
        else if (face.incidentElements.size() == 2) { face.classification = MeshFaceClass::Internal; ++report.internalFaces; }
        else { face.classification = MeshFaceClass::NonManifold; ++report.nonManifoldFaces; }

        const std::size_t corners = face.cornerCount;
        for (std::size_t i = 0; i < corners; ++i) {
            const NodeIndex from = face.nodes[i];
            const NodeIndex to = face.nodes[(i + 1) % corners];
            const auto key = std::minmax(from, to);
            const std::pair<NodeIndex, NodeIndex> edgeKey{key.first, key.second};
            bool hasMid = false;
            NodeIndex mid = 0;
            const std::size_t edgeNodeStart = corners;
            if (face.nodes.size() >= corners * 2) {
                // Edge nodes are stored in perimeter order after all corner nodes.
                mid = face.nodes[edgeNodeStart + i];
                hasMid = true;
            }
            auto it = edgeLookup.find(edgeKey);
            if (it == edgeLookup.end()) {
                MeshEdge edge;
                edge.first = edgeKey.first; edge.second = edgeKey.second;
                if (hasMid) edge.intermediateNodes.push_back(mid);
                // A quadratic edge has one midpoint, so its order is invariant
                // when the endpoint order is canonicalized.
                // A typical edge in a conforming tetrahedral mesh is incident to
                // more than four volume elements. Reserving six avoids the common
                // 4 -> 8 growth step without over-reserving the full upper bound.
                edge.incidentElements.reserve(6);
                edge.incidentElements.insert(edge.incidentElements.end(),
                                             face.incidentElements.begin(), face.incidentElements.end());
                edge.incidentFaces = 1;
                edge.incidentBoundaryFaces = face.classification == MeshFaceClass::Boundary ? 1 : 0;
                edgeLookup.emplace(edgeKey, m_edges.size());
                m_edges.push_back(std::move(edge));
            } else {
                auto& edge = m_edges[it->second];
                ++edge.incidentFaces;
                if (face.classification == MeshFaceClass::Boundary) ++edge.incidentBoundaryFaces;
                for (const auto id : face.incidentElements)
                    if (std::find(edge.incidentElements.begin(), edge.incidentElements.end(), id) == edge.incidentElements.end()) edge.incidentElements.push_back(id);
                if (hasMid && !edge.intermediateNodes.empty() && edge.intermediateNodes.front() != mid)
                    report.warnings.push_back("Adjacent faces disagree on an edge's intermediate node");
                else if (hasMid && edge.intermediateNodes.empty())
                    edge.intermediateNodes.push_back(mid);
            }
        }
    }

    for (auto& edge : m_edges) {
        edge.boundary = edge.incidentBoundaryFaces > 0;
        edge.nonManifold = edge.incidentBoundaryFaces > 2;
        if (edge.boundary) ++report.boundaryEdges;
        if (edge.incidentBoundaryFaces == 1) ++report.openBoundaryEdges;
        if (edge.nonManifold) ++report.nonManifoldEdges;
    }
    if (report.nonManifoldFaces) report.warnings.push_back("One or more faces are incident to more than two volume elements");
    if (report.openBoundaryEdges) report.warnings.push_back("One or more boundary-surface edges are incident to only one boundary face");
    if (report.nonManifoldEdges) report.warnings.push_back("One or more boundary-surface edges are incident to more than two boundary faces");
    return report;
}

const std::vector<MeshFace>& MeshTopology::faces() const noexcept { return m_faces; }
const std::vector<MeshEdge>& MeshTopology::edges() const noexcept { return m_edges; }

bool MeshTopology::isMaterialInterface(const MeshFace& face, const MeshData& mesh) {
    if (face.incidentElements.size() != 2) return false;
    const auto materialFor = [&mesh](ElementId id) {
        std::set<std::string> names;
        for (const auto& group : mesh.getPhysicalGroups()) {
            if (group.dimension != 3 || group.materialName.empty()) continue;
            if (std::find(group.elementIds.begin(), group.elementIds.end(), id) != group.elementIds.end()) names.insert(group.materialName);
        }
        return names;
    };
    const auto first = materialFor(face.incidentElements[0]);
    const auto second = materialFor(face.incidentElements[1]);
    if (first.empty() || second.empty()) return false;
    for (const auto& name : first) if (second.count(name)) return false;
    return true;
}

const std::vector<MeshTopology::FaceTemplate>& MeshTopology::facesFor(ElementType type) {
    // Face templates are immutable and shared across builds. Returning a cached
    // reference avoids rebuilding nested vectors for every element block/build.
    static const std::vector<FaceTemplate> empty;
    static const std::vector<FaceTemplate> tetra4 = {{{0,2,1},3}, {{0,1,3},3}, {{1,2,3},3}, {{2,0,3},3}};
    static const std::vector<FaceTemplate> tetra10 = {{{0,2,1,6,5,4},3}, {{0,1,3,4,8,7},3}, {{1,2,3,5,9,8},3}, {{2,0,3,6,7,9},3}};
    static const std::vector<FaceTemplate> hexa8 = {{{0,3,2,1},4}, {{4,5,6,7},4}, {{0,1,5,4},4}, {{1,2,6,5},4}, {{2,3,7,6},4}, {{3,0,4,7},4}};
    static const std::vector<FaceTemplate> hexa20 = {{{0,3,2,1,11,10,9,8},4}, {{4,5,6,7,16,17,18,19},4}, {{0,1,5,4,8,13,16,12},4}, {{1,2,6,5,9,14,17,13},4}, {{2,3,7,6,10,15,18,14},4}, {{3,0,4,7,11,12,19,15},4}};
    static const std::vector<FaceTemplate> hexa27 = {{{0,3,2,1,11,10,9,8,20},4}, {{4,5,6,7,16,17,18,19,21},4}, {{0,1,5,4,8,13,16,12,22},4}, {{1,2,6,5,9,14,17,13,23},4}, {{2,3,7,6,10,15,18,14,24},4}, {{3,0,4,7,11,12,19,15,25},4}};
    static const std::vector<FaceTemplate> prism6 = {{{0,2,1},3}, {{3,4,5},3}, {{0,1,4,3},4}, {{1,2,5,4},4}, {{2,0,3,5},4}};
    static const std::vector<FaceTemplate> prism15 = {{{0,2,1,8,7,6},3}, {{3,4,5,9,10,11},3}, {{0,1,4,3,6,13,9,12},4}, {{1,2,5,4,7,14,10,13},4}, {{2,0,3,5,8,12,11,14},4}};
    static const std::vector<FaceTemplate> pyramid5 = {{{0,3,2,1},4}, {{0,1,4},3}, {{1,2,4},3}, {{2,3,4},3}, {{3,0,4},3}};
    switch (type) {
    case ElementType::Tetrahedron4: return tetra4;
    case ElementType::Tetrahedron10: return tetra10;
    case ElementType::Hexahedron8: return hexa8;
    case ElementType::Hexahedron20: return hexa20;
    case ElementType::Hexahedron27: return hexa27;
    case ElementType::Prism6: return prism6;
    case ElementType::Prism15: return prism15;
    case ElementType::Pyramid5: return pyramid5;
    default: return empty;
    }
}
