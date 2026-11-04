#include <OpenModel/AssimpSurfaceMesher.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_set>

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

struct CompiledSelector {
    AssimpSurfaceSelector selector;
    Vec3 unitDirection{{0.0, 0.0, 1.0}};
};

bool matches(const CompiledSelector& compiled, const Vec3& a, const Vec3& b,
             const Vec3& c, const Vec3& triangleNormal) {
    const auto& s = compiled.selector;
    if ((s.kind == AssimpSurfaceSelectorKind::Normal || s.constrainNormal) &&
        std::abs(dot(triangleNormal, compiled.unitDirection)) + 1e-12 < s.minNormalAlignment)
        return false;

    const Vec3 points[] = {a, b, c};
    if (s.kind == AssimpSurfaceSelectorKind::AxisAlignedBox || s.constrainBounds) {
        for (const auto& p : points) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (p[axis] < s.boundsMin[axis] - s.tolerance ||
                    p[axis] > s.boundsMax[axis] + s.tolerance)
                    return false;
            }
        }
        if (s.kind == AssimpSurfaceSelectorKind::AxisAlignedBox) return true;
    }
    if (s.kind == AssimpSurfaceSelectorKind::Normal)
        return true;

    if (s.kind == AssimpSurfaceSelectorKind::Plane) {
        for (const auto& p : points)
            if (std::abs(dot(subtract(p, s.point), compiled.unitDirection)) > s.tolerance)
                return false;
        return true;
    }

    if (s.kind == AssimpSurfaceSelectorKind::SphereSurface) {
        for (const auto& p : points)
            if (std::abs(norm(subtract(p, s.point)) - s.radius) > s.tolerance)
                return false;
        return true;
    }

    if (s.kind == AssimpSurfaceSelectorKind::ConeSide) {
        const double tangent = std::tan(s.coneHalfAngleRadians);
        for (const auto& p : points) {
            const Vec3 relative = subtract(p, s.point);
            const double axial = dot(relative, compiled.unitDirection);
            if (axial < s.axialMin - s.tolerance || axial > s.axialMax + s.tolerance)
                return false;
            const Vec3 radial = subtract(relative, {{axial * compiled.unitDirection[0],
                                                      axial * compiled.unitDirection[1],
                                                      axial * compiled.unitDirection[2]}});
            if (std::abs(norm(radial) - axial * tangent) > s.tolerance)
                return false;
        }
        return true;
    }

    // Select the lateral cylindrical patch. End caps are intentionally not
    // selected by this selector; use plane selectors for the caps.
    for (const auto& p : points) {
        const Vec3 relative = subtract(p, s.point);
        const double axial = dot(relative, compiled.unitDirection);
        if (axial < s.axialMin - s.tolerance || axial > s.axialMax + s.tolerance)
            return false;
        const Vec3 radial = subtract(relative, {{axial * compiled.unitDirection[0],
                                                  axial * compiled.unitDirection[1],
                                                  axial * compiled.unitDirection[2]}});
        if (std::abs(norm(radial) - s.radius) > s.tolerance)
            return false;
    }
    return true;
}
} // namespace

std::string AssimpSurfaceMesher::getName() const {
    return "Assimp Surface Importer";
}

bool AssimpSurfaceMesher::buildMesh(const std::shared_ptr<CADModel>& cadModel,
                                    const MeshingOptions& options, MeshData& outMesh) {
    const auto model = std::dynamic_pointer_cast<AssimpModel>(cadModel);
    if (!model) {
        outMesh.clear();
        m_sourceTriangleElementIds.clear();
        m_diagnostics = {"AssimpSurfaceMesher requires an AssimpModel instance."};
        return false;
    }
    return buildMesh(model, options, std::vector<AssimpSurfaceSelector>{}, outMesh);
}

bool AssimpSurfaceMesher::buildMesh(
    const std::shared_ptr<AssimpModel>& model, const MeshingOptions& options,
    const std::vector<AssimpPlaneSurfaceGroup>& surfaceGroups, MeshData& outMesh) {
    std::vector<AssimpSurfaceSelector> selectors;
    selectors.reserve(surfaceGroups.size());
    for (const auto& plane : surfaceGroups) {
        AssimpSurfaceSelector selector;
        selector.name = plane.name;
        selector.kind = AssimpSurfaceSelectorKind::Plane;
        selector.point = plane.point;
        selector.direction = plane.normal;
        selector.tolerance = plane.tolerance;
        selector.constrainNormal = true;
        selector.minNormalAlignment = plane.minNormalAlignment;
        selectors.push_back(selector);
    }
    return buildMesh(model, options, selectors, outMesh);
}

