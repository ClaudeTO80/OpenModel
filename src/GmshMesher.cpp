#include <OpenModel/GmshMesher.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <limits>

namespace {
using Vec3 = std::array<double, 3>;

double dot3(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/** @brief A solid that is a straight prism: the planar face to sweep, the sweep direction and the height. */
struct PrismSweep {
    int solid = 0;
    int baseFace = 0;
    Vec3 direction{};   ///< unit vector from the base face towards the opposite face
    double length = 0.0;
};

/** @brief Unit normal of a face at the point of the face closest to a given point. */
Vec3 faceNormalNear(int face, double x, double y, double z, Vec3* closest = nullptr) {
    std::vector<double> point, parametric, normal;
    gmsh::model::getClosestPoint(2, face, {x, y, z}, point, parametric);
    gmsh::model::getNormal(face, parametric, normal);
    if (closest) *closest = {point[0], point[1], point[2]};
    return {normal[0], normal[1], normal[2]};
}

/**
 * @brief Decide whether a solid is a straight prism and find its sweep (the longest one when there are several).
 *
 * The test, all of which must hold: two planar faces with parallel planes and the same area, the volume equal
 * to area times the distance of the planes, and every other face parallel to the sweep direction (its normal
 * perpendicular to it at 27 sample points) and contained in the sweep of the base. The bounding box of the sweep
 * must coincide with that of the solid. This is a sampled test, not a proof, so the result is also checked by
 * the volume of the swept solid in the caller.
 * @param why Set to the reason when the solid is not accepted.
 */
bool analysePrism(int solid, PrismSweep& sweep, std::string& why) {
    std::vector<std::pair<int, int>> boundary;
    gmsh::model::getBoundary({{3, solid}}, boundary, false, false, false);
    struct FaceInfo { int tag; bool planar; double area; Vec3 normal; Vec3 point; };
    std::vector<FaceInfo> faces;
    for (const auto& entry : boundary) {
        FaceInfo face{};
        face.tag = std::abs(entry.second);
        std::string type;
        gmsh::model::getType(2, face.tag, type);
        face.planar = type == "Plane";
        double cx = 0, cy = 0, cz = 0;
        gmsh::model::occ::getCenterOfMass(2, face.tag, cx, cy, cz);
        gmsh::model::occ::getMass(2, face.tag, face.area);
        face.normal = faceNormalNear(face.tag, cx, cy, cz, &face.point);
        faces.push_back(face);
    }
    double volume = 0.0;
    gmsh::model::occ::getMass(3, solid, volume);
    double sx0, sy0, sz0, sx1, sy1, sz1;
    gmsh::model::getBoundingBox(3, solid, sx0, sy0, sz0, sx1, sy1, sz1);
    const double diagonal = std::sqrt((sx1 - sx0) * (sx1 - sx0) + (sy1 - sy0) * (sy1 - sy0) + (sz1 - sz0) * (sz1 - sz0));
    why = "no pair of congruent parallel planar faces whose area times distance equals the volume";
    bool found = false;   // a box can be swept in three ways: the longest sweep (smallest end face) is kept
    for (std::size_t i = 0; i < faces.size(); ++i) {
        if (!faces[i].planar) continue;
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
            if (!faces[j].planar) continue;
            if (std::abs(std::abs(dot3(faces[i].normal, faces[j].normal)) - 1.0) > 1e-9) continue;
            if (std::abs(faces[i].area - faces[j].area) > 1e-6 * faces[i].area) continue;
            const Vec3 offset = {faces[j].point[0] - faces[i].point[0], faces[j].point[1] - faces[i].point[1],
                                 faces[j].point[2] - faces[i].point[2]};
            const double along = dot3(offset, faces[i].normal);
            const double length = std::abs(along);
            if (length < 1e-9 * std::max(diagonal, 1.0)) continue;
            if (std::abs(volume - faces[i].area * length) > 1e-6 * volume) continue;
            Vec3 direction = faces[i].normal;
            if (along < 0.0) for (double& v : direction) v = -v;
            bool parallelWalls = true;
            for (const auto& wall : faces) {
                if (wall.tag == faces[i].tag || wall.tag == faces[j].tag) continue;
                double x0, y0, z0, x1, y1, z1;
                gmsh::model::getBoundingBox(2, wall.tag, x0, y0, z0, x1, y1, z1);
                for (int a = 0; a < 3 && parallelWalls; ++a)
                    for (int b = 0; b < 3 && parallelWalls; ++b)
                        for (int c = 0; c < 3 && parallelWalls; ++c) {
                            const Vec3 n = faceNormalNear(wall.tag, x0 + (x1 - x0) * a / 2.0, y0 + (y1 - y0) * b / 2.0,
                                                          z0 + (z1 - z0) * c / 2.0);
                            if (std::abs(dot3(n, direction)) > 1e-6) parallelWalls = false;
                        }
                if (!parallelWalls) break;
            }
            if (!parallelWalls) { why = "a side face is not parallel to the sweep direction"; continue; }
            // The sweep of the base face must fill the bounding box of the solid.
            double bx0, by0, bz0, bx1, by1, bz1;
            gmsh::model::getBoundingBox(2, faces[i].tag, bx0, by0, bz0, bx1, by1, bz1);
            const double lo[3] = {bx0, by0, bz0}, hi[3] = {bx1, by1, bz1};
            const double slo[3] = {sx0, sy0, sz0}, shi[3] = {sx1, sy1, sz1};
            bool same = true;
            for (int k = 0; k < 3; ++k) {
                const double shift = direction[static_cast<std::size_t>(k)] * length;
                const double a = std::min(lo[k], lo[k] + shift), b = std::max(hi[k], hi[k] + shift);
                if (std::abs(a - slo[k]) > 1e-6 * diagonal || std::abs(b - shi[k]) > 1e-6 * diagonal) same = false;
            }
            if (!same) { why = "the sweep of the end face does not fill the solid"; continue; }
            if (!found || length > sweep.length) sweep = {solid, faces[i].tag, direction, length};
            found = true;
        }
    }
    return found;
}

/**
 * @brief Replace every solid by the layered sweep of one of its end faces, when all of them are straight prisms.
 * @param layerSize Target layer thickness; each solid gets ceil(height / layerSize) layers.
 * @param recombine Recombine the end-face mesh into quadrilaterals (hexahedra) instead of leaving triangles (prisms).
 * @param reason Why the model could not be swept; the model is untouched in that case.
 * @return the number of solids swept, 0 on refusal.
 */
std::size_t sweepPrismaticSolids(double layerSize, bool recombine, std::string& reason) {
    std::vector<std::pair<int, int>> solids;
    gmsh::model::getEntities(solids, 3);
    if (solids.empty()) { reason = "the CAD model has no solid"; return 0; }
    std::vector<PrismSweep> sweeps;
    for (const auto& solid : solids) {
        PrismSweep sweep;
        std::string why;
        if (!analysePrism(solid.second, sweep, why)) {
            reason = "solid " + std::to_string(solid.second) + " is not a straight prism (" + why + ")";
            return 0;
        }
        sweeps.push_back(sweep);
    }
    for (const auto& sweep : sweeps) {
        std::vector<std::pair<int, int>> copied;
        gmsh::model::occ::copy({{2, sweep.baseFace}}, copied);
        gmsh::model::occ::remove({{3, sweep.solid}}, true);
        gmsh::model::occ::synchronize();
        const int layers = std::max(1, static_cast<int>(std::ceil(sweep.length / layerSize - 1e-9)));
        std::vector<std::pair<int, int>> extruded;
        // The layers are always "recombined": for a triangular base this is what makes them prisms.
        gmsh::model::occ::extrude(copied, sweep.direction[0] * sweep.length, sweep.direction[1] * sweep.length,
                                  sweep.direction[2] * sweep.length, extruded, {layers}, {1.0}, true);
        gmsh::model::occ::synchronize();
        if (recombine) gmsh::model::mesh::setRecombine(2, copied.front().second);
    }
    return sweeps.size();
}

/**
 * @brief Limit the mesh size at the corners of every thin CAD face to factor times its estimated width.
 * @return the number of faces whose size was limited.
 */
std::size_t limitSizeOfThinFaces(double factor, double maximumSize, double minimumSize, double& smallestSize) {
    std::vector<std::pair<int, int>> faces;
    gmsh::model::getEntities(faces, 2);
    std::size_t limited = 0;
    smallestSize = maximumSize;
    for (const auto& face : faces) {
        double area = 0.0, perimeter = 0.0;
        gmsh::model::occ::getMass(2, face.second, area);
        std::vector<std::pair<int, int>> edges;
        gmsh::model::getBoundary({face}, edges, false, false, false);
        for (const auto& edge : edges) {
            double length = 0.0;
            gmsh::model::occ::getMass(1, std::abs(edge.second), length);
            perimeter += length;
        }
        if (!(area > 0.0) || !(perimeter > 0.0)) continue;
        const double width = 2.0 * area / perimeter;   // width of a strip: area / (perimeter / 2)
        const double target = std::max(minimumSize, factor * width);
        if (target >= maximumSize) continue;
        std::vector<std::pair<int, int>> corners;
        gmsh::model::getBoundary({face}, corners, false, false, true);
        gmsh::model::mesh::setSize(corners, target);
        smallestSize = std::min(smallestSize, target);
        ++limited;
    }
    return limited;
}

/** Gmsh position of the node that OpenModel (Abaqus order) keeps at position 8..19 of a 20-node hexahedron. */
constexpr int kHexahedron20FromGmsh[12] = {8, 11, 13, 9, 16, 18, 19, 17, 10, 12, 14, 15};
/** Same for positions 6..14 of a 15-node prism. */
constexpr int kPrism15FromGmsh[9] = {6, 9, 7, 12, 14, 13, 8, 10, 11};
} // namespace

