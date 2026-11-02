#include <OpenModel/MeshBinaryIO.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
MeshData makeGrid(std::size_t n) {
    MeshData mesh;
    const std::size_t side = n + 1;
    auto idx = [side](std::size_t x, std::size_t y, std::size_t z) -> NodeIndex {
        return static_cast<NodeIndex>((z * side + y) * side + x);
    };
    for (std::size_t z = 0; z <= n; ++z)
        for (std::size_t y = 0; y <= n; ++y)
            for (std::size_t x = 0; x <= n; ++x)
                mesh.addNode(static_cast<NodeId>(idx(x, y, z) + 1), double(x), double(y), double(z));
    ElementId id = 1;
    for (std::size_t z = 0; z < n; ++z) for (std::size_t y = 0; y < n; ++y) for (std::size_t x = 0; x < n; ++x) {
        const auto a=idx(x,y,z), b=idx(x+1,y,z), c=idx(x,y+1,z), d=idx(x+1,y+1,z);
        const auto e=idx(x,y,z+1), f=idx(x+1,y,z+1), g=idx(x,y+1,z+1), h=idx(x+1,y+1,z+1);
        const NodeIndex tets[6][4] = {{a,b,d,h},{a,d,c,h},{a,c,g,h},{a,g,e,h},{a,e,f,h},{a,f,b,h}};
        for (const auto& t : tets) mesh.addElement("volume", ElementType::Tetrahedron4, id++, {t[0],t[1],t[2],t[3]});
    }
    return mesh;
}
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto m = values.size()/2;
    return values.size()%2 ? values[m] : (values[m-1]+values[m])/2.0;
}
template<class F> double timed(F&& f) {
    const auto a = Clock::now(); f(); const auto b = Clock::now();
    return std::chrono::duration<double>(b-a).count();
}
std::size_t parse(std::string value, std::size_t fallback, std::size_t maximum, const char* label) {
    if (value.empty()) return fallback;
    std::size_t used = 0; const auto n = std::stoull(value, &used);
    if (used != value.size() || n < 1 || n > maximum) throw std::invalid_argument(std::string(label) + " outside allowed range");
    return static_cast<std::size_t>(n);
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("usage: openmodel-io-benchmark [grid-divisions] [repetitions]");
        const auto n = parse(argc > 1 ? argv[1] : "", 8, 32, "grid-divisions");
        const auto reps = parse(argc > 2 ? argv[2] : "", 5, 31, "repetitions");
        const auto path = std::filesystem::temp_directory_path() / "openmodel-io-benchmark.mgmb";
        const auto mesh = makeGrid(n);
        std::vector<double> writes, reads;
        writes.reserve(reps); reads.reserve(reps);
        MeshData loaded;
        for (std::size_t i = 0; i < reps; ++i) {
            writes.push_back(timed([&] { MeshBinaryIO::write(path, mesh); }));
            reads.push_back(timed([&] { loaded = MeshBinaryIO::read(path); }));
            if (loaded.getNumNodes() != mesh.getNumNodes() || loaded.getNumElements() != mesh.getNumElements())
                throw std::runtime_error("round-trip count mismatch");
        }
        const auto bytes = std::filesystem::file_size(path);
        const auto writeSeconds = median(writes), readSeconds = median(reads);
        std::cout << "OpenModel MGMB I/O benchmark\n"
                  << "grid_divisions=" << n << " repetitions=" << reps << '\n'
                  << "nodes=" << mesh.getNumNodes() << " elements=" << mesh.getNumElements() << '\n'
                  << "file_bytes=" << bytes << '\n'
                  << "write_median_ms=" << writeSeconds * 1000.0 << '\n'
                  << "read_median_ms=" << readSeconds * 1000.0 << '\n'
                  << "write_MB_s=" << (bytes / 1.0e6) / writeSeconds << '\n'
                  << "read_MB_s=" << (bytes / 1.0e6) / readSeconds << '\n';
        std::error_code ignored; std::filesystem::remove(path, ignored);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "openmodel-io-benchmark: " << ex.what() << '\n';
        return 1;
    }
}
