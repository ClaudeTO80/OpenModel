#include <OpenModel/ResultBinding.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <sstream>
#include <utility>

namespace OpenModel {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr std::size_t kNone = static_cast<std::size_t>(-1);

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

BindResult failure(BindResult r, std::string message) {
    r.success = false;
    r.error = std::move(message);
    return r;
}

void warn(BindResult& r, std::string code, std::string message) {
    r.diagnostics.push_back({ImportDiagnosticLevel::Warning, std::move(code), std::move(message), 0});
}

enum class TensorKind { None, Stress, Strain };

/**
 * Component positions of a symmetric tensor in the order XX, YY, ZZ, XY, YZ, ZX and whether it is a stress
 * or a strain. Prefixed names (SXX / EXX, the CalculiX convention) identify the kind by themselves; bare
 * names (XX...) need the field name to say "stress" or "strain".
 */
TensorKind tensorLayout(const ResultField& field, std::array<std::size_t, 6>& index) {
    if (field.componentCount() != 6) return TensorKind::None;
    const std::string fieldName = lowered(field.name);
    static const char* keys[6] = {"xx", "yy", "zz", "xy", "yz", "zx"};
    for (const char prefix : {'s', 'e'}) {
        bool allPrefixed = true, complete = true;
        for (std::size_t k = 0; k < 6 && complete; ++k) {
            std::size_t found = kNone;
            bool withPrefix = false;
            for (std::size_t c = 0; c < 6 && found == kNone; ++c) {
                const std::string name = lowered(field.components[c]);
                if (name == std::string(1, prefix) + keys[k]) { found = c; withPrefix = true; }
                else if (name == keys[k]) { found = c; }
                else if (k == 5 && (name == std::string(1, prefix) + "xz" || name == "xz")) { found = c; withPrefix = name[0] == prefix; }
            }
            if (found == kNone) complete = false;
            else { index[k] = found; allPrefixed = allPrefixed && withPrefix; }
        }
        if (!complete) continue;
        const bool namedStress = fieldName.find("stress") != std::string::npos || fieldName == "s" || fieldName == "sigma";
        const bool namedStrain = fieldName.find("strain") != std::string::npos || fieldName == "e" || fieldName == "epsilon";
        if (prefix == 's' && (allPrefixed || namedStress)) return TensorKind::Stress;
        if (prefix == 'e' && (allPrefixed || namedStrain)) return TensorKind::Strain;
    }
    // Bare names with a field name that says what they are, whatever the prefix attempts above found.
    return TensorKind::None;
}

/**
 * Eigenvalues of a real symmetric 3x3 matrix by cyclic Jacobi rotations, sorted descending.
 * Jacobi is exact enough for repeated eigenvalues (hydrostatic states), where closed formulas lose accuracy.
 */
std::array<double, 3> principalValues(double xx, double yy, double zz, double xy, double yz, double zx) {
    double a[3][3] = {{xx, xy, zx}, {xy, yy, yz}, {zx, yz, zz}};
    const double scale = std::max({std::fabs(xx), std::fabs(yy), std::fabs(zz), std::fabs(xy), std::fabs(yz), std::fabs(zx)});
    if (scale == 0.0) return {0.0, 0.0, 0.0};
    for (int sweep = 0; sweep < 60; ++sweep) {
        const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
        if (off <= 1e-32 * scale * scale) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (a[p][q] == 0.0) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), sn = t * c;
                for (int k = 0; k < 3; ++k) {          // A <- A J (columns p, q)
                    const double akp = a[k][p], akq = a[k][q];
                    a[k][p] = c * akp - sn * akq;
                    a[k][q] = sn * akp + c * akq;
                }
                for (int k = 0; k < 3; ++k) {          // A <- J^T A (rows p, q)
                    const double apk = a[p][k], aqk = a[q][k];
                    a[p][k] = c * apk - sn * aqk;
                    a[q][k] = sn * apk + c * aqk;
                }
            }
    }
    std::array<double, 3> v{{a[0][0], a[1][1], a[2][2]}};
    std::sort(v.begin(), v.end(), std::greater<double>());
    return v;
}

bool isDisplacementName(const std::string& lowerName) {
    return lowerName == "disp" || lowerName == "displacement" || lowerName == "u";
}

