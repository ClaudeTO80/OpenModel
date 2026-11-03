#include <OpenModel/AssimpModel.hpp>

#include <stdexcept>
#include <limits>

std::string AssimpModel::lower(std::string s) {

        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    
}

std::string AssimpModel::mimeFromHint(std::string hint) {

        hint = lower(std::move(hint));
        if (hint == "png" || hint == ".png" || hint == "image/png") return "image/png";
        if (hint == "jpg" || hint == "jpeg" || hint == ".jpg" || hint == ".jpeg" || hint == "image/jpeg") return "image/jpeg";
        return {};
    
}

std::string AssimpModel::mimeFromBytes(const std::vector<unsigned char>& bytes) {

        if (bytes.size() >= 8 && bytes[0] == 137 && bytes[1] == 80 && bytes[2] == 78 && bytes[3] == 71 && bytes[4] == 13 && bytes[5] == 10 && bytes[6] == 26 && bytes[7] == 10) return "image/png";
        if (bytes.size() >= 3 && bytes[0] == 255 && bytes[1] == 216 && bytes[2] == 255) return "image/jpeg";
        return {};
    
}

bool AssimpModel::readBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes) {

        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad() && !bytes.empty();
    
}

void AssimpModel::readTextureTo(const aiScene* scene, const aiString& path, const std::filesystem::path& modelDir, std::vector<unsigned char>& image, std::string& mimeType, std::string& imageName) {

        const std::string texturePath = path.C_Str();
        if (texturePath.empty()) return;
        if (texturePath[0] == '*') {
            const aiTexture* texture = scene ? scene->GetEmbeddedTexture(texturePath.c_str()) : nullptr;
            if (!texture || texture->mHeight != 0 || texture->mWidth == 0 || !texture->pcData) return;
            const auto* begin = reinterpret_cast<const unsigned char*>(texture->pcData);
            image.assign(begin, begin + texture->mWidth);
            mimeType = mimeFromHint(texture->achFormatHint);
            if (mimeType.empty()) mimeType = mimeFromBytes(image);
            if (mimeType.empty()) image.clear();
            else imageName = texture->mFilename.length ? texture->mFilename.C_Str() : "embedded_texture";
            return;
        }
        std::filesystem::path resolved(texturePath);
        if (resolved.is_relative()) resolved = modelDir / resolved;
        if (!readBytes(resolved, image)) return;
        mimeType = mimeFromBytes(image);
        if (mimeType.empty()) { image.clear(); return; }
        imageName = resolved.filename().string();
    
}

void AssimpModel::readTexture(const aiScene* scene, const aiString& path, const std::filesystem::path& modelDir, RenderSubMesh& out) {

        readTextureTo(scene, path, modelDir, out.baseColorImage, out.baseColorImageMimeType, out.baseColorImageName);
    
}

