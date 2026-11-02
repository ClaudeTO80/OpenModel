#pragma once

#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

/**
 * @file FemResults.hpp
 * @brief In-memory model of nodal FEM results and the importers that fill it.
 *
 * The results of a finite-element run are kept apart from MeshData: a solver
 * result file refers to nodes by their external identifier (NodeId), and the
 * same mesh can be paired with many result files. Binding results to a mesh
 * (and to the viewer) is done by ResultBinding.hpp.
 *
 * Values defined at nodes, per element, and at integration points are retained separately.
 * When a format provides integration-point values, importers may additionally expose
 * an explicitly diagnosed element aggregate for viewer compatibility; the native
 * records remain available in integrationPointFields and are never replaced by it.
 */

namespace OpenModel {

/** @brief What the value of a ResultStep means. */
enum class StepKind {
    Static,     ///< No particular meaning (static solution).
    Time,       ///< Time of a transient or load-increment solution.
    Frequency,  ///< An eigenfrequency: the step holds one vibration mode.
    LoadStep,   ///< Load step (value is the step time or load factor).
    User        ///< User named / unknown.
};

/** @brief One solution state of a run: a load increment, a time step, a mode... */
struct ResultStep {
    std::string label;   ///< Human readable, e.g. "time 0.75 (step 1)" or "mode 2: f = 12.3"; never empty.
    double value = 0.0;  ///< Time, frequency or load factor stored in the file (0 when absent).
    StepKind kind = StepKind::Static;
    /**
     * 1-based position among the Frequency steps of the file (0 for other kinds). Modes are numbered in file
     * order, which for a frequency extraction is ascending frequency.
     */
    std::size_t mode = 0;
};

/** @brief Where the records of a ResultField are defined. */
enum class ResultLocation {
    Node,     ///< One value per node: `ids` are external node ids.
    Element   ///< One value per element: `ids` are external element ids.
};

/** Semantic classification of a physical result quantity. */
enum class ResultQuantityKind {
    Unknown, Displacement, Velocity, Acceleration, Force, Stress, Strain,
    Temperature, Energy, Pressure, ReactionForce, HeatFlux
};

/** Shape of the stored component tuple. */
enum class ResultComponentKind { Unknown, Scalar, Vector, SymmetricTensor, FullTensor };

/** Coordinate frame in which components are expressed. */
enum class ResultCoordinateFrame { Unknown, Global, Local, Material };

/** Meaning of off-diagonal strain components. */
enum class ResultShearConvention { Unknown, NotApplicable, Tensorial, Engineering };

/**
 * SI base-dimension exponents: length, mass, time, electric current,
 * thermodynamic temperature, amount of substance and luminous intensity.
 * known=false means the physical dimension itself is not established.
 */
struct PhysicalDimension {
    bool known = false;
    std::int8_t length = 0, mass = 0, time = 0, current = 0;
    std::int8_t temperature = 0, amount = 0, luminousIntensity = 0;
};

/**
 * Unit metadata. scaleToSI converts a value to coherent SI; zero means unknown.
 * offsetToSI supports affine units (e.g. Celsius to kelvin). A dimension can
 * be known even when the solver's unit scale is not.
 */
struct PhysicalUnit {
    PhysicalDimension dimension;
    std::string symbol;
    double scaleToSI = 0.0;
    double offsetToSI = 0.0;
};

/** Semantic and dimensional description shared by nodal, element and IP fields. */
struct ResultFieldSemantics {
    ResultQuantityKind quantity = ResultQuantityKind::Unknown;
    ResultComponentKind components = ResultComponentKind::Unknown;
    ResultCoordinateFrame frame = ResultCoordinateFrame::Unknown;
    ResultShearConvention shear = ResultShearConvention::Unknown;
    PhysicalUnit unit;
    bool derived = false;
    bool averaged = false;
    bool interpolated = false;
    bool extrapolated = false;
};

/**
 * @brief One named quantity defined at nodes, for one step.
 *
 * Values are stored node-major: the components of record `r` are
 * `values[r * componentCount() + c]`. A value that the solver could not print
 * (Fortran overflow marker, "NaN") is stored as NaN and treated as missing.
 */
struct ResultField {
    std::string name;                     ///< Quantity name as written by the solver, e.g. "DISP".
    std::vector<std::string> components;  ///< Component labels, e.g. {"D1","D2","D3"}. Never empty.
    std::size_t step = 0;                 ///< Index into FemResults::steps.
    ResultLocation location = ResultLocation::Node;
    std::vector<std::uint64_t> ids;       ///< External node or element id (see `location`) of each record.
    std::vector<double> values;           ///< ids.size() * components.size() values.
    ResultFieldSemantics semantics;

