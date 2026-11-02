#include <OpenModel/GltfWriter.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main() {
    SurfaceMesh mesh;
    SurfaceMeshPart p;
    p.name = "Pawn \"body\"";
    p.positions = {0,0,0, 1,0,0, 0,1,0};
    p.texcoords = {0,0, 1,0, 0,1};
    p.baseColorFactor = {0.2f,0.4f,0.8f,1.0f};
    p.metallicFactor = 0.25f;
    p.roughnessFactor = 0.35f;
    p.indices = {0,1,2};
    mesh.parts.push_back(p);

    const std::string path = "gltf_writer_test_output.gltf";
    const auto result = GltfWriter::writeFile(path, mesh);
    assert(result.success && result.meshesWritten == 1 && result.verticesWritten == 3 && result.trianglesWritten == 1);
    std::ifstream in(path, std::ios::binary);
    const std::string json((std::istreambuf_iterator<char>(in)), {});
    assert(json.find("\"version\":\"2.0\"") != std::string::npos);
    assert(json.find("\"POSITION\":0") != std::string::npos);
    assert(json.find("\"NORMAL\":1") != std::string::npos);
    assert(json.find("\"TEXCOORD_0\":2") != std::string::npos);
    assert(json.find("\"indices\":3") != std::string::npos);
    assert(json.find("Pawn \\\"body\\\"") != std::string::npos);
    assert(json.find("data:application/octet-stream;base64,") != std::string::npos);
    assert(json.find("0.200000003,0.400000006,0.800000012,1") != std::string::npos);
    in.close(); std::remove(path.c_str());

    const std::string glbPath = "gltf_writer_test_output.glb";
    const auto glb = GltfWriter::writeGlbFile(glbPath, mesh);
    assert(glb.success && glb.bytesWritten > 20);
    std::ifstream gin(glbPath, std::ios::binary);
    char header[12]{}; gin.read(header,12);
    assert(gin.gcount()==12 && header[0]=='g' && header[1]=='l' && header[2]=='T' && header[3]=='F');
    assert(static_cast<unsigned char>(header[4])==2);
    gin.close(); std::remove(glbPath.c_str());

    // A real 1x1 PNG is embedded as a glTF image and referenced by the PBR base-color texture.
    SurfaceMesh textured;
    SurfaceMeshPart tp;
    tp.name = "TexturedPart";
    tp.positions = {0,0,0, 1,0,0, 0,1,0};
    tp.texcoords = {0,0, 1,0, 0,1};
    tp.indices = {0,1,2};
    tp.baseColorImageMimeType = "image/png";
    tp.baseColorImageName = "sample.png";
    tp.baseColorImage = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,4,0,0,0,181,28,12,2,0,0,0,11,73,68,65,84,120,218,99,252,255,31,0,3,3,2,0,239,191,105,0,0,0,0,73,69,78,68,174,66,96,130};
    textured.parts.push_back(tp);
    const auto texturedResult = GltfWriter::writeFile(path, textured);
    assert(texturedResult.success);
    std::ifstream tin(path, std::ios::binary);
    const std::string tj((std::istreambuf_iterator<char>(tin)), {});
    assert(tj.find("\"images\":[{\"name\":\"sample.png\",\"uri\":\"data:image/png;base64,") != std::string::npos);
    assert(tj.find("\"textures\":[{\"source\":0}") != std::string::npos);
    assert(tj.find("\"baseColorTexture\":{\"index\":0}") != std::string::npos);
    // Phase 18: all glTF metallic-roughness material texture slots and alpha flags.
    textured.parts[0].normalImage = textured.parts[0].baseColorImage;
    textured.parts[0].normalImageMimeType = "image/png";
    textured.parts[0].normalImageName = "normal.png";
    textured.parts[0].metallicRoughnessImage = textured.parts[0].baseColorImage;
    textured.parts[0].metallicRoughnessImageMimeType = "image/png";
    textured.parts[0].metallicRoughnessImageName = "mr.png";
    textured.parts[0].emissiveImage = textured.parts[0].baseColorImage;
    textured.parts[0].emissiveImageMimeType = "image/png";
    textured.parts[0].emissiveImageName = "emissive.png";
    textured.parts[0].emissiveFactor = {{0.1f, 0.2f, 0.3f}};
    textured.parts[0].alphaMode = "BLEND";
    textured.parts[0].doubleSided = true;
    const auto pbrResult = GltfWriter::writeFile(path, textured);
    assert(pbrResult.success);
    std::ifstream pin(path, std::ios::binary);
    const std::string pj((std::istreambuf_iterator<char>(pin)), {});
    assert(pj.find("\"normalTexture\":{\"index\":1}") != std::string::npos);
    assert(pj.find("\"metallicRoughnessTexture\":{\"index\":2}") != std::string::npos);
    assert(pj.find("\"emissiveTexture\":{\"index\":3}") != std::string::npos);
    assert(pj.find("\"alphaMode\":\"BLEND\"") != std::string::npos);
    assert(pj.find("\"doubleSided\":true") != std::string::npos);
    assert(pj.find("\"emissiveFactor\":[") != std::string::npos);
    pin.close(); std::remove(path.c_str());
    textured.parts[0].alphaMode = "INVALID";
    assert(!GltfWriter::writeFile(path, textured).success);
    textured.parts[0].alphaMode = "OPAQUE";
    tin.close(); std::remove(path.c_str());
    const auto texturedGlb = GltfWriter::writeGlbFile(glbPath, textured);
    assert(texturedGlb.success);
    std::ifstream tgin(glbPath, std::ios::binary);
    const std::string tg((std::istreambuf_iterator<char>(tgin)), {});
    assert(tg.find("data:image/png;base64,") != std::string::npos);
    tgin.close(); std::remove(glbPath.c_str());

    SurfaceMesh invalidTexture = textured;
    invalidTexture.parts[0].texcoords.clear();
    assert(!GltfWriter::writeFile(path, invalidTexture).success);
    invalidTexture = textured;
    invalidTexture.parts[0].baseColorImageMimeType = "image/gif";
    assert(!GltfWriter::writeFile(path, invalidTexture).success);

    SurfaceMesh empty;
    assert(!GltfWriter::writeFile(path, empty).success);
    SurfaceMesh invalid; SurfaceMeshPart q; q.name="bad";q.positions={0,0,0};q.indices={0,1,2};invalid.parts.push_back(q);
    assert(!GltfWriter::writeFile(path, invalid).success);
}