void AssimpModel::readMaterial(const aiScene* scene, const aiMesh* mesh, const std::filesystem::path& modelDir, RenderSubMesh& out) {

        if (!scene || !mesh || mesh->mMaterialIndex >= scene->mNumMaterials) return;
        const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
        aiColor4D color(0.8f, 0.8f, 0.8f, 1.0f);
#if ASSIMP_VERSION_MAJOR >= 5
        if (aiGetMaterialColor(mat, AI_MATKEY_BASE_COLOR, &color) != AI_SUCCESS)
#endif
        {
            aiColor3D diffuse(0.8f, 0.8f, 0.8f);
            if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
                color.r = diffuse.r; color.g = diffuse.g; color.b = diffuse.b;
            }
            float opacity = 1.0f;
            if (mat->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) color.a = opacity;
        }
        const auto safeColor=[](float component) {
            return std::isfinite(component) ? std::max(0.0f,std::min(1.0f,component)) : 0.8f;
        };
        out.baseColorFactor = {{safeColor(color.r),safeColor(color.g),safeColor(color.b),safeColor(color.a)}};
        float value = 0.0f;
        if (mat->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS && std::isfinite(value))
            out.metallicFactor = std::max(0.0f, std::min(1.0f, value));
        value = 0.8f;
        if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS && std::isfinite(value))
            out.roughnessFactor = std::max(0.0f, std::min(1.0f, value));
        aiColor3D emissive(0.0f, 0.0f, 0.0f);
        if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
            const auto safeEmissive=[](float component) {
                return std::isfinite(component) ? std::max(0.0f,component) : 0.0f;
            };
            out.emissiveFactor = {{safeEmissive(emissive.r),safeEmissive(emissive.g),safeEmissive(emissive.b)}};
        }
        int twoSided = 0;
        if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) out.doubleSided = twoSided != 0;
        aiString alphaMode;
        if (mat->Get("$mat.gltf.alphaMode", 0, 0, alphaMode) == AI_SUCCESS) {
            const std::string mode = alphaMode.C_Str();
            if (mode == "OPAQUE" || mode == "MASK" || mode == "BLEND") out.alphaMode = mode;
        }
        if (mat->Get("$mat.gltf.alphaCutoff", 0, 0, value) == AI_SUCCESS && std::isfinite(value))
            out.alphaCutoff = std::max(0.0f, std::min(1.0f, value));

        aiString texturePath;
        // DIFFUSE is widely supported by Assimp; glTF base-color textures are
        // exposed through this compatibility texture slot by many importers.
        if (mat->GetTextureCount(aiTextureType_BASE_COLOR) > 0 &&
            mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) == AI_SUCCESS) {
            readTexture(scene, texturePath, modelDir, out);
        } else if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0 &&
                   mat->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == AI_SUCCESS) {
            readTexture(scene, texturePath, modelDir, out);
        }
        if (mat->GetTextureCount(aiTextureType_NORMALS) > 0 && mat->GetTexture(aiTextureType_NORMALS, 0, &texturePath) == AI_SUCCESS)
            readTextureTo(scene, texturePath, modelDir, out.normalImage, out.normalImageMimeType, out.normalImageName);
        if (mat->GetTextureCount(aiTextureType_EMISSIVE) > 0 && mat->GetTexture(aiTextureType_EMISSIVE, 0, &texturePath) == AI_SUCCESS)
            readTextureTo(scene, texturePath, modelDir, out.emissiveImage, out.emissiveImageMimeType, out.emissiveImageName);
#if ASSIMP_VERSION_MAJOR >= 5
        // Assimp exposes the packed glTF metallic-roughness map as UNKNOWN in some importers.
        if (mat->GetTextureCount(aiTextureType_UNKNOWN) > 0 && mat->GetTexture(aiTextureType_UNKNOWN, 0, &texturePath) == AI_SUCCESS)
            readTextureTo(scene, texturePath, modelDir, out.metallicRoughnessImage, out.metallicRoughnessImageMimeType, out.metallicRoughnessImageName);
#endif
    
}

bool AssimpModel::setImportOptions(const AssimpImportOptions& options, std::string* error) {
    if (!std::isfinite(options.coordinateScale) || options.coordinateScale <= 0.0) {
        if (error) *error = "Assimp coordinateScale must be finite and positive.";
        return false;
    }
    m_importOptions = options;
    if (error) error->clear();
    return true;
}

bool AssimpModel::loadFromFile(const std::string& filepath)  {

        m_filepath = filepath;
        m_lastError.clear();
        m_renderMeshes.clear();
        Assimp::Importer importer;
        unsigned int importFlags = aiProcess_Triangulate | aiProcess_ValidateDataStructure;
        if (m_importOptions.generateNormals) importFlags |= aiProcess_GenSmoothNormals;
        if (m_importOptions.joinIdenticalVertices) importFlags |= aiProcess_JoinIdenticalVertices;
        if (m_importOptions.flipUVs) importFlags |= aiProcess_FlipUVs;
        const aiScene* scene = importer.ReadFile(filepath, importFlags);
        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            m_lastError = importer.GetErrorString();
            if (m_lastError.empty()) m_lastError = "Assimp could not load the model.";
            return false;
        }
        try {
            processNode(scene->mRootNode, scene, std::filesystem::path(filepath).parent_path(), aiMatrix4x4());
        } catch (const std::exception& error) {
            m_renderMeshes.clear();
            m_lastError = error.what();
            return false;
        }
        if (m_renderMeshes.empty()) {
            m_lastError = "The imported scene contains no triangle meshes.";
            return false;
        }
        return true;
    
}

