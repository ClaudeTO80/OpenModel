#include <OpenModel/OpenModelProject.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "openmodel_project_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "mesh");
    const auto manifest = root / "sample.omproj";
    OpenModel::OpenModelProject p;
    p.metadata.id = "project-001";
    p.metadata.name = "Bracket assembly";
    p.metadata.description = "A project with spaces";
    p.metadata.applicationVersion = "0.1.0";
    { std::ofstream mesh(root / "mesh/volume.mgmb", std::ios::binary); mesh << "abc"; }
    p.addAsset({"mesh-1", "mesh.volume", "mesh/volume.mgmb",
                OpenModel::ProjectAssetStorage::Managed, "application/x-mgmb", 0, ""});
    p.refreshAssetIntegrity(manifest, "mesh-1");
    assert(p.findAsset("mesh-1")->sizeBytes == 3);
    assert(p.findAsset("mesh-1")->sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(p.verifyAssetIntegrity(manifest).empty());
    p.addAsset({"cad-1", "cad.source", "../external/part.step",
                OpenModel::ProjectAssetStorage::Linked, "model/step", 0, ""});
    assert(p.validate(manifest).empty());
    assert(p.resolveAssetPath(manifest, p.assets[0]) == std::filesystem::absolute(root / "mesh/volume.mgmb"));
    p.save(manifest);
    auto q = OpenModel::OpenModelProject::load(manifest);
    assert(q.metadata.name == p.metadata.name);
    assert(q.metadata.description == p.metadata.description);
    assert(q.assets.size() == 2);
    assert(q.verifyAssetIntegrity(manifest).empty());
    assert(q.findAsset("mesh-1") && q.findAsset("mesh-1")->role == "mesh.volume");
    assert(q.resolveAssetPath(manifest, q.assets[1]).filename() == "part.step");
    assert(q.removeAsset("cad-1"));
    assert(!q.removeAsset("not-there"));
    q.save(manifest);
    assert(OpenModel::OpenModelProject::load(manifest).assets.size() == 1);
    { std::ofstream mesh(root / "mesh/volume.mgmb", std::ios::binary | std::ios::trunc); mesh << "abd"; }
    const auto integrityIssues = q.verifyAssetIntegrity(manifest);
    assert(integrityIssues.size() == 1);
    assert(integrityIssues.front().message == "asset SHA-256 mismatch");

    // Non-ASCII asset path ("zàmbo ü", UTF-8 bytes): written as UTF-8 and read back unchanged.
    // On Linux this checks the byte round trip only; the Windows path (wide to UTF-8) is not exercised here.
    const auto utf8Manifest = root / "utf8.omproj";
    const std::filesystem::path accented = std::filesystem::path("dati") / "z\xC3\xA0mbo \xC3\xBC.mgmb";
    std::filesystem::create_directories(root / "dati");
    { std::ofstream file(root / accented, std::ios::binary); file << "xyz"; }
    OpenModel::OpenModelProject utf8;
    utf8.metadata.id = "project-utf8";
    utf8.metadata.name = "UTF-8 paths";
    utf8.metadata.applicationVersion = "0.1.0";
    utf8.addAsset({"utf8-1", "mesh.volume", accented, OpenModel::ProjectAssetStorage::Managed, "application/x-mgmb", 0, ""});
    utf8.refreshAssetIntegrity(utf8Manifest, "utf8-1");
    utf8.save(utf8Manifest);
    const auto utf8Back = OpenModel::OpenModelProject::load(utf8Manifest);
    assert(utf8Back.findAsset("utf8-1") && utf8Back.findAsset("utf8-1")->path == accented);
    assert(utf8Back.verifyAssetIntegrity(utf8Manifest).empty());

    OpenModel::OpenModelProject invalid = p;
    invalid.assets[0].path = "../../outside.mgmb";
    assert(!invalid.validate(manifest).empty());
    bool duplicateRejected = false;
    try { invalid.addAsset(invalid.assets[0]); } catch (const std::invalid_argument&) { duplicateRejected = true; }
    assert(duplicateRejected);

    const auto bad = root / "bad.omproj";
    { std::ofstream out(bad); out << "OPENMODEL_PROJECT 9 0\n"; }
    bool versionRejected = false;
    try { (void)OpenModel::OpenModelProject::load(bad); } catch (const std::runtime_error&) { versionRejected = true; }
    assert(versionRejected);
    std::filesystem::remove_all(root);
}