std::string GmshMesher::getName() const  {
 return "Gmsh Mesher"; 
}

const std::string& GmshMesher::lastError() const noexcept {
 return m_lastError; 
}

bool GmshMesher::buildMesh(const std::shared_ptr<CADModel>& cadModel, const MeshingOptions& options, MeshData& outMesh)  {

        m_lastError.clear();
        m_diagnostics.clear();
        m_faceMappingReport.clear();
        m_log.clear();
        m_trace = false;
        if (!cadModel) return fail("CAD model pointer is null");
        if (cadModel->getFilePath().empty()) return fail("CAD file path is empty");
        const double baseSize = options.maxElementSize > 0.0 ? options.maxElementSize : options.elementSize;
        if (!std::isfinite(options.elementSize) || options.elementSize <= 0.0)
            return fail("elementSize must be finite and greater than zero");
        if (!std::isfinite(baseSize) || baseSize <= 0.0)
            return fail("maxElementSize (or elementSize fallback) must be finite and positive");
        if (!std::isfinite(options.minElementSize) || options.minElementSize < 0.0 ||
            (options.minElementSize > 0.0 && options.minElementSize > baseSize))
            return fail("minElementSize must be zero or positive and no greater than maxElementSize");
        if (!std::isfinite(options.curvaturePointsPerCircle) || options.curvaturePointsPerCircle < 3.0)
            return fail("curvaturePointsPerCircle must be at least 3");
        if (options.order != 1 && options.order != 2)
            return fail("Only first- and second-order elements are supported");

        std::lock_guard<std::mutex> lock(gmshMutex());
        try {
            gmsh::initialize();
            GmshSession session;

            // Declared after the session so it is destroyed first: the log must be read before Gmsh is
            // finalised, and it is kept whether the run succeeds, fails or throws.
            struct LogCapture {
                bool on;
                std::vector<std::string>& lines;
                std::string file;
                ~LogCapture() {
                    if (!on) return;
                    try {
                        std::vector<std::string> captured;
                        gmsh::logger::get(captured);
                        gmsh::logger::stop();
                        lines = std::move(captured);
                        if (!file.empty()) {
                            std::ofstream out(file, std::ios::trunc);
                            for (const auto& line : lines) out << line << '\n';
                        }
                    } catch (...) { /* the log is best effort */ }
                }
            } logCapture{options.verboseLog, m_log, options.logFile};
            if (options.verboseLog) {
                gmsh::logger::start();
                m_trace = true;
            }
            const auto startTime = std::chrono::steady_clock::now();
            const auto elapsed = [&] {
                return std::to_string(std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count());
            };

            gmsh::option::setNumber("General.Terminal", options.verboseLog ? 1 : 0);
            if (options.verboseLog) gmsh::option::setNumber("General.Verbosity", 99);
            trace("Start: " + std::string(options.dimension == MeshDimension::Volume ? "volume" : "surface") +
                  " mesh of " + cadModel->getFilePath() + ", max size " + std::to_string(baseSize) + ", min size " +
                  std::to_string(options.minElementSize) + ", order " + std::to_string(options.order) +
                  ", curvature points per circle " +
                  std::to_string(options.curvatureAdaptation ? options.curvaturePointsPerCircle : 0.0));
            // A zero minimum means "let Gmsh adapt down to its backend default".
            // Setting the minimum equal to the maximum unintentionally disables
            // local refinement and curvature-driven sizing for the common case.
            gmsh::option::setNumber("Mesh.MeshSizeMin",
                options.minElementSize > 0.0 ? options.minElementSize : 0.0);
            gmsh::option::setNumber("Mesh.MeshSizeMax", baseSize);
            gmsh::option::setNumber("Mesh.MeshSizeFromPoints", 1.0);
            gmsh::option::setNumber("Mesh.MeshSizeFromCurvature",
                options.curvatureAdaptation ? options.curvaturePointsPerCircle : 0.0);
            gmsh::option::setNumber("Mesh.ElementOrder", options.order);
            gmsh::option::setNumber("Mesh.RecombineAll",
                options.recombineSurfaceElements ? 1.0 : 0.0);
            gmsh::option::setNumber("Mesh.SecondOrderIncomplete",
                options.order == 2 && options.incompleteSecondOrder ? 1.0 : 0.0);

            gmsh::model::add("OpenModelModel");
            std::vector<std::pair<int, int>> importedEntities;
            gmsh::model::occ::importShapes(cadModel->getFilePath(), importedEntities, true);
            gmsh::model::occ::synchronize();
            trace("CAD imported by Gmsh after " + elapsed() + " s: " + std::to_string(importedEntities.size()) + " top-level entities");

            const int meshDimension = (options.dimension == MeshDimension::Volume && options.generateTetrahedra) ? 3 : 2;

            if (options.interfaceMode == InterfaceMode::Conformal) {
                std::vector<std::pair<int, int>> solids;
                gmsh::model::getEntities(solids, 3);
                if (meshDimension != 3) {
                    m_diagnostics.push_back("Conformal interfaces apply to volume meshes only; ignored for a surface mesh.");
                } else if (solids.size() < 2) {
                    m_diagnostics.push_back("Conformal interfaces requested, but the CAD contains a single solid: nothing to fragment.");
                } else {
                    // Fragment makes touching/overlapping solids share their common faces, so the surface mesh of
                    // an interface is generated once and both sides use the same nodes.
                    std::vector<std::pair<int, int>> objects(solids.begin(), solids.begin() + 1);
                    std::vector<std::pair<int, int>> tools(solids.begin() + 1, solids.end());
                    std::vector<std::pair<int, int>> fragments;
                    std::vector<std::vector<std::pair<int, int>>> fragmentMap;
                    gmsh::model::occ::fragment(objects, tools, fragments, fragmentMap);
                    gmsh::model::occ::synchronize();
                    std::vector<std::pair<int, int>> after;
                    gmsh::model::getEntities(after, 3);
                    m_diagnostics.push_back("Conformal interfaces: " + std::to_string(solids.size()) + " solids fragmented into " +
                                            std::to_string(after.size()) + " volumes.");
                }
            }
            // Structured volume mesh: sweep the end face of every straight prismatic solid.
            bool structuredApplied = false;
            if (options.structure == MeshStructure::Structured) {
                std::string reason;
                std::vector<std::pair<int, int>> currentSolids;
                gmsh::model::getEntities(currentSolids, 3);
                if (meshDimension != 3) {
                    m_diagnostics.push_back("A structured mesh applies to volume meshes only; ignored for a surface mesh.");
                } else if (options.interfaceMode == InterfaceMode::Conformal && currentSolids.size() > 1) {
                    reason = "conformal interfaces between several solids cannot be combined with the sweep of each solid";
                } else {
                    const bool hexahedra = options.structuredShape == StructuredElementShape::Hexahedron;
                    const std::size_t swept = sweepPrismaticSolids(baseSize, hexahedra, reason);
                    structuredApplied = swept > 0;
                    if (structuredApplied)
                        m_diagnostics.push_back("Structured mesh: " + std::to_string(swept) + " solid(s) swept in layers of about " +
                                                std::to_string(baseSize) + " (" + (hexahedra ? "hexahedra" : "prisms") + ").");
                }
                if (!structuredApplied && meshDimension == 3) {
                    if (!options.structuredFallback)
                        return fail("A structured mesh is not possible: " + reason + ".");
                    m_diagnostics.push_back("A structured mesh is not possible (" + reason +
                                            "): the unstructured tetrahedral mesh was generated instead.");
                }
                trace(std::string("Structured sweep ") + (structuredApplied ? "applied" : "not applied"));
            }
            // Second-order hexahedra and prisms use the incomplete 20- and 15-node forms; pyramids and the
            // 27-node hexahedron have no counterpart in the solver decks.
            if (options.order == 2 && structuredApplied)
                gmsh::option::setNumber("Mesh.SecondOrderIncomplete", 1.0);
            if (options.thinFaceSizeFactor > 0.0) {
                if (!std::isfinite(options.thinFaceSizeFactor))
                    return fail("thinFaceSizeFactor must be finite");
                double smallest = baseSize;
                const std::size_t limited = limitSizeOfThinFaces(options.thinFaceSizeFactor, baseSize,
                                                                 options.minElementSize, smallest);
                m_diagnostics.push_back("Thin-face sizing (factor " + std::to_string(options.thinFaceSizeFactor) + "): " +
                                        std::to_string(limited) + " face(s) limited, smallest size " + std::to_string(smallest) + ".");
                trace("Thin-face sizing limited " + std::to_string(limited) + " faces");
            } else if (options.thinFaceSizeFactor < 0.0) {
                return fail("thinFaceSizeFactor must be zero or positive");
            }
            std::vector<std::pair<int, int>> entities;
            gmsh::model::getEntities(entities, meshDimension);
            if (entities.empty()) {
                return fail(meshDimension == 3
                    ? "CAD file contains no 3D volume entities"
                    : "CAD file contains no 2D surface entities");
            }

            try {
                trace("Meshing in " + std::to_string(meshDimension) + "D (" + std::to_string(entities.size()) + " entities of that dimension) at " + elapsed() + " s");
                gmsh::model::mesh::generate(meshDimension);
                trace("Gmsh meshing finished at " + elapsed() + " s");
            } catch (const std::exception& e) {
                std::string message = std::string(meshDimension == 3 ? "Gmsh 3D meshing" : "Gmsh 2D meshing") +
                                      " failed: " + e.what();
                if (meshDimension == 3) {
                    // The boundary mesh is built first; a "PLC Error" or an HXT failure means that it
                    // self-intersects, which happens when it is too coarse for thin or strongly curved
                    // parts (observed on a turbine blade at element sizes of 5 and 8 but not at 3).
                    message += ". The surface mesh is probably too coarse for the thin or curved features "
                               "of the CAD model: reduce the element size or increase the curvature adaptation.";
                }
                return fail(message);
            }

            MeshData candidate;
            std::vector<std::size_t> nodeTags;
            std::vector<double> nodeCoordinates;
            std::vector<double> parametricCoordinates;
            gmsh::model::mesh::getNodes(nodeTags, nodeCoordinates, parametricCoordinates);
            if (nodeCoordinates.size() != nodeTags.size() * 3)
                throw std::runtime_error("Gmsh returned an inconsistent node-coordinate array");
            if (nodeTags.empty())
                throw std::runtime_error("Gmsh generated no nodes");

            std::unordered_map<std::size_t, NodeIndex> nodeTagToIndex;
            nodeTagToIndex.reserve(nodeTags.size());
            for (std::size_t i = 0; i < nodeTags.size(); ++i) {
                const auto inserted = nodeTagToIndex.emplace(
                    nodeTags[i], candidate.addNode(static_cast<NodeId>(nodeTags[i]),
                        nodeCoordinates[3 * i], nodeCoordinates[3 * i + 1], nodeCoordinates[3 * i + 2]));
                if (!inserted.second)
                    throw std::runtime_error("Gmsh returned a duplicate node tag");
            }

            // Extract every mesh dimension up to the requested top dimension.
            // This is required to retain boundary elements and physical groups.
            std::map<std::pair<int, int>, std::vector<ElementId>> elementIdsByEntity;
            std::size_t extractedTopElements = 0;
            std::size_t extractedElements = 0;
            // Keep top-dimensional elements and their codimension-one boundary
            // elements. 1-D curve elements of a volume mesh are extracted only
            // when the CAD model carries edge groups that need them. Point
            // elements are never extracted: vertex groups are resolved on
            // nodes, and Point1 blocks cannot be exported to solver decks.
            bool needsCurveElements = false;
            if (options.importCadGroups && meshDimension == 3)
                for (const auto& group : cadModel->getGroups())
                    needsCurveElements = needsCurveElements || group.dimension == 1;
            const int minimumExtractedDimension = needsCurveElements ? 1 : meshDimension - 1;
            for (int dim = minimumExtractedDimension; dim <= meshDimension; ++dim) {
                std::vector<std::pair<int, int>> dimensionEntities;
                gmsh::model::getEntities(dimensionEntities, dim);
                for (const auto& entity : dimensionEntities) {
                    std::vector<int> elementTypes;
                    std::vector<std::vector<std::size_t>> elementTags;
                    std::vector<std::vector<std::size_t>> elementNodeTags;
                    gmsh::model::mesh::getElements(elementTypes, elementTags, elementNodeTags,
                                                   dim, entity.second);
                    for (std::size_t t = 0; t < elementTypes.size(); ++t) {
                        const auto& tags = elementTags[t];
                        const auto& nodes = elementNodeTags[t];
                        if (tags.empty()) continue;
                        if (nodes.size() % tags.size() != 0)
                            throw std::runtime_error("Gmsh element connectivity has an inconsistent length");
                        const std::size_t nodesPerElement = nodes.size() / tags.size();
                        const ElementType type = mapElementType(elementTypes[t]);
                        if (type == ElementType::Unknown || elementNodeCount(type) != nodesPerElement) {
                            throw std::runtime_error("Unsupported Gmsh element type " +
                                                     std::to_string(elementTypes[t]) + " (" +
                                                     std::to_string(nodesPerElement) + " nodes) in dimension " +
                                                     std::to_string(dim));
                        }
                        const char* prefix = dim == 3 ? "Volume_" : dim == 2 ? "Surface_" :
                                             dim == 1 ? "Curve_" : "Point_";
                        const std::string blockName = std::string(prefix) +
                            std::to_string(entity.second) + "_type_" + std::to_string(elementTypes[t]);
                        auto& entityIds = elementIdsByEntity[{dim, entity.second}];
                        for (std::size_t e = 0; e < tags.size(); ++e) {
                            std::vector<NodeIndex> connectivity;
                            connectivity.reserve(nodesPerElement);
                            for (std::size_t n = 0; n < nodesPerElement; ++n) {
                                // Gmsh lists the last two mid-edge nodes of a 10-node tetrahedron on the
                                // edges 3-2 and 3-1; the Abaqus/OpenModel order is 1-3 and 2-3, so they
                                // are swapped. The mid-edge nodes of 20-node hexahedra and 15-node prisms
                                // follow other edge orders in Gmsh and are permuted too. The corner nodes
                                // and the other supported types already share the order.
                                std::size_t source = n;
                                if (type == ElementType::Tetrahedron10 && n >= 8) source = 17 - n;
                                else if (type == ElementType::Hexahedron20 && n >= 8)
                                    source = static_cast<std::size_t>(kHexahedron20FromGmsh[n - 8]);
                                else if (type == ElementType::Prism15 && n >= 6)
                                    source = static_cast<std::size_t>(kPrism15FromGmsh[n - 6]);
                                const auto found = nodeTagToIndex.find(nodes[e * nodesPerElement + source]);
                                if (found == nodeTagToIndex.end())
                                    throw std::runtime_error("Element refers to an unknown Gmsh node tag");
                                connectivity.push_back(found->second);
                            }
                            const auto elementId = static_cast<ElementId>(tags[e]);
                            candidate.addElement(blockName, type, elementId, connectivity);
                            entityIds.push_back(elementId);
                            ++extractedElements;
                            if (dim == meshDimension) ++extractedTopElements;
                        }
                    }
                }
            }
            if (extractedTopElements == 0)
                throw std::runtime_error("Gmsh generated no elements in the requested dimension");
            if (extractedElements == 0)
                throw std::runtime_error("Gmsh generated no elements in the requested dimensions");

            // Physical groups are dimension-scoped in Gmsh. MeshData requires
            // globally unique names, so disambiguate only when source names collide.
            std::set<std::string> usedPhysicalNames;
            std::vector<std::pair<int, int>> physicalGroups;
            gmsh::model::getPhysicalGroups(physicalGroups);
            for (const auto& physical : physicalGroups) {
                const int dim = physical.first;
                const int tag = physical.second;
                if (dim < minimumExtractedDimension || dim > meshDimension) continue;
                std::string name;
                gmsh::model::getPhysicalName(dim, tag, name);
                if (name.empty()) name = "PhysicalGroup_" + std::to_string(dim) + "_" + std::to_string(tag);
                if (!usedPhysicalNames.insert(name).second) {
                    name += "_dim" + std::to_string(dim) + "_tag" + std::to_string(tag);
                    if (!usedPhysicalNames.insert(name).second)
                        throw std::runtime_error("Could not create a unique name for Gmsh physical group");
                }
                std::vector<int> groupEntities;
                gmsh::model::getEntitiesForPhysicalGroup(dim, tag, groupEntities);
                std::vector<ElementId> groupElementIds;
                for (const int entityTag : groupEntities) {
                    const auto found = elementIdsByEntity.find({dim, entityTag});
                    if (found != elementIdsByEntity.end()) {
                        groupElementIds.insert(groupElementIds.end(),
                                               found->second.begin(), found->second.end());
                    }
                }
                candidate.addPhysicalGroup(name, dim, tag, groupElementIds);
            }

            // User-defined face groups are an explicit request and must not
            // depend on importCadGroups, which only governs the groups stored
            // in the CAD source (components, layers, named shapes).
            if (structuredApplied) {
                std::size_t hexahedra = 0, prisms = 0, others = 0;
                for (const auto& block : candidate.getElementBlocks()) {
                    if (elementDimension(block.type) != 3) continue;
                    if (block.type == ElementType::Hexahedron8 || block.type == ElementType::Hexahedron20) hexahedra += block.size();
                    else if (block.type == ElementType::Prism6 || block.type == ElementType::Prism15) prisms += block.size();
                    else others += block.size();
                }
                m_diagnostics.push_back("Structured mesh result: " + std::to_string(hexahedra) + " hexahedra, " +
                                        std::to_string(prisms) + " prisms" +
                                        (others ? ", " + std::to_string(others) + " other volume elements" : std::string()) + ".");
            }
            trace("Mesh extracted: " + std::to_string(candidate.getNumNodes()) + " nodes, " +
                  std::to_string(candidate.getNumElements()) + " elements at " + elapsed() + " s");
            if (options.importCadGroups || !options.additionalFaceGroups.empty())
                mapCadGroups(*cadModel, candidate, nodeTagToIndex, elementIdsByEntity, usedPhysicalNames,
                             options.additionalFaceGroups, options.importCadGroups, options.auditAllCadFaces);

            trace("CAD group mapping finished at " + elapsed() + " s");
            const auto structuralErrors = candidate.validate();
            if (!structuralErrors.empty())
                throw std::runtime_error("Extracted mesh failed structural validation: " + structuralErrors.front());

            // Keep the caller's mesh intact unless a complete, structurally valid result exists.
            outMesh = std::move(candidate);
            return true;
        } catch (const std::exception& e) {
            return fail(e.what());
        } catch (...) {
            return fail("Unknown non-standard exception raised by Gmsh");
        }
    
}

