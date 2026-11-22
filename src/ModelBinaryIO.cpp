#include <OpenModel/ModelBinaryIO.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#endif

/*
 * Implementation of the MGOM 1.0 layout described in docs/MGOM_FORMAT.md.
 *
 * Encoding helpers below only use the public accessors of MeshData, so the writer needs no friendship.
 * The reader parses every section into plain value types first (ParsedModel) and only then fills the
 * private members of MeshData, inside ModelBinaryIO::read(), which is a friend.
 */

namespace {

using Bytes = std::vector<std::uint8_t>;
using Error = ModelBinaryIO::Error;

bool hostLittleEndian() noexcept {
    const std::uint16_t one = 1;
    return *reinterpret_cast<const unsigned char*>(&one) == 1;
}

constexpr std::array<char, 8> kMagic{{'M', 'G', 'O', 'M', '\r', '\n', 0x1a, '\n'}};
constexpr std::uint16_t kMajor = 1;
constexpr std::uint16_t kMinor = 1; // 1.1 adds the coordinate mode byte; 1.0 files are still read as lossless
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kDirectoryEntrySize = 40;
constexpr std::uint32_t kSectionCount = 9;
constexpr std::uint32_t kCodecStored = 0;
constexpr std::uint8_t kConnectivityRaw = 0;
constexpr std::uint8_t kConnectivityDelta = 1;

enum SectionId : std::uint32_t {
    Strings = 1,
    NodeIds = 2,
    Coordinates = 3,
    ElementBlocks = 4,
    NodeSets = 5,
    PhysicalGroups = 6,
    MeshGroups = 7,
    Constraints = 8,
    Bodies = 9,
};

// ---------------------------------------------------------------------------------------------------------------------
// Element type codes: fixed by the format, independent of the C++ enum representation.
// ---------------------------------------------------------------------------------------------------------------------
struct TypeCode {
    ElementType type;
    std::uint8_t code;
};
constexpr std::array<TypeCode, 17> kTypeCodes{{
    {ElementType::Unknown, 0},        {ElementType::Point1, 1},        {ElementType::Line2, 2},
    {ElementType::Line3, 3},          {ElementType::Triangle3, 4},     {ElementType::Triangle6, 5},
    {ElementType::Quadrilateral4, 6}, {ElementType::Quadrilateral8, 7}, {ElementType::Quadrilateral9, 8},
    {ElementType::Tetrahedron4, 9},   {ElementType::Tetrahedron10, 10}, {ElementType::Hexahedron8, 11},
    {ElementType::Hexahedron20, 12},  {ElementType::Hexahedron27, 13}, {ElementType::Prism6, 14},
    {ElementType::Prism15, 15},       {ElementType::Pyramid5, 16},
}};

std::uint8_t typeToCode(ElementType type) {
    for (const auto& entry : kTypeCodes)
        if (entry.type == type) return entry.code;
    throw Error("MGOM: unsupported element type");
}

ElementType codeToType(std::uint8_t code) {
    for (const auto& entry : kTypeCodes)
        if (entry.code == code) return entry.type;
    throw Error("MGOM: unknown element type code " + std::to_string(code));
}

// ---------------------------------------------------------------------------------------------------------------------
// Zigzag mapping: small signed deltas become small unsigned values. Written in unsigned arithmetic only.
// ---------------------------------------------------------------------------------------------------------------------
std::uint64_t zigzag(std::uint64_t delta) noexcept {
    const std::uint64_t signMask = 0ull - (delta >> 63);
    return (delta << 1) ^ signMask;
}

std::uint64_t unzigzag(std::uint64_t z) noexcept {
    return (z >> 1) ^ (0ull - (z & 1ull));
}

std::uint64_t zigzagSigned(std::int64_t value) noexcept {
    return zigzag(static_cast<std::uint64_t>(value));
}

std::int64_t unzigzagSigned(std::uint64_t z) noexcept {
    return static_cast<std::int64_t>(unzigzag(z));
}

// ---------------------------------------------------------------------------------------------------------------------
// CRC-32 (IEEE 802.3, reflected), one checksum per section.
// ---------------------------------------------------------------------------------------------------------------------
// Slicing-by-8 tables: table[0] is the classic byte table; table[k][i] is the CRC of byte i followed by k zero bytes.
constexpr std::array<std::array<std::uint32_t, 256>, 8> makeCrcTables() {
    std::array<std::array<std::uint32_t, 256>, 8> tables{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        tables[0][i] = c;
    }
    for (std::size_t t = 1; t < 8; ++t)
        for (std::size_t i = 0; i < 256; ++i)
            tables[t][i] = (tables[t - 1][i] >> 8) ^ tables[0][tables[t - 1][i] & 0xFFu];
    return tables;
}
constexpr auto kCrcTables = makeCrcTables();

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    std::size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        const std::uint32_t lo = crc ^ (static_cast<std::uint32_t>(data[i]) | (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                        (static_cast<std::uint32_t>(data[i + 2]) << 16) |
                                        (static_cast<std::uint32_t>(data[i + 3]) << 24));
        const std::uint32_t hi = static_cast<std::uint32_t>(data[i + 4]) | (static_cast<std::uint32_t>(data[i + 5]) << 8) |
                                 (static_cast<std::uint32_t>(data[i + 6]) << 16) |
                                 (static_cast<std::uint32_t>(data[i + 7]) << 24);
        crc = kCrcTables[7][lo & 0xFFu] ^ kCrcTables[6][(lo >> 8) & 0xFFu] ^ kCrcTables[5][(lo >> 16) & 0xFFu] ^
              kCrcTables[4][lo >> 24] ^ kCrcTables[3][hi & 0xFFu] ^ kCrcTables[2][(hi >> 8) & 0xFFu] ^
              kCrcTables[1][(hi >> 16) & 0xFFu] ^ kCrcTables[0][hi >> 24];
    }
    for (; i < size; ++i) crc = kCrcTables[0][(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------------------------------------------------
// Encoding sink.
// ---------------------------------------------------------------------------------------------------------------------
class Sink {
public:
    Bytes bytes;

    void u8(std::uint8_t v) { bytes.push_back(v); }
    void u16(std::uint16_t v) {
        for (int i = 0; i < 2; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void f64(double value) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof bits);
        u64(bits);
    }
    void varint(std::uint64_t v) {
        while (v >= 0x80u) {
            bytes.push_back(static_cast<std::uint8_t>(v | 0x80u));
            v >>= 7;
        }
        bytes.push_back(static_cast<std::uint8_t>(v));
    }
    void svarint(std::int64_t v) { varint(zigzagSigned(v)); }
    void text(const std::string& s) {
        varint(s.size());
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
    void append(const Bytes& other) { bytes.insert(bytes.end(), other.begin(), other.end()); }

    // Bulk writes: on little-endian hosts the in-memory representation already is the file representation.
    void f64Block(const double* values, std::size_t n) {
        const auto old = bytes.size();
        bytes.resize(old + n * 8);
        if (hostLittleEndian()) {
            std::memcpy(bytes.data() + old, values, n * 8);
            return;
        }
        for (std::size_t i = 0; i < n; ++i) {
            std::uint64_t bits = 0;
            std::memcpy(&bits, &values[i], 8);
            for (int k = 0; k < 8; ++k) bytes[old + i * 8 + k] = static_cast<std::uint8_t>(bits >> (8 * k));
        }
    }
    void u32Block(const std::uint32_t* values, std::size_t n) {
        const auto old = bytes.size();
        bytes.resize(old + n * 4);
        if (hostLittleEndian()) {
            std::memcpy(bytes.data() + old, values, n * 4);
            return;
        }
        for (std::size_t i = 0; i < n; ++i)
            for (int k = 0; k < 4; ++k) bytes[old + i * 4 + k] = static_cast<std::uint8_t>(values[i] >> (8 * k));
    }
    void u64Block(const std::uint64_t* values, std::size_t n) {
        const auto old = bytes.size();
        bytes.resize(old + n * 8);
        if (hostLittleEndian()) {
            std::memcpy(bytes.data() + old, values, n * 8);
            return;
        }
        for (std::size_t i = 0; i < n; ++i)
            for (int k = 0; k < 8; ++k) bytes[old + i * 8 + k] = static_cast<std::uint8_t>(values[i] >> (8 * k));
    }
};

std::size_t varintSize(std::uint64_t v) noexcept {
    std::size_t n = 1;
    while (v >= 0x80u) {
        v >>= 7;
        ++n;
    }
    return n;
}

// Delta run without a count prefix: the first value is relative to zero, each next one to its predecessor.
void putDeltaRun(Sink& s, const std::vector<std::uint64_t>& values) {
    s.bytes.reserve(s.bytes.size() + values.size() * 2); // typical deltas are one or two bytes
    std::uint64_t previous = 0;
    for (const auto value : values) {
        s.varint(zigzag(value - previous));
        previous = value;
    }
}

// Count-prefixed delta run.
void putDeltaList(Sink& s, const std::vector<std::uint64_t>& values) {
    s.varint(values.size());
    putDeltaRun(s, values);
}

class StringTable {
public:
    std::uint64_t ref(const std::string& value) {
        const auto found = index_.find(value);
        if (found != index_.end()) return found->second;
        const auto id = static_cast<std::uint64_t>(values_.size());
        values_.push_back(value);
        index_.emplace(value, id);
        return id;
    }
    const std::vector<std::string>& values() const noexcept { return values_; }

private:
    std::vector<std::string> values_;
    std::unordered_map<std::string, std::uint64_t> index_;
};

// ---------------------------------------------------------------------------------------------------------------------
// Decoding cursor: bounds-checked reads over one section payload or the header.
// ---------------------------------------------------------------------------------------------------------------------
class Cursor {
public:
    Cursor(const std::uint8_t* data, std::size_t size, const char* what) noexcept
        : pos_(data), end_(data + size), what_(what) {}

    std::uint8_t u8() {
        need(1);
        return *pos_++;
    }
    std::uint16_t u16() {
        need(2);
        std::uint16_t v = 0;
        for (int i = 0; i < 2; ++i) v |= static_cast<std::uint16_t>(static_cast<std::uint16_t>(pos_[i]) << (8 * i));
        pos_ += 2;
        return v;
    }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(pos_[i]) << (8 * i);
        pos_ += 4;
        return v;
    }
    std::uint64_t u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(pos_[i]) << (8 * i);
        pos_ += 8;
        return v;
    }
    double f64() {
        const auto bits = u64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }
    std::uint64_t varint() {
        if (remaining() >= 10) {
            const std::uint8_t* p = pos_;
            std::uint64_t v = 0;
            for (unsigned i = 0; i < 10; ++i) {
                const std::uint8_t byte = p[i];
                if (i == 9 && (byte & 0x7Eu) != 0) break; // would overflow 64 bits
                v |= static_cast<std::uint64_t>(byte & 0x7Fu) << (7 * i);
                if ((byte & 0x80u) == 0) {
                    pos_ = p + i + 1;
                    return v;
                }
            }
            throw Error(std::string(what_) + ": malformed varint");
        }
        std::uint64_t v = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            const auto byte = u8();
            if (shift == 63 && (byte & 0x7Eu) != 0) break; // would overflow 64 bits
            v |= static_cast<std::uint64_t>(byte & 0x7Fu) << shift;
            if ((byte & 0x80u) == 0) return v;
        }
        throw Error(std::string(what_) + ": malformed varint");
    }
    std::int64_t svarint() { return unzigzagSigned(varint()); }
    int svarintInt() {
        const auto v = svarint();
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
            throw Error(std::string(what_) + ": integer out of range");
        return static_cast<int>(v);
    }
    std::string text() {
        const auto size = varint();
        need(size);
        std::string value(reinterpret_cast<const char*>(pos_), static_cast<std::size_t>(size));
        pos_ += size;
        return value;
    }
    // Count of items that each take at least `minBytesPerItem` bytes; rejects counts the payload cannot hold.
    std::uint64_t count(std::uint64_t minBytesPerItem) {
        const auto n = varint();
        if (minBytesPerItem != 0 && n > remaining() / minBytesPerItem)
            throw Error(std::string(what_) + ": count exceeds the section size");
        return n;
    }
    // Returns a pointer to the next `n` bytes and skips them; the caller converts them.
    const std::uint8_t* take(std::uint64_t n) {
        need(n);
        const auto* p = pos_;
        pos_ += n;
        return p;
    }
    std::size_t remaining() const noexcept { return static_cast<std::size_t>(end_ - pos_); }
    void expectEnd() const {
        if (pos_ != end_) throw Error(std::string(what_) + ": unexpected trailing bytes");
    }

private:
    void need(std::uint64_t n) const {
        if (n > static_cast<std::uint64_t>(end_ - pos_)) throw Error(std::string(what_) + ": truncated section");
    }

    const std::uint8_t* pos_;
    const std::uint8_t* end_;
    const char* what_;
};

std::vector<std::uint64_t> getDeltaRun(Cursor& c, std::uint64_t count) {
    std::vector<std::uint64_t> values;
    values.reserve(static_cast<std::size_t>(count));
    std::uint64_t previous = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        previous += unzigzag(c.varint());
        values.push_back(previous);
    }
    return values;
}

