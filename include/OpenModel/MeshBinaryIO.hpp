#pragma once

#include <OpenModel/MeshData.hpp>

#include <filesystem>
#include <stdexcept>

// MGMB v1 binary persistence for MeshData. The format is little-endian,
// sectioned, uncompressed, and independent of compiler ABI/layout.
class MeshBinaryIO {
public:
    struct Options {
        // Validate MeshData before writing and after reading. Structural validation
        // is enabled by default; geometric quality checks remain a separate concern.
        bool validateMesh;
        constexpr explicit Options(bool validate = true) noexcept : validateMesh(validate) {}
    };

    class Error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    static void write(const std::filesystem::path& path,
                      const MeshData& mesh,
                      const Options& options = Options{});

    static MeshData read(const std::filesystem::path& path,
                         const Options& options = Options{});
};