void AssimpModel::tessellate(double)  {

}

const std::vector<RenderSubMesh>& AssimpModel::getRenderMeshes() const  {
 return m_renderMeshes; 
}

std::string AssimpModel::getFilePath() const  {
 return m_filepath; 
}

const std::string& AssimpModel::getLastError() const  {
 return m_lastError; 
}

void AssimpModel::processNode(const aiNode* node, const aiScene* scene, const std::filesystem::path& modelDir, const aiMatrix4x4& parentTransform) {

        const aiMatrix4x4 transform = parentTransform * node->mTransformation;
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            const aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            auto imported = processMesh(mesh, scene, modelDir, transform);
            if (!imported.indices.empty()) m_renderMeshes.push_back(std::move(imported));
        }
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], scene, modelDir, transform);
    
}

RenderSubMesh AssimpModel::processMesh(const aiMesh* mesh, const aiScene* scene,
    const std::filesystem::path& modelDir, const aiMatrix4x4& transform) {
    if (!mesh) throw std::runtime_error("Assimp returned a null mesh.");

    RenderSubMesh out;
    out.name = mesh->mName.length ? mesh->mName.C_Str() : "UnnamedMesh";
    const unsigned int nv = mesh->mNumVertices;
    if (nv == 0) throw std::runtime_error("Assimp mesh '" + out.name + "' has no vertices.");

    aiMatrix3x3 normalTransform(transform);
    const double determinant = static_cast<double>(normalTransform.Determinant());
    if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-15)
        throw std::runtime_error("Assimp mesh '" + out.name + "' has a singular or invalid node transform.");
    const bool mirrored = determinant < 0.0;
    normalTransform.Inverse().Transpose();

    out.vertices.reserve(static_cast<std::size_t>(nv) * 3);
    out.normals.reserve(static_cast<std::size_t>(nv) * 3);
    if (mesh->HasTextureCoords(0)) out.texcoords.reserve(static_cast<std::size_t>(nv) * 2);
    for (unsigned int i = 0; i < nv; ++i) {
        const aiVector3D transformed = transform * mesh->mVertices[i];
        const double x = static_cast<double>(transformed.x) * m_importOptions.coordinateScale;
        const double y = static_cast<double>(transformed.y) * m_importOptions.coordinateScale;
        const double z = static_cast<double>(transformed.z) * m_importOptions.coordinateScale;
        const double floatLimit = std::numeric_limits<float>::max();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            std::abs(x) > floatLimit || std::abs(y) > floatLimit || std::abs(z) > floatLimit)
            throw std::runtime_error("Assimp mesh '" + out.name + "' produced an invalid scaled vertex.");
        out.vertices.insert(out.vertices.end(), {
            static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});

        if (mesh->HasNormals()) {
            aiVector3D normal = normalTransform * mesh->mNormals[i];
            const double normalLength = std::sqrt(static_cast<double>(normal.x) * normal.x +
                static_cast<double>(normal.y) * normal.y + static_cast<double>(normal.z) * normal.z);
            if (!std::isfinite(normalLength) || normalLength <= 1e-15)
                throw std::runtime_error("Assimp mesh '" + out.name + "' contains an invalid transformed normal.");
            normal /= static_cast<ai_real>(normalLength);
            out.normals.insert(out.normals.end(), {normal.x, normal.y, normal.z});
        }
        if (mesh->HasTextureCoords(0))
            out.texcoords.insert(out.texcoords.end(), {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y});
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
        const aiFace& face = mesh->mFaces[i];
        if (face.mNumIndices != 3) continue;
        for (unsigned int j = 0; j < 3; ++j) {
            const unsigned int sourceIndex = mirrored && j != 0 ? face.mIndices[3-j] : face.mIndices[j];
            if (sourceIndex >= nv)
                throw std::runtime_error("Assimp mesh '" + out.name + "' contains an out-of-range triangle index.");
            out.indices.push_back(sourceIndex);
        }
    }
    readMaterial(scene, mesh, modelDir, out);
    return out;
}