std::vector<std::uint64_t> getDeltaList(Cursor& c) {
    const auto count = c.count(1);
    return getDeltaRun(c, count);
}

// ---------------------------------------------------------------------------------------------------------------------
// Section encoders. Each returns the payload and the item count recorded in the directory.
// ---------------------------------------------------------------------------------------------------------------------
struct EncodedSection {
    std::uint32_t id = 0;
    std::uint64_t count = 0;
    Bytes data;
};

EncodedSection encodeNodeIds(const MeshData& mesh) {
    Sink s;
    const auto& ids = mesh.getNodeIds();
    s.varint(ids.size());
    putDeltaRun(s, ids);
    return {NodeIds, ids.size(), std::move(s.bytes)};
}

// Quantized coordinates: per axis, the exact minimum and maximum (binary64), then one unsigned integer per value,
// q = round((v - min) / (max - min) * (2^bits - 1)). Reconstruction is min + q * (max - min) / (2^bits - 1).
// The error per value is at most half a step, i.e. (max - min) / (2 * (2^bits - 1)).
unsigned coordinateBits(ModelBinaryIO::CoordinateEncoding encoding) {
    switch (encoding) {
    case ModelBinaryIO::CoordinateEncoding::Exact64: return 0;
    case ModelBinaryIO::CoordinateEncoding::Quantized16: return 16;
    case ModelBinaryIO::CoordinateEncoding::Quantized32: return 32;
    }
    throw Error("MGOM: unknown coordinate encoding");
}

