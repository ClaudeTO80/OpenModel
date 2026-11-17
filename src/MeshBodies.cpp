#include <OpenModel/MeshBodies.hpp>

#include <unordered_map>

namespace OpenModel {

std::unordered_set<ElementId> nonExportedElements(const MeshData& mesh) {
    std::unordered_set<ElementId> out;
    if (mesh.getBodies().empty()) return out;
    // Dimension of every element, and the set of bodies that hold it with the dimension each body was meshed in.
    std::unordered_map<ElementId, int> dimensionOf;
    for (const auto& block : mesh.getElementBlocks())
        for (const ElementId id : block.ids) dimensionOf.emplace(id, elementDimension(block.type));
    // Keep an element when some body containing it has its dimension.
    std::unordered_map<ElementId, bool> kept;
    for (const auto& body : mesh.getBodies())
        for (const ElementId id : body.elementIds) {
            const auto dim = dimensionOf.find(id);
            if (dim == dimensionOf.end()) continue;
            const bool match = dim->second == body.dimension;
            auto entry = kept.emplace(id, match);
            if (!entry.second) entry.first->second = entry.first->second || match;
        }
    for (const auto& entry : kept)
        if (!entry.second) out.insert(entry.first);
    return out;
}

MeshData exportableMesh(const MeshData& mesh) {
    const auto excluded = nonExportedElements(mesh);
    if (excluded.empty()) return mesh;

    MeshData out;
    for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
        const auto c = mesh.nodeCoordinates(static_cast<NodeIndex>(i));
        out.addNode(mesh.nodeId(static_cast<NodeIndex>(i)), c[0], c[1], c[2]);
    }
    for (const auto& block : mesh.getElementBlocks()) {
        for (std::size_t e = 0; e < block.size(); ++e) {
            if (excluded.count(block.ids[e])) continue;
            std::vector<NodeIndex> conn(block.connectivity.begin() + static_cast<std::ptrdiff_t>(e * block.nodesPerElement),
                                        block.connectivity.begin() + static_cast<std::ptrdiff_t>((e + 1) * block.nodesPerElement));
            out.addElement(block.name, block.type, block.ids[e], conn);
        }
    }
    auto kept = [&](const std::vector<ElementId>& ids) {
        std::vector<ElementId> out_ids;
        for (const ElementId id : ids) if (!excluded.count(id)) out_ids.push_back(id);
        return out_ids;
    };
    for (const auto& g : mesh.getPhysicalGroups())
        out.addPhysicalGroup(g.name, g.dimension, g.tag, kept(g.elementIds), g.materialName);
    for (const auto& nodeSet : mesh.getNodeSets()) out.addNodeSet(nodeSet.first, nodeSet.second);
    for (const auto& group : mesh.getMeshGroupRegistry().groups()) {
        MeshGroup copy = group;
        copy.elementIds = kept(group.elementIds);
        out.addMeshGroup(std::move(copy));
    }
    for (const auto& entry : mesh.getConstraints())
        out.constrainNode(entry.second.node, entry.second.dofs, entry.second.values);
    std::vector<MeshBody> bodies = mesh.getBodies();
    for (auto& body : bodies) body.elementIds = kept(body.elementIds);
    out.setBodies(std::move(bodies));
    return out;
}

} // namespace OpenModel
