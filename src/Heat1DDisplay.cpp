/**
 * @file Heat1DDisplay.cpp
 * @brief Display mesh of a one-dimensional heat problem: extruded cross-sections per material, the axis tube and the
 *        temperature of each element along the axis.
 */
#include <OpenModel/Heat1DDisplay.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace OpenModel::Heat1D {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr const char* kAxisGroup = "AXIS";

/** @brief A closed outline in the plane of the section: (y, z) pairs, counter-clockwise. */
using Ring = std::vector<std::array<double, 2>>;

/** @brief Regular polygon with `n` vertices on a circle of radius r. */
Ring polygonRing(int n, double r) {
    Ring ring;
    ring.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double angle = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(n);
        ring.push_back({{r * std::cos(angle), r * std::sin(angle)}});
    }
    return ring;
}

Ring rectRing(double a, double b) {
    return {{{{-0.5 * a, -0.5 * b}}, {{0.5 * a, -0.5 * b}}, {{0.5 * a, 0.5 * b}}, {{-0.5 * a, 0.5 * b}}}};
}

/** @brief Outline of a constant section, or an empty ring when the kind is not a solid shape. */
Ring outlineOf(const SectionSpec& s, int ringPoints) {
    if (s.kind == "rect") return rectRing(s.a, s.b);
    if (s.kind == "circle") return polygonRing(ringPoints, s.r);
    if (s.kind == "polygon") return polygonRing(s.sides, s.r);
    return {};
}

/** @brief Adds triangles to the surface groups and records the position along the axis of each element. */
class Builder {
public:
    explicit Builder(MeshData& mesh) : m_mesh(mesh) {}

    NodeIndex node(double x, double y, double z) { return m_mesh.addNode(x, y, z); }

    /** @brief One triangle of the surface of `group`; its position is the mean x of its corners. */
    void triangle(const std::string& group, NodeIndex a, NodeIndex b, NodeIndex c) {
        const ElementId id = m_nextElement++;
        m_mesh.addElement(blockName(group), ElementType::Triangle3, id, {a, b, c});
        m_groups[group].push_back(id);
        const double xa = m_mesh.nodeCoordinates(a)[0], xb = m_mesh.nodeCoordinates(b)[0], xc = m_mesh.nodeCoordinates(c)[0];
        m_x[id] = (xa + xb + xc) / 3.0;
    }

    /** @brief A quadrilateral (p0 p1 p2 p3 in order) split into two triangles. */
    void quad(const std::string& group, NodeIndex p0, NodeIndex p1, NodeIndex p2, NodeIndex p3) {
        triangle(group, p0, p1, p2);
        triangle(group, p0, p2, p3);
    }

    /** @brief Registers one physical group per surface group. */
    void finish() {
        int tag = 1;
        for (const auto& [name, ids] : m_groups) m_mesh.addPhysicalGroup(name, 2, tag++, ids, name);
    }

    /** @brief Position along the axis of an element. */
    double xOf(ElementId id) const { return m_x.at(id); }

private:
    static std::string blockName(const std::string& group) { return "heat:" + group; }

    MeshData& m_mesh;
    ElementId m_nextElement = 1;
    std::map<std::string, std::vector<ElementId>> m_groups;
    std::map<ElementId, double> m_x;
};

/**
 * @brief Extrudes one segment [x0, x1] of a section into surfaces. A tube has an outer and an inner outline and two
 *        annular end faces; the other sections are solid with two fan-triangulated end faces.
 */
void extrudeSegment(Builder& b, const std::string& material, const SectionSpec& s, double x0, double x1, int ringPoints) {
    const Ring outer = s.kind == "tube" ? polygonRing(ringPoints, s.ro) : outlineOf(s, ringPoints);
    const std::size_t n = outer.size();
    if (s.kind == "tube" && s.ri > 0.0) {
        const Ring inner = polygonRing(ringPoints, s.ri);
        std::vector<NodeIndex> o0(n), o1(n), i0(n), i1(n);
        for (std::size_t k = 0; k < n; ++k) {
            o0[k] = b.node(x0, outer[k][0], outer[k][1]);
            o1[k] = b.node(x1, outer[k][0], outer[k][1]);
            i0[k] = b.node(x0, inner[k][0], inner[k][1]);
            i1[k] = b.node(x1, inner[k][0], inner[k][1]);
        }
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t j = (k + 1) % n;
            b.quad(material, o0[k], o0[j], o1[j], o1[k]);   // outer lateral surface
            b.quad(material, i0[k], i1[k], i1[j], i0[j]);   // inner lateral surface
            b.quad(material, o0[k], i0[k], i0[j], o0[j]);   // end face at x0
            b.quad(material, o1[k], o1[j], i1[j], i1[k]);   // end face at x1
        }
        return;
    }
    // Solid section: lateral surface and the two end faces, each fan-triangulated from its centre.
    std::vector<NodeIndex> a(n), c(n);
    for (std::size_t k = 0; k < n; ++k) {
        a[k] = b.node(x0, outer[k][0], outer[k][1]);
        c[k] = b.node(x1, outer[k][0], outer[k][1]);
    }
    const NodeIndex centre0 = b.node(x0, 0.0, 0.0);
    const NodeIndex centre1 = b.node(x1, 0.0, 0.0);
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t j = (k + 1) % n;
        b.quad(material, a[k], a[j], c[j], c[k]);
        b.triangle(material, centre0, a[j], a[k]);
        b.triangle(material, centre1, c[k], c[j]);
    }
}

