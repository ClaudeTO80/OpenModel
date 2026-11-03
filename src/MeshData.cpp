#include <OpenModel/MeshData.hpp>

std::size_t elementNodeCount(ElementType type) noexcept {
    switch (type) {
    case ElementType::Point1: return 1;
    case ElementType::Line2: return 2;
    case ElementType::Line3: return 3;
    case ElementType::Triangle3: return 3;
    case ElementType::Triangle6: return 6;
    case ElementType::Quadrilateral4: return 4;
    case ElementType::Quadrilateral8: return 8;
    case ElementType::Quadrilateral9: return 9;
    case ElementType::Tetrahedron4: return 4;
    case ElementType::Tetrahedron10: return 10;
    case ElementType::Hexahedron8: return 8;
    case ElementType::Hexahedron20: return 20;
    case ElementType::Hexahedron27: return 27;
    case ElementType::Prism6: return 6;
    case ElementType::Prism15: return 15;
    case ElementType::Pyramid5: return 5;
    default: return 0;
    }
}

int elementDimension(ElementType type) noexcept {
    switch (type) {
    case ElementType::Point1: return 0;
    case ElementType::Line2: case ElementType::Line3: return 1;
    case ElementType::Triangle3: case ElementType::Triangle6:
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8:
    case ElementType::Quadrilateral9: return 2;
    case ElementType::Tetrahedron4: case ElementType::Tetrahedron10:
    case ElementType::Hexahedron8: case ElementType::Hexahedron20:
    case ElementType::Hexahedron27: case ElementType::Prism6: case ElementType::Prism15:
    case ElementType::Pyramid5: return 3;
    default: return -1;
    }
}


std::size_t ElementBlock::size() const noexcept {
 return ids.size(); 
}

bool ElementBlock::empty() const noexcept {
 return ids.empty(); 
}

void MeshGroupRegistry::clear() noexcept {
    m_groups.clear();
}

void MeshGroupRegistry::add(MeshGroup group) {
    if (group.name.empty()) throw std::invalid_argument("MeshGroupRegistry: group name cannot be empty");
    if (group.dimension < 0 || group.dimension > 3)
        throw std::invalid_argument("MeshGroupRegistry: group dimension must be 0..3");
    if (find(group.name)) throw std::invalid_argument("MeshGroupRegistry: duplicate group name");
    auto uniquePreservingOrder = [](auto& values) {
        using Value = typename std::decay<decltype(values)>::type::value_type;
        std::unordered_set<Value> seen;
        seen.reserve(values.size());
        auto out = values.begin();
        for (auto it = values.begin(); it != values.end(); ++it)
            if (seen.insert(*it).second) *out++ = *it;
        values.erase(out, values.end());
    };
    uniquePreservingOrder(group.sourceEntityIds);
    uniquePreservingOrder(group.elementIds);
    uniquePreservingOrder(group.nodeIndices);
    m_groups.push_back(std::move(group));
}

const std::vector<MeshGroup>& MeshGroupRegistry::groups() const noexcept {
    return m_groups;
}

const MeshGroup* MeshGroupRegistry::find(const std::string& name) const noexcept {
    for (const auto& group : m_groups)
        if (group.name == name) return &group;
    return nullptr;
}

void MeshData::clear() noexcept {

        m_nodeIds.clear();
        m_coordinates.clear();
        m_nodeIdToIndex.clear();
        m_blocks.clear();
        m_blockLookup.clear();
        m_nodeSets.clear();
        m_physicalGroups.clear();
        m_groupRegistry.clear();
    
}

bool MeshData::isEmpty() const noexcept {
 return m_nodeIds.empty(); 
}

std::size_t MeshData::getNumNodes() const noexcept {
 return m_nodeIds.size(); 
}

std::size_t MeshData::getNumElements() const noexcept {

        std::size_t count = 0;
        for (const auto& block : m_blocks) count += block.size();
        return count;
    
}

std::size_t MeshData::getNumBlocks() const noexcept {
 return m_blocks.size(); 
}

NodeIndex MeshData::addNode(NodeId id, double x, double y, double z) {

        if (m_nodeIdToIndex.find(id) != m_nodeIdToIndex.end())
            throw std::invalid_argument("MeshData: duplicate node ID");
        const auto index = static_cast<NodeIndex>(m_nodeIds.size());
        m_nodeIds.push_back(id);
        m_coordinates.insert(m_coordinates.end(), {x, y, z});
        m_nodeIdToIndex.emplace(id, index);
        return index;
    
}

NodeIndex MeshData::addNode(double x, double y, double z) {

        NodeId id = m_nodeIds.empty() ? 1 : m_nodeIds.back() + 1;
        while (m_nodeIdToIndex.find(id) != m_nodeIdToIndex.end()) {
            if (id == std::numeric_limits<NodeId>::max())
                throw std::overflow_error("MeshData: node ID space exhausted");
            ++id;
        }
        return addNode(id, x, y, z);
    
}