void encodeQuantized(Sink& s, const std::vector<double>& coords, unsigned bits) {
    const auto nodes = coords.size() / 3;
    std::array<double, 3> lo{}, hi{};
    if (nodes > 0)
        for (std::size_t a = 0; a < 3; ++a) lo[a] = hi[a] = coords[a];
    for (std::size_t i = 0; i < coords.size(); ++i) {
        const auto value = coords[i];
        if (!std::isfinite(value)) throw Error("MGOM: non-finite coordinate cannot be quantized");
        const auto a = i % 3;
        lo[a] = std::min(lo[a], value);
        hi[a] = std::max(hi[a], value);
    }
    s.u8(static_cast<std::uint8_t>(bits));
    for (std::size_t a = 0; a < 3; ++a) {
        s.f64(lo[a]);
        s.f64(hi[a]);
    }
    const double levels = static_cast<double>((std::uint64_t{1} << bits) - 1);
    for (std::size_t i = 0; i < coords.size(); ++i) {
        const auto a = i % 3;
        const double range = hi[a] - lo[a];
        double q = range > 0.0 ? std::round((coords[i] - lo[a]) / range * levels) : 0.0;
        q = std::min(std::max(q, 0.0), levels);
        const auto code = static_cast<std::uint64_t>(q);
        if (bits == 16) s.u16(static_cast<std::uint16_t>(code));
        else s.u32(static_cast<std::uint32_t>(code));
    }
}

