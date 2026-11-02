#include <OpenModel/WebViewWriter.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>
#include <tuple>
#include <unordered_map>

namespace {

constexpr std::array<unsigned char, 8> kMagic{{'M', 'G', 'W', 'V', '\r', '\n', 0x1a, '\n'}};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 24;
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

/** Local corner-node polygon of one element face (3 or 4 corners). */
struct FaceDef {
    int count;
    int v[4];
};

constexpr FaceDef kTetFaces[] = {{3, {0, 1, 2, 0}}, {3, {0, 1, 3, 0}}, {3, {1, 2, 3, 0}}, {3, {0, 2, 3, 0}}};
constexpr FaceDef kHexFaces[] = {{4, {0, 1, 2, 3}}, {4, {4, 5, 6, 7}}, {4, {0, 1, 5, 4}},
                                 {4, {1, 2, 6, 5}}, {4, {2, 3, 7, 6}}, {4, {3, 0, 4, 7}}};
constexpr FaceDef kPrismFaces[] = {{3, {0, 1, 2, 0}}, {3, {3, 4, 5, 0}}, {4, {0, 1, 4, 3}},
                                   {4, {1, 2, 5, 4}}, {4, {2, 0, 3, 5}}};
constexpr FaceDef kPyramidFaces[] = {{4, {0, 1, 2, 3}}, {3, {0, 1, 4, 0}}, {3, {1, 2, 4, 0}},
                                     {3, {2, 3, 4, 0}}, {3, {3, 0, 4, 0}}};

/** Face table of a 3D element type, or nullptr when the type has none. */
const FaceDef* faceTable(ElementType type, std::size_t& count) noexcept {
    switch (type) {
    case ElementType::Tetrahedron4:
    case ElementType::Tetrahedron10: count = 4; return kTetFaces;
    case ElementType::Hexahedron8:
    case ElementType::Hexahedron20:
    case ElementType::Hexahedron27: count = 6; return kHexFaces;
    case ElementType::Prism6:
    case ElementType::Prism15: count = 5; return kPrismFaces;
    case ElementType::Pyramid5: count = 5; return kPyramidFaces;
    default: count = 0; return nullptr;
    }
}

/** Number of corner nodes of a 2D element, or 0 when the type is not a surface element. */
int surfaceCorners(ElementType type) noexcept {
    switch (type) {
    case ElementType::Triangle3:
    case ElementType::Triangle6: return 3;
    case ElementType::Quadrilateral4:
    case ElementType::Quadrilateral8:
    case ElementType::Quadrilateral9: return 4;
    default: return 0;
    }
}

const char* typeName(ElementType type) noexcept {
    switch (type) {
    case ElementType::Point1: return "Point1";
    case ElementType::Line2: return "Line2";
    case ElementType::Line3: return "Line3";
    case ElementType::Triangle3: return "Triangle3";
    case ElementType::Triangle6: return "Triangle6";
    case ElementType::Quadrilateral4: return "Quadrilateral4";
    case ElementType::Quadrilateral8: return "Quadrilateral8";
    case ElementType::Quadrilateral9: return "Quadrilateral9";
    case ElementType::Tetrahedron4: return "Tetrahedron4";
    case ElementType::Tetrahedron10: return "Tetrahedron10";
    case ElementType::Hexahedron8: return "Hexahedron8";
    case ElementType::Hexahedron20: return "Hexahedron20";
    case ElementType::Hexahedron27: return "Hexahedron27";
    case ElementType::Prism6: return "Prism6";
    case ElementType::Prism15: return "Prism15";
    case ElementType::Pyramid5: return "Pyramid5";
    default: return "Unknown";
    }
}

/** Sort the first @p count (3 or 4) corner ids ascending with a fixed compare-exchange network. */
inline void sortCorners(std::array<std::uint32_t, 4>& k, int count) noexcept {
    auto order = [&k](int i, int j) {
        if (k[j] < k[i]) std::swap(k[i], k[j]);
    };
    order(0, 1); order(1, 2); order(0, 1);
    if (count == 4) { order(2, 3); order(1, 2); order(0, 1); }
}

/** Face occurrence used to detect which faces are shared between elements. */
struct FaceRecord {
    std::array<std::uint32_t, 4> key; ///< Sorted corner nodes; the fourth is kNone for triangles.
    std::uint32_t block;
    std::uint32_t element;            ///< Element position inside its block.
    std::uint8_t local;               ///< Local face index (3D elements only).
    std::uint8_t surface;             ///< 1 when the record comes from a 2D element.

