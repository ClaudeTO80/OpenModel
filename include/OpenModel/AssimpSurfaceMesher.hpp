#pragma once

#include <OpenModel/AssimpModel.hpp>
#include <OpenModel/Mesher.hpp>

#include <array>
#include <memory>
#include <string>
#include <vector>

/**
 * A mesh-based surface selection. It deliberately describes geometry rather
 * than a CAD face identifier, because Assimp does not preserve B-Rep topology.
 *
 * A triangle is selected only when all three corner vertices lie within
 * tolerance of the plane and its normal is sufficiently aligned with the
 * requested plane normal.
 */
struct AssimpPlaneSurfaceGroup {
    std::string name;
    std::array<double, 3> point{{0.0, 0.0, 0.0}};
    std::array<double, 3> normal{{0.0, 0.0, 1.0}};
    double tolerance = 1e-5;
    double minNormalAlignment = 0.95;
};

/**
 * Imports Assimp's already-triangulated geometry into MeshData.
 *
 * This adapter is intentionally surface-only: Assimp supplies polygonal
 * triangles, not a watertight CAD boundary representation from which a
 * reliable volume mesh can be generated. It can create imported surface
 * groups and corresponding node sets using geometric plane selectors.
 */
class AssimpSurfaceMesher final : public Mesher {
public:
    std::string getName() const override;
    const std::vector<std::string>& diagnostics() const override { return m_diagnostics; }

    bool buildMesh(const std::shared_ptr<CADModel>& cadModel,
                   const MeshingOptions& options,
                   MeshData& outMesh) override;

    bool buildMesh(const std::shared_ptr<AssimpModel>& model,
                   const MeshingOptions& options,
                   const std::vector<AssimpPlaneSurfaceGroup>& surfaceGroups,
                   MeshData& outMesh);

private:
    std::vector<std::string> m_diagnostics;
};