    std::size_t componentCount() const noexcept { return components.size(); }
    std::size_t recordCount() const noexcept { return ids.size(); }
};

/**
 * @brief One result record at a solver integration point.
 *
 * Values are record-major. The identity is the external element id plus the
 * solver's 1-based integration-point and optional section-point numbers.
 * No extrapolation or averaging is implied by this structure.
 */
struct IntegrationPointResultField {
    std::string name;
    std::vector<std::string> components;
    std::size_t step = 0;
    std::vector<std::uint64_t> elementIds;
    std::vector<std::uint32_t> integrationPointIds;
    std::vector<std::uint32_t> sectionPointIds; // zero when not supplied by the format
    std::vector<double> values;
    ResultFieldSemantics semantics;

    std::size_t componentCount() const noexcept { return components.size(); }
    std::size_t recordCount() const noexcept { return elementIds.size(); }
};

/** @brief One row of the eigenvalue table of a frequency analysis (CalculiX `.dat`). */
struct EigenMode {
    std::size_t mode = 0;      ///< Mode number as printed (1-based).
    double eigenvalue = 0.0;   ///< omega^2 (rad/time)^2; negative values indicate an unstable/rigid-body problem.
    double omega = 0.0;        ///< Real part of the circular frequency, rad/time.
    double frequency = 0.0;    ///< Cycles/time (Hz when the time unit is seconds).
    double imaginary = 0.0;    ///< Imaginary part, rad/time (non-zero for negative eigenvalues).
};

/** @brief Everything read from one result file. */
struct FemResults {
    std::string format;                   ///< Importer name, e.g. "CalculiX FRD".
    std::vector<ResultStep> steps;
    std::vector<ResultField> fields;      ///< Nodal and element-level fields, ordered as found in the file.
    std::vector<IntegrationPointResultField> integrationPointFields; ///< Native integration-point records; never averaged.
    std::vector<EigenMode> eigenmodes;    ///< Eigenvalue table (only the `.dat` importer fills it); no mode shapes.

    /**
     * Node coordinates written in the result file, when the format has them (FRD does).
     * They allow ResultBinding to detect results that belong to a different mesh.
     */
    std::vector<NodeId> modelNodeIds;
    std::vector<std::array<double, 3>> modelCoordinates;

    void clear() noexcept;
    bool empty() const noexcept { return fields.empty() && integrationPointFields.empty() && eigenmodes.empty(); }

    /** @brief Indices into `steps` of the vibration modes (kind Frequency), in file order. */
    std::vector<std::size_t> modeSteps() const;
};

/** @brief Outcome of a result import; shares status codes and diagnostics with MeshImportResult. */
struct ResultImportResult {
    ImportStatus status = ImportStatus::Success;
    std::vector<ImportDiagnostic> diagnostics;
    std::size_t stepsRead = 0;
    std::size_t fieldsRead = 0;

    explicit operator bool() const noexcept { return status == ImportStatus::Success; }
    void addError(ImportStatus newStatus, std::string code, std::string message, std::size_t line = 0);
    void addWarning(std::string code, std::string message, std::size_t line = 0);
};

/** @brief Abstract reader of a result file format. */
class ResultImporter {
public:
    virtual ~ResultImporter() = default;
    virtual std::string formatName() const = 0;
    /** @brief True when the file extension is one this importer handles (the content is not inspected). */
    virtual bool canRead(const std::filesystem::path& path) const = 0;
    /**
     * @brief Read a file.
     * @param path Source file.
     * @param[out] destination Replaced only when the import succeeds.
     */
    virtual ResultImportResult read(const std::filesystem::path& path, FemResults& destination) const = 0;
    /** @brief Same as read() for an already open text stream; `name` is used in messages only. */
    virtual ResultImportResult parse(std::istream& input, FemResults& destination) const = 0;
};

/**
 * @brief Reader for the ASCII result file (`.frd`) written by CalculiX.
 *
 * Reads every nodal result block (`100C` header, `-4` dataset, `-5` components,
 * `-1`/`-2` data lines). Steps are identified by the block header, so several
 * quantities of the same increment share one ResultStep. Components flagged as
 * calculated (the `ALL` entry of vectors) are not stored in the file and are skipped.
 * The node block is read as well, to let the binder check the coordinates.
 *
 * Not supported (reported with a diagnostic): binary `.frd`, element results
 * and element-at-node results, which are skipped.
 */
class CalculixFrdImporter final : public ResultImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    ResultImportResult read(const std::filesystem::path& path, FemResults& destination) const override;
    ResultImportResult parse(std::istream& input, FemResults& destination) const override;
};