void GmshMesher::trace(const std::string& message) const {
    if (!m_trace) return;
    try { gmsh::logger::write("[OpenModel] " + message, "info"); } catch (...) { /* tracing never fails the run */ }
}

GmshMesher::GmshSession::~GmshSession() noexcept {

            try { gmsh::finalize(); } catch (...) { /* no exceptions from a destructor */ }
        
}

GmshLibraryInfo GmshMesher::libraryInfo() {
    GmshLibraryInfo info;
#ifdef GMSH_API_VERSION
    info.version = GMSH_API_VERSION;
#endif
    std::lock_guard<std::mutex> lock(gmshMutex());
    try {
        gmsh::initialize();
        GmshSession session; // finalises Gmsh when it goes out of scope
        std::string value;
        gmsh::option::getString("General.Version", value);
        if (!value.empty()) info.version = value;
        value.clear();
        gmsh::option::getString("General.BuildInfo", value);
        info.buildInfo = value;
    } catch (...) {
        // Not being able to describe the library must never be an error: keep what was read so far.
    }
    return info;
}

std::mutex& GmshMesher::gmshMutex() {

        static std::mutex mutex;
        return mutex;
    
}

bool GmshMesher::fail(const std::string& message) {

        m_lastError = message;
        return false;
    
}

ElementType GmshMesher::mapElementType(int gmshType) noexcept {

        switch (gmshType) {
        case 15: return ElementType::Point1;
        case 1: return ElementType::Line2;
        case 8: return ElementType::Line3;
        case 2: return ElementType::Triangle3;
        case 9: return ElementType::Triangle6;
        case 3: return ElementType::Quadrilateral4;
        case 16: return ElementType::Quadrilateral8;
        case 10: return ElementType::Quadrilateral9;
        case 4: return ElementType::Tetrahedron4;
        case 11: return ElementType::Tetrahedron10;
        case 5: return ElementType::Hexahedron8;
        case 17: return ElementType::Hexahedron20;
        case 12: return ElementType::Hexahedron27;
        case 6: return ElementType::Prism6;
        case 18: return ElementType::Prism15;
        case 7: return ElementType::Pyramid5;
        default: return ElementType::Unknown;
        }
    
}