EncodedSection encodeCoordinates(const MeshData& mesh, ModelBinaryIO::CoordinateEncoding encoding) {
    Sink s;
    const auto& coords = mesh.getCoordinates();
    const auto nodes = coords.size() / 3;
    s.varint(nodes);
    const unsigned bits = coordinateBits(encoding);
    if (bits == 0) {
        s.u8(0);
        s.f64Block(coords.data(), coords.size());
    } else {
        encodeQuantized(s, coords, bits);
    }
    return {Coordinates, nodes, std::move(s.bytes)};
}

EncodedSection encodeElementBlocks(const MeshData& mesh, StringTable& strings, bool narrow) {
    Sink s;
    const auto& blocks = mesh.getElementBlocks();
    s.varint(blocks.size());
    for (const auto& block : blocks) {
        s.varint(strings.ref(block.name));
        s.u8(typeToCode(block.type));
        s.varint(block.nodesPerElement);
        s.varint(block.ids.size());
        s.u8(narrow ? 4 : 8);
        if (block.connectivity.size() != block.ids.size() * block.nodesPerElement)
            throw Error("MGOM: block '" + block.name + "' connectivity does not match its element count");

        // Choose the connectivity encoding per block: raw fixed-width indices, or zigzag deltas over the flat
        // connectivity array. Deltas win for structured numbering; raw wins when node numbering is scattered.
        const auto& conn = block.connectivity;
        const std::size_t width = narrow ? 4 : 8;
        std::size_t deltaBytes = 0;
        std::uint64_t previous = 0;
        for (const auto index : conn) {
            deltaBytes += varintSize(zigzag(index - previous));
            previous = index;
        }
        const bool useDelta = deltaBytes < conn.size() * width;
        s.u8(useDelta ? kConnectivityDelta : kConnectivityRaw);
        putDeltaRun(s, block.ids);
        if (useDelta) {
            putDeltaRun(s, conn);
        } else if (narrow) {
            std::vector<std::uint32_t> narrowed(conn.size());
            for (std::size_t i = 0; i < conn.size(); ++i) narrowed[i] = static_cast<std::uint32_t>(conn[i]);
            s.u32Block(narrowed.data(), narrowed.size());
        } else {
            s.u64Block(conn.data(), conn.size());
        }
    }
    return {ElementBlocks, blocks.size(), std::move(s.bytes)};
}

EncodedSection encodeNodeSets(const MeshData& mesh, StringTable& strings) {
    Sink s;
    const auto& sets = mesh.getNodeSets();
    std::vector<std::string> names;
    names.reserve(sets.size());
    for (const auto& entry : sets) names.push_back(entry.first);
    std::sort(names.begin(), names.end()); // deterministic output despite the unordered map

    s.varint(names.size());
    for (const auto& name : names) {
        s.varint(strings.ref(name));
        putDeltaList(s, sets.at(name));
    }
    return {NodeSets, names.size(), std::move(s.bytes)};
}

EncodedSection encodePhysicalGroups(const MeshData& mesh, StringTable& strings) {
    Sink s;
    const auto& groups = mesh.getPhysicalGroups();
    s.varint(groups.size());
    for (const auto& group : groups) {
        s.varint(strings.ref(group.name));
        s.svarint(group.dimension);
        s.svarint(group.tag);
        s.varint(strings.ref(group.materialName));
        putDeltaList(s, group.elementIds);
    }
    return {PhysicalGroups, groups.size(), std::move(s.bytes)};
}

EncodedSection encodeMeshGroups(const MeshData& mesh, StringTable& strings) {
    Sink s;
    const auto& groups = mesh.getMeshGroupRegistry().groups();
    s.varint(groups.size());
    for (const auto& group : groups) {
        s.varint(strings.ref(group.name));
        s.svarint(group.dimension);
        s.u8(static_cast<std::uint8_t>(group.source));
        s.varint(group.sourceEntityIds.size());
        for (const int id : group.sourceEntityIds) s.svarint(id);
        putDeltaList(s, group.elementIds);
        putDeltaList(s, group.nodeIndices);
    }
    return {MeshGroups, groups.size(), std::move(s.bytes)};
}

EncodedSection encodeConstraints(const MeshData& mesh) {
    Sink s;
    const auto& constraints = mesh.getConstraints();
    s.varint(constraints.size());
    std::uint64_t previous = 0;
    for (const auto& entry : constraints) {
        const auto& constraint = entry.second;
        if (constraint.dofs == 0 || (constraint.dofs & ~kDofAll) != 0)
            throw Error("MGOM: constraint with an empty or invalid degree-of-freedom mask");
        s.varint(zigzag(constraint.node - previous));
        previous = constraint.node;
        s.u8(constraint.dofs);
        // Only constrained degrees of freedom carry a value; the others are zero by definition.
        for (unsigned bit = 0; bit < 6; ++bit)
            if (constraint.dofs & (1u << bit)) s.f64(constraint.values[bit]);
    }
    return {Constraints, constraints.size(), std::move(s.bytes)};
}

EncodedSection encodeBodies(const MeshData& mesh, StringTable& strings) {
    Sink s;
    const auto& bodies = mesh.getBodies();
    s.varint(bodies.size());
    for (const auto& body : bodies) {
        s.varint(strings.ref(body.name));
        s.svarint(body.dimension);
        putDeltaList(s, body.elementIds);
    }
    return {Bodies, bodies.size(), std::move(s.bytes)};
}

