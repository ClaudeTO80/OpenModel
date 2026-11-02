#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshTopology.hpp>
#include <OpenModel/MeshValidator.hpp>
#include <OpenModel/MeshTopology.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <new>
#include <cstdlib>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#  include <psapi.h>
#elif defined(__linux__)
#  include <fstream>
#  include <unistd.h>
#  include <sys/resource.h>
#elif defined(__APPLE__)
#  include <sys/resource.h>
#endif


// Benchmark-only allocation probe. It records cumulative C++ allocation traffic
// during a stage, not retained bytes or process RSS. It intentionally tracks
// ordinary operator new/new[] requests; platform/runtime allocations made via
// malloc or custom allocators are outside this metric.
namespace allocation_probe {
thread_local bool active = false;
thread_local std::uint64_t calls = 0;
thread_local std::uint64_t requestedBytes = 0;
struct Stats { std::uint64_t calls = 0; std::uint64_t requestedBytes = 0; };
void begin() noexcept { calls = 0; requestedBytes = 0; active = true; }
Stats end() noexcept { active = false; return {calls, requestedBytes}; }
void record(std::size_t size) noexcept {
    if (active) { ++calls; requestedBytes += static_cast<std::uint64_t>(size); }
}
}
void* operator new(std::size_t size) {
    if (void* p = std::malloc(size ? size : 1)) { allocation_probe::record(size); return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    if (void* p = std::malloc(size ? size : 1)) { allocation_probe::record(size); return p; }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {
using Clock = std::chrono::steady_clock;
struct Timing { double seconds = 0.0; };
struct MemorySnapshot {
    std::uint64_t currentRssBytes = 0;
    std::uint64_t peakRssBytes = 0;
};
MemorySnapshot memorySnapshot() {
    MemorySnapshot result;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        result.currentRssBytes = static_cast<std::uint64_t>(counters.WorkingSetSize);
        result.peakRssBytes = static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    }
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t totalPages = 0, residentPages = 0;
    if (statm >> totalPages >> residentPages) {
        (void)totalPages;
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize > 0) result.currentRssBytes = residentPages * static_cast<std::uint64_t>(pageSize);
    }
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0)
        result.peakRssBytes = static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#elif defined(__APPLE__)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0) {
        result.currentRssBytes = 0; // Current RSS is not exposed by this portable API.
        result.peakRssBytes = static_cast<std::uint64_t>(usage.ru_maxrss);
    }
