#include <OpenModel/AbaqusInpWriter.hpp>

std::string AbaqusInpWriter::abaqusElementType(ElementType type) {

        switch (type) {
        case ElementType::Triangle3: return "S3";
        case ElementType::Triangle6: return "S6";
        case ElementType::Quadrilateral4: return "S4";
        case ElementType::Quadrilateral8: return "S8";
        case ElementType::Quadrilateral9: return "S9";
        case ElementType::Tetrahedron4: return "C3D4";
        case ElementType::Tetrahedron10: return "C3D10";
        case ElementType::Hexahedron8: return "C3D8";
        case ElementType::Hexahedron20: return "C3D20";
        case ElementType::Prism6: return "C3D6";
        case ElementType::Prism15: return "C3D15";
        case ElementType::Pyramid5: return "C3D5";
        default: return {};
        }
    
}

std::string AbaqusInpWriter::sanitizeName(const std::string& input) {

        std::string result;
        result.reserve(input.size());
        for (unsigned char c : input) {
            if (std::isalnum(c) || c == '_') result.push_back(static_cast<char>(std::toupper(c)));
            else if (!result.empty() && result.back() != '_') result.push_back('_');
        }
        while (!result.empty() && result.back() == '_') result.pop_back();
        if (!result.empty() && !std::isalpha(static_cast<unsigned char>(result.front())) && result.front() != '_')
            result.insert(result.begin(), '_');
        if (result.size() > 80) result.resize(80);
        return result;
    
}

