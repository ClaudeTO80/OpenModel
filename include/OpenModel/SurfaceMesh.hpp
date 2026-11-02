#pragma once

#include <OpenModel/Mesher.hpp>
#include <OpenModel/CADModel.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// Render-oriented surface representation, independent of FEM volume elements.
// Positions/normals are packed xyz float arrays; triangle indices are 0-based.
struct SurfaceMeshPart {
    std::string name;
    int sourcePartId = -1;
    int sourceFaceId = -1;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords; // optional packed uv pairs, two floats per vertex
    std::array<float, 4> baseColorFactor{{0.8f, 0.8f, 0.8f, 1.0f}};
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.8f;
    // Optional embedded glTF-compatible textures. PNG/JPEG only.
    std::vector<unsigned char> baseColorImage;
    std::string baseColorImageMimeType;
    std::string baseColorImageName;
    std::vector<unsigned char> normalImage;
    std::string normalImageMimeType;
    std::string normalImageName;
    std::vector<unsigned char> metallicRoughnessImage;
    std::string metallicRoughnessImageMimeType;
    std::string metallicRoughnessImageName;
    std::vector<unsigned char> emissiveImage;
    std::string emissiveImageMimeType;
    std::string emissiveImageName;
    std::array<float, 3> emissiveFactor{{0.0f, 0.0f, 0.0f}};
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    std::vector<unsigned int> indices;
};

struct SurfaceMesh {
    std::vector<SurfaceMeshPart> parts;

    void clear() ;
    bool empty() const noexcept ;
    std::size_t vertexCount() const noexcept ;
    std::size_t triangleCount() const noexcept ;
};

struct SurfaceMeshReport {
    bool ok = false;
    std::string error;
    std::size_t parts = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
};

class SurfaceMesher {
public:
    SurfaceMeshReport build(const std::shared_ptr<CADModel>& cadModel,
                            const MeshingOptions& options,
                            SurfaceMesh& output) const ;
};
