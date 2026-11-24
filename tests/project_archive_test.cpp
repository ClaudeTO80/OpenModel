/**
 * @file project_archive_test.cpp
 * @brief Tests of the stored ZIP project archive: round trip of the manifest, the MGOM mesh, the analysis script with
 *        its jobs, steps and loads, and a linked result file; and refusal of corrupted, compressed, unsafe or
 *        overwriting archives.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/ModelBinaryIO.hpp>
#include <OpenModel/OpenModelProject.hpp>
#include <OpenModel/ProjectArchive.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace OpenModel;

namespace {

int failures = 0;
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                               \
        }                                                                             \
    } while (0)

template <class Fn> bool throwsArchive(Fn&& fn) {
    try { fn(); }
    catch (const ProjectArchiveError& error) { std::fprintf(stderr, "  refused: %s\n", error.what()); return true; }
    catch (...) { return false; }
    return false;
}

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void spill(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Script with two jobs. The first job settles under one load; the second adds a load and resets the first one.
const char* kScript =
    "material E=210000 nu=0.3 rho=7.85e-9\n"
    "fix plane:x=0\n"
    "force id=F1 plane:x=10 F=0,-100,0\n"
    "job SETTLE\n"
    "step static loads=F1\n"
    "job PULL\n"
    "step static loads=F1\n";

fs::path freshDirectory(const std::string& name) {
    const auto root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// Project with a mesh (MGOM), the analysis script as a managed text asset, and a linked result file outside the root.
fs::path makeProject(const fs::path& root, const fs::path& outside) {
    fs::create_directories(root / "mesh");
    fs::create_directories(root / "analysis");
    ModelBinaryIO::write(root / "mesh/volume.omb", MeshData{});
    spill(root / "analysis/model.txt", kScript);
    spill(outside / "solver.frd", "linked result payload");

    OpenModelProject project;
    project.metadata = {"project-archive", "Archive test", "", "0.1.2", "2026-11-24T00:00:00Z", "2026-11-24T00:00:00Z"};
    project.addAsset({"mesh-1", "mesh.volume", "mesh/volume.omb", ProjectAssetStorage::Managed, "application/x-omb", 0, ""});
    project.addAsset({"analysis-1", "analysis.model", "analysis/model.txt", ProjectAssetStorage::Managed, "text/plain", 0, ""});
    project.addAsset({"result-1", "results.solver", (outside / "solver.frd").string(), ProjectAssetStorage::Linked,
                      "application/octet-stream", 0, ""});
    const auto manifest = root / "sample.omproj";
    project.save(manifest);
    project.refreshAssetIntegrity(manifest, "mesh-1");
    project.refreshAssetIntegrity(manifest, "analysis-1");
    project.refreshAssetIntegrity(manifest, "result-1");
    project.save(manifest);
    return manifest;
}

void testRoundTrip() {
    const auto root = freshDirectory("openmodel_archive_test_src");
    const auto outside = freshDirectory("openmodel_archive_test_outside");
    const auto manifest = makeProject(root, outside);
    const auto archive = root / "sample.zip";
    exportProjectArchive(manifest, archive);

    // Stored, not compressed: the script and the linked payload appear verbatim in the file.
    const auto zipBytes = slurp(archive);
    CHECK(zipBytes.compare(0, 4, "PK\x03\x04") == 0);
    CHECK(zipBytes.find(kScript) != std::string::npos);
    CHECK(zipBytes.find("linked result payload") != std::string::npos);

    const auto destination = fs::temp_directory_path() / "openmodel_archive_test_dst";
    fs::remove_all(destination);
    const auto imported = importProjectArchive(archive, destination);
    CHECK(imported == destination / kProjectArchiveManifestName);

    const auto project = OpenModelProject::load(imported);
    CHECK(project.metadata.name == "Archive test" && project.metadata.applicationVersion == "0.1.2");
    CHECK(project.assets.size() == 3);
    CHECK(project.verifyAssetIntegrity(imported).empty());
    for (const auto& asset : project.assets) CHECK(asset.storage == ProjectAssetStorage::Managed);

    // The analysis script comes back byte for byte, and its jobs, steps and loads are intact.
    const auto* scriptAsset = project.findAsset("analysis-1");
    CHECK(scriptAsset != nullptr);
    if (scriptAsset) {
        const auto text = slurp(project.resolveAssetPath(imported, *scriptAsset));
        CHECK(text == kScript);
        const auto parsed = parseAnalysisScript(text);
        for (const auto& d : parsed.diagnostics) std::fprintf(stderr, "script: %s %s\n", d.code.c_str(), d.message.c_str());
        CHECK(parsed.success);
        CHECK(parsed.model.jobs.size() == 2);
        if (parsed.model.jobs.size() == 2) {
            CHECK(parsed.model.jobs[0].id == "SETTLE" && parsed.model.jobs[1].id == "PULL");
            CHECK(parsed.model.jobs[0].steps.size() == 1 && parsed.model.jobs[1].steps.size() == 1);
            CHECK(parsed.model.jobs[0].steps[0].loads == std::vector<std::string>{"F1"});
        }
    }

    // The linked result was copied into the archive and is restored inside the project.
    const auto* resultAsset = project.findAsset("result-1");
    CHECK(resultAsset != nullptr);
    if (resultAsset) CHECK(slurp(project.resolveAssetPath(imported, *resultAsset)) == "linked result payload");

    // The mesh is an MGOM file that still loads.
    CHECK(ModelBinaryIO::read(destination / "assets/mesh-1/volume.omb").isEmpty());

    fs::remove_all(root);
    fs::remove_all(outside);
    fs::remove_all(destination);
}

void testRefusals() {
    const auto root = freshDirectory("openmodel_archive_test_refuse");
    const auto outside = freshDirectory("openmodel_archive_test_refuse_out");
    const auto manifest = makeProject(root, outside);
    const auto archive = root / "good.zip";
    exportProjectArchive(manifest, archive);
    const auto good = slurp(archive);

    // A managed asset missing on disk: nothing is written.
    fs::remove(root / "analysis/model.txt");
    CHECK(throwsArchive([&] { exportProjectArchive(manifest, root / "missing.zip"); }));
    CHECK(!fs::exists(root / "missing.zip"));
    spill(root / "analysis/model.txt", kScript);

    // An asset whose recorded digest no longer matches: refused, the archive is not created.
    spill(root / "analysis/model.txt", std::string(kScript) + "# changed\n");
    CHECK(throwsArchive([&] { exportProjectArchive(manifest, root / "changed.zip"); }));
    CHECK(!fs::exists(root / "changed.zip"));
    spill(root / "analysis/model.txt", kScript);

    // A corrupted byte in the stored script is caught by the CRC.
    {
        auto bad = good;
        const auto at = bad.find("job SETTLE");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) bad[at] = 'X';
        spill(root / "bad_crc.zip", bad);
        const auto destination = root / "dst_crc";
        CHECK(throwsArchive([&] { importProjectArchive(root / "bad_crc.zip", destination); }));
        CHECK(!fs::exists(destination));
    }

    // Unsafe entry names: "assets/" replaced by "../ets/" (same length, so offsets stay valid) in both headers.
    {
        auto bad = good;
        std::size_t replaced = 0;
        for (std::size_t at = bad.find("assets/"); at != std::string::npos; at = bad.find("assets/", at + 7)) {
            bad.replace(at, 7, "../ets/");
            ++replaced;
        }
        CHECK(replaced >= 2);
        spill(root / "traversal.zip", bad);
        CHECK(throwsArchive([&] { importProjectArchive(root / "traversal.zip", root / "dst_traversal"); }));
        CHECK(!fs::exists(root / "dst_traversal"));
        CHECK(!fs::exists(root / "ets"));
    }

    // A compressed entry (method 8 in the central directory) is refused.
    {
        auto bad = good;
        const auto at = bad.find("PK\x01\x02");
        CHECK(at != std::string::npos);
        if (at != std::string::npos) bad[at + 10] = 8;
        spill(root / "deflated.zip", bad);
        CHECK(throwsArchive([&] { importProjectArchive(root / "deflated.zip", root / "dst_deflated"); }));
    }

    // Importing over an existing project is refused and leaves that project alone.
    {
        const auto destination = root / "existing";
        fs::create_directories(destination);
        spill(destination / kProjectArchiveManifestName, "OPENMODEL_PROJECT 1 0\n");
        CHECK(throwsArchive([&] { importProjectArchive(archive, destination); }));
        CHECK(slurp(destination / kProjectArchiveManifestName) == "OPENMODEL_PROJECT 1 0\n");
    }

    // An asset ID that cannot be a path component is refused at export.
    {
        auto project = OpenModelProject::load(manifest);
        project.assets[0].id = "../escape";
        const auto evil = root / "evil.omproj";
        project.save(evil);
        CHECK(throwsArchive([&] { exportProjectArchive(evil, root / "evil.zip"); }));
    }

    // Not an archive at all.
    spill(root / "text.zip", "plain text, not a zip file at all");
    CHECK(throwsArchive([&] { importProjectArchive(root / "text.zip", root / "dst_text"); }));

    fs::remove_all(root);
    fs::remove_all(outside);
}

} // namespace

int main() {
    testRoundTrip();
    testRefusals();
    if (failures == 0) std::printf("project_archive_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
