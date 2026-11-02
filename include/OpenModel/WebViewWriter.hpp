#pragma once

#include <OpenModel/MeshData.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <array>
#include <map>
#include <string>
#include <vector>

/**
 * @file WebViewWriter.hpp
 * @brief Export of a MeshData FEM mesh to the MGWV render format for WebGL.
 *
 * MGWV is a compact, little-endian container (a JSON descriptor followed by a
 * binary payload) holding only what a viewer needs: the unique faces of the
 * mesh (skin and, optionally, interior faces), the unique edges, per-face
 * block/group identifiers and optional nodal scalar fields. Coordinates are
 * stored as float32 relative to an origin so large models keep their precision.
 * See docs/MGWV_FORMAT.md for the layout.
 */

/** @brief A named scalar value per mesh node, in node-index order. */
struct WebNodalField {
    std::string name;          ///< Label shown in the viewer.
    std::vector<double> values; ///< One value per node index; size must equal the node count.
};

/**
 * @brief A scalar defined per element (for example the von Mises stress of each element).
 *
 * `values` is indexed by the global element position: elements are counted block after block in
 * MeshData::getElementBlocks() order, so element `e` of block `b` is `offset(b) + e`.
 * The viewer paints every triangle of a face with the value of the element that owns the face.
 */
struct WebElementField {
    std::string name;            ///< Label shown in the viewer.
    std::vector<double> values;  ///< One finite value per element; size must equal MeshData::getNumElements().
};

/** @brief One vibration mode: a displacement shape and the frequency it belongs to. */
struct WebMode {
    std::string label;                              ///< For example "mode 2: f = 12.3".
    double frequency = 0.0;                         ///< Frequency in the unit of the solver (0 = unknown).
    std::vector<std::array<double, 3>> displacement; ///< One entry per node index; size must equal the node count.
};

/** @brief Options controlling WebViewWriter output. */
struct WebViewOptions {
    /**
     * Export the faces shared by two volume elements (needed for cutaway views).
     * Faces between two different element blocks (material interfaces) are
     * always exported, even when this is false.
     */
    bool includeInteriorFaces = true;
    /** Export the mesh edges used for the wireframe overlay. */
    bool includeEdges = true;
    /** Optional nodal scalar fields displayed with a colour map. */
    std::vector<WebNodalField> fields;
    /** Optional per-element scalar fields (drawn flat-shaded per element). */
    std::vector<WebElementField> elementFields;
    /**
     * Optional vibration modes. When not empty they replace `displacement`: the viewer lets the user pick a
     * mode and animate it. `displacement` must then be empty.
     */
    std::vector<WebMode> modes;
    /**
     * Optional display colour (sRGB, 0..1) per physical group name, for example the colour a component has in
     * the CAD file. Groups without an entry get the viewer's palette. Not stored in MGMB: only in the MGWV image.
     */
    std::map<std::string, std::array<float, 3>> groupColors;
    /**
     * Optional nodal displacement in model units, one entry per node index (empty = none). It is stored as a
     * separate chunk, so the viewer can draw the deformed shape with an adjustable scale factor; the stored
     * positions stay undeformed.
     */
    std::vector<std::array<double, 3>> displacement;
    /** Label of the displacement quantity shown by the viewer (for example "DISP"). */
    std::string displacementName;
    /** Free text describing the loaded solver results (file, step). Non-empty marks the model as a result view. */
    std::string resultsLabel;
};

/** @brief Outcome and statistics of a WebViewWriter call. */
struct WebViewResult {
    bool success = false;
    std::string error;
    std::size_t vertices = 0;          ///< Vertices stored in the file.
    std::size_t triangles = 0;         ///< Triangles stored (quads count as two).
    std::size_t boundaryTriangles = 0; ///< Triangles on the skin (stored first).
    std::size_t edges = 0;             ///< Edges stored.
    std::size_t boundaryEdges = 0;     ///< Edges on the skin (stored first).
    std::size_t bytesWritten = 0;
};

/**
 * @brief Writes MGWV files from MeshData.
 *
 * Only corner nodes of higher-order elements are rendered, assuming the
 * conventional ordering that lists corner nodes first. Point elements are
 * ignored; line elements are exported as edges.
 */
class WebViewWriter {
public:
    /**
     * @brief Serialize a mesh into an in-memory MGWV image.
     * @param mesh Source mesh; it should satisfy MeshData::validate().
     * @param options Export options.
     * @param[out] image Receives the file bytes on success.
     * @return Result with statistics; success is false on failure.
     */
    static WebViewResult build(const MeshData& mesh, const WebViewOptions& options,
                               std::vector<unsigned char>& image);

    /**
     * @brief Write a mesh to an MGWV file.
     * @param path Destination file.
     * @param mesh Source mesh.
     * @param options Export options.
     * @return Result with statistics; the file is written only on success.
     */
    static WebViewResult writeFile(const std::filesystem::path& path, const MeshData& mesh,
                                   const WebViewOptions& options = {});
};
