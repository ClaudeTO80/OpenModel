#include <OpenModel/CADFaceMapper.hpp>
#include <OpenModel/GmshMesher.hpp>
#include <OpenModel/OCCModel.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
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
double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}
bool hasAudit(const GmshMesher& mesher) {
    for (const auto& line : mesher.diagnostics())
        if (line.find("CAD face mapping audit") != std::string::npos) return true;
    return false;
}
}

/**
 * Faces mapped on demand on a finished mesh must be exactly the nodes the Gmsh audit puts in a face group,
 * for every face of the model, and a mesh built without the audit must carry no face mapping at all.
 */
int main(int argc, char** argv) {
    CHECK(argc == 2);
    auto cad = std::make_shared<OCCModel>();
    CHECK(cad->loadFromFile(argv[1]));
    cad->tessellate(0.5);
    std::set<int> faceSet;
    for (const auto& part : cad->getRenderMeshes()) if (part.sourceFaceId >= 0) faceSet.insert(part.sourceFaceId);
    CHECK(!faceSet.empty());

    MeshingOptions options;
    options.dimension = MeshDimension::Volume;
    options.generateTetrahedra = true;
    options.elementSize = 12.0;
    options.maxElementSize = 12.0;
    options.order = 1;
    options.importCadGroups = true;

    // Without the audit and without groups nothing is mapped.
    {
        options.auditAllCadFaces = false;
        options.importCadGroups = false;   // a native face group stored in the STEP would need the mapping
        GmshMesher mesher;
        MeshData mesh;
        const auto start = std::chrono::steady_clock::now();
        CHECK(mesher.buildMesh(cad, options, mesh));
        std::cout << "meshing without audit: " << seconds(start) << " s\n";
        CHECK(!hasAudit(mesher));
        CHECK(mesher.faceMappingReport().empty());
    }

    // With one group per CAD face, compare the audit's node sets with the on-demand ones.
    options.auditAllCadFaces = true;
    options.importCadGroups = true;
    for (const int id : faceSet)
        options.additionalFaceGroups.push_back(CADGroup{"F" + std::to_string(id), CADGroupKind::NamedShape, 2, {id}});
    GmshMesher mesher;
    MeshData mesh;
    const auto start = std::chrono::steady_clock::now();
    CHECK(mesher.buildMesh(cad, options, mesh));
    std::cout << "meshing with audit and " << faceSet.size() << " groups: " << seconds(start) << " s\n";
    CHECK(hasAudit(mesher));

    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300, zmin = 1e300, zmax = -1e300;
    for (NodeIndex n = 0; n < mesh.getNumNodes(); ++n) {
        const auto p = mesh.nodeCoordinates(n);
        xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
        ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
        zmin = std::min(zmin, p[2]); zmax = std::max(zmax, p[2]);
    }
    const double diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) + (zmax - zmin) * (zmax - zmin));

    const auto topologyStart = std::chrono::steady_clock::now();
    MeshTopology topology;
    topology.build(mesh);
    double onDemand = seconds(topologyStart);
    std::cout << "topology of " << mesh.getNumElements() << " elements: " << onDemand << " s\n";
    for (const int id : faceSet) {
        const auto* group = mesh.getMeshGroupRegistry().find("F" + std::to_string(id));
        CHECK(group != nullptr);
        std::vector<NodeIndex> expected = group->nodeIndices;
        std::sort(expected.begin(), expected.end());
        expected.erase(std::unique(expected.begin(), expected.end()), expected.end());

        const auto t = std::chrono::steady_clock::now();
        const auto mapped = mapCADFacesToMesh(*cad, mesh, topology, {id}, 1e-6 * diagonal);
        onDemand += seconds(t);
        if (mapped.nodes != expected) {
            std::cerr << "face " << id << ": audit " << expected.size() << " nodes, on demand " << mapped.nodes.size() << " nodes\n";
            return 1;
        }
    }
    std::cout << faceSet.size() << " faces identical to the audit; on-demand mapping of all of them one by one, topology included: "
              << onDemand << " s\n";
    return 0;
}
