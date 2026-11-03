#include <OpenModel/AssimpSurfaceMesher.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>

namespace {
using Vec3 = std::array<double, 3>;

Vec3 subtract(const Vec3& a, const Vec3& b) {
    return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}};
}

double dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {{a[1] * b[2] - a[2] * b[1],
             a[2] * b[0] - a[0] * b[2],
             a[0] * b[1] - a[1] * b[0]}};
}

double norm(const Vec3& a) { return std::sqrt(dot(a, a)); }

bool finite(const Vec3& a) {
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}

std::string blockName(std::size_t index, const std::string& name) {
    std::ostringstream out;
    out << "ASSIMP_SURFACE_" << index;
    if (!name.empty()) out << "_" << name;
    return out.str();
}
} // namespace

std::string AssimpSurfaceMesher::getName() const {
    return "Assimp Surface Importer";
}

bool AssimpSurfaceMesher::buildMesh(const std::shared_ptr<CADModel>& cadModel,
                                    const MeshingOptions& options,
                                    MeshData& outMesh) {
    const auto model = std::dynamic_pointer_cast<AssimpModel>(cadModel);
    if (!model) {
        outMesh.clear();
        m_diagnostics = {"AssimpSurfaceMesher requires an AssimpModel instance."};
        return false;
    }
    return buildMesh(model, options, {}, outMesh);
}

