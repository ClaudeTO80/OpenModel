#pragma once

#include <OpenModel/MeshBase.hpp>
#include <OpenModel/MeshData.hpp>

#include <mfem.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Optional adapter: this header is available when MFEM is installed and linked.
// The core MeshData model remains independent of MFEM.
class MFEMMesh : public MeshBase {
public:
    MFEMMesh() = default;
    ~MFEMMesh() override = default;

    void clear() override ;

    bool isEmpty() const override ;

    std::size_t getNumNodes() const override ;

    std::size_t getNumElements() const override ;

    std::string getMeshType() const override ;

    const std::string& getLastError() const noexcept ;
    const std::string& getSourceFile() const noexcept ;

    // Load a mesh supported by MFEM's own file reader. On failure, the previous
    // mesh is retained and getLastError() explains the failure.
    bool loadFromFile(const std::string& filepath, int /*dimension*/ = 3) ;

    // Converts a MeshData mesh into MFEM. Supported here: linear triangles and
    // quads for 2-D meshes; linear tetrahedra and hexahedra for 3-D meshes.
    // For 3-D meshes, linear triangles/quads in other blocks are imported as
    // boundary elements. Unsupported/mixed topologies are rejected explicitly.
    bool createFromData(const MeshData& source, int meshDimension = 3) ;

    mfem::Mesh* getNativeMesh() const noexcept ;

private:
    static int attributeForElement(const MeshData& source, ElementId id,
                                   int dimension, int fallback) ;

    std::unique_ptr<mfem::Mesh> m_mfemMesh;
    std::string m_sourceFile;
    std::string m_error;
};
