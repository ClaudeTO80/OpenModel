#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
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

// Backend-independent membership for a named mesh group. A group may contain
// both elements and nodes; memberships are many-to-many (shared nodes are valid).
// sourceEntityIds retain the originating CAD entity identifiers when available.
enum class MeshGroupSource { UserDefined, CAD, Imported };

struct MeshGroup {
    std::string name;
    int dimension = -1; // Topological dimension represented by the group: 0..3.
    MeshGroupSource source = MeshGroupSource::UserDefined;
    std::vector<int> sourceEntityIds;
    std::vector<ElementId> elementIds;
    std::vector<NodeIndex> nodeIndices;
};

/**
 * @brief A CAD body of the mesh and the dimension it was meshed in.
 *
 * A body is one top-level entity of the CAD file: a solid meshed in volume (dimension 3) or a sheet / shell meshed in
 * surface (dimension 2). Elements of a body are listed in elementIds, both volume and surface elements of its faces.
 * Exporters write only the elements of the body's own dimension (see exportableMesh()), so the boundary triangles of a
 * volume body are kept in memory for the groups and loads but are not written to the solver decks or the mesh files.
 * Elements that belong to no body are exported as before.
 */
struct MeshBody {
    std::string name;                  ///< Name of the body (CAD name when known, otherwise "Body_<index>").
    int dimension = 3;                 ///< 3 = volume mesh, 2 = surface mesh.
    std::vector<ElementId> elementIds; ///< Elements of the body, of any dimension.
};

class MeshGroupRegistry {
public:
    void clear() noexcept;
    void add(MeshGroup group);
    const std::vector<MeshGroup>& groups() const noexcept;
    const MeshGroup* find(const std::string& name) const noexcept;
private:
    std::vector<MeshGroup> m_groups;
};

/**
 * @brief Degrees of freedom of a node, as bits of a DofMask.
 *
 * Bit 0..5 are the degrees of freedom 1..6 of Abaqus and CalculiX: the translations ux, uy, uz and the rotations
 * rx, ry, rz (rotations matter for shells, beams and rigid supports; solid elements have the translations only).
 */
enum NodeDof : std::uint8_t {
    DofUx = 1u << 0, DofUy = 1u << 1, DofUz = 1u << 2,
    DofRx = 1u << 3, DofRy = 1u << 4, DofRz = 1u << 5
};
using DofMask = std::uint8_t;
constexpr DofMask kDofTranslations = DofUx | DofUy | DofUz;               ///< ux, uy, uz.
constexpr DofMask kDofRotations = DofRx | DofRy | DofRz;                  ///< rx, ry, rz.
constexpr DofMask kDofAll = kDofTranslations | kDofRotations;             ///< All six.

/**
 * @brief The degrees of freedom of one node that are constrained, and the value each is held at.
 *
 * `values[i]` is the prescribed value of degree of freedom i (a displacement in model length units or a rotation in
 * radians); it is zero for a free degree of freedom and for a plain fixed one. A constraint with an empty mask does not
 * exist: MeshData drops it.
 */
struct NodeConstraint {
    NodeIndex node = 0;                        ///< Zero-based node index, not the external NodeId.
    DofMask dofs = 0;                          ///< Constrained degrees of freedom.
    std::array<double, 6> values{};            ///< Prescribed values (zero for free degrees of freedom).
};

class MeshBinaryIO;
class ModelBinaryIO;

class MeshData {
    friend class MeshBinaryIO;
    friend class ModelBinaryIO;
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

    /**
     * @name Constraints on the degrees of freedom of the nodes
     *
     * A mesh can carry the kinematic boundary conditions of an analysis: which degrees of freedom of which nodes are
     * held, and at which value (zero for a support, non-zero for a prescribed displacement). They are independent of the
     * analysis script (AnalysisModel), which selects nodes by plane or group when the solver input is written; the
     * constraints here are already resolved to nodes, so they can be shown, imported from a solver deck and stored
     * with the mesh (MGMB 1.2).
     * @{
     */

    /**
     * @brief Constrain the degrees of freedom in `dofs` of a node.
     *
     * Degrees of freedom that are already constrained take the new value; the other degrees of freedom of the node are
     * left as they are, so successive calls accumulate.
     * @param index  Node index; throws std::out_of_range if it does not exist.
     * @param dofs   Non-empty mask of NodeDof bits; throws std::invalid_argument for an empty or out-of-range mask.
     * @param values Value of each degree of freedom (only the ones in `dofs` are read, and they must be finite).
     */
    void constrainNode(NodeIndex index, DofMask dofs, const std::array<double, 6>& values = {});

    /** @brief constrainNode() for every node of `indices`; nothing is changed when one of them is invalid. */
    void constrainNodes(const std::vector<NodeIndex>& indices, DofMask dofs, const std::array<double, 6>& values = {});

    /** @brief Free the degrees of freedom in `dofs` of a node; the constraint disappears when none is left. */
    void releaseNode(NodeIndex index, DofMask dofs = kDofAll);

    /** @brief Remove every constraint. */
    void clearConstraints() noexcept;

    /** @brief The constraint of a node, or nullptr when it has none. */
    const NodeConstraint* constraintOf(NodeIndex index) const noexcept;

    /** @brief Every constraint, ordered by node index. */
    const std::map<NodeIndex, NodeConstraint>& getConstraints() const noexcept;

    /** @brief Number of nodes with at least one constrained degree of freedom. */
    std::size_t constrainedNodeCount() const noexcept;

    /** @brief Total number of constrained degrees of freedom (a node fixed in x, y and z counts three). */
    std::size_t constrainedDofCount() const noexcept;
    /** @} */

    void addPhysicalGroup(const std::string& name, int dimension, int tag,
                          const std::vector<ElementId>& elementIds,
                          const std::string& materialName = {});

    const std::vector<MeshPhysicalGroup>& getPhysicalGroups() const noexcept ;

    // Canonical backend-independent group registry (CAD, imported or user-defined).
    void addMeshGroup(MeshGroup group);
    const MeshGroupRegistry& getMeshGroupRegistry() const noexcept;

    const std::vector<NodeId>& getNodeIds() const noexcept ;
    const std::vector<double>& getCoordinates() const noexcept ;
    const std::vector<ElementBlock>& getElementBlocks() const noexcept ;
    const ElementBlock& getElementBlock(const std::string& name) const ;

    /** @brief Bodies of the mesh with the dimension each was meshed in (empty when the mesh has no body structure). */
    void setBodies(std::vector<MeshBody> bodies);
    const std::vector<MeshBody>& getBodies() const noexcept;

    // Validation of structural invariants; geometric quality checks are implemented separately.
    std::vector<std::string> validate() const ;

private:
    std::vector<NodeId> m_nodeIds;
    std::vector<double> m_coordinates; // Structure of arrays: x,y,z for each node.
    std::unordered_map<NodeId, NodeIndex> m_nodeIdToIndex;
    std::vector<ElementBlock> m_blocks;
    std::unordered_map<std::string, std::size_t> m_blockLookup;
    std::unordered_map<std::string, std::vector<NodeIndex>> m_nodeSets;
    std::map<NodeIndex, NodeConstraint> m_constraints;
    std::vector<MeshPhysicalGroup> m_physicalGroups;
    MeshGroupRegistry m_groupRegistry;
    std::vector<MeshBody> m_bodies;
};
