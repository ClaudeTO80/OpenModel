#pragma once

#include <OpenModel/MeshData.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>

/**
 * @brief MGOM 1.0 binary persistence for the mesh model (MeshData) — compact, checksummed, fast to read and write.
 *
 * MGOM is the persistence format for the whole mesh model: nodes, element blocks, node sets, physical groups, mesh
 * groups, node constraints and CAD bodies. It is the only mesh persistence format: the former MGMB format was removed.
 * The layout is specified in docs/MGOM_FORMAT.md.
 *
 * Size and speed come from four choices:
 * - identifiers and sorted index lists are stored as zigzag-encoded deltas in LEB128 varints (sequential IDs cost one
 *   byte each instead of eight);
 * - every name (blocks, groups, sets, bodies, materials) is stored once in a string table and referenced by index;
 * - connectivity and index lists use 32-bit indices whenever the mesh has fewer than 2^32 nodes;
 * - each section is read in one pass with a CRC-32 check, so corruption is detected before any structure is built.
 *
 * Coordinates are stored as IEEE-754 binary64 by default, so the format round-trips a mesh exactly. An opt-in
 * quantized mode stores each axis as 16- or 32-bit integers between the axis minimum and maximum; it is lossy and
 * its error bound is documented in docs/MGOM_FORMAT.md.
 */
class ModelBinaryIO {
public:
    /// How node coordinates are stored. Exact64 is lossless; the other modes trade precision for size.
    enum class CoordinateEncoding : std::uint8_t {
        Exact64 = 0,   ///< IEEE-754 binary64, bit-exact round trip
        Quantized16 = 16, ///< 16-bit integers per axis; error at most range / 131070
        Quantized32 = 32, ///< 32-bit integers per axis; error at most range / 8589934590
    };

    struct Options {
        /// Validate MeshData structural invariants before writing and after reading.
        bool validateMesh;
        /// Coordinate storage used by write(). Ignored by read(), which honours the file.
        CoordinateEncoding coordinates;
        constexpr explicit Options(bool validate = true,
                                   CoordinateEncoding encoding = CoordinateEncoding::Exact64) noexcept
            : validateMesh(validate), coordinates(encoding) {}
    };

    class Error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /// Write the file atomically: the destination is replaced only after a complete, flushed temporary file exists.
    static void write(const std::filesystem::path& path,
                      const MeshData& mesh,
                      const Options& options = Options{});

    /// Read and fully validate a file. Throws Error on any malformed, truncated or corrupted input.
    static MeshData read(const std::filesystem::path& path,
                         const Options& options = Options{});
};
