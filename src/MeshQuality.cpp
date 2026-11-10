#include <OpenModel/MeshQuality.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using Vec = std::array<double, 3>;

Vec sub(const Vec& a, const Vec& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec& a, const Vec& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
double norm(const Vec& a) { return std::sqrt(dot(a, a)); }

/** Scaled Jacobian det(a,b,c)/(|a||b||c|); zero for a degenerate corner. */
double scaledJacobian(const Vec& a, const Vec& b, const Vec& c) {
    const double lengths = norm(a) * norm(b) * norm(c);
    return lengths > 0.0 ? dot(cross(a, b), c) / lengths : 0.0;
}

double triangleQuality(const std::array<Vec, 3>& p) {
    const Vec ab = sub(p[1], p[0]), bc = sub(p[2], p[1]), ca = sub(p[0], p[2]);
    const double sumSquares = dot(ab, ab) + dot(bc, bc) + dot(ca, ca);
    if (sumSquares <= 0.0) return 0.0;
    return 4.0 * std::sqrt(3.0) * 0.5 * norm(cross(ab, sub(p[2], p[0]))) / sumSquares;
}

double tetrahedronQuality(const std::array<Vec, 4>& p) {
    const Vec e1 = sub(p[1], p[0]), e2 = sub(p[2], p[0]), e3 = sub(p[3], p[0]);
    const double volume = dot(cross(e1, e2), e3) / 6.0;
    static constexpr int edges[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    double sumSquares = 0.0;
    for (const auto& edge : edges) {
        const Vec d = sub(p[static_cast<std::size_t>(edge[1])], p[static_cast<std::size_t>(edge[0])]);
        sumSquares += dot(d, d);
    }
    if (sumSquares <= 0.0) return 0.0;
    const double magnitude = 12.0 * std::cbrt(9.0 * volume * volume) / sumSquares;
    return volume < 0.0 ? -magnitude : magnitude;
}

/** Minimum corner sine of a quadrilateral, negative when a corner is reflex relative to the element normal. */
double quadrilateralQuality(const std::array<Vec, 4>& p) {
    Vec normal{0.0, 0.0, 0.0};
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec c = cross(sub(p[(i + 1) % 4], p[i]), sub(p[(i + 3) % 4], p[i]));
        for (std::size_t k = 0; k < 3; ++k) normal[k] += c[k];
    }
    const double length = norm(normal);
    if (length <= 0.0) return 0.0;
    for (double& v : normal) v /= length;
    double minimum = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < 4; ++i) {
        const Vec a = sub(p[(i + 1) % 4], p[i]), b = sub(p[(i + 3) % 4], p[i]);
        const double lengths = norm(a) * norm(b);
        minimum = std::min(minimum, lengths > 0.0 ? dot(cross(a, b), normal) / lengths : 0.0);
    }
    return minimum;
}

/** Corner neighbour triples, ordered so that a unit cube (and a unit right prism) gives +1 at every corner. */
struct Corner { int a, b, c; };
constexpr Corner kHexCorners[8] = {{1, 3, 4}, {2, 0, 5}, {3, 1, 6}, {0, 2, 7}, {7, 5, 0}, {4, 6, 1}, {5, 7, 2}, {6, 4, 3}};
constexpr Corner kPrismCorners[6] = {{1, 2, 3}, {2, 0, 4}, {0, 1, 5}, {5, 4, 0}, {3, 5, 1}, {4, 3, 2}};
constexpr Corner kPyramidCorners[4] = {{1, 3, 4}, {2, 0, 4}, {3, 1, 4}, {0, 2, 4}};

template <std::size_t N>
double cornerQuality(const std::array<Vec, N>& p, const Corner* corners, std::size_t count) {
    double minimum = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < count; ++i) {
        const Vec& origin = p[i];
        minimum = std::min(minimum, scaledJacobian(sub(p[static_cast<std::size_t>(corners[i].a)], origin),
                                                   sub(p[static_cast<std::size_t>(corners[i].b)], origin),
                                                   sub(p[static_cast<std::size_t>(corners[i].c)], origin)));
    }
    return minimum;
}

/** Number of leading (corner) nodes of an element type, or 0 when the type has no quality metric. */
std::size_t cornerCount(ElementType type) {
    switch (type) {
    case ElementType::Triangle3: case ElementType::Triangle6: return 3;
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8: case ElementType::Quadrilateral9: return 4;
    case ElementType::Tetrahedron4: case ElementType::Tetrahedron10: return 4;
    case ElementType::Hexahedron8: case ElementType::Hexahedron20: case ElementType::Hexahedron27: return 8;
    case ElementType::Prism6: case ElementType::Prism15: return 6;
    case ElementType::Pyramid5: return 5;
    default: return 0;
    }
}

double elementQuality(ElementType type, const std::vector<Vec>& p) {
    switch (type) {
    case ElementType::Triangle3: case ElementType::Triangle6:
        return triangleQuality({p[0], p[1], p[2]});
    case ElementType::Quadrilateral4: case ElementType::Quadrilateral8: case ElementType::Quadrilateral9:
        return quadrilateralQuality({p[0], p[1], p[2], p[3]});
    case ElementType::Tetrahedron4: case ElementType::Tetrahedron10:
        return tetrahedronQuality({p[0], p[1], p[2], p[3]});
    case ElementType::Hexahedron8: case ElementType::Hexahedron20: case ElementType::Hexahedron27:
        return cornerQuality<8>({p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]}, kHexCorners, 8);
    case ElementType::Prism6: case ElementType::Prism15:
        return cornerQuality<6>({p[0], p[1], p[2], p[3], p[4], p[5]}, kPrismCorners, 6);
    case ElementType::Pyramid5:
        return cornerQuality<5>({p[0], p[1], p[2], p[3], p[4]}, kPyramidCorners, 4);
    default: return 0.0;
    }
}

