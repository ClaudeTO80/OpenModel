#include <OpenModel/SurfaceMesh.hpp>

void SurfaceMesh::clear() {
 parts.clear(); 
}

bool SurfaceMesh::empty() const noexcept {
 return parts.empty(); 
}

std::size_t SurfaceMesh::vertexCount() const noexcept {

        std::size_t n = 0;
        for (const auto& p : parts) n += p.positions.size() / 3;
        return n;
    
}

std::size_t SurfaceMesh::triangleCount() const noexcept {

        std::size_t n = 0;
        for (const auto& p : parts) n += p.indices.size() / 3;
        return n;
    
}

SurfaceMeshReport SurfaceMesher::build(const std::shared_ptr<CADModel>& cadModel, const MeshingOptions& options, SurfaceMesh& output) const {

        output.clear();
        SurfaceMeshReport report;
        if (!cadModel) { report.error = "CAD model is null."; return report; }
        if (!std::isfinite(options.linearDeflection) || options.linearDeflection <= 0.0) {
            report.error = "linearDeflection must be finite and positive."; return report;
        }
        if (!std::isfinite(options.angularDeflection) || options.angularDeflection <= 0.0 ||
            options.angularDeflection > 3.141592653589793) {
            report.error = "angularDeflection must be in (0, pi]."; return report;
        }

        cadModel->tessellate(options.linearDeflection, options.angularDeflection);
        const auto& source = cadModel->getRenderMeshes();
        if (source.empty()) {
            report.error = cadModel->getLastError().empty()
                ? "CAD tessellation produced no surface triangles." : cadModel->getLastError();
            return report;
        }

        for (const auto& src : source) {
            if (src.vertices.empty() || src.vertices.size() % 3 != 0 ||
                src.indices.empty() || src.indices.size() % 3 != 0) {
                report.error = "A surface part has malformed position or triangle-index arrays.";
                output.clear(); return report;
            }
            const std::size_t nv = src.vertices.size() / 3;
            for (float v : src.vertices) if (!std::isfinite(v)) {
                report.error = "A surface part contains a non-finite coordinate.";
                output.clear(); return report;
            }
            for (unsigned int i : src.indices) if (i >= nv) {
                report.error = "A surface triangle references a vertex outside its part.";
                output.clear(); return report;
            }
            if (!src.normals.empty() && src.normals.size() != src.vertices.size()) {
                report.error = "A surface part has a normals array with a mismatched size.";
                output.clear(); return report;
            }
            if (!src.texcoords.empty() && src.texcoords.size() != nv * 2) {
                report.error = "A surface part has a UV array with a mismatched size.";
                output.clear(); return report;
            }
            for (float uv : src.texcoords) if (!std::isfinite(uv)) {
                report.error = "A surface part contains a non-finite UV coordinate.";
                output.clear(); return report;
            }
            SurfaceMeshPart part;
            part.name = src.name;
            part.sourcePartId = src.sourcePartId;
            part.sourceFaceId = src.sourceFaceId;
            part.positions = src.vertices;
            part.normals = src.normals;
            part.texcoords = src.texcoords;
            part.baseColorFactor = src.baseColorFactor;
            part.metallicFactor = src.metallicFactor;
            part.roughnessFactor = src.roughnessFactor;
            part.baseColorImage = src.baseColorImage;
            part.baseColorImageMimeType = src.baseColorImageMimeType;
            part.baseColorImageName = src.baseColorImageName;
            part.normalImage = src.normalImage;
            part.normalImageMimeType = src.normalImageMimeType;
            part.normalImageName = src.normalImageName;
            part.metallicRoughnessImage = src.metallicRoughnessImage;
            part.metallicRoughnessImageMimeType = src.metallicRoughnessImageMimeType;
            part.metallicRoughnessImageName = src.metallicRoughnessImageName;
            part.emissiveImage = src.emissiveImage;
            part.emissiveImageMimeType = src.emissiveImageMimeType;
            part.emissiveImageName = src.emissiveImageName;
            part.emissiveFactor = src.emissiveFactor;
            part.alphaMode = src.alphaMode;
            part.alphaCutoff = src.alphaCutoff;
            part.doubleSided = src.doubleSided;
            part.indices = src.indices;
            output.parts.push_back(std::move(part));
        }
        report.ok = true;
        report.parts = output.parts.size();
        report.vertices = output.vertexCount();
        report.triangles = output.triangleCount();
        return report;
    
}