bool AssimpSurfaceMesher::buildMesh(
    const std::shared_ptr<AssimpModel>& model,
    const MeshingOptions& options,
    const std::vector<AssimpPlaneSurfaceGroup>& surfaceGroups,
    MeshData& outMesh) {

    outMesh.clear();
    m_diagnostics.clear();

    if (!model) {
        m_diagnostics.push_back("Assimp model is null.");
        return false;
    }
    if (options.dimension != MeshDimension::Surface) {
        m_diagnostics.push_back(
            "Assimp can import polygonal surface meshes, but this adapter does not generate volume elements.");
        return false;
    }
    const auto& parts = model->getRenderMeshes();
    if (parts.empty()) {
        m_diagnostics.push_back("Assimp model contains no imported triangle meshes.");
        return false;
    }

    struct GroupAccumulator {
        std::vector<ElementId> elements;
        std::set<NodeIndex> nodes;
    };
    std::vector<GroupAccumulator> accumulators(surfaceGroups.size());

    for (std::size_t g = 0; g < surfaceGroups.size(); ++g) {
        const auto& group = surfaceGroups[g];
        const double n = norm(group.normal);
        if (group.name.empty() || !finite(group.point) || !finite(group.normal) ||
            !std::isfinite(group.tolerance) || group.tolerance <= 0.0 ||
            !std::isfinite(group.minNormalAlignment) ||
            group.minNormalAlignment < 0.0 || group.minNormalAlignment > 1.0 ||
            n <= 1e-14) {
            m_diagnostics.push_back("Invalid plane selector at index " + std::to_string(g) + ".");
            return false;
        }
    }

    NodeId nextNodeId = 1;
    ElementId nextElementId = 1;
    int physicalTag = 1;
    std::size_t totalTriangles = 0;

    for (std::size_t partIndex = 0; partIndex < parts.size(); ++partIndex) {
        const auto& part = parts[partIndex];
        if (part.vertices.empty() || part.vertices.size() % 3 != 0 ||
            part.indices.empty() || part.indices.size() % 3 != 0) {
            m_diagnostics.push_back("Assimp part " + std::to_string(partIndex) +
                                    " has malformed vertex or triangle arrays.");
            outMesh.clear();
            return false;
        }
        const std::size_t vertexCount = part.vertices.size() / 3;
        std::vector<NodeIndex> localNodes;
        localNodes.reserve(vertexCount);
        for (std::size_t v = 0; v < vertexCount; ++v) {
            const double x = part.vertices[3 * v];
            const double y = part.vertices[3 * v + 1];
            const double z = part.vertices[3 * v + 2];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                m_diagnostics.push_back("Assimp part contains a non-finite vertex.");
                outMesh.clear();
                return false;
            }
            localNodes.push_back(outMesh.addNode(nextNodeId++, x, y, z));
        }

        const std::string name = blockName(partIndex, part.name);
        auto& block = outMesh.getOrCreateBlock(name, ElementType::Triangle3, 3);
        (void)block;
        for (std::size_t t = 0; t < part.indices.size(); t += 3) {
            const unsigned int ia = part.indices[t];
            const unsigned int ib = part.indices[t + 1];
            const unsigned int ic = part.indices[t + 2];
            if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
                m_diagnostics.push_back("Assimp triangle references a vertex outside its part.");
                outMesh.clear();
                return false;
            }
            const NodeIndex a = localNodes[ia], b = localNodes[ib], c = localNodes[ic];
            const Vec3 pa{{part.vertices[3 * ia], part.vertices[3 * ia + 1], part.vertices[3 * ia + 2]}};
            const Vec3 pb{{part.vertices[3 * ib], part.vertices[3 * ib + 1], part.vertices[3 * ib + 2]}};
            const Vec3 pc{{part.vertices[3 * ic], part.vertices[3 * ic + 1], part.vertices[3 * ic + 2]}};
            const Vec3 rawNormal = cross(subtract(pb, pa), subtract(pc, pa));
            const double area2 = norm(rawNormal);
            if (!std::isfinite(area2) || area2 <= 1e-18) {
                m_diagnostics.push_back("Degenerate triangle skipped in Assimp part " +
                                        std::to_string(partIndex) + ".");
                continue;
            }
            const Vec3 triangleNormal{{rawNormal[0] / area2, rawNormal[1] / area2,
                                       rawNormal[2] / area2}};
            const ElementId elementId = nextElementId++;
            outMesh.addElement(name, ElementType::Triangle3, elementId, {a, b, c});
            ++totalTriangles;

            for (std::size_t g = 0; g < surfaceGroups.size(); ++g) {
                const auto& selector = surfaceGroups[g];
                Vec3 planeNormal = selector.normal;
                const double planeNorm = norm(planeNormal);
                for (double& component : planeNormal) component /= planeNorm;
                const double alignment = std::abs(dot(triangleNormal, planeNormal));
                if (alignment + 1e-12 < selector.minNormalAlignment) continue;
                const auto distance = [&](const Vec3& p) {
                    return std::abs(dot(subtract(p, selector.point), planeNormal));
                };
                if (distance(pa) <= selector.tolerance &&
                    distance(pb) <= selector.tolerance &&
                    distance(pc) <= selector.tolerance) {
                    accumulators[g].elements.push_back(elementId);
                    accumulators[g].nodes.insert(a);
                    accumulators[g].nodes.insert(b);
                    accumulators[g].nodes.insert(c);
                }
            }
        }
    }

    if (totalTriangles == 0) {
        m_diagnostics.push_back("No non-degenerate surface triangles were imported.");
        outMesh.clear();
        return false;
    }

    for (std::size_t g = 0; g < surfaceGroups.size(); ++g) {
        auto& selected = accumulators[g];
        if (selected.elements.empty()) {
            m_diagnostics.push_back("Plane group '" + surfaceGroups[g].name +
                                     "' matched no triangles; no group was created.");
            continue;
        }
        std::vector<NodeIndex> nodes(selected.nodes.begin(), selected.nodes.end());
        outMesh.addPhysicalGroup(surfaceGroups[g].name, 2, physicalTag++, selected.elements);
        outMesh.addNodeSet(surfaceGroups[g].name + "_NODES", nodes);
        outMesh.addMeshGroup(MeshGroup{surfaceGroups[g].name, 2, MeshGroupSource::Imported,
                                       {}, selected.elements, nodes});
        m_diagnostics.push_back("Plane group '" + surfaceGroups[g].name + "' selected " +
                                std::to_string(selected.elements.size()) + " triangle(s) and " +
                                std::to_string(nodes.size()) + " node(s).");
    }

    m_diagnostics.push_back("Assimp surface import: " + std::to_string(parts.size()) +
                            " part(s), " + std::to_string(totalTriangles) + " triangle(s).");
    return !outMesh.isEmpty();
}