bool AssimpSurfaceMesher::buildMesh(
    const std::shared_ptr<AssimpModel>& model, const MeshingOptions& options,
    const std::vector<AssimpSurfaceSelector>& selectors, MeshData& outMesh) {

    outMesh.clear();
    m_diagnostics.clear();
    m_sourceTriangleElementIds.clear();
    if (!model) {
        m_diagnostics.push_back("Assimp model is null.");
        return false;
    }
    if (options.dimension != MeshDimension::Surface) {
        m_diagnostics.push_back(
            "Assimp imports existing polygonal surfaces only; volume meshing is not implemented.");
        return false;
    }
    const auto& parts = model->getRenderMeshes();
    if (parts.empty()) {
        m_diagnostics.push_back("Assimp model contains no imported triangle meshes.");
        return false;
    }

    std::vector<CompiledSelector> compiled;
    compiled.reserve(selectors.size());
    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < selectors.size(); ++i) {
        const auto& s = selectors[i];
        const bool needsPoint = s.kind == AssimpSurfaceSelectorKind::Plane ||
            s.kind == AssimpSurfaceSelectorKind::CylinderSide ||
            s.kind == AssimpSurfaceSelectorKind::SphereSurface ||
            s.kind == AssimpSurfaceSelectorKind::ConeSide;
        const bool needsDirection = s.kind == AssimpSurfaceSelectorKind::Plane ||
            s.kind == AssimpSurfaceSelectorKind::Normal ||
            s.kind == AssimpSurfaceSelectorKind::CylinderSide ||
            s.kind == AssimpSurfaceSelectorKind::ConeSide || s.constrainNormal;
        const double directionNorm = norm(s.direction);
        if (s.name.empty() || !names.insert(s.name).second ||
            (needsPoint && !finite(s.point)) || (needsDirection && !finite(s.direction)) ||
            !std::isfinite(s.tolerance) || s.tolerance <= 0.0 ||
            (needsDirection && directionNorm <= 1e-14) ||
            !std::isfinite(s.minNormalAlignment) ||
            s.minNormalAlignment < 0.0 || s.minNormalAlignment > 1.0) {
            m_diagnostics.push_back("Invalid or duplicate surface selector at index " +
                                    std::to_string(i) + ".");
            return false;
        }
        if (s.kind == AssimpSurfaceSelectorKind::AxisAlignedBox || s.constrainBounds) {
            if (!finite(s.boundsMin) || !finite(s.boundsMax) ||
                s.boundsMin[0] > s.boundsMax[0] ||
                s.boundsMin[1] > s.boundsMax[1] ||
                s.boundsMin[2] > s.boundsMax[2]) {
                m_diagnostics.push_back("Invalid axis-aligned box selector '" + s.name + "'.");
                return false;
            }
        }
        if (s.kind == AssimpSurfaceSelectorKind::CylinderSide) {
            if (!std::isfinite(s.radius) || s.radius <= 0.0 ||
                !std::isfinite(s.axialMin) || !std::isfinite(s.axialMax) ||
                s.axialMin > s.axialMax) {
                m_diagnostics.push_back("Invalid cylinder selector '" + s.name + "'.");
                return false;
            }
        } else if (s.kind == AssimpSurfaceSelectorKind::SphereSurface) {
            if (!std::isfinite(s.radius) || s.radius <= 0.0) {
                m_diagnostics.push_back("Invalid sphere selector '" + s.name + "'.");
                return false;
            }
        } else if (s.kind == AssimpSurfaceSelectorKind::ConeSide) {
            if (!std::isfinite(s.coneHalfAngleRadians) ||
                s.coneHalfAngleRadians <= 0.0 ||
                s.coneHalfAngleRadians >= 1.5707963267948966 ||
                !std::isfinite(std::tan(s.coneHalfAngleRadians)) ||
                !std::isfinite(s.axialMin) || !std::isfinite(s.axialMax) ||
                s.axialMin < 0.0 || s.axialMin > s.axialMax) {
                m_diagnostics.push_back("Invalid cone selector '" + s.name + "'.");
                return false;
            }
        } else if (s.kind != AssimpSurfaceSelectorKind::Plane &&
                   s.kind != AssimpSurfaceSelectorKind::Normal &&
                   s.kind != AssimpSurfaceSelectorKind::AxisAlignedBox) {
            m_diagnostics.push_back("Unsupported selector kind for '" + s.name + "'.");
            return false;
        }
        CompiledSelector item;
        item.selector = s;
        if (needsDirection) {
            for (std::size_t axis = 0; axis < 3; ++axis)
                item.unitDirection[axis] = s.direction[axis] / directionNorm;
        } else {
            item.unitDirection = {{0.0, 0.0, 1.0}};
        }
        compiled.push_back(item);
    }

    struct GroupAccumulator {
        std::vector<ElementId> elements;
        std::set<NodeIndex> nodes;
    };
    std::vector<GroupAccumulator> accumulators(selectors.size());
    NodeId nextNodeId = 1;
    ElementId nextElementId = 1;
    int physicalTag = 1;
    std::size_t totalTriangles = 0;
    std::vector<std::vector<ElementId>> sourceTriangleElementIds;


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
        sourceTriangleElementIds.emplace_back(part.indices.size() / 3, ElementId{0});
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
        outMesh.getOrCreateBlock(name, ElementType::Triangle3, 3);
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
            const Vec3 edgeAB = subtract(pb, pa);
            const Vec3 edgeAC = subtract(pc, pa);
            const Vec3 edgeBC = subtract(pc, pb);
            const double maxEdgeSquared = std::max({dot(edgeAB, edgeAB),
                dot(edgeAC, edgeAC), dot(edgeBC, edgeBC)});
            const Vec3 rawNormal = cross(edgeAB, edgeAC);
            const double area2 = norm(rawNormal);
            const double degeneracyThreshold =
                std::numeric_limits<double>::epsilon() * maxEdgeSquared * 64.0;
            if (!std::isfinite(area2) || !std::isfinite(maxEdgeSquared) ||
                maxEdgeSquared <= 0.0 || area2 <= degeneracyThreshold) {
                m_diagnostics.push_back("Degenerate triangle skipped in Assimp part " +
                                        std::to_string(partIndex) + ".");
                continue;
            }
            const Vec3 triangleNormal{{rawNormal[0] / area2, rawNormal[1] / area2,
                                       rawNormal[2] / area2}};
            const ElementId elementId = nextElementId++;
            outMesh.addElement(name, ElementType::Triangle3, elementId, {a, b, c});
            sourceTriangleElementIds.back()[t / 3] = elementId;
            ++totalTriangles;

            for (std::size_t g = 0; g < compiled.size(); ++g) {
                if (matches(compiled[g], pa, pb, pc, triangleNormal)) {
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

    // A requested FEM boundary selector that matches nothing is almost
    // always a modelling error. Reject the complete import rather than
    // silently returning a mesh with a missing boundary-condition group.
    for (std::size_t g = 0; g < selectors.size(); ++g) {
        if (accumulators[g].elements.empty()) {
            m_diagnostics.push_back("Selector '" + selectors[g].name +
                                    "' matched no triangles; import rejected to avoid a missing FEM group.");
            outMesh.clear();
            m_sourceTriangleElementIds.clear();
            return false;
        }
    }

    for (std::size_t g = 0; g < selectors.size(); ++g) {
        auto& selected = accumulators[g];
        std::vector<NodeIndex> nodes(selected.nodes.begin(), selected.nodes.end());
        outMesh.addPhysicalGroup(selectors[g].name, 2, physicalTag++, selected.elements);
        outMesh.addNodeSet(selectors[g].name + "_NODES", nodes);
        outMesh.addMeshGroup(MeshGroup{selectors[g].name, 2, MeshGroupSource::Imported,
                                       {}, selected.elements, nodes});
        m_diagnostics.push_back("Selector '" + selectors[g].name + "' selected " +
                                std::to_string(selected.elements.size()) + " triangle(s) and " +
                                std::to_string(nodes.size()) + " node(s).");
    }

    m_sourceTriangleElementIds = std::move(sourceTriangleElementIds);
    m_diagnostics.push_back("Assimp surface import: " + std::to_string(parts.size()) +
                            " part(s), " + std::to_string(totalTriangles) + " non-degenerate triangle(s).");
    return !outMesh.isEmpty();
}