Bytes encodeFile(const MeshData& mesh, ModelBinaryIO::CoordinateEncoding coordinateEncoding) {
    const bool narrow = mesh.getNumNodes() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    StringTable strings;

    std::vector<EncodedSection> sections;
    sections.push_back(encodeNodeIds(mesh));
    sections.push_back(encodeCoordinates(mesh, coordinateEncoding));
    sections.push_back(encodeElementBlocks(mesh, strings, narrow));
    sections.push_back(encodeNodeSets(mesh, strings));
    sections.push_back(encodePhysicalGroups(mesh, strings));
    sections.push_back(encodeMeshGroups(mesh, strings));
    sections.push_back(encodeConstraints(mesh));
    sections.push_back(encodeBodies(mesh, strings));

    // The string table is complete only after every other section has been encoded.
    Sink stringSink;
    stringSink.varint(strings.values().size());
    for (const auto& value : strings.values()) stringSink.text(value);
    sections.push_back({Strings, strings.values().size(), std::move(stringSink.bytes)});

    std::uint64_t elementCount = 0;
    for (const auto& block : mesh.getElementBlocks()) elementCount += block.ids.size();

    // Layout: header, section payloads in order, then the directory (so the header can point at it).
    Bytes file(kHeaderSize, 0);
    struct Entry {
        std::uint32_t id;
        std::uint64_t offset;
        std::uint64_t size;
        std::uint64_t count;
        std::uint32_t crc;
    };
    std::vector<Entry> directory;
    std::sort(sections.begin(), sections.end(),
              [](const EncodedSection& a, const EncodedSection& b) { return a.id < b.id; });
    for (const auto& section : sections) {
        directory.push_back({section.id, file.size(), section.data.size(), section.count,
                             crc32(section.data.data(), section.data.size())});
        file.insert(file.end(), section.data.begin(), section.data.end());
    }

    const auto directoryOffset = static_cast<std::uint64_t>(file.size());
    Sink dir;
    for (const auto& entry : directory) {
        dir.u32(entry.id);
        dir.u32(kCodecStored);
        dir.u64(entry.offset);
        dir.u64(entry.size);
        dir.u64(entry.count);
        dir.u32(entry.crc);
        dir.u32(0);
    }
    file.insert(file.end(), dir.bytes.begin(), dir.bytes.end());
    const auto fileSize = static_cast<std::uint64_t>(file.size());

    Sink header;
    header.bytes.insert(header.bytes.end(), kMagic.begin(), kMagic.end());
    header.u16(kMajor);
    header.u16(kMinor);
    header.u32(kEndianMarker);
    header.u64(0); // feature flags: none defined in 1.0
    header.u64(mesh.getNumNodes());
    header.u64(elementCount);
    header.u32(static_cast<std::uint32_t>(mesh.getElementBlocks().size()));
    header.u32(kSectionCount);
    header.u64(directoryOffset);
    header.u64(fileSize);
    if (header.bytes.size() != kHeaderSize) throw Error("MGOM: internal header size mismatch");
    std::copy(header.bytes.begin(), header.bytes.end(), file.begin());
    return file;
}

// ---------------------------------------------------------------------------------------------------------------------
// Decoding.
// ---------------------------------------------------------------------------------------------------------------------
struct SectionRecord {
    std::uint32_t id = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint64_t count = 0;
    std::uint32_t crc = 0;
};

// The mesh contents after parsing, before they are installed into MeshData.
struct ParsedModel {
    std::vector<NodeId> nodeIds;
    std::vector<double> coordinates;
    std::vector<ElementBlock> blocks;
    std::unordered_map<std::string, std::vector<NodeIndex>> nodeSets;
    std::vector<MeshPhysicalGroup> physicalGroups;
    std::vector<MeshGroup> meshGroups;
    std::map<NodeIndex, NodeConstraint> constraints;
    std::vector<MeshBody> bodies;
};

Bytes readWholeFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("MGOM: cannot open " + path.string());
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) throw Error("MGOM: cannot determine the size of " + path.string());
    in.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!in || in.gcount() != static_cast<std::streamsize>(data.size()))
            throw Error("MGOM: short read of " + path.string());
    }
    return data;
}

std::vector<SectionRecord> readDirectory(const Bytes& file, std::uint64_t directoryOffset, std::uint64_t fileSize) {
    std::vector<SectionRecord> records;
    records.reserve(kSectionCount);
    Cursor c(file.data() + directoryOffset, static_cast<std::size_t>(fileSize - directoryOffset), "MGOM directory");
    std::array<bool, kSectionCount + 1> seen{};
    for (std::uint32_t i = 0; i < kSectionCount; ++i) {
        SectionRecord r;
        r.id = c.u32();
        const auto codec = c.u32();
        r.offset = c.u64();
        r.size = c.u64();
        r.count = c.u64();
        r.crc = c.u32();
        const auto reserved = c.u32();
        if (codec != kCodecStored || reserved != 0) throw Error("MGOM: unsupported section codec or reserved bits");
        if (r.id < 1 || r.id > kSectionCount) throw Error("MGOM: unknown section id " + std::to_string(r.id));
        if (seen[r.id]) throw Error("MGOM: duplicate section id " + std::to_string(r.id));
        seen[r.id] = true;
        if (r.offset < kHeaderSize || r.offset > directoryOffset || r.size > directoryOffset - r.offset)
            throw Error("MGOM: section " + std::to_string(r.id) + " lies outside the payload area");
        records.push_back(r);
    }
    c.expectEnd();

    std::sort(records.begin(), records.end(),
              [](const SectionRecord& a, const SectionRecord& b) { return a.offset < b.offset; });
    for (std::size_t i = 1; i < records.size(); ++i)
        if (records[i - 1].offset + records[i - 1].size > records[i].offset)
            throw Error("MGOM: overlapping sections");
    for (const auto& r : records)
        if (crc32(file.data() + r.offset, static_cast<std::size_t>(r.size)) != r.crc)
            throw Error("MGOM: checksum mismatch in section " + std::to_string(r.id));
    return records;
}

