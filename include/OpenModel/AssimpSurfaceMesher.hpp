#pragma once

#include <OpenModel/AssimpModel.hpp>
#include <OpenModel/Mesher.hpp>

#include <array>
#include <limits>
#include <memory>
#include <string>
#include <vector>

/** Geometric selector operating on the imported triangle mesh, never on B-Rep IDs. */
enum class AssimpSurfaceSelectorKind {
    Plane,          ///< Triangle vertices lie on a plane within tolerance.
    Normal,         ///< Select triangles by normal direction.
    AxisAlignedBox, ///< All triangle vertices lie inside the supplied AABB.
    CylinderSide    ///< All triangle vertices lie on the lateral cylinder within tolerance.
};

struct AssimpSurfaceSelector {
    std::string name;
    AssimpSurfaceSelectorKind kind = AssimpSurfaceSelectorKind::Plane;

    // Plane: a point on the plane. Cylinder: a point on the cylinder axis.
    std::array<double, 3> point{{0.0, 0.0, 0.0}};
    // Plane: plane normal. Cylinder: cylinder axis direction.
    std::array<double, 3> direction{{0.0, 0.0, 1.0}};

    // Axis-aligned box limits in model coordinates.
    std::array<double, 3> boundsMin{{0.0, 0.0, 0.0}};
    std::array<double, 3> boundsMax{{0.0, 0.0, 0.0}};

    // Cylinder lateral surface.
    double radius = 1.0;
    double axialMin = -std::numeric_limits<double>::max();
    double axialMax = std::numeric_limits<double>::max();

    // Absolute distance tolerance, in the source model's units.
    double tolerance = 1e-5;

    // Optional normal filter. For planes it is enabled by default.
    bool constrainNormal = true;
    double minNormalAlignment = 0.95;
};

/** Backward-compatible plane selector used by the first Assimp implementation. */
struct AssimpPlaneSurfaceGroup {
    std::string name;
    std::array<double, 3> point{{0.0, 0.0, 0.0}};
    std::array<double, 3> normal{{0.0, 0.0, 1.0}};
    double tolerance = 1e-5;
    double minNormalAlignment = 0.95;
};

/**
 * Imports Assimp's already-triangulated geometry into MeshData and optionally
 * builds geometric surface groups plus matching node sets. This is a surface
 * mesh adapter; it does not create tetrahedra or infer CAD topology.
 */
class AssimpSurfaceMesher final : public Mesher {
public:
    std::string getName() const override;
    const std::vector<std::string>& diagnostics() const override { return m_diagnostics; }
    // One entry per source triangle; zero denotes a skipped degenerate triangle.
    const std::vector<std::vector<ElementId>>& sourceTriangleElementIds() const noexcept {
        return m_sourceTriangleElementIds;
    }

    bool buildMesh(const std::shared_ptr<CADModel>& cadModel,
                   const MeshingOptions& options,
                   MeshData& outMesh) override;

    bool buildMesh(const std::shared_ptr<AssimpModel>& model,
                   const MeshingOptions& options,
                   const std::vector<AssimpPlaneSurfaceGroup>& surfaceGroups,
                   MeshData& outMesh);

    bool buildMesh(const std::shared_ptr<AssimpModel>& model,
                   const MeshingOptions& options,
                   const std::vector<AssimpSurfaceSelector>& selectors,
                   MeshData& outMesh);

private:
    std::vector<std::string> m_diagnostics;
    std::vector<std::vector<ElementId>> m_sourceTriangleElementIds;
};