/** @brief Temperature of the 1D solution at x: linear between the nodes, held at the ends. */
double temperatureAt(const std::vector<double>& nodes, const std::vector<double>& T, double x) {
    if (x <= nodes.front()) return T.front();
    if (x >= nodes.back()) return T.back();
    const auto it = std::upper_bound(nodes.begin(), nodes.end(), x);
    const std::size_t i = static_cast<std::size_t>(it - nodes.begin()) - 1;
    const double t = (x - nodes[i]) / (nodes[i + 1] - nodes[i]);
    return T[i] + t * (T[i + 1] - T[i]);
}

} // namespace

DisplayResult displayMesh(const ScriptProblem& problem, const DisplayOptions& options, const std::vector<double>* temperatures) {
    DisplayResult result;
    if (options.ringPoints < 3 || options.axisSides < 3) {
        result.error = "ringPoints and axisSides must be at least 3";
        return result;
    }
    if (!(options.axisRadiusPercent > 0.0)) {
        result.error = "the axis radius must be positive";
        return result;
    }
    if (problem.segments.empty()) {
        result.error = "the script has no segment to draw";
        return result;
    }
    const auto& nodes = problem.model.nodes;
    if (nodes.size() < 2) {
        result.error = "the model has no element";
        return result;
    }
    if (temperatures && temperatures->size() != nodes.size()) {
        result.error = "the temperatures do not match the nodes of the model";
        return result;
    }
    // Every segment must have a constant section: its own shape, or the global one.
    for (const SegmentSpec& sg : problem.segments) {
        const SectionSpec& s = sg.section.kind.empty() ? problem.globalSection : sg.section;
        if (s.kind == "table" || s.kind.empty()) {
            result.error = "a tabulated section cannot be drawn: the display needs a constant shape on each segment";
            return result;
        }
        if (s.kind != "tube" && outlineOf(s, options.ringPoints).empty()) {
            result.error = "unknown section '" + s.kind + "'";
            return result;
        }
    }

    Builder b(result.mesh);
    double x = 0.0;
    for (const SegmentSpec& sg : problem.segments) {
        const SectionSpec& s = sg.section.kind.empty() ? problem.globalSection : sg.section;
        extrudeSegment(b, sg.material, s, x, x + sg.length, options.ringPoints);
        x += sg.length;
    }

    // The axis: a tube along the nodes of the solver. Its radius is a percentage of the extent of the whole model,
    // computed from the solid just built.
    double extent = 0.0;
    const auto& coords = result.mesh.getCoordinates();
    for (int axis = 0; axis < 3; ++axis) {
        double lo = 0.0, hi = 0.0;
        bool first = true;
        for (std::size_t i = axis; i < coords.size(); i += 3) {
            if (first) { lo = hi = coords[i]; first = false; }
            lo = std::min(lo, coords[i]);
            hi = std::max(hi, coords[i]);
        }
        extent = std::max(extent, hi - lo);
    }
    const double radius = 0.01 * options.axisRadiusPercent * extent;
    const Ring ring = polygonRing(options.axisSides, radius);
    const std::size_t n = ring.size();
    for (std::size_t i = 0; i + 1 < nodes.size(); ++i) {
        std::vector<NodeIndex> p(n), q(n);
        for (std::size_t k = 0; k < n; ++k) {
            p[k] = b.node(nodes[i], ring[k][0], ring[k][1]);
            q[k] = b.node(nodes[i + 1], ring[k][0], ring[k][1]);
        }
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t j = (k + 1) % n;
            b.quad(kAxisGroup, p[k], p[j], q[j], q[k]);
        }
    }
    b.finish();

    // One value per element, in the order of the blocks (the order the viewer reads them).
    if (temperatures) {
        result.values.reserve(result.mesh.getNumElements());
        for (const ElementBlock& block : result.mesh.getElementBlocks()) {
            for (const ElementId id : block.ids) {
                result.values.push_back(temperatureAt(nodes, *temperatures, b.xOf(id)));
            }
        }
    }
    return result;
}

} // namespace OpenModel::Heat1D
