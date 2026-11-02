#pragma once

#include <OpenModel/FemResults.hpp>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/WebViewWriter.hpp>

#include <cstddef>
#include <string>
#include <vector>

/**
 * @file ResultBinding.hpp
 * @brief Attaches imported FEM results to a MeshData and prepares them for the web viewer.
 *
 * Binding matches result records to mesh nodes by their external node id
 * (NodeId), never by position in the file. When the result file carries the
 * model coordinates (CalculiX FRD does) they are compared with the mesh, so
 * results computed on a different mesh are rejected instead of being painted
 * on the wrong nodes.
 */

namespace OpenModel {

/** @brief Options for bindResults(). */
struct BindOptions {
    /** Value of `step` selecting the last step of the results. */
    static constexpr std::size_t kLastStep = static_cast<std::size_t>(-1);

    /** Value of `maxModes` that selects the modes automatically (see below). */
    static constexpr std::size_t kAutoModes = static_cast<std::size_t>(-1);
    /** Number of modes taken when they are selected automatically. */
    static constexpr std::size_t kAutoModeLimit = 20;

    std::size_t step = kLastStep;   ///< Index into FemResults::steps.
    /**
     * Add derived fields:
     *  - `|v|` for vector fields (3 components);
     *  - for stress tensors (SXX,SYY,SZZ,SXY,SYZ,SZX): von Mises, principal 1..3 (sorted descending,
     *    so principal 1 is the largest), Tresca (principal 1 - principal 3) and hydrostatic (mean) stress;
     *  - for strain tensors (EXX,...): equivalent (von Mises) strain, principal 1..3, maximum shear
     *    (principal 1 - principal 3, engineering shear strain) and volumetric strain (trace).
     * They are computed for nodal and per-element fields alike.
     */
    bool derivedFields = true;
    /**
     * True when the shear components of strain tensors are engineering shear strains (gamma = 2 epsilon),
     * false (default) when they are tensor components. CalculiX writes tensor components.
     */
    bool engineeringShearStrain = false;
    /**
     * Vibration modes to hand to the viewer.
     *  - kAutoModes: when the selected step is the last one and every step of the file is a mode, up to
     *    kAutoModeLimit modes are bound; otherwise no modes are (a plain single step view).
     *  - N > 0: the first N modes of the file (error when the file has none).
     *  - 0: never bind modes.
     * With modes, the displacement of each mode is stored (BindResult::modesBound) and the scalar fields
     * come from the mode given by `step`; the default `step` then means the first mode, not the last.
     */
    std::size_t maxModes = kAutoModes;
    /**
     * Name of the field used as nodal displacement (case-insensitive). Empty selects automatically the first
     * field named DISP, DISPLACEMENT or U with at least three components; "none" disables the displacement.
     */
    std::string displacementField;
    /** Reject results whose model coordinates differ from the mesh (needs coordinates in the result file). */
    bool checkCoordinates = true;
    /**
     * Allowed coordinate difference as a fraction of max(bounding-box diagonal, largest |coordinate|).
     * The default covers the six significant digits of CalculiX's E12.5 output.
     */
    double coordinateTolerance = 2e-5;
    /** Name shown in the viewer for the result source (usually the file name); may be empty. */
    std::string sourceName;
};

/** @brief Outcome of bindResults(). */
struct BindResult {
    bool success = false;
    std::string error;                          ///< Set when success is false.
    std::vector<ImportDiagnostic> diagnostics;  ///< Warnings (success) or context for the error.
    std::size_t step = 0;                       ///< Step actually used.
    std::string stepLabel;
    std::size_t fieldsBound = 0;                ///< Nodal scalar fields appended to WebViewOptions::fields.
    std::size_t elementFieldsBound = 0;         ///< Scalar fields appended to WebViewOptions::elementFields.
    std::size_t unmatchedElementRecords = 0;    ///< Element records whose element id is not in the mesh.
    std::size_t elementsWithoutData = 0;        ///< Largest number of mesh elements lacking a value in one field.
    std::size_t modesBound = 0;                 ///< Entries appended to WebViewOptions::modes.
    std::vector<double> modeFrequencies;        ///< Frequency of each bound mode.
    std::size_t unmatchedRecords = 0;           ///< Result records whose node is not in the mesh (all fields).
    std::size_t nodesWithoutData = 0;           ///< Largest number of mesh nodes lacking a value in one field.
    bool hasDisplacement = false;
    std::string displacementName;
};

/**
 * @brief Convert the results of one step into viewer fields and, when present, a displacement.
 *
 * Every component becomes a scalar field named `FIELD.COMPONENT` (a single-component
 * field keeps the plain name); derived fields come first. Nodal fields become WebNodalField,
 * per-element fields WebElementField (matched by element id). Mesh nodes or elements that have
 * no value in a field are filled with that field's minimum so they do not stretch the colour
 * range; the count is reported in BindResult and as a warning.
 *
 * @param mesh Mesh the results belong to.
 * @param results Imported results.
 * @param options Binding options.
 * @param[in,out] view Receives the fields, displacement and results label; existing fields are kept.
 *                     Left unchanged when binding fails.
 */
BindResult bindResults(const MeshData& mesh, const FemResults& results, const BindOptions& options, WebViewOptions& view);

} // namespace OpenModel
