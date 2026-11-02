#include <OpenModel/MeshImporter.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto path = std::filesystem::temp_directory_path() / "openmodel_importer_fixture.inp";
    {
        std::ofstream out(path);
        out << "*HEADING\nFixture\n"
            << "*NODE\n"
            << "10, 0, 0, 0\n20, 1, 0, 0\n30, 0, 1, 0\n40, 0, 0, 1\n"
            << "*NSET, NSET=FIXED\n10, 20\n"
            << "*ELEMENT, TYPE=C3D4, ELSET=VOLUME\n100, 10, 20, 30, 40\n"
            << "*ELSET, ELSET=ALL_ELEMENTS\n100\n";
    }

    OpenModel::AbaqusInpImporter abaqus;
    OpenModel::CalculixInpImporter calculix;
    assert(abaqus.canRead(path));
    assert(calculix.canRead(path));
    assert(abaqus.formatName() == "Abaqus INP");
    assert(calculix.formatName() == "CalculiX INP");

    MeshData mesh;
    const auto result = abaqus.read(path, mesh);
    assert(result);
    assert(mesh.getNumNodes() == 4);
    assert(mesh.getNumElements() == 1);
    assert(mesh.nodeId(mesh.nodeIndex(30)) == 30);
    assert(mesh.getNodeSets().at("FIXED").size() == 2);
    assert(mesh.getPhysicalGroups().size() == 1);
    assert(mesh.getPhysicalGroups().front().name == "ALL_ELEMENTS");

    MeshData calcMesh;
    const auto calcResult = calculix.read(path, calcMesh);
    assert(calcResult);
    assert(calcMesh.getNumNodes() == mesh.getNumNodes());
    assert(calcMesh.getNumElements() == mesh.getNumElements());

    // Failed import is transactional: destination remains unchanged.
    {
        std::ofstream out(path);
        out << "*NODE\n1, 0, 0, 0\n*ELEMENT, TYPE=UNSUPPORTED\n1, 1\n";
    }
    const auto failed = abaqus.read(path, mesh);
    assert(!failed);
    assert(mesh.getNumNodes() == 4);
    assert(mesh.getNumElements() == 1);

    std::filesystem::remove(path);
    return 0;
}