const char* typeName(ElementType type) {
    switch (type) {
    case ElementType::Triangle3: return "triangles";
    case ElementType::Triangle6: return "quadratic triangles";
    case ElementType::Quadrilateral4: return "quadrilaterals";
    case ElementType::Quadrilateral8: return "quadratic quadrilaterals (8 nodes)";
    case ElementType::Quadrilateral9: return "quadratic quadrilaterals (9 nodes)";
    case ElementType::Tetrahedron4: return "tetrahedra";
    case ElementType::Tetrahedron10: return "quadratic tetrahedra";
    case ElementType::Hexahedron8: return "hexahedra";
    case ElementType::Hexahedron20: return "quadratic hexahedra (20 nodes)";
    case ElementType::Hexahedron27: return "quadratic hexahedra (27 nodes)";
    case ElementType::Prism6: return "prisms";
    case ElementType::Prism15: return "quadratic prisms";
    case ElementType::Pyramid5: return "pyramids";
    default: return "elements";
    }
}
} // namespace

MeshQualityReport computeMeshQuality(const MeshData& mesh, double poorThreshold) {
    if (!(poorThreshold > 0.0 && poorThreshold < 1.0))
        throw std::invalid_argument("The poor-quality threshold must lie in (0, 1)");
    MeshQualityReport report;
    report.poorThreshold = poorThreshold;
    const auto& coordinates = mesh.getCoordinates();
    std::vector<ElementTypeQuality> stats;
    std::vector<double> sums;
    std::vector<Vec> corners;
    for (const auto& block : mesh.getElementBlocks()) {
        const std::size_t corner = cornerCount(block.type);
        if (corner == 0 || block.nodesPerElement < corner || block.nodesPerElement == 0) continue;
        std::size_t slot = stats.size();
        for (std::size_t i = 0; i < stats.size(); ++i) if (stats[i].type == block.type) slot = i;
        if (slot == stats.size()) {
            ElementTypeQuality entry;
            entry.type = block.type;
            entry.dimension = elementDimension(block.type);
            entry.minimum = std::numeric_limits<double>::infinity();
            stats.push_back(entry);
            sums.push_back(0.0);
        }
        auto& entry = stats[slot];
        const std::size_t elements = block.connectivity.size() / block.nodesPerElement;
        for (std::size_t e = 0; e < elements; ++e) {
            corners.clear();
            for (std::size_t n = 0; n < corner; ++n) {
                const std::size_t node = block.connectivity[e * block.nodesPerElement + n];
                corners.push_back({coordinates[3 * node], coordinates[3 * node + 1], coordinates[3 * node + 2]});
            }
            const double q = elementQuality(block.type, corners);
            ++entry.count;
            sums[slot] += q;
            entry.minimum = std::min(entry.minimum, q);
            if (q <= 0.0) ++entry.inverted;
            else if (q < poorThreshold) ++entry.poor;
        }
    }
    report.minimumVolumeQuality = std::numeric_limits<double>::infinity();
    report.minimumSurfaceQuality = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < stats.size(); ++i) {
        auto& entry = stats[i];
        if (entry.count == 0) continue;
        entry.mean = sums[i] / static_cast<double>(entry.count);
        report.evaluatedElements += entry.count;
        report.poorElements += entry.poor;
        report.invertedElements += entry.inverted;
        if (entry.dimension == 3) {
            report.hasVolumeElements = true;
            report.minimumVolumeQuality = std::min(report.minimumVolumeQuality, entry.minimum);
        } else {
            report.hasSurfaceElements = true;
            report.minimumSurfaceQuality = std::min(report.minimumSurfaceQuality, entry.minimum);
        }
        report.types.push_back(entry);
    }
    if (!report.hasVolumeElements) report.minimumVolumeQuality = 0.0;
    if (!report.hasSurfaceElements) report.minimumSurfaceQuality = 0.0;
    std::stable_sort(report.types.begin(), report.types.end(),
                     [](const ElementTypeQuality& a, const ElementTypeQuality& b) { return a.dimension > b.dimension; });
    return report;
}

std::string MeshQualityReport::summary() const {
    if (types.empty()) return "Mesh quality: no surface or volume element to evaluate.";
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << "Mesh quality (1 = ideal element)";
    for (const auto& entry : types)
        out << "; " << entry.count << ' ' << typeName(entry.type) << ": min " << entry.minimum << ", mean " << entry.mean;
    if (invertedElements > 0)
        out << ". WARNING: " << invertedElements << " inverted or degenerate element(s): the mesh is not usable for analysis";
    if (poorElements > 0)
        out << ". WARNING: " << poorElements << " element(s) below " << std::setprecision(2) << poorThreshold
            << " (poor): refine the mesh, use the thin-face size factor, or lower the element size";
    if (acceptable()) out << ". No element below " << std::setprecision(2) << poorThreshold << '.';
    return out.str();
}