/**
 * @brief Reader for the text output (`.dat`) written by CalculiX for `*NODE PRINT` and `*EL PRINT`.
 *
 * Recognised blocks (header `name (components) for set SET and time T`, a blank line, then one row per record):
 *  - **nodal** data, rows `node v1 v2 ...` (`U` displacements, `NT` temperatures, `RF` forces...): a nodal field;
 *  - **element data at integration points**, header components starting with `elem, integ.pnt.` and rows
 *    `element ip v1 v2 ...` (`S` stresses, `E` strains, `ENER`...): reduced to **one value per element by the
 *    plain arithmetic mean over its integration points** (a warning says so; the mean is not volume weighted, which
 *    is exact for the one-point tetrahedron and the 2x2x2 hexahedron but only approximate for 27-point elements);
 *  - the **eigenvalue table** of a frequency step (eigenvalue, rad/time, cycles/time, imaginary part), kept in
 *    FemResults::eigenmodes. It carries no mode shapes: those are in the `.frd`.
 * Quantities are named like the `.frd` ones where there is one (`DISP`, `STRESS`, `STRAIN`, `MESTRAIN`), so the
 * binder treats them in the same way; components keep the labels of the header (stress order `SXX SYY SZZ SXY SXZ SYZ`
 * is the one CalculiX uses in this file). Blocks with the same time share a ResultStep. `TOTALS` blocks (no row ids)
 * and other text are skipped. Use mergeResults() to combine a `.dat` with the `.frd` of the same run.
 */
class CalculixDatImporter final : public ResultImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    ResultImportResult read(const std::filesystem::path& path, FemResults& destination) const override;
    ResultImportResult parse(std::istream& input, FemResults& destination) const override;
};

/**
 * @brief Add the steps and fields of `extra` to `base` (for example the element stresses of a `.dat` to the nodal
 *        results of the `.frd` of the same run).
 *
 * Steps are matched by time (relative difference <= 1e-5, which covers the 6-7 significant digits the solvers print);
 * frequency steps are matched by mode number. A step of `extra` without partner is appended. A field that `base`
 * already has for the matched step and location is not replaced (a warning is added). Model coordinates and eigenvalues
 * of `base` are kept; those of `extra` are used only when `base` has none.
 * @return Warnings about skipped fields and unmatched steps; success is always true.
 */
ResultImportResult mergeResults(FemResults& base, const FemResults& extra);

/**
 * @brief Reader for delimited text tables with one row per node (generic exchange format).
 *
 * - The first non-empty line that is not a `#` or `//` comment is the header.
 * - The delimiter (`,` `;` tab) is detected from the header. With `;` or tab, a decimal comma
 *   ("1,5") is accepted, as written by spreadsheets in many locales.
 * - One column identifies the rows: `node`, `node_id`, `nodeid`, `nid` or `id` (case-insensitive) for a
 *   **nodal** table with the external node id, or `element`, `element_id`, `elementid`, `elem`, `elem_id`
 *   or `eid` for an **element** table with the external element id. Having both kinds is an error.
 *   Every other column becomes a scalar field named like its header.
 * - Columns `<name>_x`, `<name>_y`, `<name>_z` are grouped into one 3-component field `<name>`
 *   (so `U_x,U_y,U_z` gives the displacement vector `U`, usable for the deformed shape).
 * - An empty cell or `nan` is a missing value. Duplicate ids are an error.
 * - A CSV file holds a single step.
 */
class CsvResultImporter final : public ResultImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    ResultImportResult read(const std::filesystem::path& path, FemResults& destination) const override;
    ResultImportResult parse(std::istream& input, FemResults& destination) const override;
};

/**
 * @brief Read a result file, choosing the importer from the file extension.
 * @return The importer's result; UnsupportedFormat when no importer handles the extension.
 */
ResultImportResult readResults(const std::filesystem::path& path, FemResults& destination);

/**
 * @brief Read several result files and merge them in order with mergeResults().
 * @param paths One or more files; the first one provides the model coordinates (a `.frd`, normally).
 * The destination is replaced only when every file was read.
 */
ResultImportResult readResultsMerged(const std::vector<std::filesystem::path>& paths, FemResults& destination);

} // namespace OpenModel