std::set<int> GmshMesher::solidsByMajority(const std::vector<std::vector<int>>& hitsPerSample, bool& agree) {
    agree = true;
    std::map<int, std::size_t> votes;
    std::set<int> first;
    for (std::size_t k = 0; k < hitsPerSample.size(); ++k) {
        const std::set<int> hits(hitsPerSample[k].begin(), hitsPerSample[k].end());
        if (k == 0) first = hits; else if (hits != first) agree = false;
        for (const int solid : hits) ++votes[solid];
    }
    std::size_t best = 0;
    for (const auto& vote : votes) best = std::max(best, vote.second);
    std::set<int> result;
    for (const auto& vote : votes) if (vote.second == best && best > 0) result.insert(vote.first);
    return result;
}

void GmshMesher::mapCadGroups(const CADModel& cad, MeshData& mesh,
                              const std::unordered_map<std::size_t, NodeIndex>& nodeTagToIndex,
                              const std::map<std::pair<int, int>, std::vector<ElementId>>& elementIdsByEntity,
                              std::set<std::string>& usedPhysicalNames,
                              const std::vector<CADGroup>& additionalFaceGroups,
                              bool importNativeGroups, bool auditAllFaces) {
    trace("CAD group mapping started (solid ownership of volumes first, then every meshed surface)");
    // Components are always needed to attribute faces to solids; the groups stored in
    // the CAD source are transferred only when requested.
    static const std::vector<CADGroup> noGroups;
    const auto& components = cad.getComponents();
    const auto& cadGroups = importNativeGroups ? cad.getGroups() : noGroups;
    if (components.empty() && cadGroups.empty() && additionalFaceGroups.empty()) {
        m_diagnostics.push_back("The CAD model carries no assembly structure or groups: no CAD groups were created.");
        return;
    }

    double xmin, ymin, zmin, xmax, ymax, zmax;
    gmsh::model::getBoundingBox(-1, -1, xmin, ymin, zmin, xmax, ymax, zmax);
    const double diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) + (zmax - zmin) * (zmax - zmin));
    const double tolerance = std::max(1e-6 * diagonal, 1e-9);

    auto coordinates = [&](std::size_t nodeTag) {
        return mesh.nodeCoordinates(nodeTagToIndex.at(nodeTag));
    };

    // ---- Volumes -> CAD solids -------------------------------------------------------------
    std::vector<std::pair<int, int>> volumes;
    gmsh::model::getEntities(volumes, 3);
    std::map<int, std::set<int>> solidsOfVolume;
    std::size_t unmatchedVolumes = 0, ambiguousVolumes = 0, overlapVolumes = 0;
    for (const auto& volume : volumes) {
        std::vector<int> types;
        std::vector<std::vector<std::size_t>> tags, nodeTags;
        gmsh::model::mesh::getElements(types, tags, nodeTags, 3, volume.second);
        if (types.empty() || tags[0].empty()) continue;
        std::string typeName;
        int dim = 0, order = 0, numNodes = 0, numPrimary = 0;
        std::vector<double> localCoordinates;
        gmsh::model::mesh::getElementProperties(types[0], typeName, dim, order, numNodes, localCoordinates, numPrimary);
        const std::size_t count = tags[0].size();
        // Up to 32 evenly spaced elements: the solids found at them are voted on (see solidsByMajority).
        const std::size_t sampleCount = std::min<std::size_t>(count, 32);
        std::vector<std::vector<int>> hitsPerSample;
        hitsPerSample.reserve(sampleCount);
        for (std::size_t k = 0; k < sampleCount; ++k) {
            const std::size_t sample = sampleCount == 1 ? 0 : k * (count - 1) / (sampleCount - 1);
            std::array<double, 3> centroid{{0, 0, 0}};
            for (int n = 0; n < numPrimary; ++n) {
                const auto c = coordinates(nodeTags[0][sample * static_cast<std::size_t>(numNodes) + static_cast<std::size_t>(n)]);
                for (int j = 0; j < 3; ++j) centroid[static_cast<std::size_t>(j)] += c[static_cast<std::size_t>(j)] / numPrimary;
            }
            hitsPerSample.push_back(cad.solidsContainingPoint(centroid, tolerance));
        }
        bool agree = true;
        const std::set<int> common = solidsByMajority(hitsPerSample, agree);
        if (common.empty()) { ++unmatchedVolumes; continue; }
        if (!agree) ++ambiguousVolumes;
        if (common.size() > 1) ++overlapVolumes;
        solidsOfVolume[volume.second] = common;
    }
    if (unmatchedVolumes)
        m_diagnostics.push_back(std::to_string(unmatchedVolumes) + " Gmsh volume(s) could not be matched to any CAD solid (left out of the CAD groups).");
    if (ambiguousVolumes)
        m_diagnostics.push_back(std::to_string(ambiguousVolumes) + " Gmsh volume(s) gave inconsistent solid matches across sample points; the solid(s) found at most samples were used.");
    if (overlapVolumes)
        m_diagnostics.push_back(std::to_string(overlapVolumes) + " Gmsh volume(s) lie inside more than one CAD solid (overlapping solids): they belong to several groups.");

    auto nextTag = [&](int dimension) {
        int tag = 0;
        for (const auto& group : mesh.getPhysicalGroups())
            if (group.dimension == dimension) tag = std::max(tag, group.tag);
        return tag + 1;
    };
    auto uniqueName = [&](std::string name, int dimension) {
        if (name.empty()) name = "CAD_group";
        if (usedPhysicalNames.insert(name).second) return name;
        name += "_dim" + std::to_string(dimension);
        for (int n = 2; !usedPhysicalNames.insert(name).second; ++n) name += "_" + std::to_string(n);
        return name;
    };
    auto collect = [&](int dimension, const std::set<int>& entityTags) {
        std::vector<ElementId> ids;
        for (const int entityTag : entityTags) {
            const auto found = elementIdsByEntity.find({dimension, entityTag});
            if (found != elementIdsByEntity.end()) ids.insert(ids.end(), found->second.begin(), found->second.end());
        }
        return ids;
    };

    // Resolve element membership to node membership without making node tags
    // the canonical CAD mapping: a node may belong to several face groups.
    auto collectNodes = [&](const std::vector<ElementId>& elementIds) {
        const std::unordered_set<ElementId> wanted(elementIds.begin(), elementIds.end());
        std::unordered_set<NodeIndex> seen;
        std::vector<NodeIndex> nodes;
        for (const auto& block : mesh.getElementBlocks()) {
            for (std::size_t e = 0; e < block.ids.size(); ++e) {
                if (!wanted.count(block.ids[e])) continue;
                const auto begin = e * block.nodesPerElement;
                for (std::size_t n = 0; n < block.nodesPerElement; ++n) {
                    const auto index = block.connectivity[begin + n];
                    if (seen.insert(index).second) nodes.push_back(index);
                }
            }
        }
        return nodes;
    };

    // Component groups: leaf components only. Sub-assemblies stay implicit in the "A/B/C" path of the name,
    // so groups of one dimension do not overlap each other.
    std::size_t componentGroups = 0;
    for (const auto& component : components) {
        if (!importNativeGroups || component.isAssembly || component.solidIds.empty()) continue;
        const std::set<int> solids(component.solidIds.begin(), component.solidIds.end());
        std::set<int> volumeTags;
        for (const auto& [volumeTag, volumeSolids] : solidsOfVolume)
            for (const int s : volumeSolids)
                if (solids.count(s)) { volumeTags.insert(volumeTag); break; }
        const auto ids = collect(3, volumeTags);
        if (ids.empty()) continue;
        const auto groupName = uniqueName(component.path, 3);
        mesh.addPhysicalGroup(groupName, 3, nextTag(3), ids);
        mesh.addMeshGroup(MeshGroup{groupName, 3, MeshGroupSource::CAD,
                                    std::vector<int>(solids.begin(), solids.end()), ids, collectNodes(ids)});
        ++componentGroups;
    }

    // Layers / named shapes made of solids.
    std::size_t solidGroups = 0, faceGroups = 0;
    for (const auto& group : cadGroups) {
        if (group.dimension != 3) continue;
        const std::set<int> solids(group.ids.begin(), group.ids.end());
        std::set<int> volumeTags;
        for (const auto& [volumeTag, volumeSolids] : solidsOfVolume)
            for (const int s : volumeSolids)
                if (solids.count(s)) { volumeTags.insert(volumeTag); break; }
        const auto ids = collect(3, volumeTags);
        if (ids.empty()) continue;
        const auto groupName = uniqueName(group.name, 3);
        mesh.addPhysicalGroup(groupName, 3, nextTag(3), ids);
        mesh.addMeshGroup(MeshGroup{groupName, 3, MeshGroupSource::CAD,
                                    group.ids, ids, collectNodes(ids)});
        ++solidGroups;
    }

    // ---- Surfaces -> CAD faces -------------------------------------------------------------------
    bool anyFaceGroup = !additionalFaceGroups.empty();
    for (const auto& group : cadGroups) anyFaceGroup = anyFaceGroup || group.dimension == 2;
    // Audit all imported CAD faces even when the file has no named face groups.
    if (auditAllFaces)
        for (const auto& component : components)
            anyFaceGroup = anyFaceGroup || !component.faceIds.empty();
    if (anyFaceGroup) {
        // Which leaf components own each CAD face, to tell apart coincident faces of different solids.
        std::map<int, std::vector<const CADComponent*>> ownersOfFace;
        for (const auto& component : components)
            if (!component.isAssembly)
                for (const int f : component.faceIds) ownersOfFace[f].push_back(&component);

        std::vector<std::pair<int, int>> surfaces;
        gmsh::model::getEntities(surfaces, 2);
        std::map<int, std::set<int>> facesOfSurface;
        std::size_t surfaceNumber = 0;
        for (const auto& surface : surfaces) {
            if (m_trace && (surfaceNumber % 25 == 0))
                trace("Mapping CAD faces: surface " + std::to_string(surfaceNumber + 1) + " of " + std::to_string(surfaces.size()));
            ++surfaceNumber;
            std::vector<int> upward, downward;
            gmsh::model::getAdjacencies(2, surface.second, upward, downward);
            std::set<int> adjacentSolids;
            for (const int v : upward) {
                const auto found = solidsOfVolume.find(v);
                if (found != solidsOfVolume.end()) adjacentSolids.insert(found->second.begin(), found->second.end());
            }

            // Probe element interiors, not only boundary nodes. Boundary nodes are
            // shared by adjacent CAD faces and can make a perfectly valid face
            // appear ambiguous. Projecting element centroids back onto the Gmsh
            // surface gives points on the exact OCC surface (also for curved faces).
            std::vector<int> types;
            std::vector<std::vector<std::size_t>> elementTags, elementNodeTags;
            gmsh::model::mesh::getElements(types, elementTags, elementNodeTags, 2, surface.second);
            std::size_t totalElements = 0;
            for (const auto& tags : elementTags) totalElements += tags.size();
            CADFaceMappingRecord mapping;
            mapping.gmshSurfaceTag = surface.second;
            mapping.elementCount = totalElements;
            if (totalElements == 0) {
                m_diagnostics.push_back("Gmsh surface " + std::to_string(surface.second) +
                    " has no 2-D mesh elements and cannot be mapped to a CAD face.");
                m_faceMappingReport.push_back(std::move(mapping));
                continue;
            }

            // Validate every exported 2-D element. The previous bounded
            // sampling strategy could miss a small unmapped region on a large
            // surface, which is unacceptable for FEM boundary-condition sets.
            //
            // One representative point per element, on the exact surface and inside the element's
            // footprint: the centroid of the corner nodes' (u, v) coordinates, evaluated on the
            // surface. Gmsh reparametrizes the nodes of the surface boundary on it, so the corner
            // (u, v) come from one call per surface. This replaces projecting the 3-D centroid onto
            // the untrimmed surface and testing the trim: that projection slips across the boundary
            // of faces that are small next to the element size (small cones of a gearbox sample) and
            // cost most of the audit time. The old projection remains as the fallback for elements
            // whose (u, v) average is not plausible, e.g. across a periodic seam.
            std::unordered_map<std::size_t, std::array<double, 2>> nodeUv;
            {
                std::vector<std::size_t> surfaceNodeTags;
                std::vector<double> surfaceCoordinates, surfaceParametric;
                gmsh::model::mesh::getNodes(surfaceNodeTags, surfaceCoordinates, surfaceParametric,
                                            2, surface.second, true, true);
                if (surfaceParametric.size() == 2 * surfaceNodeTags.size()) {
                    nodeUv.reserve(surfaceNodeTags.size());
                    for (std::size_t i = 0; i < surfaceNodeTags.size(); ++i)
                        nodeUv.emplace(surfaceNodeTags[i], std::array<double, 2>{{surfaceParametric[2 * i], surfaceParametric[2 * i + 1]}});
                }
            }
            struct Candidate {
                std::array<double, 3> centroid{{0.0, 0.0, 0.0}};
                double longestEdge = 0.0;
                bool hasUv = false;
            };
            std::vector<Candidate> candidatesOfSurface;
            std::vector<double> uvOfCandidates;     // two values per candidate with hasUv
            candidatesOfSurface.reserve(totalElements);
            for (std::size_t t = 0; t < types.size(); ++t) {
                if (elementTags[t].empty()) continue;
                std::string typeName;
                int dim = 0, order = 0, numNodes = 0, numPrimary = 0;
                std::vector<double> localCoordinates;
                gmsh::model::mesh::getElementProperties(types[t], typeName, dim, order,
                                                        numNodes, localCoordinates, numPrimary);
                if (dim != 2 || numPrimary <= 0 || numNodes <= 0 ||
                    elementNodeTags[t].size() != elementTags[t].size() * static_cast<std::size_t>(numNodes))
                    throw std::runtime_error("Invalid Gmsh surface element connectivity during CAD face audit");
                for (std::size_t e = 0; e < elementTags[t].size(); ++e) {
                    Candidate candidate;
                    std::vector<std::array<double, 3>> corners;
                    corners.reserve(static_cast<std::size_t>(numPrimary));
                    double u = 0.0, v = 0.0;
                    bool hasUv = true;
                    for (int n = 0; n < numPrimary; ++n) {
                        const std::size_t tag = elementNodeTags[t][e * static_cast<std::size_t>(numNodes) + static_cast<std::size_t>(n)];
                        const auto point = coordinates(tag);
                        corners.push_back(point);
                        for (std::size_t axis = 0; axis < 3; ++axis)
                            candidate.centroid[axis] += point[axis] / static_cast<double>(numPrimary);
                        const auto uv = nodeUv.find(tag);
                        if (uv == nodeUv.end()) { hasUv = false; continue; }
                        u += uv->second[0] / static_cast<double>(numPrimary);
                        v += uv->second[1] / static_cast<double>(numPrimary);
                    }
                    for (std::size_t c = 0; c < corners.size(); ++c) {
                        const auto& p = corners[c];
                        const auto& q = corners[(c + 1) % corners.size()];
                        candidate.longestEdge = std::max(candidate.longestEdge,
                            std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2])));
                    }
                    candidate.hasUv = hasUv;
                    if (hasUv) { uvOfCandidates.push_back(u); uvOfCandidates.push_back(v); }
                    candidatesOfSurface.push_back(candidate);
                }
            }
            std::vector<double> evaluated;
            if (!uvOfCandidates.empty()) gmsh::model::getValue(2, surface.second, uvOfCandidates, evaluated);

            std::vector<std::array<double, 3>> probes;
            probes.reserve(totalElements);
            std::vector<std::array<double, 2>> probeUv;   // (u, v) of each probe on the surface; NaN when unknown
            probeUv.reserve(totalElements);
            const double unknownUv = std::numeric_limits<double>::quiet_NaN();
            std::size_t uvIndex = 0;
            for (const auto& candidate : candidatesOfSurface) {
                if (candidate.hasUv) {
                    const std::size_t at = 3 * uvIndex++;
                    if (at + 2 < evaluated.size()) {
                        const double dx = evaluated[at] - candidate.centroid[0];
                        const double dy = evaluated[at + 1] - candidate.centroid[1];
                        const double dz = evaluated[at + 2] - candidate.centroid[2];
                        // The point must stay near the flat element (chord sag); an (u, v) average taken
                        // across a periodic seam or at a cone apex lands far from it.
                        if (std::sqrt(dx * dx + dy * dy + dz * dz) <= candidate.longestEdge + tolerance) {
                            probes.push_back({{evaluated[at], evaluated[at + 1], evaluated[at + 2]}});
                            probeUv.push_back({{uvOfCandidates[2 * (uvIndex - 1)], uvOfCandidates[2 * (uvIndex - 1) + 1]}});
                            continue;
                        }
                    }
                }
                // Fallback: project the 3-D centroid and require the projection to lie inside the face.
                std::vector<double> closest, ignoredParametric, parametric;
                gmsh::model::getClosestPoint(2, surface.second,
                    {candidate.centroid[0], candidate.centroid[1], candidate.centroid[2]}, closest, ignoredParametric);
                // The parametric output of getClosestPoint is not reliable (it returned (0,0) for every
                // point on the OCC planes and cylinders tested), so the UV is recomputed from the
                // projected 3-D point.
                if (closest.size() == 3) {
                    gmsh::model::getParametrization(2, surface.second, closest, parametric);
                    if (parametric.size() == 2 &&
                        gmsh::model::isInside(2, surface.second, parametric, true)) {
                        probes.push_back({{closest[0], closest[1], closest[2]}});
                        probeUv.push_back({{unknownUv, unknownUv}});
                        continue;
                    }
                }
                ++mapping.projectionFailureCount;
            }
            mapping.sampledElementCount = totalElements;
            if (mapping.projectionFailureCount != 0) {
                m_diagnostics.push_back("Gmsh surface " + std::to_string(surface.second) +
                    " failed to project " + std::to_string(mapping.projectionFailureCount) +
                    " of " + std::to_string(totalElements) +
                    " element representative point(s) onto its trimmed surface.");
            }
            if (probes.empty()) {
                m_diagnostics.push_back("Gmsh surface " + std::to_string(surface.second) +
                    " produced no valid interior probes; CAD face mapping skipped.");
                m_faceMappingReport.push_back(std::move(mapping));
                continue;
            }

            // Score each CAD face by the number of per-element interior
            // representatives that lie on it. Projection failures remain in
            // the denominator, so they cannot silently inflate the match ratio.
            // Candidates must explain at least 80% of all exported elements.
            // The 80% rule makes most of the work unnecessary: a face holding fewer than `requiredVotes`
            // of the N probes cannot win, and a face hit by none of the first N - requiredVotes + 1 probes
            // holds at most requiredVotes - 1. So only that prefix is searched against every face; the rest
            // of the probes are checked against the faces the prefix found, using the (u, v) the probe was
            // built from. The votes of those faces are exact, and so is the best vote whenever a face
            // qualifies. A surface where nothing qualifies is searched in full, to keep its diagnostics.
            const std::size_t requiredVotes =
                std::max<std::size_t>(1, (totalElements * 4 + 4) / 5);
            std::map<int, std::size_t> faceVotes;
            const auto countAll = [&](std::size_t first, std::size_t last) {
                for (std::size_t k = first; k < last; ++k)
                    for (const int faceId : cad.facesContainingPoint(probes[k], tolerance)) ++faceVotes[faceId];
            };
            bool qualified = false;
            if (probes.size() >= requiredVotes) {
                const std::size_t prefix = probes.size() - requiredVotes + 1;
                countAll(0, prefix);
                std::vector<int> faceCandidates;
                for (const auto& vote : faceVotes) faceCandidates.push_back(vote.first);
                for (std::size_t k = prefix; k < probes.size(); ++k) {
                    const bool hasUv = std::isfinite(probeUv[k][0]) && std::isfinite(probeUv[k][1]);
                    for (const int faceId : faceCandidates)
                        if (cad.faceContainsPoint(faceId, probes[k], tolerance, hasUv ? &probeUv[k] : nullptr))
                            ++faceVotes[faceId];
                }
                for (const auto& vote : faceVotes) if (vote.second >= requiredVotes) qualified = true;
            }
            if (!qualified) {
                faceVotes.clear();
                countAll(0, probes.size());
            }
            for (const auto& vote : faceVotes)
                mapping.matchedSampleCount = std::max(mapping.matchedSampleCount, vote.second);
            mapping.matchRatio = static_cast<double>(mapping.matchedSampleCount) /
                                 static_cast<double>(mapping.sampledElementCount);
            std::vector<int> candidates;
            for (const auto& vote : faceVotes)
                if (vote.second >= requiredVotes) candidates.push_back(vote.first);

            // Without volume adjacency, coincident CAD faces cannot be
            // distinguished reliably from position alone. Refuse the mapping
            // rather than attaching one Gmsh surface to several CAD instances.
            if (adjacentSolids.empty() && candidates.size() > 1) {
                mapping.ambiguous = true;
                m_diagnostics.push_back("Gmsh surface " + std::to_string(surface.second) +
                    " matches multiple coincident CAD faces in a surface-only mesh; mapping skipped as ambiguous.");
                m_faceMappingReport.push_back(std::move(mapping));
                continue;
            }

            for (const int faceId : candidates) {
                const auto owners = ownersOfFace.find(faceId);
                if (owners == ownersOfFace.end()) {
                    if (components.empty()) facesOfSurface[surface.second].insert(faceId);
                    continue;
                }
                bool owned = false;
                if (!adjacentSolids.empty()) {
                    for (const CADComponent* component : owners->second)
                        for (const int solidId : component->solidIds)
                            if (adjacentSolids.count(solidId)) { owned = true; break; }
                } else {
                    owned = owners->second.size() == 1;
                }
                if (owned) facesOfSurface[surface.second].insert(faceId);
            }
            const auto mapped = facesOfSurface.find(surface.second);
            if (mapped != facesOfSurface.end())
                mapping.cadFaceIds.assign(mapped->second.begin(), mapped->second.end());
            if (mapping.cadFaceIds.empty()) {
                m_diagnostics.push_back("Gmsh surface " + std::to_string(surface.second) +
                    " did not map to a CAD face (matched sample ratio " +
                    std::to_string(mapping.matchRatio) + ").");
            }
            m_faceMappingReport.push_back(std::move(mapping));
        }
        std::size_t mappedSurfaces = 0, unmatchedSurfaces = 0, ambiguousSurfaces = 0;
        for (const auto& record : m_faceMappingReport) {
            if (record.ambiguous) ++ambiguousSurfaces;
            else if (!record.cadFaceIds.empty()) ++mappedSurfaces;
            else ++unmatchedSurfaces;
        }
        m_diagnostics.push_back("CAD face mapping audit: " + std::to_string(mappedSurfaces) +
            " mapped, " + std::to_string(unmatchedSurfaces) + " unmatched, " +
            std::to_string(ambiguousSurfaces) + " ambiguous Gmsh surface(s).");
        for (const auto& group : cadGroups) {
            if (group.dimension != 2) continue;
            const std::set<int> faces(group.ids.begin(), group.ids.end());
            std::set<int> mappedFaces;
            std::set<int> surfaceTags;
            for (const auto& [surfaceTag, surfaceFaces] : facesOfSurface) {
                for (const int faceId : surfaceFaces) {
                    if (faces.count(faceId)) {
                        mappedFaces.insert(faceId);
                        surfaceTags.insert(surfaceTag);
                    }
                }
            }

            // A named CAD group is an input contract, not a best-effort
            // selection. Creating it from only the mapped subset can leave a
            // boundary condition or contact definition incomplete without the
            // caller noticing. Refuse partial groups and report the exact
            // source face ids that could not be represented in the mesh.
            if (mappedFaces.size() != faces.size()) {
                std::string missing;
                for (const int faceId : faces)
                    if (!mappedFaces.count(faceId))
                        missing += (missing.empty() ? "" : ", ") + std::to_string(faceId);
                m_diagnostics.push_back("CAD face group '" + group.name +
                    "' was not mapped completely; missing CAD face id(s): " + missing +
                    ". No physical group was created.");
                continue;
            }

            const auto ids = collect(2, surfaceTags);
            if (ids.empty()) {
                m_diagnostics.push_back("CAD face group '" + group.name +
                    "' mapped to no 2-D mesh elements (not created).");
                continue;
            }
            const auto groupName = uniqueName(group.name, 2);
            mesh.addPhysicalGroup(groupName, 2, nextTag(2), ids);
            mesh.addMeshGroup(MeshGroup{groupName, 2, MeshGroupSource::CAD,
                                        group.ids, ids, collectNodes(ids)});
            ++faceGroups;
        }

        // User-defined groups are strict: do not create a partial physical group.
        // A partial support would silently leave part of a requested constraint free.
        for (const auto& group : additionalFaceGroups) {
            if (group.dimension != 2 || group.ids.empty()) {
                m_diagnostics.push_back("User CAD face group '" + group.name + "' is empty or has an invalid dimension.");
                continue;
            }
            const std::set<int> requestedFaces(group.ids.begin(), group.ids.end());
            std::set<int> mappedFaces;
            std::set<int> surfaceTags;
            for (const auto& [surfaceTag, surfaceFaces] : facesOfSurface) {
                for (const int faceId : surfaceFaces) {
                    if (requestedFaces.count(faceId)) {
                        mappedFaces.insert(faceId);
                        surfaceTags.insert(surfaceTag);
                    }
                }
            }
            if (mappedFaces.size() != requestedFaces.size()) {
                std::string missing;
                for (const int faceId : requestedFaces)
                    if (!mappedFaces.count(faceId))
                        missing += (missing.empty() ? "" : ", ") + std::to_string(faceId);
                m_diagnostics.push_back("User CAD face group '" + group.name +
                    "' was not mapped completely; missing CAD face id(s): " + missing + ". No physical group was created.");
                continue;
            }
            const auto ids = collect(2, surfaceTags);
            if (ids.empty()) {
                m_diagnostics.push_back("User CAD face group '" + group.name + "' matched no boundary elements.");
                continue;
            }
            const auto groupName = uniqueName(group.name, 2);
            mesh.addPhysicalGroup(groupName, 2, nextTag(2), ids);
            mesh.addMeshGroup(MeshGroup{groupName, 2, MeshGroupSource::CAD,
                                        group.ids, ids, collectNodes(ids)});
            ++faceGroups;
        }
    }
    // ---- Curves -> CAD edges ---------------------------------------------------------------
    std::size_t edgeGroups = 0, vertexGroups = 0;
    bool anyEdgeGroup = false;
    for (const auto& group : cadGroups) anyEdgeGroup = anyEdgeGroup || group.dimension == 1;
    if (anyEdgeGroup) {
        std::vector<std::pair<int, int>> curves;
        gmsh::model::getEntities(curves, 1);
        std::map<int, std::set<int>> edgesOfCurve;
        for (const auto& curve : curves) {
            std::vector<std::size_t> tags;
            std::vector<double> xyz, parametric;
            // Use interior curve nodes only. Endpoints are shared by adjacent
            // CAD edges and would make a coarse, unsplit curve ambiguous.
            gmsh::model::mesh::getNodes(tags, xyz, parametric, 1, curve.second, false, false);
            if (tags.empty()) continue;
            std::set<int> common;
            bool first = true;
            for (std::size_t i = 0; i < tags.size(); ++i) {
                const auto hit = cad.edgesContainingPoint({{xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]}}, tolerance);
                const std::set<int> hits(hit.begin(), hit.end());
                if (first) { common = hits; first = false; }
                else {
                    std::set<int> intersection;
                    std::set_intersection(common.begin(), common.end(), hits.begin(), hits.end(),
                                          std::inserter(intersection, intersection.begin()));
                    common = std::move(intersection);
                }
                if (common.empty()) break;
            }
            if (!common.empty()) edgesOfCurve[curve.second] = std::move(common);
        }

        for (const auto& group : cadGroups) {
            if (group.dimension != 1 || group.ids.empty()) continue;
            const std::set<int> requested(group.ids.begin(), group.ids.end());
            std::set<int> mapped;
            std::set<int> curveTags;
            for (const auto& [curveTag, edgeIds] : edgesOfCurve)
                for (const int edgeId : edgeIds)
                    if (requested.count(edgeId)) { mapped.insert(edgeId); curveTags.insert(curveTag); }
            if (mapped.size() != requested.size()) {
                std::string missing;
                for (const int id : requested)
                    if (!mapped.count(id)) missing += (missing.empty() ? "" : ", ") + std::to_string(id);
                m_diagnostics.push_back("CAD edge group '" + group.name +
                    "' could not be mapped completely; missing edge id(s): " + missing + ".");
                continue;
            }
            const auto ids = collect(1, curveTags);
            if (ids.empty()) {
                m_diagnostics.push_back("CAD edge group '" + group.name + "' matched no 1D mesh elements.");
                continue;
            }
            const auto groupName = uniqueName(group.name, 1);
            mesh.addPhysicalGroup(groupName, 1, nextTag(1), ids);
            const auto edgeNodes = collectNodes(ids);
            mesh.addNodeSet(groupName + "_NODES", edgeNodes);
            mesh.addMeshGroup(MeshGroup{groupName, 1, MeshGroupSource::CAD,
                                        group.ids, ids, edgeNodes});
            ++edgeGroups;
        }
    }

    // ---- CAD vertices -> FEM nodes ----------------------------------------------------------
    bool anyVertexGroup = false;
    for (const auto& group : cadGroups) anyVertexGroup = anyVertexGroup || group.dimension == 0;
    if (anyVertexGroup) {
        std::map<int, std::vector<NodeIndex>> nodesOfVertex;
        for (const auto& [nodeTag, nodeIndex] : nodeTagToIndex) {
            const auto p = mesh.nodeCoordinates(nodeIndex);
            const auto hits = cad.verticesContainingPoint(p, tolerance);
            for (const int vertexId : hits) nodesOfVertex[vertexId].push_back(nodeIndex);
        }
        for (const auto& group : cadGroups) {
            if (group.dimension != 0 || group.ids.empty()) continue;
            const std::set<int> requested(group.ids.begin(), group.ids.end());
            std::vector<NodeIndex> nodes;
            std::vector<int> missing;
            for (const int vertexId : requested) {
                const auto found = nodesOfVertex.find(vertexId);
                if (found == nodesOfVertex.end() || found->second.empty()) missing.push_back(vertexId);
                else nodes.insert(nodes.end(), found->second.begin(), found->second.end());
            }
            if (!missing.empty()) {
                std::string message;
                for (const int id : missing) message += (message.empty() ? "" : ", ") + std::to_string(id);
                m_diagnostics.push_back("CAD vertex group '" + group.name +
                    "' could not be mapped completely; missing vertex id(s): " + message + ".");
                continue;
            }
            std::sort(nodes.begin(), nodes.end());
            nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
            const auto groupName = uniqueName(group.name, 0);
            mesh.addNodeSet(groupName + "_NODES", nodes);
            mesh.addMeshGroup(MeshGroup{groupName, 0, MeshGroupSource::CAD,
                                        group.ids, {}, std::move(nodes)});
            ++vertexGroups;
        }
    }

    m_diagnostics.push_back("CAD groups created: " + std::to_string(componentGroups) + " components, " +
                            std::to_string(solidGroups) + " solid groups, " + std::to_string(faceGroups) + " face groups, " + std::to_string(edgeGroups) + " edge groups and " + std::to_string(vertexGroups) + " vertex groups.");
}
