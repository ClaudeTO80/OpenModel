#include <OpenModel/MeshSelection.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace OpenModel {
namespace {

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/** Corner positions (indices into the element's connectivity) of each face, outward or inward irrespective. */
std::vector<std::vector<int>> faceCorners(ElementType type) {
    switch (type) {
    case ElementType::Tetrahedron4: case ElementType::Tetrahedron10:
        return {{0, 1, 2}, {0, 1, 3}, {1, 2, 3}, {0, 2, 3}};
    case ElementType::Hexahedron8: case ElementType::Hexahedron20: case ElementType::Hexahedron27:
        return {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    case ElementType::Prism6: case ElementType::Prism15:
        return {{0, 1, 2}, {3, 4, 5}, {0, 1, 4, 3}, {1, 2, 5, 4}, {2, 0, 3, 5}};
    case ElementType::Pyramid5:
        return {{0, 1, 2, 3}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    default:
        return {};
    }
}

int cornerCount(ElementType type) {
    switch (type) {
    case ElementType::Tetrahedron4: case ElementType::Tetrahedron10: return 4;
    case ElementType::Hexahedron8: case ElementType::Hexahedron20: case ElementType::Hexahedron27: return 8;
    case ElementType::Prism6: case ElementType::Prism15: return 6;
    case ElementType::Pyramid5: return 5;
    default: return 0;
    }
}

int surfaceCorners(ElementType type) {
    switch (type) {
    case ElementType::Triangle3: case ElementType::Triangle6: return 3;
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8: case ElementType::Quadrilateral9: return 4;
    default: return 0;
    }
}

struct Location { const ElementBlock* block; std::size_t position; };

std::unordered_map<ElementId, Location> indexElements(const MeshData& mesh) {
    std::unordered_map<ElementId, Location> map;
    map.reserve(mesh.getNumElements());
    for (const auto& b : mesh.getElementBlocks())
        for (std::size_t e = 0; e < b.size(); ++e) map.emplace(b.ids[e], Location{&b, e});
    return map;
}

const NodeIndex* connectivity(const Location& at) { return &at.block->connectivity[at.position * at.block->nodesPerElement]; }

/** A resolved face of a volume element. */
struct ResolvedFace {
    std::vector<NodeIndex> corners;   ///< 3 or 4 corner nodes, outward oriented.
    std::vector<NodeIndex> allNodes;  ///< corners + mid-side nodes found geometrically (quadratic elements).
};

using Vec3 = std::array<double, 3>;
Vec3 sub(const Vec3& a, const Vec3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 cross(const Vec3& a, const Vec3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

/** The face of `at` that contains the three given nodes among its corners; error text when there is none. */
bool resolveFace(const MeshData& mesh, const Location& at, const std::array<NodeIndex, 3>& picked, bool withMidSide, ResolvedFace& out, std::string& error) {
    const auto table = faceCorners(at.block->type);
    if (table.empty()) { error = "element type has no faces"; return false; }
    const NodeIndex* conn = connectivity(at);
    for (const auto& face : table) {
        std::size_t found = 0;
        for (const NodeIndex p : picked)
            for (const int c : face) if (conn[c] == p) { ++found; break; }
        if (found != 3) continue;
        out.corners.clear();
        for (const int c : face) out.corners.push_back(conn[c]);
        // Outward orientation: the normal must point away from the centroid of the element corners.
        Vec3 centre{{0, 0, 0}};
        const int nc = cornerCount(at.block->type);
        for (int c = 0; c < nc; ++c) { const auto x = mesh.nodeCoordinates(conn[c]); for (int k = 0; k < 3; ++k) centre[k] += x[k] / nc; }
        const auto p0 = mesh.nodeCoordinates(out.corners[0]), p1 = mesh.nodeCoordinates(out.corners[1]), p2 = mesh.nodeCoordinates(out.corners[2]);
        const Vec3 n = cross(sub(p1, p0), sub(p2, p0));
        if (dot(n, sub(p0, centre)) < 0) std::reverse(out.corners.begin(), out.corners.end());
        out.allNodes = out.corners;
        if (withMidSide && at.block->nodesPerElement > static_cast<std::size_t>(nc)) {
            // Each face edge takes the non-corner node of the element nearest to its midpoint (within a quarter edge).
            for (std::size_t i = 0; i < out.corners.size(); ++i) {
                const auto a = mesh.nodeCoordinates(out.corners[i]), b = mesh.nodeCoordinates(out.corners[(i + 1) % out.corners.size()]);
                const Vec3 mid{{(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}};
                const double edge = norm(sub(a, b));
                double best = 0.25 * edge;
                NodeIndex pick = static_cast<NodeIndex>(~NodeIndex(0));
                for (std::size_t k = static_cast<std::size_t>(nc); k < at.block->nodesPerElement; ++k) {
                    const double d = norm(sub(mesh.nodeCoordinates(conn[k]), mid));
                    if (d < best) { best = d; pick = conn[k]; }
                }
                if (pick != static_cast<NodeIndex>(~NodeIndex(0))) out.allNodes.push_back(pick);
            }
        }
        return true;
    }
    error = "the three nodes are not corners of one face of the element";
    return false;
}

GroupResult fail(GroupResult r, std::string message) {
    r.success = false;
    r.error = std::move(message);
    return r;
}

int nextTag(const MeshData& mesh) {
    int tag = 0;
    for (const auto& g : mesh.getPhysicalGroups()) tag = std::max(tag, g.tag);
    return tag + 1;
}

ElementId nextElementId(const MeshData& mesh) {
    ElementId id = 0;
    for (const auto& b : mesh.getElementBlocks()) for (const ElementId e : b.ids) id = std::max(id, e);
    return id + 1;
}

} // namespace

const char* groupKindName(GroupKind kind) noexcept {
    switch (kind) {
    case GroupKind::NodeSet: return "node";
    case GroupKind::ElementGroup: return "element";
    case GroupKind::FaceGroup: return "face";
    }
    return "node";
}

bool isValidGroupName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
}

bool groupNameInUse(const MeshData& mesh, const std::string& name) {
    const std::string key = lowered(name);
    for (const auto& s : mesh.getNodeSets()) if (lowered(s.first) == key) return true;
    for (const auto& g : mesh.getPhysicalGroups()) if (lowered(g.name) == key) return true;
    for (const auto& b : mesh.getElementBlocks()) if (lowered(b.name) == key) return true;
    return false;
}

std::string uniqueGroupName(const MeshData& mesh, const std::string& prefix) {
    for (int i = 1;; ++i) {
        const std::string candidate = prefix + std::to_string(i);
        if (!groupNameInUse(mesh, candidate) && !groupNameInUse(mesh, candidate + "_tri") && !groupNameInUse(mesh, candidate + "_quad")) return candidate;
    }
}

GroupResult createGroup(MeshData& mesh, const std::string& name, GroupKind kind, const ViewerSelection& selection) {
    GroupResult r;
    r.name = name;
    r.kind = kind;
    if (!isValidGroupName(name)) return fail(r, "Invalid group name '" + name + "': use 1-64 characters from letters, digits, '_', '-' and '.'");
    if (groupNameInUse(mesh, name) || (kind == GroupKind::FaceGroup && (groupNameInUse(mesh, name + "_tri") || groupNameInUse(mesh, name + "_quad"))))
        return fail(r, "A group, node set or block named '" + name + "' already exists");
    const bool byNode = selection.mode == "node", byElement = selection.mode == "element", byFace = selection.mode == "face";
    if (!byNode && !byElement && !byFace) return fail(r, "Nothing is selected (selection mode '" + selection.mode + "')");
    if ((byNode && selection.nodes.empty()) || (byElement && selection.elements.empty()) || (byFace && selection.faces.empty())) return fail(r, "The selection is empty");
    if (kind == GroupKind::FaceGroup && !byFace) return fail(r, "A face group needs a selection of faces");
    if (kind == GroupKind::ElementGroup && byNode) return fail(r, "An element group needs a selection of elements or faces");

    const auto elements = indexElements(mesh);
    auto nodeIndexOf = [&](NodeId id, NodeIndex& out) {
        if (!mesh.hasNodeId(id)) return false;
        out = mesh.nodeIndex(id);
        return true;
    };

    if (byNode) {
        std::set<NodeIndex> nodes;
        for (const NodeId id : selection.nodes) {
            NodeIndex index;
            if (!nodeIndexOf(id, index)) return fail(r, "Node " + std::to_string(id) + " is not in the mesh");
            nodes.insert(index);
        }
        mesh.addNodeSet(name, std::vector<NodeIndex>(nodes.begin(), nodes.end()));
        r.count = nodes.size();
        r.success = true;
        return r;
    }

    if (byElement) {
        std::vector<ElementId> ids;
        std::set<ElementId> seen;
        for (const ElementId id : selection.elements) {
            if (!elements.count(id)) return fail(r, "Element " + std::to_string(id) + " is not in the mesh");
            if (seen.insert(id).second) ids.push_back(id);
        }
        if (kind == GroupKind::NodeSet) {
            std::set<NodeIndex> nodes;
            for (const ElementId id : ids) {
                const auto& at = elements.at(id);
                const NodeIndex* c = connectivity(at);
                nodes.insert(c, c + at.block->nodesPerElement);
            }
            mesh.addNodeSet(name, std::vector<NodeIndex>(nodes.begin(), nodes.end()));
            r.count = nodes.size();
        } else {
            std::sort(ids.begin(), ids.end());
            mesh.addPhysicalGroup(name, 3, nextTag(mesh), ids);
            r.count = ids.size();
        }
        r.success = true;
        return r;
    }

    // Faces.
    struct Pending { ElementId element; ResolvedFace face; };
    std::vector<Pending> faces;
    std::set<std::vector<NodeIndex>> seenFaces;
    for (const auto& pf : selection.faces) {
        const auto at = elements.find(pf.element);
        if (at == elements.end()) return fail(r, "Element " + std::to_string(pf.element) + " is not in the mesh");
        std::array<NodeIndex, 3> nodes{};
        for (std::size_t k = 0; k < 3; ++k)
            if (!nodeIndexOf(pf.nodes[k], nodes[k])) return fail(r, "Node " + std::to_string(pf.nodes[k]) + " is not in the mesh");
        ResolvedFace face;
        std::string why;
        if (!resolveFace(mesh, at->second, nodes, kind == GroupKind::NodeSet, face, why))
            return fail(r, "Face of element " + std::to_string(pf.element) + ": " + why);
        auto key = face.corners;
        std::sort(key.begin(), key.end());
        if (seenFaces.insert(key).second) faces.push_back({pf.element, std::move(face)});
    }

    if (kind == GroupKind::NodeSet) {
        std::set<NodeIndex> nodes;
        for (const auto& f : faces) nodes.insert(f.face.allNodes.begin(), f.face.allNodes.end());
        mesh.addNodeSet(name, std::vector<NodeIndex>(nodes.begin(), nodes.end()));
        r.count = nodes.size();
    } else if (kind == GroupKind::ElementGroup) {
        std::set<ElementId> ids;
        for (const auto& f : faces) ids.insert(f.element);
        mesh.addPhysicalGroup(name, 3, nextTag(mesh), std::vector<ElementId>(ids.begin(), ids.end()));
        r.count = ids.size();
    } else {
        ElementId id = nextElementId(mesh);
        std::vector<ElementId> members;
        for (const auto& f : faces) {
            const bool tri = f.face.corners.size() == 3;
            mesh.addElement(name + (tri ? "_tri" : "_quad"), tri ? ElementType::Triangle3 : ElementType::Quadrilateral4, id, f.face.corners);
            members.push_back(id++);
        }
        mesh.addPhysicalGroup(name, 2, nextTag(mesh), members);
        r.count = members.size();
        r.diagnostics.push_back({ImportDiagnosticLevel::Info, "GROUP_SURFACE_ELEMENTS",
                                 std::to_string(members.size()) + " surface element(s) were added to the mesh to represent the faces", 0});
    }
    r.success = true;
    return r;
}

std::vector<GroupInfo> listGroups(const MeshData& mesh) {
    std::vector<GroupInfo> out;
    std::vector<std::string> names;
    for (const auto& s : mesh.getNodeSets()) names.push_back(s.first);
    std::sort(names.begin(), names.end());
    for (const auto& n : names) out.push_back({n, GroupKind::NodeSet, mesh.getNodeSets().at(n).size()});
    for (const auto& g : mesh.getPhysicalGroups()) {
        if (g.dimension == 3) out.push_back({g.name, GroupKind::ElementGroup, g.elementIds.size()});
        else if (g.dimension == 2) out.push_back({g.name, GroupKind::FaceGroup, g.elementIds.size()});
    }
    return out;
}

bool groupAsSelection(const MeshData& mesh, const std::string& name, ViewerSelection& out, std::size_t& skipped, std::string& error) {
    out = ViewerSelection{};
    skipped = 0;
    const std::string key = lowered(name);
    for (const auto& s : mesh.getNodeSets()) {
        if (lowered(s.first) != key) continue;
        out.mode = "node";
        for (const NodeIndex n : s.second) out.nodes.push_back(mesh.nodeId(n));
        return true;
    }
    for (const auto& g : mesh.getPhysicalGroups()) {
        if (lowered(g.name) != key) continue;
        if (g.dimension == 3) { out.mode = "element"; out.elements = g.elementIds; return true; }
        if (g.dimension != 2) { error = "Group '" + name + "' has dimension " + std::to_string(g.dimension) + ": only node sets, volume groups and surface groups can be shown"; return false; }
        // Surface elements -> the volume element that has the same face.
        std::map<std::vector<NodeIndex>, ElementId> owners;
        std::set<std::vector<NodeIndex>> shared;
        for (const auto& b : mesh.getElementBlocks()) {
            const auto table = faceCorners(b.type);
            for (std::size_t e = 0; e < b.size(); ++e)
                for (const auto& f : table) {
                    std::vector<NodeIndex> k;
                    for (const int c : f) k.push_back(b.connectivity[e * b.nodesPerElement + static_cast<std::size_t>(c)]);
                    std::sort(k.begin(), k.end());
                    if (!owners.emplace(k, b.ids[e]).second) shared.insert(k);
                }
        }
        const auto elements = indexElements(mesh);
        out.mode = "face";
        for (const ElementId id : g.elementIds) {
            const auto at = elements.find(id);
            const int corners = at == elements.end() ? 0 : surfaceCorners(at->second.block->type);
            if (!corners) { ++skipped; continue; }
            const NodeIndex* c = connectivity(at->second);
            std::vector<NodeIndex> k(c, c + corners);
            std::sort(k.begin(), k.end());
            const auto owner = owners.find(k);
            if (owner == owners.end()) { ++skipped; continue; }
            PickedFace pf;
            pf.element = owner->second;   // for an interior face (shared) this is one of the two owners
            for (std::size_t i = 0; i < 3; ++i) pf.nodes[i] = mesh.nodeId(c[i]);
            out.faces.push_back(pf);
        }
        return true;
    }
    error = "Unknown group '" + name + "'";
    return false;
}

} // namespace OpenModel