#endif
    return result;
}
void retainMaximum(std::uint64_t& target, std::uint64_t value) {
    if (value > target) target = value;
}
template <class F> Timing measure(F&& operation) {
    const auto start = Clock::now(); operation(); const auto end = Clock::now();
    return {std::chrono::duration<double>(end - start).count()};
}
std::size_t parsePositive(int argc, char** argv, int index, std::size_t fallback,
                          std::size_t maximum, const char* name) {
    if (argc <= index) return fallback;
    char* end = nullptr;
    const auto parsed = std::strtoull(argv[index], &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > maximum)
        throw std::invalid_argument(std::string(name) + " must be an integer in [1, " + std::to_string(maximum) + "]");
    return static_cast<std::size_t>(parsed);
}
std::size_t parseDivisions(int argc, char** argv) {
    if (argc > 3) throw std::invalid_argument("usage: openmodel-benchmark [grid-divisions] [repetitions]");
    return parsePositive(argc, argv, 1, 8, 32, "grid-divisions");
}
std::size_t parseRepetitions(int argc, char** argv) { return parsePositive(argc, argv, 2, 5, 31, "repetitions"); }
double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
}
MeshData makeStructuredTetGrid(std::size_t n) {
    MeshData mesh; const std::size_t side = n + 1;
    if (side > std::numeric_limits<std::size_t>::max() / side ||
        side * side > std::numeric_limits<std::size_t>::max() / side)
        throw std::overflow_error("grid node count overflow");
    const std::size_t nodeCount = side * side * side;
    if (n > 0 && n > std::numeric_limits<std::size_t>::max() / n / n / 6)
        throw std::overflow_error("grid element count overflow");
    auto index = [side](std::size_t i, std::size_t j, std::size_t k) -> NodeIndex {
        return static_cast<NodeIndex>((k * side + j) * side + i);
    };
    for (std::size_t k = 0; k <= n; ++k)
        for (std::size_t j = 0; j <= n; ++j)
            for (std::size_t i = 0; i <= n; ++i)
                mesh.addNode(static_cast<NodeId>(index(i, j, k) + 1),
                             static_cast<double>(i), static_cast<double>(j), static_cast<double>(k));
    ElementId elementId = 1;
    for (std::size_t k = 0; k < n; ++k) for (std::size_t j = 0; j < n; ++j) for (std::size_t i = 0; i < n; ++i) {
        const auto v000=index(i,j,k), v100=index(i+1,j,k), v010=index(i,j+1,k), v110=index(i+1,j+1,k);
        const auto v001=index(i,j,k+1), v101=index(i+1,j,k+1), v011=index(i,j+1,k+1), v111=index(i+1,j+1,k+1);
        const NodeIndex tets[6][4] = {
            {v000,v100,v110,v111}, {v000,v110,v010,v111}, {v000,v010,v011,v111},
            {v000,v011,v001,v111}, {v000,v001,v101,v111}, {v000,v101,v100,v111}
        };
        for (const auto& tet : tets)
            mesh.addElement("volume", ElementType::Tetrahedron4, elementId++, {tet[0],tet[1],tet[2],tet[3]});
    }
    if (mesh.getNumNodes() != nodeCount) throw std::runtime_error("internal benchmark mesh count error");
    return mesh;
}
std::size_t estimateRetainedCapacityBytes(const MeshData& mesh, const MeshTopology& topology) {
    std::size_t bytes = mesh.getNodeIds().capacity() * sizeof(NodeId);
    bytes += mesh.getCoordinates().capacity() * sizeof(double);
    bytes += mesh.getElementBlocks().capacity() * sizeof(ElementBlock);
    for (const auto& block : mesh.getElementBlocks()) {
        bytes += block.ids.capacity() * sizeof(ElementId);
        bytes += block.connectivity.capacity() * sizeof(NodeIndex);
        bytes += block.name.capacity();
    }
    bytes += topology.faces().capacity() * sizeof(MeshFace);
    for (const auto& face : topology.faces()) {
        bytes += face.nodes.capacity() * sizeof(NodeIndex);
        bytes += face.incidentElements.capacity() * sizeof(ElementId);
        for (const auto& group : face.physicalGroups) bytes += group.capacity();
        bytes += face.physicalGroups.capacity() * sizeof(std::string);
    }
    bytes += topology.edges().capacity() * sizeof(MeshEdge);
    for (const auto& edge : topology.edges()) {
        bytes += edge.intermediateNodes.capacity() * sizeof(NodeIndex);
        bytes += edge.incidentElements.capacity() * sizeof(ElementId);
    }
    return bytes;
}
}
int main(int argc, char** argv) {
    try {
        const std::size_t n = parseDivisions(argc, argv);
        const std::size_t repetitions = parseRepetitions(argc, argv);
        MeshData mesh;
        std::vector<double> buildSamples, topologySamples, validationSamples;
        std::vector<std::uint64_t> buildAllocCalls, topologyAllocCalls, validationAllocCalls;
        std::vector<std::uint64_t> buildAllocBytes, topologyAllocBytes, validationAllocBytes;
        buildSamples.reserve(repetitions); topologySamples.reserve(repetitions); validationSamples.reserve(repetitions);
        buildAllocCalls.reserve(repetitions); topologyAllocCalls.reserve(repetitions); validationAllocCalls.reserve(repetitions);
        buildAllocBytes.reserve(repetitions); topologyAllocBytes.reserve(repetitions); validationAllocBytes.reserve(repetitions);
        MeshTopology topology; MeshTopologyReport topologyReport;
        MeshValidationReport validationReport;
        std::uint64_t peakObservedRssAfterMeshBytes = 0;
        std::uint64_t peakObservedRssAfterTopologyBytes = 0;
        std::uint64_t peakObservedRssAfterValidationBytes = 0;
        std::uint64_t peakProcessRssBytes = 0;
        for (std::size_t run = 0; run < repetitions; ++run) {
            allocation_probe::begin();
            buildSamples.push_back(measure([&] { mesh = makeStructuredTetGrid(n); }).seconds);
            auto allocations = allocation_probe::end();
            buildAllocCalls.push_back(allocations.calls); buildAllocBytes.push_back(allocations.requestedBytes);
            auto memory = memorySnapshot();
            retainMaximum(peakObservedRssAfterMeshBytes, memory.currentRssBytes);
            retainMaximum(peakProcessRssBytes, memory.peakRssBytes);
            allocation_probe::begin();
            topologySamples.push_back(measure([&] {
                topology = MeshTopology{};
                topologyReport = topology.build(mesh);
            }).seconds);
            allocations = allocation_probe::end();
            topologyAllocCalls.push_back(allocations.calls); topologyAllocBytes.push_back(allocations.requestedBytes);
            memory = memorySnapshot();
            retainMaximum(peakObservedRssAfterTopologyBytes, memory.currentRssBytes);
            retainMaximum(peakProcessRssBytes, memory.peakRssBytes);
            allocation_probe::begin();
            validationSamples.push_back(measure([&] { validationReport = MeshValidator::validate(mesh); }).seconds);
            allocations = allocation_probe::end();
            validationAllocCalls.push_back(allocations.calls); validationAllocBytes.push_back(allocations.requestedBytes);
            memory = memorySnapshot();
            retainMaximum(peakObservedRssAfterValidationBytes, memory.currentRssBytes);
            retainMaximum(peakProcessRssBytes, memory.peakRssBytes);
            if (topologyReport.nonManifoldFaces != 0 || !topologyReport.errors.empty()) {
                std::cerr << "openmodel-benchmark: topology check failed during repetition " << (run + 1) << '\n';
                return 2;
            }
            if (validationReport.hasErrors()) {
                std::cerr << "openmodel-benchmark: validation failed during repetition " << (run + 1) << '\n';
                return 2;
            }
        }
        const double buildTime = median(buildSamples);
        const double topologyTime = median(topologySamples);
        const double validationTime = median(validationSamples);
        auto medianU64 = [](std::vector<std::uint64_t> values) -> std::uint64_t {
            std::sort(values.begin(), values.end());
            const auto middle = values.size() / 2;
            return values.size() % 2 ? values[middle] : values[middle - 1] / 2 + values[middle] / 2 + ((values[middle - 1] % 2 + values[middle] % 2) / 2);
        };
        std::cout << "OpenModel benchmark (synthetic structured tetrahedral grid)\n"
                  << "grid_divisions=" << n << '\n' << "repetitions=" << repetitions << '\n'
                  << "statistic=median\n" << "nodes=" << mesh.getNumNodes() << '\n'
                  << "elements=" << mesh.getNumElements() << '\n' << "topology_faces=" << topology.faces().size() << '\n'
                  << "topology_edges=" << topology.edges().size() << '\n' << "boundary_faces=" << topologyReport.boundaryFaces << '\n'
                  << "internal_faces=" << topologyReport.internalFaces << '\n' << "non_manifold_faces=" << topologyReport.nonManifoldFaces << '\n'
                  << "validation_errors=" << validationReport.hasErrors() << '\n' << "validation_issues=" << validationReport.issues.size() << '\n'
                  << "estimated_retained_capacity_bytes=" << estimateRetainedCapacityBytes(mesh, topology) << '\n'
                  << "max_observed_rss_after_mesh_bytes=" << peakObservedRssAfterMeshBytes << '\n'
                  << "max_observed_rss_after_topology_bytes=" << peakObservedRssAfterTopologyBytes << '\n'
                  << "max_observed_rss_after_validation_bytes=" << peakObservedRssAfterValidationBytes << '\n'
                  << "peak_process_rss_bytes=" << peakProcessRssBytes << '\n'
                  << "allocation_probe_note=cumulative requested bytes/calls from ordinary C++ new/new[] during each stage; excludes malloc/custom allocators and is not retained memory\n"
                  << "mesh_construction_new_calls_median=" << medianU64(buildAllocCalls) << '\n'
                  << "mesh_construction_new_bytes_median=" << medianU64(buildAllocBytes) << '\n'
                  << "topology_build_new_calls_median=" << medianU64(topologyAllocCalls) << '\n'
                  << "topology_build_new_bytes_median=" << medianU64(topologyAllocBytes) << '\n'
                  << "mesh_validation_new_calls_median=" << medianU64(validationAllocCalls) << '\n'
                  << "mesh_validation_new_bytes_median=" << medianU64(validationAllocBytes) << '\n'
                  << "rss_measurement_note=process-level snapshots; peak is cumulative across repetitions; zero means unavailable on this platform\n"
                  << std::fixed << std::setprecision(6) << "stage,median_seconds,elements_per_second\n"
                  << "mesh_construction," << buildTime << ',' << (buildTime > 0 ? mesh.getNumElements()/buildTime : 0) << '\n'
                  << "topology_build," << topologyTime << ',' << (topologyTime > 0 ? mesh.getNumElements()/topologyTime : 0) << '\n'
                  << "mesh_validation," << validationTime << ',' << (validationTime > 0 ? mesh.getNumElements()/validationTime : 0) << '\n';
        return 0;
    } catch (const std::exception& ex) { std::cerr << "openmodel-benchmark: " << ex.what() << '\n'; return 1; }
}
