#pragma once
#include <OpenModel/Fatigue/FatigueInput.hpp>
#include <array>
#include <string>
namespace OpenModel { namespace Fatigue {
enum class TensorNormalizationStatus {
    Success, UnsupportedQuantity, UnsupportedComponentKind, InvalidComponentCount,
    UnknownComponentLabel, DuplicateComponent, MissingComponent, UnknownShearConvention,
    UnsupportedCoordinateFrame, MissingCoordinateTransform, InvalidCoordinateTransform, NonFiniteValue
};
struct Tensor3 {
    std::array<double,9> values{{0,0,0,0,0,0,0,0,0}};
    double operator()(std::size_t r, std::size_t c) const noexcept { return values[r*3+c]; }
};
struct Vector3 {
    std::array<double,3> values{{0,0,0}};
    double operator[](std::size_t i) const noexcept { return values[i]; }
};
struct TensorNormalizationResult {
    TensorNormalizationStatus status = TensorNormalizationStatus::InvalidComponentCount;
    Tensor3 tensor;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == TensorNormalizationStatus::Success; }
};
struct VectorNormalizationResult {
    TensorNormalizationStatus status = TensorNormalizationStatus::InvalidComponentCount;
    Vector3 vector;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == TensorNormalizationStatus::Success; }
};
/** Canonical 3D tensor/vector normalization; no solver order or unit assumptions. */
class TensorNormalizer final {
public:
    static TensorNormalizationResult normalizeTensor(const ResultSample&, ReferenceFrame target = ReferenceFrame::Global);
    static VectorNormalizationResult normalizeVector(const ResultSample&, ReferenceFrame target = ReferenceFrame::Global);
    static double vonMisesStress(const Tensor3&) noexcept;
    static bool principalValues(const Tensor3&, std::array<double,3>& descending) noexcept;
};
} }
