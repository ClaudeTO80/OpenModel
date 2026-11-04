/**
 * @file gmsh_assembly_test.cpp
 * @brief CAD components and groups become physical groups, and the interface mode controls whether two
 *        touching solids share nodes. Uses the XDE assembly fixture (Base/Cover touching on z = 10, two Pins).
 */
#include "support/assembly_fixture.hpp"

#include <OpenModel/GmshMesher.hpp>
#include <OpenModel/OCCModel.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <memory>
#include <set>

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "CHECK failed: " #cond " (line " << __LINE__ << ")\n";          \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

namespace {

struct Located {
    const ElementBlock* block;
    std::size_t index;
};

std::map<ElementId, Located> indexElements(const MeshData& mesh) {
    std::map<ElementId, Located> map;
    for (const auto& block : mesh.getElementBlocks())
        for (std::size_t i = 0; i < block.ids.size(); ++i) map[block.ids[i]] = {&block, i};
    return map;
}

const MeshPhysicalGroup* group(const MeshData& mesh, const std::string& name) {
    for (const auto& g : mesh.getPhysicalGroups())
        if (g.name == name) return &g;
    return nullptr;
}

bool validFaceAudit(const GmshMesher& mesher) {
    const auto& report = mesher.faceMappingReport();
    if (report.empty()) return false;
    std::set<int> seenSurfaces;
    for (const auto& record : report) {
        if (!seenSurfaces.insert(record.gmshSurfaceTag).second) return false;
        if (record.matchedSampleCount > record.sampledElementCount) return false;
        if (record.sampledElementCount != record.elementCount) return false;
        if (record.projectionFailureCount != 0) return false;
        if (record.matchRatio < 0.0 || record.matchRatio > 1.0) return false;
        if (record.elementCount > 0 && record.sampledElementCount == 0) return false;
        if (!record.cadFaceIds.empty() && record.matchRatio < 0.8) return false;
        if (record.ambiguous && !record.cadFaceIds.empty()) return false;
    }
    return true;
}

/** Sum of tetrahedron volumes of a group; also collects the node indices lying on the plane z = 10. */
double groupVolume(const MeshData& mesh, const std::map<ElementId, Located>& elements, const MeshPhysicalGroup& g,
                   std::set<NodeIndex>* nodesOnPlane) {
    double volume = 0.0;
    for (const ElementId id : g.elementIds) {
        const auto& at = elements.at(id);
        if (at.block->type != ElementType::Tetrahedron4 && at.block->type != ElementType::Tetrahedron10) continue;
        const std::size_t n = at.block->nodesPerElement;
        std::array<std::array<double, 3>, 4> p;
        for (std::size_t k = 0; k < 4; ++k) p[k] = mesh.nodeCoordinates(at.block->connectivity[at.index * n + k]);
        auto sub = [](const std::array<double, 3>& a, const std::array<double, 3>& b) {
            return std::array<double, 3>{{a[0] - b[0], a[1] - b[1], a[2] - b[2]}};
        };
        const auto a = sub(p[1], p[0]), b = sub(p[2], p[0]), c = sub(p[3], p[0]);
        volume += std::fabs(a[0] * (b[1] * c[2] - b[2] * c[1]) - a[1] * (b[0] * c[2] - b[2] * c[0]) + a[2] * (b[0] * c[1] - b[1] * c[0])) / 6.0;
        if (nodesOnPlane)
            for (std::size_t k = 0; k < 4; ++k) {
                const NodeIndex node = at.block->connectivity[at.index * n + k];
                if (std::fabs(mesh.nodeCoordinates(node)[2] - 10.0) < 1e-6) nodesOnPlane->insert(node);
            }
    }
    return volume;
}

int run(InterfaceMode mode, const std::string& step) {
    auto cad = std::make_shared<OCCModel>();
    CHECK(cad->loadFromFile(step));
    MeshingOptions options;
    options.elementSize = 2.0;
    options.maxElementSize = 2.0;
    options.interfaceMode = mode;
    GmshMesher mesher;
    MeshData mesh;
    CHECK(mesher.buildMesh(cad, options, mesh));
    CHECK(validFaceAudit(mesher));
    const char* label = mode == InterfaceMode::Conformal ? "conformal" : "independent";
    std::cout << "--- " << label << ": " << mesh.getNumNodes() << " nodes, " << mesh.getNumElements() << " elements\n";
    for (const auto& line : mesher.diagnostics()) std::cout << "    " << line << '\n';

    const auto elements = indexElements(mesh);
    const auto* base = group(mesh, "Assy/Base");
    const auto* cover = group(mesh, "Assy/Cover");
    const auto* pin1 = group(mesh, "Assy/Pin");
    const auto* pin2 = group(mesh, "Assy/Pin #2");
    CHECK(base && cover && pin1 && pin2);
    CHECK(base->dimension == 3 && cover->dimension == 3);

    std::set<NodeIndex> onPlaneBase, onPlaneCover;
    const double vBase = groupVolume(mesh, elements, *base, &onPlaneBase);
    const double vCover = groupVolume(mesh, elements, *cover, &onPlaneCover);
    const double vPin1 = groupVolume(mesh, elements, *pin1, nullptr);
    const double vPin2 = groupVolume(mesh, elements, *pin2, nullptr);
    std::cout << "    volumes: base " << vBase << " cover " << vCover << " pin " << vPin1 << " / " << vPin2
              << "  (exact 1000, 500, " << M_PI * 5 << ")\n";
    CHECK(std::fabs(vBase - 1000.0) < 1.0 && std::fabs(vCover - 500.0) < 1.0);
    CHECK(vPin1 > 0.0 && vPin2 > 0.0 && std::fabs(vPin1 - M_PI * 5.0) / (M_PI * 5.0) < 0.2);

    // Face groups: the top face of the Base is both the layer "Interface" and the named face "TopFace".
    const auto* layer = group(mesh, "Interface");
    const auto* named = group(mesh, "TopFace");
    CHECK(layer && named && layer->dimension == 2 && named->dimension == 2);
    CHECK(!layer->elementIds.empty() && layer->elementIds == named->elementIds);
    const auto* edgeGroup = mesh.getMeshGroupRegistry().find("EdgeLayer");
    const auto* vertexGroup = mesh.getMeshGroupRegistry().find("VertexLayer");
    CHECK(edgeGroup && edgeGroup->dimension == 1 && !edgeGroup->elementIds.empty());
    CHECK(vertexGroup && vertexGroup->dimension == 0 && !vertexGroup->nodeIndices.empty());
    CHECK(mesh.getNodeSets().count("EdgeLayer_NODES") == 1);
    CHECK(mesh.getNodeSets().count("VertexLayer_NODES") == 1);
    for (const ElementId id : layer->elementIds) {
        const auto& at = elements.at(id);
        CHECK(at.block->type == ElementType::Triangle3);
        for (std::size_t k = 0; k < 3; ++k)
            CHECK(std::fabs(mesh.nodeCoordinates(at.block->connectivity[at.index * 3 + k])[2] - 10.0) < 1e-6);
    }

    std::set<NodeIndex> shared;
    for (const NodeIndex n : onPlaneBase) if (onPlaneCover.count(n)) shared.insert(n);
    std::cout << "    nodes on z=10: base " << onPlaneBase.size() << ", cover " << onPlaneCover.size() << ", shared " << shared.size() << '\n';
    if (mode == InterfaceMode::Conformal) {
        CHECK(!shared.empty() && shared.size() == onPlaneBase.size());
    } else {
        CHECK(shared.empty());
    }
    return 0;
}


int runSurfaceOnly(const std::string& step, bool incompleteSecondOrder, bool recombineSurfaceElements) {
    auto cad = std::make_shared<OCCModel>();
    CHECK(cad->loadFromFile(step));
    MeshingOptions options;
    options.dimension = MeshDimension::Surface;
    options.generateTetrahedra = false;
    options.elementSize = 2.0;
    options.maxElementSize = 2.0;
    options.order = 2;
    options.recombineSurfaceElements = recombineSurfaceElements;
    options.incompleteSecondOrder = incompleteSecondOrder;
    options.importCadGroups = true;
    const auto uniqueSideFace = cad->facesContainingPoint({{0.0, 5.0, 5.0}}, 1e-6);
    CHECK(uniqueSideFace.size() == 1);
    options.additionalFaceGroups.push_back(
        CADGroup{"UniqueSideFace", CADGroupKind::NamedShape, 2, uniqueSideFace});
    // A user-defined group must never be emitted partially: the second source
    // face id deliberately does not exist in this imported STEP model.
    options.additionalFaceGroups.push_back(
        CADGroup{"IncompleteFaceGroup", CADGroupKind::NamedShape, 2,
                 {uniqueSideFace.front(), 999999999}});

    GmshMesher mesher;
    MeshData mesh;
    CHECK(mesher.buildMesh(cad, options, mesh));
    CHECK(validFaceAudit(mesher));
    CHECK(mesh.getNumElements() > 0);
    bool sawExpectedSurfaceType = false;
    for (const auto& block : mesh.getElementBlocks()) {
        CHECK(block.type != ElementType::Tetrahedron4 && block.type != ElementType::Tetrahedron10);
        if (!recombineSurfaceElements && block.type == ElementType::Triangle6)
            sawExpectedSurfaceType = true;
        if (recombineSurfaceElements && incompleteSecondOrder &&
            block.type == ElementType::Quadrilateral8)
            sawExpectedSurfaceType = true;
        if (recombineSurfaceElements && !incompleteSecondOrder &&
            block.type == ElementType::Quadrilateral9)
            sawExpectedSurfaceType = true;
    }
    CHECK(sawExpectedSurfaceType);

    // Surface-only meshes have no upward volume adjacency. CAD faces must still
    // be mapped from their geometric location and actual 2-D mesh elements.
    const auto* uniqueFace = mesh.getMeshGroupRegistry().find("UniqueSideFace");
    CHECK(uniqueFace && uniqueFace->dimension == 2);
    CHECK(!uniqueFace->elementIds.empty());

    // Verify that the CAD-derived Node Set includes every node of every
    // quadratic boundary element, including all midside/interior nodes.
    const auto elementIndex = indexElements(mesh);
    std::set<NodeIndex> expectedFaceNodes;
    for (const ElementId id : uniqueFace->elementIds) {
        const auto found = elementIndex.find(id);
        CHECK(found != elementIndex.end());
        const auto& block = *found->second.block;
        if (!recombineSurfaceElements)
            CHECK(block.type == ElementType::Triangle6);
        else if (incompleteSecondOrder)
            CHECK(block.type == ElementType::Triangle6 || block.type == ElementType::Quadrilateral8);
        else
            CHECK(block.type == ElementType::Triangle6 || block.type == ElementType::Quadrilateral9);
        const std::size_t offset = found->second.index * block.nodesPerElement;
        for (std::size_t n = 0; n < block.nodesPerElement; ++n)
            expectedFaceNodes.insert(block.connectivity[offset + n]);
    }
    const std::set<NodeIndex> actualFaceNodes(uniqueFace->nodeIndices.begin(),
                                               uniqueFace->nodeIndices.end());
    CHECK(actualFaceNodes == expectedFaceNodes);

    const auto* physical = group(mesh, "UniqueSideFace");
    CHECK(physical && !physical->elementIds.empty());

    // Incomplete groups are rejected atomically in both the backend-neutral
    // registry and Gmsh physical-group representation.
    CHECK(mesh.getMeshGroupRegistry().find("IncompleteFaceGroup") == nullptr);
    CHECK(group(mesh, "IncompleteFaceGroup") == nullptr);
    bool reportedMissingFace = false;
    for (const auto& diagnostic : mesher.diagnostics())
        if (diagnostic.find("IncompleteFaceGroup") != std::string::npos &&
            diagnostic.find("999999999") != std::string::npos)
            reportedMissingFace = true;
    CHECK(reportedMissingFace);

    // Abaqus and CalculiX share the INP keyword format. Verify that the
    // backend-neutral CAD group exports both the element set and its derived
    // node set, not merely the Gmsh-specific physical group.
    const std::string inpPath = "gmsh_surface_face_group.inp";
    const auto writeResult = AbaqusInpWriter::writeFile(inpPath, mesh);
    if (!writeResult.success)
        for (const auto& error : writeResult.errors) std::cerr << "Abaqus export: " << error << '\n';
    CHECK(writeResult.success);
    CHECK(writeResult.elementSetsWritten > 0);
    CHECK(writeResult.nodeSetsWritten > 0);
    std::ifstream inpFile(inpPath);
    CHECK(inpFile.good());
    std::ostringstream inpContents;
    inpContents << inpFile.rdbuf();
    const std::string inpText = inpContents.str();
    CHECK(inpText.find("*ELSET, ELSET=UNIQUESIDEFACE") != std::string::npos);
    CHECK(inpText.find("*NSET, NSET=UNIQUESIDEFACE_NODES") != std::string::npos);
    // Quadratic CAD boundary curves are exported as Abaqus T3D3 elements.
    CHECK(inpText.find("*ELEMENT, TYPE=T3D3") != std::string::npos);
    inpFile.close();
    std::remove(inpPath.c_str());

    std::cout << "--- surface-only CAD face mapping: " << uniqueFace->elementIds.size()
              << " elements, " << uniqueFace->nodeIndices.size() << " nodes\n";
    return 0;
}

} // namespace

int main() {
    const std::string step = "openmodel_gmsh_assembly.step";
    CHECK(writeAssemblyFixture(step));
    const int a = run(InterfaceMode::Independent, step);
    const int b = a == 0 ? run(InterfaceMode::Conformal, step) : a;
    const int c = (a == 0 && b == 0) ? runSurfaceOnly(step, false, false) : 1;
    const int d = (a == 0 && b == 0 && c == 0) ? runSurfaceOnly(step, false, true) : 1;
    const int e = (a == 0 && b == 0 && c == 0 && d == 0) ? runSurfaceOnly(step, true, true) : 1;
    std::remove(step.c_str());
    if (a == 0 && b == 0 && c == 0 && d == 0 && e == 0) std::cout << "gmsh_assembly_test passed\n";
    return a != 0 ? a : (b != 0 ? b : (c != 0 ? c : (d != 0 ? d : e)));
}