AbaqusWriteResult AbaqusInpWriter::writeFile(const std::string& path,
                                       const MeshData& mesh,
                                       const AbaqusWriteOptions& options) {

        AbaqusWriteResult result;
        std::ostringstream content;
        content << std::setprecision(17);

        const auto structuralErrors = mesh.validate();
        for (const auto& error : structuralErrors)
            result.errors.push_back("Invalid mesh: " + error);

        if (mesh.getNumNodes() == 0)
            result.errors.emplace_back("Cannot export an empty mesh");
        std::unordered_set<ElementId> allElementIds;
        std::unordered_set<std::string> exportedSetNames;
        std::unordered_set<std::string> exportedNodeSetNames;
        for (const auto& nodeSet : mesh.getNodeSets()) {
            const auto setName = sanitizeName(nodeSet.first);
            if (setName.empty()) result.errors.push_back("Node set name has no valid Abaqus set-name characters: '" + nodeSet.first + "'");
            else if (!exportedNodeSetNames.insert(setName).second)
                result.errors.push_back("Node set names collide after Abaqus name sanitization: '" + nodeSet.first + "'");
        }
        std::vector<std::string> setNames;
        setNames.reserve(mesh.getNumBlocks());

        for (const auto& block : mesh.getElementBlocks()) {
            const auto typeName = abaqusElementType(block.type);
            if (typeName.empty()) {
                result.errors.push_back("Unsupported Abaqus element type in block '" + block.name + "'");
            }
            const auto setName = sanitizeName(block.name);
            if (setName.empty()) {
                result.errors.push_back("Element block name has no valid Abaqus set-name characters: '" + block.name + "'");
            } else if (!exportedSetNames.insert(setName).second) {
                result.errors.push_back("Element block names collide after Abaqus name sanitization: '" + block.name + "'");
            }
            setNames.push_back(setName);
            for (const auto id : block.ids) {
                if (id == 0)
                    result.errors.push_back("Element ID 0 is not accepted by this writer");
                if (!allElementIds.insert(id).second)
                    result.errors.push_back("Duplicate element ID across blocks: " + std::to_string(id));
            }
        }
        for (const auto& group : mesh.getPhysicalGroups()) {
            const auto setName = sanitizeName(group.name);
            if (setName.empty()) result.errors.push_back("Physical group name has no valid Abaqus set-name characters: '" + group.name + "'");
            else if (!exportedSetNames.insert(setName).second)
                result.errors.push_back("Physical group name collides with another element set after sanitization: '" + group.name + "'");
        }
        for (const auto& group : mesh.getMeshGroupRegistry().groups()) {
            const auto base = sanitizeName(group.name);
            if (base.empty()) result.errors.push_back("Mesh group name has no valid Abaqus set-name characters: '" + group.name + "'");
            bool representedByPhysicalGroup = false;
            for (const auto& physical : mesh.getPhysicalGroups())
                if (physical.name == group.name && physical.dimension == group.dimension &&
                    physical.elementIds == group.elementIds) { representedByPhysicalGroup = true; break; }
            if (!group.elementIds.empty() && !representedByPhysicalGroup && !exportedSetNames.insert(base).second)
                result.errors.push_back("Mesh group element-set name collides after Abaqus name sanitization: '" + group.name + "'");
            if (!group.nodeIndices.empty()) {
                const auto nodeName = sanitizeName(group.name + "_NODES");
                if (nodeName.empty()) result.errors.push_back("Mesh group node-set name has no valid Abaqus set-name characters: '" + group.name + "'");
                else if (!exportedNodeSetNames.insert(nodeName).second)
                    result.errors.push_back("Mesh group node-set name collides after Abaqus name sanitization: '" + group.name + "'");
            }
        }
        for (const auto id : mesh.getNodeIds()) {
            if (id == 0) result.errors.emplace_back("Node ID 0 is not accepted by this writer");
        }
        for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
            const auto xyz = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
            if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
                result.errors.push_back("Non-finite coordinate at node ID " +
                                        std::to_string(mesh.nodeId(static_cast<NodeIndex>(i))));
            }
        }
        if (!result.errors.empty()) return result;

        if (options.writeHeading) {
            content << "*HEADING\n"
                    << "Generated by OpenModel Abaqus/CalculiX writer\n";
        }
        content << "*NODE\n";
        for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
            const auto index = static_cast<NodeIndex>(i);
            const auto xyz = mesh.nodeCoordinates(index);
            if (options.writeNodeComments) content << "** Node index " << i << "\n";
            content << mesh.nodeId(index) << ", " << xyz[0] << ", " << xyz[1] << ", " << xyz[2] << "\n";
            ++result.nodesWritten;
        }

        for (const auto& nodeSet : mesh.getNodeSets()) {
            content << "*NSET, NSET=" << sanitizeName(nodeSet.first) << "\n";
            std::size_t count = 0;
            for (const auto index : nodeSet.second) {
                if (count != 0) content << ", ";
                content << mesh.nodeId(index);
                ++count;
                if (count == 16) { content << "\n"; count = 0; }
            }
            if (count != 0 || nodeSet.second.empty()) content << "\n";
            ++result.nodeSetsWritten;
        }
        for (const auto& group : mesh.getMeshGroupRegistry().groups()) {
            if (group.nodeIndices.empty()) continue;
            content << "** MESH_GROUP, DIMENSION=" << group.dimension << ", SOURCE="
                    << (group.source == MeshGroupSource::CAD ? "CAD" :
                        group.source == MeshGroupSource::Imported ? "IMPORTED" : "USER_DEFINED") << "\n";
            content << "*NSET, NSET=" << sanitizeName(group.name + "_NODES") << "\n";
            std::size_t count = 0;
            for (const auto index : group.nodeIndices) {
                if (count != 0) content << ", ";
                content << mesh.nodeId(index);
                if (++count == 16) { content << "\n"; count = 0; }
            }
            if (count != 0) content << "\n";
            ++result.nodeSetsWritten;
        }

        for (std::size_t b = 0; b < mesh.getElementBlocks().size(); ++b) {
            const auto& block = mesh.getElementBlocks()[b];
            content << "*ELEMENT, TYPE=" << abaqusElementType(block.type)
                    << ", ELSET=" << setNames[b] << "\n";
            for (std::size_t e = 0; e < block.ids.size(); ++e) {
                content << block.ids[e];
                const auto offset = e * block.nodesPerElement;
                for (std::size_t n = 0; n < block.nodesPerElement; ++n) {
                    const auto index = block.connectivity[offset + n];
                    content << ", " << mesh.nodeId(index);
                }
                content << "\n";
                ++result.elementsWritten;
            }
            ++result.elementSetsWritten;
        }
        for (const auto& group : mesh.getPhysicalGroups()) {
            content << "** PHYSICAL_GROUP, DIMENSION=" << group.dimension
                    << ", TAG=" << group.tag;
            if (!group.materialName.empty()) content << ", MATERIAL=" << group.materialName;
            content << "\n*ELSET, ELSET=" << sanitizeName(group.name) << "\n";
            std::size_t count = 0;
            for (const auto id : group.elementIds) {
                if (count != 0) content << ", ";
                content << id;
                if (++count == 16) { content << "\n"; count = 0; }
            }
            if (count != 0 || group.elementIds.empty()) content << "\n";
            ++result.elementSetsWritten;
        }
        for (const auto& group : mesh.getMeshGroupRegistry().groups()) {
            if (group.elementIds.empty()) continue;
            bool representedByPhysicalGroup = false;
            for (const auto& physical : mesh.getPhysicalGroups())
                if (physical.name == group.name && physical.dimension == group.dimension &&
                    physical.elementIds == group.elementIds) { representedByPhysicalGroup = true; break; }
            if (representedByPhysicalGroup) continue;
            content << "** MESH_GROUP, DIMENSION=" << group.dimension
                    << ", SOURCE=" << (group.source == MeshGroupSource::CAD ? "CAD" :
                        group.source == MeshGroupSource::Imported ? "IMPORTED" : "USER_DEFINED") << "\n";
            content << "*ELSET, ELSET=" << sanitizeName(group.name) << "\n";
            std::size_t count = 0;
            for (const auto id : group.elementIds) {
                if (count != 0) content << ", ";
                content << id;
                if (++count == 16) { content << "\n"; count = 0; }
            }
            if (count != 0) content << "\n";
            ++result.elementSetsWritten;
        }

        const auto text = content.str();
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            result.errors.push_back("Cannot open output file: " + path);
            return result;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            result.errors.push_back("Failed while writing output file: " + path);
            return result;
        }
        result.success = true;
        return result;
    
}
