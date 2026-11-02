#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <set>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using NodeId = std::uint64_t;
using ElementId = std::uint64_t;
using NodeIndex = std::uint64_t;

// Topological element type. Keep backend-specific numeric element codes out of the core.
enum class ElementType {
    Unknown,
    Point1,
    Line2,
    Line3,
    Triangle3,
    Triangle6,
    Quadrilateral4,
    Quadrilateral8,
    Quadrilateral9,
    Tetrahedron4,
    Tetrahedron10,
    Hexahedron8,
    Hexahedron20,
    Hexahedron27,
    Prism6,
    Prism15,
    Pyramid5
};

std::size_t elementNodeCount(ElementType type) noexcept;

struct Node {
    NodeId id{}; // Stable external identifier; not an array index.
    std::array<double, 3> coordinates{};
};

// A block contains elements with the same topology and number of nodes.
// connectivity stores zero-based NodeIndex values, not external NodeId values.
// Physical groups collect elements of a common topological dimension. A group
// can represent a volume region, a boundary patch, or a lower-dimensional set.
// materialName is metadata only: it does not define constitutive properties.
struct MeshPhysicalGroup {
    std::string name;
    int dimension = -1;
    int tag = 0;
    std::vector<ElementId> elementIds;
    std::string materialName;
};

int elementDimension(ElementType type) noexcept;

struct ElementBlock {
    std::string name;
    ElementType type = ElementType::Unknown;
    std::size_t nodesPerElement = 0;
    std::vector<ElementId> ids;
    std::vector<NodeIndex> connectivity;

    std::size_t size() const noexcept ;
    bool empty() const noexcept ;
};

class MeshBinaryIO;

class MeshData {
    friend class MeshBinaryIO;
public:
    void clear() noexcept ;

    bool isEmpty() const noexcept ;
    std::size_t getNumNodes() const noexcept ;
    std::size_t getNumElements() const noexcept ;
    std::size_t getNumBlocks() const noexcept ;

    NodeIndex addNode(NodeId id, double x, double y, double z) ;

    NodeIndex addNode(double x, double y, double z) ;

    bool hasNodeId(NodeId id) const noexcept ;

    NodeIndex nodeIndex(NodeId id) const ;

    NodeId nodeId(NodeIndex index) const ;

    std::array<double, 3> nodeCoordinates(NodeIndex index) const ;

    ElementBlock& getOrCreateBlock(const std::string& name, ElementType type,
                                   std::size_t nodesPerElement = 0) ;

    void addElement(const std::string& blockName, ElementType type, ElementId id,
                    const std::vector<NodeIndex>& connectivity) ;

    void addNodeToSet(const std::string& setName, NodeIndex index) ;

    void addNodeSet(const std::string& setName, const std::vector<NodeIndex>& indices) ;

    const std::unordered_map<std::string, std::vector<NodeIndex>>& getNodeSets() const noexcept ;

    void addPhysicalGroup(const std::string& name, int dimension, int tag,
                          const std::vector<ElementId>& elementIds,
                          const std::string& materialName = {});

    const std::vector<MeshPhysicalGroup>& getPhysicalGroups() const noexcept ;

    const std::vector<NodeId>& getNodeIds() const noexcept ;
    const std::vector<double>& getCoordinates() const noexcept ;
    const std::vector<ElementBlock>& getElementBlocks() const noexcept ;
    const ElementBlock& getElementBlock(const std::string& name) const ;

    // Validation of structural invariants; geometric quality checks are implemented separately.
    std::vector<std::string> validate() const ;

private:
    std::vector<NodeId> m_nodeIds;
    std::vector<double> m_coordinates; // Structure of arrays: x,y,z for each node.
    std::unordered_map<NodeId, NodeIndex> m_nodeIdToIndex;
    std::vector<ElementBlock> m_blocks;
    std::unordered_map<std::string, std::size_t> m_blockLookup;
    std::unordered_map<std::string, std::vector<NodeIndex>> m_nodeSets;
    std::vector<MeshPhysicalGroup> m_physicalGroups;
};