bool MeshData::hasNodeId(NodeId id) const noexcept {

        return m_nodeIdToIndex.find(id) != m_nodeIdToIndex.end();
    
}

NodeIndex MeshData::nodeIndex(NodeId id) const {

        const auto it = m_nodeIdToIndex.find(id);
        if (it == m_nodeIdToIndex.end()) throw std::out_of_range("MeshData: unknown node ID");
        return it->second;
    
}

NodeId MeshData::nodeId(NodeIndex index) const {

        if (index >= m_nodeIds.size()) throw std::out_of_range("MeshData: node index out of range");
        return m_nodeIds[static_cast<std::size_t>(index)];
    
}

std::array<double, 3> MeshData::nodeCoordinates(NodeIndex index) const {

        if (index >= m_nodeIds.size()) throw std::out_of_range("MeshData: node index out of range");
        const auto offset = static_cast<std::size_t>(index) * 3;
        return {m_coordinates[offset], m_coordinates[offset + 1], m_coordinates[offset + 2]};
    
}

ElementBlock& MeshData::getOrCreateBlock(const std::string& name, ElementType type, std::size_t nodesPerElement) {

        if (name.empty()) throw std::invalid_argument("MeshData: block name cannot be empty");
        if (nodesPerElement == 0) nodesPerElement = elementNodeCount(type);
        if (nodesPerElement == 0) throw std::invalid_argument("MeshData: unknown element topology requires nodesPerElement");
        const auto it = m_blockLookup.find(name);
        if (it != m_blockLookup.end()) {
            auto& block = m_blocks[it->second];
            if (block.type != type || block.nodesPerElement != nodesPerElement)
                throw std::invalid_argument("MeshData: block topology mismatch");
            return block;
        }
        const auto index = m_blocks.size();
        m_blocks.push_back(ElementBlock{name, type, nodesPerElement, {}, {}});
        m_blockLookup.emplace(name, index);
        return m_blocks.back();
    
}

void MeshData::addElement(const std::string& blockName, ElementType type, ElementId id, const std::vector<NodeIndex>& connectivity) {

        auto& block = getOrCreateBlock(blockName, type, connectivity.size());
        if (connectivity.size() != block.nodesPerElement)
            throw std::invalid_argument("MeshData: invalid connectivity length");
        for (const auto node : connectivity) {
            if (node >= m_nodeIds.size()) throw std::out_of_range("MeshData: connectivity node index out of range");
        }
        block.ids.push_back(id);
        block.connectivity.insert(block.connectivity.end(), connectivity.begin(), connectivity.end());
    
}

void MeshData::addNodeToSet(const std::string& setName, NodeIndex index) {

        if (setName.empty()) throw std::invalid_argument("MeshData: node set name cannot be empty");
        if (index >= m_nodeIds.size()) throw std::out_of_range("MeshData: node set index out of range");
        auto& nodes = m_nodeSets[setName];
        for (const auto existing : nodes) if (existing == index) return;
        nodes.push_back(index);
    
}

void MeshData::addNodeSet(const std::string& setName, const std::vector<NodeIndex>& indices) {

        if (setName.empty()) throw std::invalid_argument("MeshData: node set name cannot be empty");
        for (const auto index : indices) {
            if (index >= m_nodeIds.size()) throw std::out_of_range("MeshData: node set index out of range");
        }
        auto& nodes = m_nodeSets[setName];
        std::unordered_set<NodeIndex> seen(nodes.begin(), nodes.end());
        seen.reserve(nodes.size() + indices.size());
        for (const auto index : indices)
            if (seen.insert(index).second) nodes.push_back(index);
    
}

const std::unordered_map<std::string, std::vector<NodeIndex>>& MeshData::getNodeSets() const noexcept {

        return m_nodeSets;
    
}

const std::vector<MeshPhysicalGroup>& MeshData::getPhysicalGroups() const noexcept {
 return m_physicalGroups;
}

void MeshData::addMeshGroup(MeshGroup group) {
    for (const auto index : group.nodeIndices)
        if (index >= m_nodeIds.size())
            throw std::out_of_range("MeshData: mesh group node index out of range");
    m_groupRegistry.add(std::move(group));
}

const MeshGroupRegistry& MeshData::getMeshGroupRegistry() const noexcept {
    return m_groupRegistry;
}

const std::vector<NodeId>& MeshData::getNodeIds() const noexcept {
 return m_nodeIds; 
}

const std::vector<double>& MeshData::getCoordinates() const noexcept {
 return m_coordinates; 
}

const std::vector<ElementBlock>& MeshData::getElementBlocks() const noexcept {
 return m_blocks; 
}

