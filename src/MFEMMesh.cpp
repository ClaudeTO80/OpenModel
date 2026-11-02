#include <OpenModel/MFEMMesh.hpp>

void MFEMMesh::clear()  {

        m_mfemMesh.reset();
        m_sourceFile.clear();
        m_error.clear();
    
}

bool MFEMMesh::isEmpty() const  {

        return !m_mfemMesh || m_mfemMesh->GetNV() == 0;
    
}

std::size_t MFEMMesh::getNumNodes() const  {

        return m_mfemMesh ? static_cast<std::size_t>(m_mfemMesh->GetNV()) : 0U;
    
}

std::size_t MFEMMesh::getNumElements() const  {

        return m_mfemMesh ? static_cast<std::size_t>(m_mfemMesh->GetNE()) : 0U;
    
}

std::string MFEMMesh::getMeshType() const  {
 return "MFEM Finite Element Mesh"; 
}

const std::string& MFEMMesh::getLastError() const noexcept {
 return m_error; 
}

const std::string& MFEMMesh::getSourceFile() const noexcept {
 return m_sourceFile; 
}

// Load a mesh supported by MFEM's own file reader. On failure, the previous
    // mesh is retained and MFEMMesh::getLastError() explains the failure.
    bool MFEMMesh::loadFromFile(const std::string& filepath, int /*dimension*/) {

        try {
            auto candidate = std::make_unique<mfem::Mesh>(filepath.c_str(), 1, 0, false);
            if (candidate->GetNV() <= 0) {
                m_error = "MFEM loaded a mesh with no vertices";
                return false;
            }
            m_mfemMesh = std::move(candidate);
            m_sourceFile = filepath;
            m_error.clear();
            return true;
        } catch (const std::exception& e) {
            m_error = e.what();
            return false;
        } catch (...) {
            m_error = "Unknown error while loading MFEM mesh";
            return false;
        }
    
}

