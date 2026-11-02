#pragma once

#include <OpenModel/MeshData.hpp>

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

enum class ValidationSeverity { Warning, Error };

enum class ValidationCode {
    NonFiniteCoordinate,
    DuplicateElementId,
    InvalidBlockTopology,
    InvalidOptions,
    DuplicateNodeInElement,
    UnusedNode,
    DegenerateElement,
    NegativeOrientation,
    PoorElementQuality,
    EmptyMesh
};

struct MeshValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Error;
    ValidationCode code = ValidationCode::DegenerateElement;
    std::string message;
    std::string blockName;
    ElementId elementId = 0;
    NodeIndex nodeIndex = 0;
};

struct MeshValidationOptions {
    // Tolerances are measured in model units and scaled by the bounding-box diagonal.
    double absoluteTolerance = 1.0e-12;
    double relativeTolerance = 1.0e-12;
    bool requirePositiveTetrahedronOrientation = true;
    // Warn if tetrahedron mean-ratio quality (0..1, 1 is regular) is below threshold.
    // Set to zero to disable the warning.
    double minimumTetrahedronQuality = 0.0;
    bool reportEmptyMesh = true;
    bool reportUnusedNodes = true;
};

struct MeshValidationReport {
    std::vector<MeshValidationIssue> issues;
    std::size_t checkedNodes = 0;
    std::size_t checkedElements = 0;
    std::size_t degenerateElements = 0;
    std::size_t duplicateNodeElements = 0;
    std::size_t unusedNodes = 0;
    std::size_t negativelyOrientedTetrahedra = 0;
    double boundingBoxDiagonal = 0.0;
    double minimumTriangleAngleDegrees = std::numeric_limits<double>::infinity();
    double minimumTetrahedronQuality = std::numeric_limits<double>::infinity();

    bool hasErrors() const noexcept;
    bool isValid() const noexcept;
};

class MeshValidator {
public:
    static MeshValidationReport validate(const MeshData& mesh,
                                         const MeshValidationOptions& options = {});

private:
    static std::array<double, 3> sub(const std::array<double, 3>& a,
                                    const std::array<double, 3>& b) noexcept;
    static double dot(const std::array<double, 3>& a,
                      const std::array<double, 3>& b) noexcept;
    static std::array<double, 3> cross(const std::array<double, 3>& a,
                                       const std::array<double, 3>& b) noexcept;
    static double norm(const std::array<double, 3>& a) noexcept;
};
