#include <OpenModel/MeshValidator.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
constexpr double radiansToDegrees = 57.295779513082320876;

void addIssue(MeshValidationReport& report, ValidationSeverity severity,
              ValidationCode code, std::string message,
              const std::string& block = {}, ElementId element = 0,
              NodeIndex node = 0) {
    report.issues.push_back({severity, code, std::move(message), block, element, node});
}

double clampCosine(double value) noexcept {
    return std::max(-1.0, std::min(1.0, value));
}
}

bool MeshValidationReport::hasErrors() const noexcept {
    return std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.severity == ValidationSeverity::Error;
    });
}

bool MeshValidationReport::isValid() const noexcept { return !hasErrors(); }

std::array<double, 3> MeshValidator::sub(const std::array<double, 3>& a,
                                         const std::array<double, 3>& b) noexcept {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

double MeshValidator::dot(const std::array<double, 3>& a,
                          const std::array<double, 3>& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

std::array<double, 3> MeshValidator::cross(const std::array<double, 3>& a,
                                           const std::array<double, 3>& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

double MeshValidator::norm(const std::array<double, 3>& a) noexcept {
    return std::sqrt(dot(a, a));
}

MeshValidationReport MeshValidator::validate(const MeshData& mesh,
                                             const MeshValidationOptions& options) {
    MeshValidationReport report;
    if (!std::isfinite(options.absoluteTolerance) || options.absoluteTolerance < 0.0 ||
        !std::isfinite(options.relativeTolerance) || options.relativeTolerance < 0.0 ||
        !std::isfinite(options.minimumTetrahedronQuality) ||
        options.minimumTetrahedronQuality < 0.0 || options.minimumTetrahedronQuality > 1.0) {
        addIssue(report, ValidationSeverity::Error, ValidationCode::InvalidOptions,
                 "Validation tolerances must be finite and non-negative; tetrahedron quality threshold must be in [0, 1]");
        return report;
    }

    const auto structuralErrors = mesh.validate();
    for (const auto& message : structuralErrors)
        addIssue(report, ValidationSeverity::Error, ValidationCode::InvalidBlockTopology, message);
    if (!structuralErrors.empty()) return report;

    if (mesh.getNumNodes() == 0 && mesh.getNumElements() == 0) {
        if (options.reportEmptyMesh)
            addIssue(report, ValidationSeverity::Warning, ValidationCode::EmptyMesh,
                     "Mesh contains no nodes or elements");
        report.minimumTriangleAngleDegrees = 0.0;
        report.minimumTetrahedronQuality = 0.0;
        return report;
    }

    report.checkedNodes = mesh.getNumNodes();
    const auto& coords = mesh.getCoordinates();
    std::array<double, 3> lo{INFINITY, INFINITY, INFINITY};
    std::array<double, 3> hi{-INFINITY, -INFINITY, -INFINITY};
    std::vector<bool> nodeFinite(mesh.getNumNodes(), true);
    for (std::size_t i = 0; i < mesh.getNumNodes(); ++i) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double value = coords[i * 3 + axis];
            if (!std::isfinite(value)) nodeFinite[i] = false;
            else {
                lo[axis] = std::min(lo[axis], value);
                hi[axis] = std::max(hi[axis], value);
            }
        }
        if (!nodeFinite[i])
            addIssue(report, ValidationSeverity::Error, ValidationCode::NonFiniteCoordinate,
                     "Node has a NaN or infinite coordinate", {}, 0, static_cast<NodeIndex>(i));
    }

    if (mesh.getNumNodes() > 0 && std::isfinite(lo[0]) && std::isfinite(hi[0]) &&
        std::isfinite(lo[1]) && std::isfinite(hi[1]) &&
        std::isfinite(lo[2]) && std::isfinite(hi[2])) {
        const double dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
        report.boundingBoxDiagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const double scale = std::max(report.boundingBoxDiagonal, options.absoluteTolerance);
    const double lengthTolerance = std::max(options.absoluteTolerance, options.relativeTolerance * scale);
    const double areaTolerance = lengthTolerance * lengthTolerance;
    const double sixVolumeTolerance = 6.0 * lengthTolerance * lengthTolerance * lengthTolerance;

    std::vector<bool> nodeUsed(mesh.getNumNodes(), false);
    std::unordered_set<ElementId> seenElementIds;
    for (const auto& block : mesh.getElementBlocks()) {
        const auto expected = elementNodeCount(block.type);
        if (expected != 0 && expected != block.nodesPerElement) {
            addIssue(report, ValidationSeverity::Error, ValidationCode::InvalidBlockTopology,
                     "Element block node count does not match its declared topology", block.name);
            continue;
        }
        for (std::size_t e = 0; e < block.size(); ++e) {
            ++report.checkedElements;
            const ElementId elementId = block.ids[e];
            if (!seenElementIds.insert(elementId).second)
                addIssue(report, ValidationSeverity::Error, ValidationCode::DuplicateElementId,
                         "Element ID is duplicated across the mesh", block.name, elementId);

            const std::size_t base = e * block.nodesPerElement;
            const std::size_t cornerCount = [&]() -> std::size_t {
                switch (block.type) {
                case ElementType::Point1: return 1;
                case ElementType::Line2: case ElementType::Line3: return 2;
                case ElementType::Triangle3: case ElementType::Triangle6: return 3;
                case ElementType::Quadrilateral4: case ElementType::Quadrilateral8:
                case ElementType::Quadrilateral9: return 4;
                case ElementType::Tetrahedron4: case ElementType::Tetrahedron10: return 4;
                case ElementType::Hexahedron8: case ElementType::Hexahedron20:
                case ElementType::Hexahedron27: return 8;
                case ElementType::Prism6: case ElementType::Prism15: return 6;
                case ElementType::Pyramid5: return 5;
                default: return block.nodesPerElement;
                }
            }();

            bool repeatedCorner = false;
            std::unordered_set<NodeIndex> localCorners;
            for (std::size_t n = 0; n < cornerCount; ++n) {
                const NodeIndex index = block.connectivity[base + n];
                nodeUsed[static_cast<std::size_t>(index)] = true;
                if (!localCorners.insert(index).second) repeatedCorner = true;
            }
            for (std::size_t n = cornerCount; n < block.nodesPerElement; ++n)
                nodeUsed[static_cast<std::size_t>(block.connectivity[base + n])] = true;

            if (repeatedCorner) {
                ++report.duplicateNodeElements;
                ++report.degenerateElements;
                addIssue(report, ValidationSeverity::Error, ValidationCode::DuplicateNodeInElement,
                         "Element repeats a corner node in its connectivity", block.name, elementId);
                continue;
            }

            bool finite = true;
            for (std::size_t n = 0; n < block.nodesPerElement; ++n) {
                const auto index = static_cast<std::size_t>(block.connectivity[base + n]);
                finite = finite && nodeFinite[index];
            }
            if (!finite) continue;

            auto point = [&](std::size_t local) { return mesh.nodeCoordinates(block.connectivity[base + local]); };
            bool degenerate = false;
            if (block.type == ElementType::Line2 || block.type == ElementType::Line3) {
                degenerate = norm(sub(point(1), point(0))) <= lengthTolerance;
            } else if (block.type == ElementType::Triangle3 || block.type == ElementType::Triangle6) {
                const auto a = point(0), b = point(1), c = point(2);
                const double ab = norm(sub(b, a)), bc = norm(sub(c, b)), ca = norm(sub(a, c));
                const double area = 0.5 * norm(cross(sub(b, a), sub(c, a)));
                degenerate = area <= areaTolerance || ab <= lengthTolerance ||
                             bc <= lengthTolerance || ca <= lengthTolerance;
                if (!degenerate) {
                    const double angleA = std::acos(clampCosine((ab * ab + ca * ca - bc * bc) / (2.0 * ab * ca))) * radiansToDegrees;
                    const double angleB = std::acos(clampCosine((ab * ab + bc * bc - ca * ca) / (2.0 * ab * bc))) * radiansToDegrees;
                    const double angleC = 180.0 - angleA - angleB;
                    report.minimumTriangleAngleDegrees = std::min(report.minimumTriangleAngleDegrees,
                        std::min(angleA, std::min(angleB, angleC)));
                }
            } else if (block.type == ElementType::Quadrilateral4 ||
                       block.type == ElementType::Quadrilateral8 ||
                       block.type == ElementType::Quadrilateral9) {
                const auto p0 = point(0), p1 = point(1), p2 = point(2), p3 = point(3);
                const double area = 0.5 * norm(cross(sub(p1, p0), sub(p2, p0))) +
                                    0.5 * norm(cross(sub(p2, p0), sub(p3, p0)));
                const double shortest = std::min({norm(sub(p1, p0)), norm(sub(p2, p1)),
                                                  norm(sub(p3, p2)), norm(sub(p0, p3))});
                degenerate = area <= areaTolerance || shortest <= lengthTolerance;
            } else if (block.type == ElementType::Tetrahedron4 || block.type == ElementType::Tetrahedron10) {
                const auto a = point(0), b = point(1), c = point(2), d = point(3);
                const auto ab = sub(b, a), ac = sub(c, a), ad = sub(d, a);
                const double determinant = dot(ab, cross(ac, ad));
                degenerate = std::abs(determinant) <= sixVolumeTolerance;
                const std::array<double, 6> edgeLengths{
                    norm(sub(b, a)), norm(sub(c, a)), norm(sub(d, a)),
                    norm(sub(c, b)), norm(sub(d, b)), norm(sub(d, c))};
                double sumSquaredEdges = 0.0;
                for (const double length : edgeLengths) sumSquaredEdges += length * length;
                const double volume = std::abs(determinant) / 6.0;
                const double quality = sumSquaredEdges > 0.0
                    ? 12.0 * std::pow(3.0 * volume, 2.0 / 3.0) / sumSquaredEdges : 0.0;
                report.minimumTetrahedronQuality = std::min(report.minimumTetrahedronQuality, quality);
                if (determinant < -sixVolumeTolerance) {
                    ++report.negativelyOrientedTetrahedra;
                    addIssue(report, options.requirePositiveTetrahedronOrientation ? ValidationSeverity::Error : ValidationSeverity::Warning,
                             ValidationCode::NegativeOrientation,
                             "Tetrahedron has negative signed volume (corner-node ordering is reversed)",
                             block.name, elementId);
                }
                if (options.minimumTetrahedronQuality > 0.0 && quality < options.minimumTetrahedronQuality)
                    addIssue(report, ValidationSeverity::Warning, ValidationCode::PoorElementQuality,
                             "Tetrahedron mean-ratio quality is below the configured threshold",
                             block.name, elementId);
            }

            if (degenerate) {
                ++report.degenerateElements;
                addIssue(report, ValidationSeverity::Error, ValidationCode::DegenerateElement,
                         "Element is degenerate or below the scale-aware geometric tolerance",
                         block.name, elementId);
            }
        }
    }

    // A node referenced by a named node set is not considered orphaned: sets
    // can intentionally preserve boundary-condition or coupling nodes.
    for (const auto& set : mesh.getNodeSets()) {
        for (const auto index : set.second) {
            if (index < nodeUsed.size()) nodeUsed[static_cast<std::size_t>(index)] = true;
        }
    }

    if (options.reportUnusedNodes) {
        for (std::size_t i = 0; i < nodeUsed.size(); ++i) {
            if (!nodeUsed[i]) {
                ++report.unusedNodes;
                addIssue(report, ValidationSeverity::Warning, ValidationCode::UnusedNode,
                         "Node is not referenced by any element", {}, 0, static_cast<NodeIndex>(i));
            }
        }
    }
    if (!std::isfinite(report.minimumTriangleAngleDegrees)) report.minimumTriangleAngleDegrees = 0.0;
    if (!std::isfinite(report.minimumTetrahedronQuality)) report.minimumTetrahedronQuality = 0.0;
    return report;
}
