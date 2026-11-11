#pragma once

#include <OpenModel/CADModel.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

// Assimp import behaviour. coordinateScale converts source coordinates into
// the application's working units (for example, metres to millimetres).
struct AssimpImportOptions {
    double coordinateScale = 1.0;
    bool generateNormals = true;
    bool joinIdenticalVertices = true;
    bool flipUVs = false;
};

// Imports polygonal CAD/exchange formats supported by Assimp. Material support
// intentionally targets glTF's metallic-roughness workflow and diffuse/base color.
class AssimpModel : public CADModel {
private:
    std::string m_filepath;
    std::string m_lastError;
    std::vector<RenderSubMesh> m_renderMeshes;
    AssimpImportOptions m_importOptions;

    static std::string lower(std::string s) ;
    static std::string mimeFromHint(std::string hint) ;
    static std::string mimeFromBytes(const std::vector<unsigned char>& bytes) ;
    static bool readBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes) ;
    static void readTextureTo(const aiScene* scene, const aiString& path, const std::filesystem::path& modelDir,
                              std::vector<unsigned char>& image, std::string& mimeType, std::string& imageName) ;
    static void readTexture(const aiScene* scene, const aiString& path, const std::filesystem::path& modelDir,
                            RenderSubMesh& out) ;
    static void readMaterial(const aiScene* scene, const aiMesh* mesh, const std::filesystem::path& modelDir,
                             RenderSubMesh& out) ;

public:
    AssimpModel() = default;
    ~AssimpModel() override = default;

    bool loadFromFile(const std::string& filepath) override ;
    bool setImportOptions(const AssimpImportOptions& options, std::string* error = nullptr);
    const AssimpImportOptions& getImportOptions() const noexcept { return m_importOptions; }

    void tessellate(double) override ;
    const std::vector<RenderSubMesh>& getRenderMeshes() const override ;
    std::string getFilePath() const override ;
    const std::string& getLastError() const override ;

private:
    void processNode(const aiNode* node, const aiScene* scene, const std::filesystem::path& modelDir,
                     const aiMatrix4x4& parentTransform) ;

    RenderSubMesh processMesh(const aiMesh* mesh, const aiScene* scene, const std::filesystem::path& modelDir,
                              const aiMatrix4x4& transform) ;
};