const SectionRecord& findSection(const std::vector<SectionRecord>& records, std::uint32_t id) {
    for (const auto& r : records)
        if (r.id == id) return r;
    throw Error("MGOM: missing section " + std::to_string(id));
}

Cursor sectionCursor(const Bytes& file, const SectionRecord& r, const char* what) {
    return Cursor(file.data() + r.offset, static_cast<std::size_t>(r.size), what);
}

std::vector<std::string> decodeStrings(const Bytes& file, const SectionRecord& r) {
    auto c = sectionCursor(file, r, "MGOM strings");
    const auto count = c.count(1);
    std::vector<std::string> strings;
    strings.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t i = 0; i < count; ++i) strings.push_back(c.text());
    c.expectEnd();
    return strings;
}

const std::string& stringAt(const std::vector<std::string>& strings, std::uint64_t ref) {
    if (ref >= strings.size()) throw Error("MGOM: string reference out of range");
    return strings[static_cast<std::size_t>(ref)];
}

std::vector<NodeIndex> checkedIndices(std::vector<std::uint64_t> values, std::uint64_t nodeCount) {
    for (const auto v : values)
        if (v >= nodeCount) throw Error("MGOM: node index out of range");
    return values;
}

// Reads the coordinate payload that follows the node count: mode byte, then exact or quantized values.
void decodeCoordinates(Cursor& c, std::size_t nodes, std::vector<double>& out, bool hasModeByte) {
    const std::size_t values = nodes * 3;
    const std::uint8_t mode = hasModeByte ? c.u8() : 0;
    out.resize(values);
    if (mode == 0) {
        const auto* raw = c.take(values * 8);
        if (hostLittleEndian()) {
            std::memcpy(out.data(), raw, values * 8);
        } else {
            for (std::size_t i = 0; i < values; ++i) {
                std::uint64_t bits = 0;
                for (int k = 0; k < 8; ++k) bits |= static_cast<std::uint64_t>(raw[i * 8 + k]) << (8 * k);
                std::memcpy(&out[i], &bits, 8);
            }
        }
        return;
    }
    if (mode != 16 && mode != 32) throw Error("MGOM: unknown coordinate mode");

    std::array<double, 3> lo{}, hi{};
    for (std::size_t a = 0; a < 3; ++a) {
        lo[a] = c.f64();
        hi[a] = c.f64();
        if (!std::isfinite(lo[a]) || !std::isfinite(hi[a]) || lo[a] > hi[a])
            throw Error("MGOM: invalid quantization range");
    }
    const std::size_t width = mode / 8;
    const double levels = static_cast<double>((std::uint64_t{1} << mode) - 1);
    const auto* raw = c.take(values * width);
    for (std::size_t i = 0; i < values; ++i) {
        const auto a = i % 3;
        std::uint64_t code = 0;
        for (std::size_t k = 0; k < width; ++k) code |= static_cast<std::uint64_t>(raw[i * width + k]) << (8 * k);
        if (static_cast<double>(code) > levels) throw Error("MGOM: quantized coordinate out of range");
        const double range = hi[a] - lo[a];
        out[i] = range > 0.0 ? lo[a] + static_cast<double>(code) * (range / levels) : lo[a];
    }
}