// Converts a MeshData mesh into MFEM. Supported here: linear triangles and
    // quads for 2-D meshes; linear tetrahedra and hexahedra for 3-D meshes.
    // For 3-D meshes, linear triangles/quads in other blocks are imported as
    // boundary elements. Unsupported/mixed topologies are rejected explicitly.
    bool MFEMMesh::createFromData(const MeshData& source, int meshDimension) {

        m_error.clear();
        if (meshDimension != 2 && meshDimension != 3) {
            m_error = "MFEM adapter supports mesh dimensions 2 and 3";
            return false;
        }
        const auto structuralErrors = source.validate();
        if (!structuralErrors.empty()) {
            m_error = "Invalid MeshData: " + structuralErrors.front();
            return false;
        }
        if (source.getNumNodes() == 0 || source.getNumElements() == 0) {
            m_error = "Cannot create an MFEM mesh from an empty mesh";
            return false;
        }
        if (source.getNumNodes() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            source.getNumElements() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            m_error = "Mesh exceeds MFEM int index limits";
            return false;
        }

        int topElements = 0;
        int boundaryElements = 0;
        for (const auto& block : source.getElementBlocks()) {
            const bool top2d = meshDimension == 2 &&
                (block.type == ElementType::Triangle3 || block.type == ElementType::Quadrilateral4);
            const bool top3d = meshDimension == 3 &&
                (block.type == ElementType::Tetrahedron4 || block.type == ElementType::Hexahedron8);
            const bool boundary2d = meshDimension == 2 && block.type == ElementType::Line2;
            const bool boundary3d = meshDimension == 3 &&
                (block.type == ElementType::Triangle3 || block.type == ElementType::Quadrilateral4);
            const bool ignorableLowerDimension =
                (meshDimension == 2 && block.type == ElementType::Point1) ||
                (meshDimension == 3 && (block.type == ElementType::Line2 ||
                                        block.type == ElementType::Line3 ||
                                        block.type == ElementType::Point1));
            if (top2d || top3d) topElements += static_cast<int>(block.size());
            else if (boundary2d || boundary3d) boundaryElements += static_cast<int>(block.size());
            else if (!ignorableLowerDimension) {
                m_error = "Unsupported element type in block '" + block.name + "' (MFEM adapter supports linear top-dimensional elements and codimension-one boundaries)";
                return false;
            }
        }
        if (topElements == 0) {
            m_error = "No top-dimensional elements found for requested mesh dimension";
            return false;
        }

        try {
            auto candidate = std::make_unique<mfem::Mesh>(
                meshDimension, static_cast<int>(source.getNumNodes()), topElements,
                boundaryElements, 3);
            for (std::size_t i = 0; i < source.getNumNodes(); ++i) {
                const auto xyz = source.nodeCoordinates(static_cast<NodeIndex>(i));
                if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
                    m_error = "Non-finite node coordinate";
                    return false;
                }
                candidate->AddVertex(xyz[0], xyz[1], xyz[2]);
            }

            int attribute = 1;
            for (const auto& block : source.getElementBlocks()) {
                const bool top2d = meshDimension == 2 &&
                    (block.type == ElementType::Triangle3 || block.type == ElementType::Quadrilateral4);
                const bool top3d = meshDimension == 3 &&
                    (block.type == ElementType::Tetrahedron4 || block.type == ElementType::Hexahedron8);
                const bool boundary2d = meshDimension == 2 && block.type == ElementType::Line2;
                const bool boundary3d = meshDimension == 3 &&
                    (block.type == ElementType::Triangle3 || block.type == ElementType::Quadrilateral4);
                if (!(top2d || top3d || boundary2d || boundary3d)) continue;
                for (std::size_t e = 0; e < block.size(); ++e) {
                    const int elementDimensionForAttribute = (boundary2d || boundary3d) ? meshDimension - 1 : meshDimension;
                    const int elementAttribute = attributeForElement(
                        source, block.ids[e], elementDimensionForAttribute, attribute);
                    const auto offset = e * block.nodesPerElement;
                    auto v = [&](std::size_t local) -> int {
                        const NodeIndex index = block.connectivity[offset + local];
                        if (index > static_cast<NodeIndex>(std::numeric_limits<int>::max()))
                            throw std::out_of_range("Node index exceeds MFEM int index limits");
                        return static_cast<int>(index);
                    };
                    if (meshDimension == 2 && block.type == ElementType::Triangle3) {
                        candidate->AddTriangle(v(0), v(1), v(2), elementAttribute);
                    } else if (meshDimension == 2 && block.type == ElementType::Quadrilateral4) {
                        candidate->AddQuad(v(0), v(1), v(2), v(3), elementAttribute);
                    } else if (boundary2d && block.type == ElementType::Line2) {
                        candidate->AddBdrSegment(v(0), v(1), elementAttribute);
                    } else if (boundary3d && block.type == ElementType::Triangle3) {
                        candidate->AddBdrTriangle(v(0), v(1), v(2), elementAttribute);
                    } else if (top3d && block.type == ElementType::Tetrahedron4) {
                        candidate->AddTet(v(0), v(1), v(2), v(3), elementAttribute);
                    } else if (top3d && block.type == ElementType::Hexahedron8) {
                        candidate->AddHex(v(0), v(1), v(2), v(3), v(4), v(5), v(6), v(7), elementAttribute);
                    } else if (boundary3d && block.type == ElementType::Quadrilateral4) {
                        candidate->AddBdrQuad(v(0), v(1), v(2), v(3), elementAttribute);
                    }
                }
                ++attribute;
            }
            candidate->FinalizeTopology();
            candidate->Finalize();
            m_mfemMesh = std::move(candidate);
            m_sourceFile.clear();
            return true;
        } catch (const std::exception& e) {
            m_error = e.what();
            return false;
        } catch (...) {
            m_error = "Unknown error while converting MeshData to MFEM";
            return false;
        }
    
}

mfem::Mesh* MFEMMesh::getNativeMesh() const noexcept {
 return m_mfemMesh.get(); 
}

int MFEMMesh::attributeForElement(const MeshData& source, ElementId id, int dimension, int fallback) {

        int selected = fallback;
        bool found = false;
        for (const auto& group : source.getPhysicalGroups()) {
            if (group.dimension != dimension) continue;
            if (std::find(group.elementIds.begin(), group.elementIds.end(), id) == group.elementIds.end()) continue;
            if (found) {
                throw std::invalid_argument("Element ID " + std::to_string(id) +
                    " belongs to multiple physical groups of the same dimension; MFEM attributes cannot represent overlapping groups");
            }
            selected = group.tag;
            found = true;
        }
        return selected;
    
}
