#pragma once

#include <array>
#include <string>
#include <vector>

// Render-oriented triangulation extracted from a CAD face. Indices are zero-based.
// sourcePartId identifies the owning solid when the source contains solids;
// sourceFaceId is stable only for the lifetime/order of this import.
struct RenderSubMesh {
    std::string name;
    int sourcePartId = -1;
    int sourceFaceId = -1;
    std::vector<float> vertices;       // [x1, y1, z1, x2, y2, z2, ...]
    std::vector<float> normals;        // one normal per vertex, same packed layout
    std::vector<float> texcoords;      // optional UV0 pairs, two floats per vertex
    std::array<float, 4> baseColorFactor{{0.8f, 0.8f, 0.8f, 1.0f}};
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.8f;
    std::vector<unsigned char> baseColorImage; // encoded PNG/JPEG bytes
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
    std::string alphaMode = "OPAQUE"; // OPAQUE, MASK, BLEND
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    std::vector<unsigned int> indices; // [i1, i2, i3, ...]
};

/**
 * @brief A node of the assembly tree stored in the CAD file (assembly, sub-assembly or part instance).
 *
 * Everything here comes from the file: OpenModel never invents names. A file written
 * without product names yields components with an empty name.
 */
struct CADComponent {
    int id = -1;                  ///< Index in CADModel::getComponents().
    int parent = -1;              ///< Parent component index, -1 for a root.
    std::string name;             ///< Instance/product name as written in the CAD file (may be empty).
    std::string path;             ///< Names from the root joined with '/'; unnamed levels use "Component_<id>".
    bool isAssembly = false;      ///< True when the node only contains other components.
    bool hasColor = false;        ///< True when the file assigns a colour to this component.
    std::array<float, 3> color{{0.8f, 0.8f, 0.8f}}; ///< sRGB colour in [0,1], valid if hasColor.
    std::vector<int> solidIds;    ///< Solids below this node (whole subtree), numbered as RenderSubMesh::sourcePartId.
    std::vector<int> faceIds;     ///< Faces below this node (whole subtree), numbered as RenderSubMesh::sourceFaceId.
};

/** @brief Kind of a user-visible group stored in the CAD file. */
enum class CADGroupKind {
    Layer,        ///< Presentation layer / IGES level.
    NamedShape    ///< A named sub-shape (typically a named face or solid inside a part).
};

/** @brief A named set of faces or solids stored in the CAD file. */
struct CADGroup {
    std::string name;             ///< Name as written in the CAD file.
    CADGroupKind kind = CADGroupKind::Layer;
    int dimension = 2;            ///< 0 = vertex ids, 1 = edge ids, 2 = face ids, 3 = solid ids.
    std::vector<int> ids;         ///< Face ids (dimension 2) or solid ids (dimension 3), sorted, unique.
};

class CADModel {
public:
    virtual ~CADModel() = default;

    virtual bool loadFromFile(const std::string& filepath) = 0;
    virtual void tessellate(double tolerance = 0.1) = 0;

    // Backends may override this overload to honor angular deflection. The
    // default preserves compatibility with existing CADModel implementations.
    virtual void tessellate(double linearDeflection, double angularDeflection) ;
    virtual const std::vector<RenderSubMesh>& getRenderMeshes() const = 0;
    virtual std::string getFilePath() const = 0;

    // Diagnostic text from the most recent load/tessellation operation.
    virtual const std::string& getLastError() const ;

    /** @brief Assembly tree read from the CAD file. Empty when the backend carries no structure. */
    virtual const std::vector<CADComponent>& getComponents() const;

    /** @brief Named groups (layers, named sub-shapes) read from the CAD file. */
    virtual const std::vector<CADGroup>& getGroups() const;

    /**
     * @brief Geometric query used by meshers to map their own entities back to CAD objects.
     * @param point     Query point in model coordinates.
     * @param tolerance Absolute distance tolerance.
     * @return Ids of the solids that contain the point (numbered as RenderSubMesh::sourcePartId).
     *         Empty when unsupported or when no solid contains it.
     */
    virtual std::vector<int> solidsContainingPoint(const std::array<double, 3>& point, double tolerance) const;

    /**
     * @brief Geometric query: faces whose surface passes through a point.
     * @param point     Query point in model coordinates.
     * @param tolerance Absolute distance tolerance.
     * @return Ids of the faces (numbered as RenderSubMesh::sourceFaceId) the point lies on.
     */
    virtual std::vector<int> facesContainingPoint(const std::array<double, 3>& point, double tolerance) const;

    /** @brief CAD edges whose curve passes through a point within tolerance (edge IDs are importer-local). */
    virtual std::vector<int> edgesContainingPoint(const std::array<double, 3>& point, double tolerance) const;

    /** @brief CAD vertices within tolerance of a point (vertex IDs are importer-local). */
    virtual std::vector<int> verticesContainingPoint(const std::array<double, 3>& point, double tolerance) const;
};