ParsedModel parseModel(const Bytes& file, const std::vector<SectionRecord>& records, std::uint64_t nodeCount,
                       std::uint64_t elementCount, std::uint64_t blockCount, bool hasCoordinateMode) {
    ParsedModel model;
    const auto strings = decodeStrings(file, findSection(records, Strings));
    // The directory item counts are not covered by a checksum, so each one is checked against what the section
    // actually contains. Without this, a corrupted count would be accepted silently.
    const auto expectCount = [&](std::uint32_t id, std::uint64_t actual) {
        if (findSection(records, id).count != actual)
            throw Error("MGOM: directory count disagrees with section " + std::to_string(id));
    };
    expectCount(Strings, strings.size());

    {
        auto c = sectionCursor(file, findSection(records, NodeIds), "MGOM node ids");
        const auto count = c.count(1);
        if (count != nodeCount) throw Error("MGOM: node count disagrees with the header");
        model.nodeIds.reserve(static_cast<std::size_t>(count));
        std::uint64_t previous = 0;
        for (std::uint64_t i = 0; i < count; ++i) {
            previous += unzigzag(c.varint());
            model.nodeIds.push_back(previous);
        }
        c.expectEnd();
    }
    expectCount(NodeIds, model.nodeIds.size());
    {
        auto c = sectionCursor(file, findSection(records, Coordinates), "MGOM coordinates");
        const auto count = c.count(2 * 3); // smallest possible quantized payload per node
        if (count != nodeCount) throw Error("MGOM: coordinate count disagrees with the header");
        expectCount(Coordinates, count);
        decodeCoordinates(c, static_cast<std::size_t>(count), model.coordinates, hasCoordinateMode);
        c.expectEnd();
    }
    {
        auto c = sectionCursor(file, findSection(records, ElementBlocks), "MGOM element blocks");
        const auto count = c.count(4);
        if (count != blockCount) throw Error("MGOM: block count disagrees with the header");
        model.blocks.reserve(static_cast<std::size_t>(count));
        std::uint64_t totalElements = 0;
        for (std::uint64_t b = 0; b < count; ++b) {
            ElementBlock block;
            block.name = stringAt(strings, c.varint());
            block.type = codeToType(c.u8());
            const auto nodesPerElement = c.varint();
            if (nodesPerElement > std::numeric_limits<std::size_t>::max()) throw Error("MGOM: bad nodes per element");
            block.nodesPerElement = static_cast<std::size_t>(nodesPerElement);
            const auto elements = c.count(1);
            const auto width = c.u8();
            const auto encoding = c.u8();
            if (width != 4 && width != 8) throw Error("MGOM: unsupported index width");
            if (encoding != kConnectivityRaw && encoding != kConnectivityDelta)
                throw Error("MGOM: unknown connectivity encoding");
            if (block.nodesPerElement != 0 && elements > std::numeric_limits<std::uint64_t>::max() / block.nodesPerElement)
                throw Error("MGOM: connectivity size overflow");
            const auto connectivityCount = elements * block.nodesPerElement;

            block.ids.reserve(static_cast<std::size_t>(elements));
            std::uint64_t previous = 0;
            for (std::uint64_t i = 0; i < elements; ++i) {
                previous += unzigzag(c.varint());
                block.ids.push_back(previous);
            }

            // Every connectivity entry takes at least one byte in both encodings, so a count larger than the
            // remaining payload is malformed. Checking first also keeps the reserve below bounded by the file size.
            if (connectivityCount > c.remaining()) throw Error("MGOM: truncated connectivity");
            block.connectivity.reserve(static_cast<std::size_t>(connectivityCount));
            if (encoding == kConnectivityRaw) {
                if (connectivityCount > c.remaining() / width) throw Error("MGOM: truncated connectivity");
                const auto n = static_cast<std::size_t>(connectivityCount);
                const auto* raw = c.take(connectivityCount * width);
                block.connectivity.resize(n);
                if (width == 8 && hostLittleEndian()) {
                    std::memcpy(block.connectivity.data(), raw, n * 8);
                } else {
                    for (std::size_t i = 0; i < n; ++i) {
                        std::uint64_t v = 0;
                        for (std::size_t k = 0; k < width; ++k)
                            v |= static_cast<std::uint64_t>(raw[i * width + k]) << (8 * k);
                        block.connectivity[i] = v;
                    }
                }
            } else {
                std::uint64_t prev = 0;
                for (std::uint64_t i = 0; i < connectivityCount; ++i) {
                    prev += unzigzag(c.varint());
                    block.connectivity.push_back(prev);
                }
            }
            block.connectivity = checkedIndices(std::move(block.connectivity), nodeCount);
            totalElements += elements;
            model.blocks.push_back(std::move(block));
        }
        if (totalElements != elementCount) throw Error("MGOM: element count disagrees with the header");
        c.expectEnd();
    }
    {
        auto c = sectionCursor(file, findSection(records, NodeSets), "MGOM node sets");
        const auto count = c.count(2);
        for (std::uint64_t i = 0; i < count; ++i) {
            auto name = stringAt(strings, c.varint());
            auto indices = checkedIndices(getDeltaList(c), nodeCount);
            if (!model.nodeSets.emplace(std::move(name), std::move(indices)).second)
                throw Error("MGOM: duplicate node set name");
        }
        c.expectEnd();
    }
    {
        auto c = sectionCursor(file, findSection(records, PhysicalGroups), "MGOM physical groups");
        const auto count = c.count(4);
        model.physicalGroups.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            MeshPhysicalGroup group;
            group.name = stringAt(strings, c.varint());
            group.dimension = c.svarintInt();
            group.tag = c.svarintInt();
            group.materialName = stringAt(strings, c.varint());
            group.elementIds = getDeltaList(c);
            model.physicalGroups.push_back(std::move(group));
        }
        c.expectEnd();
    }
    {
        auto c = sectionCursor(file, findSection(records, MeshGroups), "MGOM mesh groups");
        const auto count = c.count(4);
        model.meshGroups.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            MeshGroup group;
            group.name = stringAt(strings, c.varint());
            group.dimension = c.svarintInt();
            const auto source = c.u8();
            if (source > static_cast<std::uint8_t>(MeshGroupSource::Imported))
                throw Error("MGOM: unknown mesh group source");
            group.source = static_cast<MeshGroupSource>(source);
            const auto entities = c.count(1);
            group.sourceEntityIds.reserve(static_cast<std::size_t>(entities));
            for (std::uint64_t e = 0; e < entities; ++e) group.sourceEntityIds.push_back(c.svarintInt());
            group.elementIds = getDeltaList(c);
            group.nodeIndices = checkedIndices(getDeltaList(c), nodeCount);
            model.meshGroups.push_back(std::move(group));
        }
        c.expectEnd();
    }
    {
        auto c = sectionCursor(file, findSection(records, Constraints), "MGOM constraints");
        const auto count = c.count(2);
        std::uint64_t previous = 0;
        for (std::uint64_t i = 0; i < count; ++i) {
            NodeConstraint constraint;
            previous += unzigzag(c.varint());
            constraint.node = previous;
            if (constraint.node >= nodeCount) throw Error("MGOM: constrained node out of range");
            const auto mask = c.u8();
            if (mask == 0 || (mask & ~kDofAll) != 0) throw Error("MGOM: invalid constraint mask");
            constraint.dofs = mask;
            for (unsigned bit = 0; bit < 6; ++bit)
                if (mask & (1u << bit)) constraint.values[bit] = c.f64();
            if (!model.constraints.emplace(constraint.node, constraint).second)
                throw Error("MGOM: duplicate constraint");
        }
        c.expectEnd();
    }
    {
        auto c = sectionCursor(file, findSection(records, Bodies), "MGOM bodies");
        const auto count = c.count(3);
        model.bodies.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i) {
            MeshBody body;
            body.name = stringAt(strings, c.varint());
            body.dimension = c.svarintInt();
            body.elementIds = getDeltaList(c);
            model.bodies.push_back(std::move(body));
        }
        c.expectEnd();
    }
    expectCount(ElementBlocks, model.blocks.size());
    expectCount(NodeSets, model.nodeSets.size());
    expectCount(PhysicalGroups, model.physicalGroups.size());
    expectCount(MeshGroups, model.meshGroups.size());
    expectCount(Constraints, model.constraints.size());
    expectCount(Bodies, model.bodies.size());
    return model;
}

} // namespace