const ElementBlock& MeshData::getElementBlock(const std::string& name) const {

        const auto it = m_blockLookup.find(name);
        if (it == m_blockLookup.end()) throw std::out_of_range("MeshData: unknown element block");
        return m_blocks[it->second];
    
}

// Validation of structural invariants; geometric quality checks are implemented separately.
    std::vector<std::string> MeshData::validate() const {

        std::vector<std::string> errors;
        if (m_coordinates.size() != m_nodeIds.size() * 3)
            errors.emplace_back("Coordinate count does not match node count");
        if (m_nodeIdToIndex.size() != m_nodeIds.size())
            errors.emplace_back("Node ID index is inconsistent");
        for (const auto& set : m_nodeSets) {
            for (const auto index : set.second) {
                if (index >= m_nodeIds.size()) {
                    errors.emplace_back("Invalid node index in node set: " + set.first);
                    break;
                }
            }
        }
        std::unordered_map<ElementId, int> elementDimensions;
        for (const auto& block : m_blocks) {
            for (const auto id : block.ids) {
                // Duplicate IDs are reported by MeshValidator with its structured error code.
                elementDimensions.emplace(id, elementDimension(block.type));
            }
        }
        std::unordered_set<std::string> groupNames;
        std::set<std::pair<int, int>> groupTags;
        for (const auto& group : m_physicalGroups) {
            if (group.name.empty() || !groupNames.insert(group.name).second)
                errors.emplace_back("Empty or duplicate physical group name: " + group.name);
            if (group.dimension < 0 || group.dimension > 3 || group.tag <= 0 ||
                !groupTags.insert({group.dimension, group.tag}).second)
                errors.emplace_back("Invalid or duplicate physical group tag: " + group.name);
            for (const auto id : group.elementIds) {
                const auto it = elementDimensions.find(id);
                if (it == elementDimensions.end()) {
                    errors.emplace_back("Physical group references unknown element ID: " + std::to_string(id));
                    break;
                }
                if (it->second != group.dimension) {
                    errors.emplace_back("Physical group dimension does not match element ID: " + std::to_string(id));
                    break;
                }
            }
        }
        std::unordered_set<std::string> meshGroupNames;
        for (const auto& group : m_groupRegistry.groups()) {
            if (group.name.empty() || !meshGroupNames.insert(group.name).second)
                errors.emplace_back("Empty or duplicate mesh group name: " + group.name);
            if (group.dimension < 0 || group.dimension > 3)
                errors.emplace_back("Invalid mesh group dimension: " + group.name);
            for (const auto index : group.nodeIndices) {
                if (index >= m_nodeIds.size()) {
                    errors.emplace_back("Mesh group references invalid node index: " + group.name);
                    break;
                }
            }
            for (const auto id : group.elementIds) {
                const auto it = elementDimensions.find(id);
                if (it == elementDimensions.end()) {
                    errors.emplace_back("Mesh group references unknown element ID: " + std::to_string(id));
                    break;
                }
                if (it->second != group.dimension) {
                    errors.emplace_back("Mesh group dimension does not match element ID: " + std::to_string(id));
                    break;
                }
            }
        }
        for (const auto& block : m_blocks) {
            if (block.nodesPerElement == 0 || block.connectivity.size() != block.ids.size() * block.nodesPerElement)
                errors.emplace_back("Connectivity size mismatch in block: " + block.name);
            for (const auto index : block.connectivity) {
                if (index >= m_nodeIds.size()) {
                    errors.emplace_back("Invalid node index in block: " + block.name);
                    break;
                }
            }
        }
        return errors;
    
}

void MeshData::addPhysicalGroup(const std::string& name, int dimension, int tag,
                          const std::vector<ElementId>& elementIds,
                          const std::string& materialName) {

        if (name.empty()) throw std::invalid_argument("MeshData: physical group name cannot be empty");
        if (dimension < 0 || dimension > 3) throw std::invalid_argument("MeshData: physical group dimension must be 0..3");
        if (tag <= 0) throw std::invalid_argument("MeshData: physical group tag must be positive");
        for (const auto& group : m_physicalGroups) {
            if (group.name == name) throw std::invalid_argument("MeshData: duplicate physical group name");
            if (group.dimension == dimension && group.tag == tag)
                throw std::invalid_argument("MeshData: duplicate physical group tag in dimension");
        }
        MeshPhysicalGroup group{name, dimension, tag, {}, materialName};
        // Remove duplicates while preserving the caller's order (linear time).
        group.elementIds.reserve(elementIds.size());
        std::unordered_set<ElementId> seen;
        seen.reserve(elementIds.size());
        for (const auto id : elementIds) {
            if (seen.insert(id).second) group.elementIds.push_back(id);
        }
        m_physicalGroups.push_back(std::move(group));
    
}
