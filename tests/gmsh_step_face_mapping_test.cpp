#include <OpenModel/GmshMesher.hpp>
#include <OpenModel/OCCModel.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::cerr << "CHECK failed: " #condition " (line " << __LINE__ << ")\n";     \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

constexpr double kEndZ = -500.0;
constexpr double kExpectedEndArea = 1062.3800304821466;

struct ElementLocation {
    const ElementBlock* block = nullptr;
    std::size_t index = 0;
};

std::map<ElementId, ElementLocation> indexElements(const MeshData& mesh) {
    std::map<ElementId, ElementLocation> result;
    for (const auto& block : mesh.getElementBlocks())
        for (std::size_t i = 0; i < block.ids.size(); ++i)
            result.emplace(block.ids[i], ElementLocation{&block, i});
    return result;
}

double triangleArea(const std::array<double, 3>& a,
                    const std::array<double, 3>& b,
                    const std::array<double, 3>& c) {
    const std::array<double, 3> u{{b[0] - a[0], b[1] - a[1], b[2] - a[2]}};
    const std::array<double, 3> v{{c[0] - a[0], c[1] - a[1], c[2] - a[2]}};
    const std::array<double, 3> cross{{
        u[1] * v[2] - u[2] * v[1],
        u[2] * v[0] - u[0] * v[2],
        u[0] * v[1] - u[1] * v[0]}};
    return 0.5 * std::sqrt(cross[0] * cross[0] +
                           cross[1] * cross[1] +
                           cross[2] * cross[2]);
}

} // namespace

int main(int argc, char** argv) {
    CHECK(argc == 2);
    const std::string stepPath = argv[1];

    auto cad = std::make_shared<OCCModel>();
    CHECK(cad->loadFromFile(stepPath));

    // The centre of the I-profile lies strictly inside the end cap, away from
    // the perimeter shared with the longitudinal faces.
    const auto fixedFaceIds = cad->facesContainingPoint({{0.0, 0.0, kEndZ}}, 1e-5);
    const auto oppositeFaceIds = cad->facesContainingPoint({{0.0, 0.0, 500.0}}, 1e-5);
    CHECK(fixedFaceIds.size() == 1);
    CHECK(oppositeFaceIds.size() == 1);
    CHECK(fixedFaceIds.front() != oppositeFaceIds.front());

    MeshingOptions options;
    options.dimension = MeshDimension::Volume;
    options.generateTetrahedra = true;
    options.elementSize = 8.0;
    options.maxElementSize = 8.0;
    options.linearDeflection = 0.2;
    options.angularDeflection = 0.5;
    options.importCadGroups = true;
    options.additionalFaceGroups.push_back(
        CADGroup{"FIXED_END_ZMIN", CADGroupKind::NamedShape, 2, fixedFaceIds});

    GmshMesher mesher;
    MeshData mesh;
    if (!mesher.buildMesh(cad, options, mesh)) {
        std::cerr << "GmshMesher failed: " << mesher.lastError() << '\n';
        for (const auto& diagnostic : mesher.diagnostics())
            std::cerr << diagnostic << '\n';
        return 1;
    }

    const auto* fixed = mesh.getMeshGroupRegistry().find("FIXED_END_ZMIN");
    CHECK(fixed != nullptr);
    CHECK(fixed->dimension == 2);
    CHECK(fixed->source == MeshGroupSource::CAD);
    CHECK(fixed->sourceEntityIds == fixedFaceIds);
    CHECK(!fixed->elementIds.empty());
    CHECK(!fixed->nodeIndices.empty());

    const auto physical = std::find_if(
        mesh.getPhysicalGroups().begin(), mesh.getPhysicalGroups().end(),
        [](const MeshPhysicalGroup& group) { return group.name == "FIXED_END_ZMIN"; });
    CHECK(physical != mesh.getPhysicalGroups().end());
    CHECK(physical->dimension == 2);
    CHECK(physical->elementIds == fixed->elementIds);

    // The end cap must correspond to exactly one Gmsh surface. Its reported
    // element count must equal the entire exported group, not merely a sample.
    const CADFaceMappingRecord* endMapping = nullptr;
    for (const auto& record : mesher.faceMappingReport()) {
        if (std::find(record.cadFaceIds.begin(), record.cadFaceIds.end(),
                      fixedFaceIds.front()) == record.cadFaceIds.end())
            continue;
        CHECK(endMapping == nullptr);
        endMapping = &record;
    }
    CHECK(endMapping != nullptr);
    CHECK(!endMapping->ambiguous);
    CHECK(endMapping->cadFaceIds.size() == 1);
    CHECK(endMapping->sampledElementCount > 0);
    CHECK(endMapping->matchedSampleCount == endMapping->sampledElementCount);
    CHECK(endMapping->matchRatio >= 0.8);
    CHECK(endMapping->elementCount == fixed->elementIds.size());

    const auto elements = indexElements(mesh);
    std::set<NodeIndex> expectedNodes;
    double mappedArea = 0.0;
    for (const ElementId id : fixed->elementIds) {
        const auto found = elements.find(id);
        CHECK(found != elements.end());
        const auto& block = *found->second.block;
        CHECK(block.type == ElementType::Triangle3 ||
              block.type == ElementType::Triangle6);
        const std::size_t offset = found->second.index * block.nodesPerElement;
        std::array<std::array<double, 3>, 3> p{};
        for (std::size_t n = 0; n < block.nodesPerElement; ++n) {
            const NodeIndex node = block.connectivity[offset + n];
            expectedNodes.insert(node);
            const auto xyz = mesh.nodeCoordinates(node);
            // No longitudinal/side-face element may leak into the clamp group.
            CHECK(std::abs(xyz[2] - kEndZ) < 1e-5);
            if (n < 3) p[n] = xyz;
        }
        mappedArea += triangleArea(p[0], p[1], p[2]);
    }

    const std::set<NodeIndex> actualNodes(fixed->nodeIndices.begin(),
                                          fixed->nodeIndices.end());
    CHECK(actualNodes == expectedNodes);
    CHECK(actualNodes.size() == fixed->nodeIndices.size());
    CHECK(std::abs(mappedArea - kExpectedEndArea) / kExpectedEndArea < 1e-3);

    std::cout << "STEP end-face mapping passed: CAD face " << fixedFaceIds.front()
              << ", Gmsh surface " << endMapping->gmshSurfaceTag
              << ", " << fixed->elementIds.size() << " surface elements, "
              << fixed->nodeIndices.size() << " nodes, area " << mappedArea
              << " mm^2\n";
    for (const auto& diagnostic : mesher.diagnostics())
        std::cout << diagnostic << '\n';
    return 0;
}