void ModelBinaryIO::write(const std::filesystem::path& path, const MeshData& mesh, const Options& options) {
    if (options.validateMesh) {
        const auto errors = mesh.validate();
        if (!errors.empty()) throw Error("MGOM: refusing to write invalid mesh: " + errors.front());
    }
    const Bytes file = encodeFile(mesh, options.coordinates);

    const auto nonce = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto temporary = std::filesystem::path(path.string() + ".tmp." + std::to_string(nonce));
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw Error("MGOM: cannot create " + temporary.string());
        out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
        out.flush();
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw Error("MGOM: write failed for " + temporary.string());
        }
    }

#if defined(_WIN32)
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw Error("MGOM: cannot replace " + path.string());
    }
#else
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw Error("MGOM: cannot replace " + path.string() + ": " + ec.message());
    }
#endif
}

MeshData ModelBinaryIO::read(const std::filesystem::path& path, const Options& options) {
    const Bytes file = readWholeFile(path);
    if (file.size() < kHeaderSize) throw Error("MGOM: file is smaller than the header");

    Cursor h(file.data(), kHeaderSize, "MGOM header");
    for (const char expected : kMagic)
        if (static_cast<char>(h.u8()) != expected) throw Error("MGOM: bad magic");
    const auto major = h.u16();
    const auto minor = h.u16();
    if (major != kMajor || minor > kMinor) throw Error("MGOM: unsupported version");
    if (h.u32() != kEndianMarker) throw Error("MGOM: endianness marker mismatch");
    if (h.u64() != 0) throw Error("MGOM: feature flags not understood by this reader");
    const auto nodeCount = h.u64();
    const auto elementCount = h.u64();
    const auto blockCount = h.u32();
    const auto sectionCount = h.u32();
    const auto directoryOffset = h.u64();
    const auto fileSize = h.u64();

    if (fileSize != file.size()) throw Error("MGOM: file size does not match the header");
    if (sectionCount != kSectionCount) throw Error("MGOM: unexpected section count");
    if (directoryOffset < kHeaderSize || directoryOffset > fileSize ||
        fileSize - directoryOffset != kSectionCount * kDirectoryEntrySize)
        throw Error("MGOM: directory is out of place");

    const auto records = readDirectory(file, directoryOffset, fileSize);
    ParsedModel parsed = parseModel(file, records, nodeCount, elementCount, blockCount, minor >= 1);

    MeshData mesh;
    mesh.m_nodeIds = std::move(parsed.nodeIds);
    mesh.m_coordinates = std::move(parsed.coordinates);
    mesh.m_nodeIdToIndex.reserve(mesh.m_nodeIds.size());
    for (std::size_t i = 0; i < mesh.m_nodeIds.size(); ++i)
        if (!mesh.m_nodeIdToIndex.emplace(mesh.m_nodeIds[i], static_cast<NodeIndex>(i)).second)
            throw Error("MGOM: duplicate node ID");

    mesh.m_blocks = std::move(parsed.blocks);
    mesh.m_blockLookup.reserve(mesh.m_blocks.size());
    for (std::size_t i = 0; i < mesh.m_blocks.size(); ++i) {
        const auto& name = mesh.m_blocks[i].name;
        if (name.empty() || !mesh.m_blockLookup.emplace(name, i).second)
            throw Error("MGOM: empty or duplicate block name");
    }

    mesh.m_nodeSets = std::move(parsed.nodeSets);
    mesh.m_physicalGroups = std::move(parsed.physicalGroups);
    mesh.m_constraints = std::move(parsed.constraints);
    for (auto& group : parsed.meshGroups) {
        try {
            mesh.m_groupRegistry.add(std::move(group));
        } catch (const std::exception& e) {
            throw Error(std::string("MGOM: invalid mesh group: ") + e.what());
        }
    }
    mesh.setBodies(std::move(parsed.bodies));

    if (options.validateMesh) {
        const auto errors = mesh.validate();
        if (!errors.empty()) throw Error("MGOM: invalid mesh: " + errors.front());
    }
    return mesh;
}
