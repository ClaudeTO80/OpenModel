#pragma once

#include <OpenModel/MeshData.hpp>

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file MeshQuality.hpp
 * @brief Backend-independent element quality statistics of a mesh.
 *
 * Every metric is dimensionless with 1 for the ideal element; zero marks a degenerate element and a
 * negative value an inverted one. Only the corner nodes of higher-order elements are used.
 *  - Triangle: 4*sqrt(3)*area / (sum of the squared edge lengths).
 *  - Tetrahedron: mean ratio, 12*(3*V)^(2/3) / (sum of the squared edge lengths) (signed volume).
 *  - Quadrilateral, hexahedron, prism, pyramid: minimum over the corners of the scaled Jacobian,
 *    det(e1,e2,e3)/(|e1||e2||e3|) (the sine of the corner angle for a quadrilateral). Its ideal value is
 *    1; the apex of a pyramid is not evaluated.
 */

/** @brief Quality statistics of the elements of one type. */
struct ElementTypeQuality {
    ElementType type = ElementType::Unknown;
    int dimension = 0;               ///< 2 for surface elements, 3 for volume elements
    std::size_t count = 0;
    double minimum = 0.0;
    double mean = 0.0;
    std::size_t poor = 0;            ///< elements whose quality is below the report threshold (and not inverted)
    std::size_t inverted = 0;        ///< elements with a non-positive quality
};

/** @brief Quality of a whole mesh: one entry per element type that has a quality metric. */
struct MeshQualityReport {
    std::vector<ElementTypeQuality> types;
    double poorThreshold = 0.1;
    std::size_t evaluatedElements = 0;
    std::size_t poorElements = 0;
    std::size_t invertedElements = 0;
    double minimumVolumeQuality = 0.0;   ///< 0 when the mesh has no volume elements
    double minimumSurfaceQuality = 0.0;  ///< 0 when the mesh has no surface elements
    bool hasVolumeElements = false;
    bool hasSurfaceElements = false;

    /** @brief True when the mesh has no poor or inverted element. */
    bool acceptable() const noexcept { return poorElements == 0 && invertedElements == 0; }
    /** @brief One-paragraph, human-readable summary (type counts, minima, warnings). */
    std::string summary() const;
};

/**
 * @brief Evaluate the quality of every surface and volume element of @p mesh.
 * @param mesh Mesh to evaluate; line and point elements are ignored.
 * @param poorThreshold Elements below this quality (and above zero) are counted as poor; in (0, 1).
 * @throws std::invalid_argument if @p poorThreshold is outside (0, 1).
 */
MeshQualityReport computeMeshQuality(const MeshData& mesh, double poorThreshold = 0.1);
