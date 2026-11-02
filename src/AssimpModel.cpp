#include <OpenModel/AssimpModel.hpp>

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
            if (!texture || texture->mHeight != 0 || texture->mWidth == 0) return;
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
        out.baseColorFactor = {{color.r, color.g, color.b, color.a}};
        float value = 0.0f;
        if (mat->Get(AI_MATKEY_METALLIC_FACTOR, value) == AI_SUCCESS) out.metallicFactor = value;
        value = 0.8f;
        if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, value) == AI_SUCCESS) out.roughnessFactor = value;
        aiColor3D emissive(0.0f, 0.0f, 0.0f);
        if (mat->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
            out.emissiveFactor = {{emissive.r, emissive.g, emissive.b}};
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

bool AssimpModel::loadFromFile(const std::string& filepath)  {

        m_filepath = filepath;
        m_lastError.clear();
        m_renderMeshes.clear();
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(filepath,
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_JoinIdenticalVertices |
            aiProcess_ValidateDataStructure);
        if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
            m_lastError = importer.GetErrorString();
            if (m_lastError.empty()) m_lastError = "Assimp could not load the model.";
            return false;
        }
        processNode(scene->mRootNode, scene, std::filesystem::path(filepath).parent_path(), aiMatrix4x4());
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
            m_renderMeshes.push_back(processMesh(mesh, scene, modelDir, transform));
        }
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            processNode(node->mChildren[i], scene, modelDir, transform);
    
}

RenderSubMesh AssimpModel::processMesh(const aiMesh* mesh, const aiScene* scene, const std::filesystem::path& modelDir, const aiMatrix4x4& transform) {

        RenderSubMesh out;
        out.name = mesh->mName.length ? mesh->mName.C_Str() : "UnnamedMesh";
        const unsigned int nv = mesh->mNumVertices;
        out.vertices.reserve(static_cast<std::size_t>(nv) * 3);
        out.normals.reserve(static_cast<std::size_t>(nv) * 3);
        if (mesh->HasTextureCoords(0)) out.texcoords.reserve(static_cast<std::size_t>(nv) * 2);
        aiMatrix3x3 normalTransform(transform);
        normalTransform.Inverse().Transpose();
        for (unsigned int i = 0; i < nv; ++i) {
            aiVector3D position = transform * mesh->mVertices[i];
            out.vertices.insert(out.vertices.end(), {position.x, position.y, position.z});
            if (mesh->HasNormals()) {
                aiVector3D normal = normalTransform * mesh->mNormals[i];
                normal.Normalize();
                out.normals.insert(out.normals.end(), {normal.x, normal.y, normal.z});
            }
            if (mesh->HasTextureCoords(0)) out.texcoords.insert(out.texcoords.end(), {mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y});
        }
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i) {
            const aiFace& face = mesh->mFaces[i];
            if (face.mNumIndices == 3) for (unsigned int j = 0; j < 3; ++j) out.indices.push_back(face.mIndices[j]);
        }
        readMaterial(scene, mesh, modelDir, out);
        return out;
    
}
