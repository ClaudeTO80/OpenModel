#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

/** Location at which a solver reports a quantity. */
enum class ResultLocation { Node, Element, IntegrationPoint };

/** Meaning of the numeric parameter attached to a solution step. */
enum class StepParameterKind { None, Time, Frequency, LoadFactor, User };

/** Semantic shape of a field; labels remain authoritative for component ordering. */
enum class ComponentKind { Scalar, Vector, SymmetricTensor, Matrix, Unknown };

/** Convention used for shear strain components. */
enum class ShearStrainConvention { NotApplicable, Tensorial, Engineering, Unknown };

/** Coordinate system in which vector/tensor components are expressed. */
enum class ReferenceFrame { Global, Local, Material, Unknown };

/** Describes a physical unit. Empty strings mean that the unit is not known. */
struct UnitSystem {
    std::string name;
    std::string length;
    std::string force;
    std::string mass;
    std::string time;
    std::string temperature;
    std::string displacement;
    std::string stress;
    std::string strain;
};

/** External identity of one result location. IDs are never mesh-array offsets. */
struct ResultPosition {
    ResultLocation location = ResultLocation::Node;
    std::uint64_t entityId = 0;          // node, element, or parent element ID
    std::uint32_t integrationPoint = 0;  // 1-based solver IP number; zero otherwise
    std::uint32_t sectionPoint = 0;      // 1-based section point; zero when not applicable
};

/** A physical or solver-defined state in the result history. */
struct ResultStep {
    std::size_t index = 0;
    double parameter = 0.0;
    StepParameterKind parameterKind = StepParameterKind::None;
    std::size_t mode = 0;
    std::string label;
};

/** Metadata common to every sample returned by a source. */
struct ResultSourceMetadata {
    std::string solverName;
    std::string solverVersion;
    std::string resultFormat;
    std::string sourceName;
    std::string modelFingerprint;
    UnitSystem units;
};

/**
 * One unmodified sample from a solver result source.
 *
 * components are in componentLabels order. The contract deliberately does not
 * impose a universal Voigt order: adapters must preserve solver labels and
 * fatigue algorithms must explicitly normalize a supported layout.
 */
struct ResultSample {
    ResultPosition position;
    ResultStep step;
    std::string fieldName;
    ComponentKind componentKind = ComponentKind::Unknown;
    std::vector<std::string> componentLabels;
    std::vector<double> components;
    ReferenceFrame frame = ReferenceFrame::Unknown;
    ShearStrainConvention shearConvention = ShearStrainConvention::Unknown;

    // Provenance flags. False means "not reported", not necessarily "never happened".
    bool derived = false;
    bool averaged = false;
    bool interpolated = false;
    bool extrapolated = false;
};

/** Read-only, solver-neutral interface consumed by fatigue algorithms. */
class IResultSource {
public:
    virtual ~IResultSource() = default;
    virtual const ResultSourceMetadata& metadata() const = 0;
    virtual std::size_t stepCount() const = 0;
    virtual bool getStep(std::size_t index, ResultStep& out) const = 0;

    /**
     * Read exactly the requested field/position/step.
     * No location fallback, interpolation, extrapolation or averaging is allowed.
     * False means absent, ambiguous, structurally invalid or non-finite data.
     */
    virtual bool readSample(const std::string& fieldName,
                            const ResultPosition& position,
                            std::size_t stepIndex,
                            ResultSample& out) const = 0;
};

enum class FatigueValueStatus {
    Valid,
    MissingInput,
    InvalidInput,
    UnsupportedLocation,
    OutsideModelValidity,
    CalculationFailed
};

struct FatigueValue {
    ResultPosition position;
    double damage = 0.0;
    double lifeCycles = 0.0;
    FatigueValueStatus status = FatigueValueStatus::MissingInput;
    std::string diagnostic;
};

} // namespace Fatigue
} // namespace OpenModel