    bool operator<(const FaceRecord& o) const noexcept {
        if (key != o.key) return key < o.key;
        if (surface != o.surface) return surface < o.surface;
        if (block != o.block) return block < o.block;
        if (element != o.element) return element < o.element;
        return local < o.local;
    }
};

struct OutFace {
    std::array<std::uint32_t, 4> v;
    int count;
    std::uint32_t block;
    std::uint32_t region;
    std::uint32_t surfaceGroup;
    std::uint32_t element;   ///< Global element position (block offsets applied) of the element that owns the face.
};

struct EdgeRecord {
    std::uint32_t a, b;
    bool boundary;
    std::uint32_t block; ///< Lowest block index among the faces/lines using the edge.
    std::uint32_t region = 0; ///< Lowest non-zero volume-group index among the faces using the edge (0 = none).
};

bool hostLittleEndian() noexcept {
    const std::uint16_t one = 1;
    unsigned char first = 0;
    std::memcpy(&first, &one, 1);
    return first == 1;
}

void putU32(std::vector<unsigned char>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xffu));
}

/** Append an array of 32-bit words in little-endian order. */
void appendWords(std::vector<unsigned char>& out, const std::uint32_t* data, std::size_t count) {
    const auto old = out.size();
    out.resize(old + count * 4);
    if (count == 0) return;
    if (hostLittleEndian()) {
        std::memcpy(out.data() + old, data, count * 4);
    } else {
        for (std::size_t i = 0; i < count; ++i)
            for (int b = 0; b < 4; ++b) out[old + i * 4 + b] = static_cast<unsigned char>((data[i] >> (8 * b)) & 0xffu);
    }
}

void appendFloats(std::vector<unsigned char>& out, const std::vector<float>& values) {
    static_assert(sizeof(float) == 4, "float32 required");
    std::vector<std::uint32_t> words(values.size());
    if (!values.empty()) std::memcpy(words.data(), values.data(), values.size() * 4);
    appendWords(out, words.data(), words.size());
}

void alignBinary(std::vector<unsigned char>& out) {
    while (out.size() % 4 != 0) out.push_back(0);
}

std::string jsonEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size() + 2);
    for (const unsigned char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                r += buf;
            } else {
                r += static_cast<char>(c);
            }
        }
    }
    return r;
}

std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

WebViewResult fail(std::string message) {
    WebViewResult r;
    r.error = std::move(message);
    return r;
}

} // namespace