std::string joinName(const std::string& field, const std::string& component) { return field + "." + component; }

using Column = std::vector<double>;

/**
 * Derived scalars of a tensor field, one value per entity (node or element); NaN where any component is missing.
 * @param emit Called with the suffix of the derived name ("von Mises"...) and its values.
 */
template <class Emit>
void deriveTensor(TensorKind kind, const std::vector<Column>& col, const std::array<std::size_t, 6>& s, std::size_t count,
                  bool engineeringShear, Emit emit) {
    std::vector<Column> out(6, Column(count, kNaN));
    const double shear = (kind == TensorKind::Strain && engineeringShear) ? 0.5 : 1.0;
    for (std::size_t n = 0; n < count; ++n) {
        const double xx = col[s[0]][n], yy = col[s[1]][n], zz = col[s[2]][n];
        const double xy = col[s[3]][n] * shear, yz = col[s[4]][n] * shear, zx = col[s[5]][n] * shear;
        if (!std::isfinite(xx + yy + zz + xy + yz + zx)) continue;
        const auto p = principalValues(xx, yy, zz, xy, yz, zx);
        const double mean = (xx + yy + zz) / 3.0;
        if (kind == TensorKind::Stress) {
            out[0][n] = std::sqrt(0.5 * ((xx - yy) * (xx - yy) + (yy - zz) * (yy - zz) + (zz - xx) * (zz - xx)) +
                                  3.0 * (xy * xy + yz * yz + zx * zx));
            out[4][n] = p[0] - p[2];
            out[5][n] = mean;
        } else {
            const double dx = xx - mean, dy = yy - mean, dz = zz - mean;
            out[0][n] = std::sqrt(2.0 / 3.0 * (dx * dx + dy * dy + dz * dz + 2.0 * (xy * xy + yz * yz + zx * zx)));
            out[4][n] = p[0] - p[2];  // maximum engineering shear strain (gamma = 2 * tensor shear)
            out[5][n] = xx + yy + zz;
        }
        out[1][n] = p[0];
        out[2][n] = p[1];
        out[3][n] = p[2];
    }
    // principal values come after the equivalent value, then the remaining invariant
    if (kind == TensorKind::Stress) {
        emit("von Mises", std::move(out[0]));
        emit("principal 1", std::move(out[1]));
        emit("principal 2", std::move(out[2]));
        emit("principal 3", std::move(out[3]));
        emit("Tresca", std::move(out[4]));
        emit("hydrostatic", std::move(out[5]));
    } else {
        emit("equivalent", std::move(out[0]));
        emit("principal 1", std::move(out[1]));
        emit("principal 2", std::move(out[2]));
        emit("principal 3", std::move(out[3]));
        emit("max shear", std::move(out[4]));
        emit("volumetric", std::move(out[5]));
    }
}

/** Spreads the records of a field over `count` entities (NaN where the field has no record). */
std::vector<Column> scatter(const ResultField& f, const std::vector<std::size_t>& target, std::size_t count) {
    const std::size_t comps = f.componentCount();
    std::vector<Column> column(comps, Column(count, kNaN));
    for (std::size_t i = 0; i < f.recordCount(); ++i) {
        if (target[i] == kNone) continue;
        for (std::size_t c = 0; c < comps; ++c) column[c][target[i]] = f.values[i * comps + c];
    }
    return column;
}

/** Finds the displacement field of a step: by `wanted` (lower case) or, when empty, by the usual names. */
const ResultField* findDisplacement(const std::vector<const ResultField*>& fields, const std::string& wanted) {
    for (const ResultField* f : fields) {
        if (f->location != ResultLocation::Node) continue;
        const std::string name = lowered(f->name);
        if (wanted.empty() ? (isDisplacementName(name) && f->componentCount() >= 3) : name == wanted) return f;
    }
    return nullptr;
}

} // namespace