WebViewResult WebViewWriter::build(const MeshData& mesh, const WebViewOptions& options,
                                   std::vector<unsigned char>& image) {
    image.clear();
    const auto problems = mesh.validate();
    if (!problems.empty()) return fail("mesh is structurally invalid: " + problems.front());
    const std::size_t nodeCount = mesh.getNumNodes();
    if (nodeCount >= kNone) return fail("mesh has too many nodes for 32-bit indices");
    for (const auto& field : options.fields) {
        if (field.values.size() != nodeCount) return fail("field '" + field.name + "' must have one value per node");
        for (const double v : field.values)
            if (!std::isfinite(v)) return fail("field '" + field.name + "' contains a non-finite value");
    }
    if (!options.modes.empty() && !options.displacement.empty())
        return fail("modes and displacement are mutually exclusive");
    for (const auto& field : options.elementFields) {
        if (field.values.size() != mesh.getNumElements())
            return fail("element field '" + field.name + "' must have one value per element");
        for (const double v : field.values)
            if (!std::isfinite(v)) return fail("element field '" + field.name + "' contains a non-finite value");
    }
    for (const auto& mode : options.modes) {
        if (mode.displacement.size() != nodeCount) return fail("mode '" + mode.label + "' must have one displacement per node");
        for (const auto& d : mode.displacement)
            if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2]))
                return fail("mode '" + mode.label + "' contains a non-finite displacement");
    }
    if (!options.displacement.empty()) {
        if (options.displacement.size() != nodeCount) return fail("displacement must have one entry per node");
        for (const auto& d : options.displacement)
            if (!std::isfinite(d[0]) || !std::isfinite(d[1]) || !std::isfinite(d[2])) return fail("displacement contains a non-finite value");
    }

    const auto& blocks = mesh.getElementBlocks();
    const auto& groups = mesh.getPhysicalGroups();
    std::vector<std::uint32_t> blockOffset(blocks.size() + 1, 0);
    for (std::size_t b = 0; b < blocks.size(); ++b)
        blockOffset[b + 1] = blockOffset[b] + static_cast<std::uint32_t>(blocks[b].size());

    // Element id -> physical group (1-based index; the first matching group wins).
    std::unordered_map<ElementId, std::uint32_t> volumeGroup, surfaceGroup;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        auto& target = groups[g].dimension == 3 ? volumeGroup : surfaceGroup;
        if (groups[g].dimension != 3 && groups[g].dimension != 2) continue;
        for (const ElementId id : groups[g].elementIds) target.emplace(id, static_cast<std::uint32_t>(g + 1));
    }

    // 1. Collect every face occurrence and the line elements.
    std::vector<FaceRecord> records;
    std::vector<EdgeRecord> lineEdges;
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const auto& block = blocks[b];
        const int dim = elementDimension(block.type);
        const std::size_t npe = block.nodesPerElement;
        if (dim == 3) {
            std::size_t faceCount = 0;
            const FaceDef* table = faceTable(block.type, faceCount);
            if (!table) continue;
            records.reserve(records.size() + block.size() * faceCount);
            for (std::size_t e = 0; e < block.size(); ++e) {
                const NodeIndex* nodes = &block.connectivity[e * npe];
                for (std::size_t f = 0; f < faceCount; ++f) {
                    FaceRecord rec{};
                    rec.block = static_cast<std::uint32_t>(b);
                    rec.element = static_cast<std::uint32_t>(e);
                    rec.local = static_cast<std::uint8_t>(f);
                    rec.surface = 0;
                    rec.key = {kNone, kNone, kNone, kNone};
                    const int corners = std::min(table[f].count, 4);
                    for (int i = 0; i < corners; ++i)
                        rec.key[i] = static_cast<std::uint32_t>(nodes[table[f].v[i]]);
                    sortCorners(rec.key, corners);
                    records.push_back(rec);
                }
            }
        } else if (dim == 2) {
            const int corners = std::min(surfaceCorners(block.type), 4);
            if (corners == 0) continue;
            records.reserve(records.size() + block.size());
            for (std::size_t e = 0; e < block.size(); ++e) {
                const NodeIndex* nodes = &block.connectivity[e * npe];
                FaceRecord rec{};
                rec.block = static_cast<std::uint32_t>(b);
                rec.element = static_cast<std::uint32_t>(e);
                rec.surface = 1;
                rec.key = {kNone, kNone, kNone, kNone};
                for (int i = 0; i < corners; ++i) rec.key[i] = static_cast<std::uint32_t>(nodes[i]);
                sortCorners(rec.key, corners);
                records.push_back(rec);
            }
        } else if (dim == 1) {
            for (std::size_t e = 0; e < block.size(); ++e) {
                const NodeIndex* nodes = &block.connectivity[e * npe];
                lineEdges.push_back({static_cast<std::uint32_t>(nodes[0]), static_cast<std::uint32_t>(nodes[1]), true,
                                     static_cast<std::uint32_t>(b), 0});
            }
        }
    }
    std::sort(records.begin(), records.end());

    // Rebuilds the polygon of a record in its original winding.
    auto polygonOf = [&](const FaceRecord& rec, std::array<std::uint32_t, 4>& v) -> int {
        const auto& block = blocks[rec.block];
        const NodeIndex* nodes = &block.connectivity[static_cast<std::size_t>(rec.element) * block.nodesPerElement];
        if (rec.surface) {
            const int corners = surfaceCorners(block.type);
            for (int i = 0; i < corners; ++i) v[i] = static_cast<std::uint32_t>(nodes[i]);
            return corners;
        }
        std::size_t count = 0;
        const FaceDef* table = faceTable(block.type, count);
        const FaceDef& def = table[rec.local];
        for (int i = 0; i < def.count; ++i) v[i] = static_cast<std::uint32_t>(nodes[def.v[i]]);
        return def.count;
    };

    // 2. Merge equal faces: one run of records is one geometric face.
    std::vector<OutFace> skin, interior;
    for (std::size_t i = 0; i < records.size();) {
        std::size_t j = i + 1;
        while (j < records.size() && records[j].key == records[i].key) ++j;
        std::size_t volumeCount = 0;
        const FaceRecord* owner = nullptr;
        const FaceRecord* shell = nullptr;
        bool differentBlocks = false;
        for (std::size_t k = i; k < j; ++k) {
            if (records[k].surface) {
                if (!shell) shell = &records[k];
            } else {
                ++volumeCount;
                if (!owner) owner = &records[k];
                else if (records[k].block != owner->block) differentBlocks = true;
            }
        }
        const FaceRecord* geometry = owner ? owner : shell;
        OutFace face{};
        face.count = polygonOf(*geometry, face.v);
        // A coincident 2D element (boundary patch) names the face; otherwise the owning element does.
        face.block = shell ? shell->block : geometry->block;
        face.element = blockOffset[geometry->block] + geometry->element;
        if (owner) {
            const auto& block = blocks[owner->block];
            const auto it = volumeGroup.find(block.ids[owner->element]);
            face.region = it == volumeGroup.end() ? 0 : it->second;
        }
        if (shell) {
            const auto& block = blocks[shell->block];
            const auto it = surfaceGroup.find(block.ids[shell->element]);
            face.surfaceGroup = it == surfaceGroup.end() ? 0 : it->second;
            if (!owner) face.region = face.surfaceGroup;
        }
        const bool onSkin = volumeCount <= 1;
        if (onSkin) skin.push_back(face);
        else if (options.includeInteriorFaces || differentBlocks) interior.push_back(face);
        i = j;
    }
    records.clear();
    records.shrink_to_fit();

    // 3. Edges of every exported face plus line elements, merged and skin-first.
    std::vector<EdgeRecord> edges;
    if (options.includeEdges) {
        edges = std::move(lineEdges);
        auto addEdges = [&](const std::vector<OutFace>& faces, bool boundary) {
            for (const auto& f : faces)
                for (int i = 0; i < f.count; ++i) {
                    auto a = f.v[i], b = f.v[(i + 1) % f.count];
                    if (a > b) std::swap(a, b);
                    edges.push_back({a, b, boundary, f.block, f.region});
                }
        };
        addEdges(skin, true);
        addEdges(interior, false);
        std::sort(edges.begin(), edges.end(), [](const EdgeRecord& x, const EdgeRecord& y) {
            return std::tie(x.a, x.b) < std::tie(y.a, y.b);
        });
        std::size_t out = 0;
        for (std::size_t i = 0; i < edges.size();) {
            EdgeRecord merged = edges[i];
            std::size_t j = i + 1;
            while (j < edges.size() && edges[j].a == merged.a && edges[j].b == merged.b) {
                merged.boundary = merged.boundary || edges[j].boundary;
                merged.block = std::min(merged.block, edges[j].block);
                if (edges[j].region != 0 && (merged.region == 0 || edges[j].region < merged.region)) merged.region = edges[j].region;
                ++j;
            }
            edges[out++] = merged;
            i = j;
        }
        edges.resize(out);
        std::stable_partition(edges.begin(), edges.end(), [](const EdgeRecord& e) { return e.boundary; });
    }
    std::size_t boundaryEdges = 0;
    for (const auto& e : edges) boundaryEdges += e.boundary ? 1 : 0;

    // 4. Compact the vertices that are actually referenced.
    std::vector<std::uint32_t> remap(nodeCount, kNone);
    auto markFaces = [&](const std::vector<OutFace>& faces) {
        for (const auto& f : faces) for (int i = 0; i < f.count; ++i) remap[f.v[i]] = 0;
    };
    markFaces(skin);
    markFaces(interior);
    for (const auto& e : edges) { remap[e.a] = 0; remap[e.b] = 0; }
    std::vector<std::uint32_t> used;
    for (std::size_t n = 0; n < nodeCount; ++n)
        if (remap[n] != kNone) { remap[n] = static_cast<std::uint32_t>(used.size()); used.push_back(static_cast<std::uint32_t>(n)); }
    if (used.empty()) return fail("mesh has no renderable faces or edges");

    const auto& coords = mesh.getCoordinates();
    std::array<double, 3> lo{{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()}};
    std::array<double, 3> hi{{-lo[0], -lo[1], -lo[2]}};
    for (const auto n : used)
        for (int k = 0; k < 3; ++k) {
            const double c = coords[static_cast<std::size_t>(n) * 3 + k];
            if (!std::isfinite(c)) return fail("mesh contains a non-finite coordinate");
            lo[k] = std::min(lo[k], c);
            hi[k] = std::max(hi[k], c);
        }
    std::array<double, 3> origin{{(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2}};

    std::vector<float> positions(used.size() * 3);
    for (std::size_t i = 0; i < used.size(); ++i)
        for (int k = 0; k < 3; ++k)
            positions[i * 3 + k] = static_cast<float>(coords[static_cast<std::size_t>(used[i]) * 3 + k] - origin[k]);

    // 5. Triangulate the faces (quads become two triangles) with per-triangle attributes.
    std::vector<std::uint32_t> triIndices, triBlock, triRegion, triSurface, triElement, triFace;
    std::uint32_t nextFace = 0;
    auto addTriangles = [&](const std::vector<OutFace>& faces) {
        for (const auto& f : faces) {
            const int tris = f.count - 2;
            const std::uint32_t faceIndex = nextFace++;
            for (int t = 0; t < tris; ++t) {
                triIndices.push_back(remap[f.v[0]]);
                triIndices.push_back(remap[f.v[t + 1]]);
                triIndices.push_back(remap[f.v[t + 2]]);
                triBlock.push_back(f.block);
                triRegion.push_back(f.region);
                triSurface.push_back(f.surfaceGroup);
                triElement.push_back(f.element);
                triFace.push_back(faceIndex);
            }
        }
    };
    addTriangles(skin);
    const std::size_t boundaryTriangles = triBlock.size();
    addTriangles(interior);
    const std::size_t triangleCount = triBlock.size();

    std::vector<std::uint32_t> edgeIndices, edgeBlock, edgeRegion;
    edgeIndices.reserve(edges.size() * 2);
    edgeBlock.reserve(edges.size());
    edgeRegion.reserve(edges.size());
    for (const auto& e : edges) {
        edgeIndices.push_back(remap[e.a]);
        edgeIndices.push_back(remap[e.b]);
        edgeBlock.push_back(e.block);
        edgeRegion.push_back(e.region);
    }

    // Picking support: external node id per vertex, external element id and face index per triangle.
    // Omitted when an id does not fit 32 bits (the viewer then offers no picking).
    std::vector<std::uint32_t> vertexNodeId, triElementId;
    bool idsFit = true;
    {
        const auto& nodeIds = mesh.getNodeIds();
        vertexNodeId.reserve(used.size());
        for (const auto n : used) {
            if (nodeIds[n] > 0xFFFFFFFEull) idsFit = false;
            vertexNodeId.push_back(static_cast<std::uint32_t>(nodeIds[n]));
        }
        std::vector<std::uint32_t> elementIdByPosition;
        elementIdByPosition.reserve(mesh.getNumElements());
        for (const auto& b : blocks)
            for (const auto id : b.ids) {
                if (id > 0xFFFFFFFEull) idsFit = false;
                elementIdByPosition.push_back(static_cast<std::uint32_t>(id));
            }
        triElementId.reserve(triangleCount);
        for (const auto e : triElement) triElementId.push_back(e < elementIdByPosition.size() ? elementIdByPosition[e] : 0u);
    }

    // 6. Binary payload; offsets are relative to the start of the payload.
    std::vector<unsigned char> bin;
    struct Chunk { std::size_t offset, count; };
    auto addWords = [&](const std::vector<std::uint32_t>& data) {
        Chunk c{bin.size(), data.size()};
        appendWords(bin, data.data(), data.size());
        alignBinary(bin);
        return c;
    };
    Chunk cPos{bin.size(), positions.size()};
    appendFloats(bin, positions);
    alignBinary(bin);
    const Chunk cTri = addWords(triIndices), cBlock = addWords(triBlock), cRegion = addWords(triRegion),
                cSurf = addWords(triSurface), cEdge = addWords(edgeIndices), cEdgeBlock = addWords(edgeBlock),
                cEdgeRegion = addWords(edgeRegion);
    Chunk cNodeId{0, 0}, cElemId{0, 0}, cFace{0, 0};
    if (idsFit) { cNodeId = addWords(vertexNodeId); cElemId = addWords(triElementId); cFace = addWords(triFace); }

    struct FieldInfo { std::string name; double lo, hi; Chunk chunk; };
    std::vector<FieldInfo> fields;
    for (const auto& field : options.fields) {
        std::vector<float> values(used.size());
        double fl = std::numeric_limits<double>::infinity(), fh = -fl;
        for (std::size_t i = 0; i < used.size(); ++i) {
            const double v = field.values[used[i]];
            values[i] = static_cast<float>(v);
            fl = std::min(fl, v);
            fh = std::max(fh, v);
        }
        Chunk c{bin.size(), values.size()};
        appendFloats(bin, values);
        alignBinary(bin);
        fields.push_back({field.name, fl, fh, c});
    }

    // Per-element fields: one float32 per triangle (the value of the owning element).
    struct ElementFieldInfo { std::string name; double lo, hi; Chunk chunk; };
    std::vector<ElementFieldInfo> elementFields;
    for (const auto& field : options.elementFields) {
        std::vector<float> values(triangleCount);
        double fl = std::numeric_limits<double>::infinity(), fh = -fl;
        for (std::size_t t = 0; t < triangleCount; ++t) {
            const double v = field.values[triElement[t]];
            values[t] = static_cast<float>(v);
            fl = std::min(fl, v);
            fh = std::max(fh, v);
        }
        Chunk c{bin.size(), values.size()};
        appendFloats(bin, values);
        alignBinary(bin);
        elementFields.push_back({field.name, fl, fh, c});
    }

    // Displacement (3 float32 per stored vertex): one chunk, or one chunk per vibration mode.
    auto packDisplacement = [&](const std::vector<std::array<double, 3>>& source, double& maxMagnitude) {
        std::vector<float> d(used.size() * 3);
        for (std::size_t i = 0; i < used.size(); ++i) {
            const auto& u = source[used[i]];
            for (int k = 0; k < 3; ++k) d[i * 3 + k] = static_cast<float>(u[k]);
            maxMagnitude = std::max(maxMagnitude, std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]));
        }
        Chunk c{bin.size(), d.size()};
        appendFloats(bin, d);
        alignBinary(bin);
        return c;
    };
    const bool hasModes = !options.modes.empty();
    const bool hasDisplacement = !options.displacement.empty() || hasModes;
    Chunk cDisp{0, 0};
    double maxDisplacement = 0.0;
    struct ModeInfo { std::string label; double frequency, max; Chunk chunk; };
    std::vector<ModeInfo> modes;
    if (!options.displacement.empty()) cDisp = packDisplacement(options.displacement, maxDisplacement);
    for (const auto& mode : options.modes) {
        double modeMax = 0.0;
        const Chunk c = packDisplacement(mode.displacement, modeMax);
        maxDisplacement = std::max(maxDisplacement, modeMax);
        modes.push_back({mode.label, mode.frequency, modeMax, c});
    }

    // 7. JSON descriptor.
    std::string json = "{\"format\":\"MGWV\",\"version\":" + std::to_string(kVersion);
    json += ",\"origin\":[" + num(origin[0]) + "," + num(origin[1]) + "," + num(origin[2]) + "]";
    json += ",\"bbox\":{\"min\":[" + num(lo[0]) + "," + num(lo[1]) + "," + num(lo[2]) + "],\"max\":[" +
            num(hi[0]) + "," + num(hi[1]) + "," + num(hi[2]) + "]}";
    json += ",\"counts\":{\"nodes\":" + std::to_string(nodeCount) + ",\"elements\":" + std::to_string(mesh.getNumElements()) +
            ",\"vertices\":" + std::to_string(used.size()) + ",\"triangles\":" + std::to_string(triangleCount) +
            ",\"boundaryTriangles\":" + std::to_string(boundaryTriangles) + ",\"edges\":" + std::to_string(edges.size()) +
            ",\"boundaryEdges\":" + std::to_string(boundaryEdges) + "}";
    json += ",\"blocks\":[";
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        if (b) json += ",";
        json += "{\"name\":\"" + jsonEscape(blocks[b].name) + "\",\"type\":\"" + typeName(blocks[b].type) +
                "\",\"dimension\":" + std::to_string(elementDimension(blocks[b].type)) +
                ",\"elements\":" + std::to_string(blocks[b].size()) + "}";
    }
    json += "],\"groups\":[";
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (g) json += ",";
        json += "{\"name\":\"" + jsonEscape(groups[g].name) + "\",\"dimension\":" + std::to_string(groups[g].dimension) +
                ",\"tag\":" + std::to_string(groups[g].tag) + ",\"elements\":" + std::to_string(groups[g].elementIds.size()) +
                ",\"material\":\"" + jsonEscape(groups[g].materialName) + "\"";
        const auto color = options.groupColors.find(groups[g].name);
        if (color != options.groupColors.end()) {
            char hex[8];
            auto channel = [](float v) { return static_cast<unsigned>(std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f)); };
            std::snprintf(hex, sizeof hex, "#%02x%02x%02x", channel(color->second[0]), channel(color->second[1]), channel(color->second[2]));
            json += std::string(",\"color\":\"") + hex + "\"";
        }
        json += "}";
    }
    auto chunkJson = [](const char* name, const Chunk& c) {
        return std::string("\"") + name + "\":{\"offset\":" + std::to_string(c.offset) + ",\"count\":" + std::to_string(c.count) + "}";
    };
    json += "],\"chunks\":{" + chunkJson("positions", cPos) + "," + chunkJson("triIndices", cTri) + "," +
            chunkJson("triBlock", cBlock) + "," + chunkJson("triRegion", cRegion) + "," +
            chunkJson("triSurface", cSurf) + "," + chunkJson("edgeIndices", cEdge) + "," + chunkJson("edgeBlock", cEdgeBlock) + "," + chunkJson("edgeRegion", cEdgeRegion);
    if (idsFit) json += "," + chunkJson("vertexNodeId", cNodeId) + "," + chunkJson("triElementId", cElemId) + "," + chunkJson("triFace", cFace);
    if (!options.displacement.empty()) json += "," + chunkJson("displacement", cDisp);
    json += "},\"fields\":[";
    for (std::size_t f = 0; f < fields.size(); ++f) {
        if (f) json += ",";
        json += "{\"name\":\"" + jsonEscape(fields[f].name) + "\",\"min\":" + num(fields[f].lo) + ",\"max\":" +
                num(fields[f].hi) + ",\"offset\":" + std::to_string(fields[f].chunk.offset) + "}";
    }
    for (std::size_t f = 0; f < elementFields.size(); ++f) {
        if (!fields.empty() || f) json += ",";
        json += "{\"name\":\"" + jsonEscape(elementFields[f].name) + "\",\"location\":\"element\",\"min\":" +
                num(elementFields[f].lo) + ",\"max\":" + num(elementFields[f].hi) + ",\"offset\":" +
                std::to_string(elementFields[f].chunk.offset) + "}";
    }
    json += "]";
    if (hasDisplacement)
        json += ",\"deformation\":{\"name\":\"" + jsonEscape(options.displacementName) + "\",\"max\":" + num(maxDisplacement) + "}";
    if (hasModes) {
        json += ",\"modes\":[";
        for (std::size_t m = 0; m < modes.size(); ++m) {
            if (m) json += ",";
            json += "{\"label\":\"" + jsonEscape(modes[m].label) + "\",\"frequency\":" + num(modes[m].frequency) +
                    ",\"max\":" + num(modes[m].max) + ",\"offset\":" + std::to_string(modes[m].chunk.offset) + "}";
        }
        json += "]";
    }
    if (!options.resultsLabel.empty()) json += ",\"results\":{\"label\":\"" + jsonEscape(options.resultsLabel) + "\"}";
    json += "}";
    while (json.size() % 4 != 0) json += ' ';

    // 8. Assemble: header, JSON, payload.
    image.reserve(kHeaderSize + json.size() + bin.size());
    image.insert(image.end(), kMagic.begin(), kMagic.end());
    putU32(image, kVersion);
    putU32(image, static_cast<std::uint32_t>(json.size()));
    putU32(image, static_cast<std::uint32_t>(bin.size()));
    putU32(image, 0);
    image.insert(image.end(), json.begin(), json.end());
    image.insert(image.end(), bin.begin(), bin.end());

    WebViewResult result;
    result.success = true;
    result.vertices = used.size();
    result.triangles = triangleCount;
    result.boundaryTriangles = boundaryTriangles;
    result.edges = edges.size();
    result.boundaryEdges = boundaryEdges;
    result.bytesWritten = image.size();
    return result;
}

WebViewResult WebViewWriter::writeFile(const std::filesystem::path& path, const MeshData& mesh,
                                       const WebViewOptions& options) {
    if (path.empty()) return fail("output path is empty");
    std::vector<unsigned char> image;
    auto result = build(mesh, options, image);
    if (!result.success) return result;
    if (image.size() > 0xFFFFFFFFull) return fail("MGWV payload exceeds 4 GiB");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = std::filesystem::path(path.string() + ".tmp." + std::to_string(nonce));
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot open output file");
        out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        out.flush();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return fail("failed writing output file");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return fail("cannot publish output file");
    }
    return result;
}