BindResult bindResults(const MeshData& mesh, const FemResults& results, const BindOptions& options, WebViewOptions& view) {
    BindResult r;
    if (mesh.getNumNodes() == 0) return failure(r, "the mesh has no nodes");
    if (results.steps.empty() || results.fields.empty())
        return failure(r, results.eigenmodes.empty() ? "the results are empty"
                                                     : "the file only holds the eigenvalue table (frequencies), no fields or mode shapes: "
                                                       "give the .frd of the run as well (files can be merged)");

    // 0. Modal or single-step view?
    const std::vector<std::size_t> modeSteps = results.modeSteps();
    const std::string wanted = lowered(options.displacementField);
    bool useModes = false;
    std::size_t modeLimit = 0;
    if (wanted != "none") {
        if (options.maxModes == BindOptions::kAutoModes) {
            if (options.step == BindOptions::kLastStep && !modeSteps.empty() && modeSteps.size() == results.steps.size()) {
                useModes = true;
                modeLimit = BindOptions::kAutoModeLimit;
            }
        } else if (options.maxModes > 0) {
            if (modeSteps.empty()) return failure(r, "modes were requested but the results contain no vibration mode");
            useModes = true;
            modeLimit = options.maxModes;
        }
    }

    if (options.step == BindOptions::kLastStep) r.step = useModes ? modeSteps.front() : results.steps.size() - 1;
    else r.step = options.step;
    if (r.step >= results.steps.size())
        return failure(r, "step " + std::to_string(r.step) + " does not exist (the file has " + std::to_string(results.steps.size()) + ")");
    r.stepLabel = results.steps[r.step].label;

    std::vector<const ResultField*> fields;
    for (const auto& f : results.fields)
        if (f.step == r.step) fields.push_back(&f);
    if (fields.empty()) return failure(r, "step " + std::to_string(r.step) + " (" + r.stepLabel + ") has no result fields");

    const std::size_t nodeCount = mesh.getNumNodes();
    const std::size_t elementCount = mesh.getNumElements();

    // 1. The results must belong to this mesh.
    if (options.checkCoordinates && !results.modelNodeIds.empty()) {
        double scale = 0.0;
        std::array<double, 3> lo{{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::infinity()}};
        std::array<double, 3> hi{{-lo[0], -lo[1], -lo[2]}};
        for (std::size_t n = 0; n < nodeCount; ++n) {
            const auto c = mesh.nodeCoordinates(n);
            for (int k = 0; k < 3; ++k) {
                lo[k] = std::min(lo[k], c[k]);
                hi[k] = std::max(hi[k], c[k]);
                scale = std::max(scale, std::fabs(c[k]));
            }
        }
        const double diagonal = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                          (hi[2] - lo[2]) * (hi[2] - lo[2]));
        scale = std::max(scale, diagonal);
        const double tolerance = options.coordinateTolerance * scale;
        std::size_t compared = 0, mismatched = 0;
        NodeId firstBad = 0;
        for (std::size_t i = 0; i < results.modelNodeIds.size(); ++i) {
            if (!mesh.hasNodeId(results.modelNodeIds[i])) continue;
            ++compared;
            const auto c = mesh.nodeCoordinates(mesh.nodeIndex(results.modelNodeIds[i]));
            const auto& m = results.modelCoordinates[i];
            if (std::fabs(c[0] - m[0]) > tolerance || std::fabs(c[1] - m[1]) > tolerance || std::fabs(c[2] - m[2]) > tolerance) {
                if (mismatched++ == 0) firstBad = results.modelNodeIds[i];
            }
        }
        if (compared == 0)
            return failure(r, "none of the " + std::to_string(results.modelNodeIds.size()) +
                              " nodes of the result file exists in the mesh: the results belong to a different model");
        if (mismatched > 0) {
            std::ostringstream text;
            text << mismatched << " of " << compared << " shared nodes have different coordinates in the result file (first: node "
                 << firstBad << ")";
            if (mismatched * 100 > compared) return failure(r, text.str() + ": the results were computed on a different mesh");
            warn(r, "BIND_COORDINATES", text.str());
        }
        if (compared < results.modelNodeIds.size() || compared < nodeCount)
            warn(r, "BIND_NODE_SETS", "the result file has " + std::to_string(results.modelNodeIds.size()) + " nodes, the mesh " +
                                          std::to_string(nodeCount) + ", " + std::to_string(compared) + " in common");
    }

    // 2. Displacement field of the selected step (single-step view) and id maps.
    const ResultField* displacementSource = nullptr;
    if (wanted != "none" && !useModes) {
        displacementSource = findDisplacement(fields, wanted);
        if (!wanted.empty() && !displacementSource)
            return failure(r, "displacement field '" + options.displacementField + "' not found in step " + std::to_string(r.step));
        if (displacementSource && displacementSource->componentCount() < 3)
            return failure(r, "displacement field '" + displacementSource->name + "' has fewer than 3 components");
    }

    std::unordered_map<ElementId, std::size_t> elementIndex;
    auto ensureElementIndex = [&]() {
        if (!elementIndex.empty()) return;
        std::size_t global = 0;
        for (const auto& block : mesh.getElementBlocks())
            for (const ElementId id : block.ids) elementIndex.emplace(id, global++);
    };

    // 3. Fields of the selected step.
    std::vector<WebNodalField> producedNodal;
    std::vector<WebElementField> producedElement;
    std::vector<std::array<double, 3>> displacement;
    std::size_t unmatched = 0, unmatchedElements = 0, worstMissing = 0, worstMissingElements = 0;

    for (const ResultField* f : fields) {
        const std::size_t comps = f->componentCount();
        const std::size_t records = f->recordCount();
        const bool nodal = f->location == ResultLocation::Node;
        const std::size_t count = nodal ? nodeCount : elementCount;
        if (!nodal) ensureElementIndex();
        std::vector<std::size_t> target(records, kNone);
        std::size_t unmatchedHere = 0;
        for (std::size_t i = 0; i < records; ++i) {
            if (nodal) {
                if (mesh.hasNodeId(static_cast<NodeId>(f->ids[i]))) target[i] = mesh.nodeIndex(static_cast<NodeId>(f->ids[i]));
                else ++unmatchedHere;
            } else {
                const auto it = elementIndex.find(static_cast<ElementId>(f->ids[i]));
                if (it != elementIndex.end()) target[i] = it->second;
                else ++unmatchedHere;
            }
        }
        (nodal ? unmatched : unmatchedElements) += unmatchedHere;
        if (unmatchedHere == records) {
            warn(r, "BIND_FIELD_SKIPPED", std::string("no ") + (nodal ? "node" : "element") + " of '" + f->name +
                                              "' exists in the mesh; field skipped");
            continue;
        }

        std::vector<Column> column = scatter(*f, target, count);

        if (f == displacementSource) {
            displacement.assign(nodeCount, {0.0, 0.0, 0.0});
            for (std::size_t n = 0; n < nodeCount; ++n)
                for (std::size_t k = 0; k < 3; ++k)
                    if (std::isfinite(column[k][n])) displacement[n][k] = column[k][n];
        }

        auto finish = [&](std::string name, Column values) {
            double lo = std::numeric_limits<double>::infinity();
            for (const double v : values)
                if (std::isfinite(v)) lo = std::min(lo, v);
            if (!std::isfinite(lo)) lo = 0.0;
            std::size_t missing = 0;
            for (double& v : values)
                if (!std::isfinite(v)) { v = lo; ++missing; }
            if (nodal) {
                worstMissing = std::max(worstMissing, missing);
                producedNodal.push_back({std::move(name), std::move(values)});
            } else {
                worstMissingElements = std::max(worstMissingElements, missing);
                producedElement.push_back({std::move(name), std::move(values)});
            }
        };

        if (options.derivedFields) {
            if (comps >= 3 && comps != 6) {
                Column magnitude(count);
                for (std::size_t n = 0; n < count; ++n)
                    magnitude[n] = std::sqrt(column[0][n] * column[0][n] + column[1][n] * column[1][n] + column[2][n] * column[2][n]);
                finish(f->name + " magnitude", std::move(magnitude));
            }
            std::array<std::size_t, 6> layout{};
            const TensorKind kind = tensorLayout(*f, layout);
            if (kind != TensorKind::None)
                deriveTensor(kind, column, layout, count, options.engineeringShearStrain,
                             [&](const char* suffix, Column values) { finish(f->name + " " + suffix, std::move(values)); });
        }
        if (comps == 1) {
            finish(f->name, std::move(column[0]));
        } else {
            for (std::size_t c = 0; c < comps; ++c) finish(joinName(f->name, f->components[c]), std::move(column[c]));
        }
    }
    if (producedNodal.empty() && producedElement.empty())
        return failure(r, "no field of step " + std::to_string(r.step) + " matches the nodes or elements of the mesh");

    // 4. Vibration modes: the displacement of each mode step.
    std::vector<WebMode> modes;
    if (useModes) {
        std::size_t taken = 0;
        for (const std::size_t stepIndex : modeSteps) {
            if (taken >= modeLimit) break;
            ++taken;
            std::vector<const ResultField*> stepFields;
            for (const auto& f : results.fields)
                if (f.step == stepIndex) stepFields.push_back(&f);
            const ResultField* source = findDisplacement(stepFields, wanted);
            if (!source) {
                warn(r, "BIND_MODE_SKIPPED", "'" + results.steps[stepIndex].label + "' has no displacement field; mode skipped");
                continue;
            }
            std::vector<std::size_t> target(source->recordCount(), kNone);
            for (std::size_t i = 0; i < target.size(); ++i)
                if (mesh.hasNodeId(static_cast<NodeId>(source->ids[i]))) target[i] = mesh.nodeIndex(static_cast<NodeId>(source->ids[i]));
            const std::vector<Column> column = scatter(*source, target, nodeCount);
            WebMode mode;
            mode.label = results.steps[stepIndex].label;
            mode.frequency = results.steps[stepIndex].value;
            mode.displacement.assign(nodeCount, {0.0, 0.0, 0.0});
            for (std::size_t n = 0; n < nodeCount; ++n)
                for (std::size_t k = 0; k < 3; ++k)
                    if (std::isfinite(column[k][n])) mode.displacement[n][k] = column[k][n];
            modes.push_back(std::move(mode));
        }
        if (modes.empty()) return failure(r, "none of the vibration modes has a displacement field");
        if (modeSteps.size() > taken)
            warn(r, "BIND_MODES_LIMITED", "only the first " + std::to_string(taken) + " of " + std::to_string(modeSteps.size()) + " modes are bound");
    }

    r.unmatchedRecords = unmatched;
    r.nodesWithoutData = worstMissing;
    r.unmatchedElementRecords = unmatchedElements;
    r.elementsWithoutData = worstMissingElements;
    if (unmatched > 0)
        warn(r, "BIND_UNMATCHED", std::to_string(unmatched) + " result records refer to nodes that are not in the mesh and were ignored");
    if (unmatchedElements > 0)
        warn(r, "BIND_UNMATCHED_ELEMENTS", std::to_string(unmatchedElements) + " result records refer to elements that are not in the mesh and were ignored");
    if (worstMissing > 0)
        warn(r, "BIND_MISSING", std::to_string(worstMissing) + " mesh nodes have no value in at least one field; they are drawn with the field minimum");
    if (worstMissingElements > 0)
        warn(r, "BIND_MISSING_ELEMENTS", std::to_string(worstMissingElements) + " mesh elements have no value in at least one field; they are drawn with the field minimum");

    // 5. Commit to the view options only now that nothing can fail any more.
    r.fieldsBound = producedNodal.size();
    r.elementFieldsBound = producedElement.size();
    for (auto& f : producedNodal) view.fields.push_back(std::move(f));
    for (auto& f : producedElement) view.elementFields.push_back(std::move(f));
    if (useModes) {
        r.modesBound = modes.size();
        for (const auto& m : modes) r.modeFrequencies.push_back(m.frequency);
        view.displacement.clear();
        view.modes = std::move(modes);
        view.displacementName = "mode shape";
        r.hasDisplacement = true;
        r.displacementName = "mode shape";
    } else if (displacementSource && !displacement.empty()) {
        view.modes.clear();
        view.displacement = std::move(displacement);
        view.displacementName = displacementSource->name;
        r.hasDisplacement = true;
        r.displacementName = displacementSource->name;
    }
    view.resultsLabel = (options.sourceName.empty() ? results.format : options.sourceName) + " - " +
                        (useModes ? std::to_string(r.modesBound) + " modes" : r.stepLabel);
    r.success = true;
    return r;
}

} // namespace OpenModel
