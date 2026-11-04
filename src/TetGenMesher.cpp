#include <OpenModel/TetGenMesher.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <map>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

std::string TetGenMesher::getName() const  {
 return "TetGen Mesher"; 
}

bool TetGenMesher::buildMesh(const std::shared_ptr<CADModel>& cadModel, const MeshingOptions& options, MeshData& outMesh)  {

        m_diagnostics.clear();
        if (!cadModel) return false;
        outMesh.clear();
        // TetGen receives one merged surface triangulation: it keeps no solid identity and cannot make
        // touching solids conform. Say so instead of silently ignoring the request.
        if (options.interfaceMode == InterfaceMode::Conformal)
            m_diagnostics.push_back("TetGen backend ignores interfaceMode: use Gmsh for conformal interfaces between solids.");
        if (options.importCadGroups) {
            const bool hasSolidGroups = std::any_of(cadModel->getGroups().begin(), cadModel->getGroups().end(),
                [](const CADGroup& group) { return group.dimension == 3; });
            if (!cadModel->getComponents().empty() || hasSolidGroups)
                m_diagnostics.push_back("TetGen maps CAD face groups but does not preserve assembly or solid-region groups; the volume mesh is a single block.");
        }
        if (options.dimension != MeshDimension::Volume || !options.generateTetrahedra) {
            std::cerr << "OpenModel generates volume meshes only; use SurfaceMesher for a surface mesh.\n";
            return false;
        }
        if (!std::isfinite(options.elementSize) || options.elementSize <= 0.0) {
            std::cerr << "elementSize must be finite and positive.\n";
            return false;
        }

        {
            // One PLC cannot represent several solids that touch or overlap: TetGen fails or crashes on it.
            std::size_t solids = 0;
            for (const auto& component : cadModel->getComponents())
                if (!component.isAssembly) solids += component.solidIds.size();
            if (solids > 1) {
                m_diagnostics.push_back("TetGen backend supports a single solid; this CAD has " + std::to_string(solids) +
                                        " solids. Use Gmsh (optionally with conformal interfaces).");
                std::cerr << m_diagnostics.back() << '\n';
                return false;
            }
        }

        // Raccoglie le sub-mesh superficiali dal CADModel (funziona benissimo con AssimpModel)
        const auto& renderMeshes = cadModel->getRenderMeshes();
        if (renderMeshes.empty()) return false;

        tetgenio in, out;

        // Merge the surfaces of all parts into one PLC. Each CAD face is tessellated on its own, so a vertex
        // shared by several faces appears once per face: weld coincident vertices first, otherwise TetGen sees
        // disconnected patches ("coincident point ignored") and can fail on the result.
        std::array<double, 3> lo{{1e300, 1e300, 1e300}}, hi{{-1e300, -1e300, -1e300}};
        std::size_t rawNodes = 0;
        for (const auto& sub : renderMeshes)
            for (std::size_t i = 0; i + 2 < sub.vertices.size(); i += 3, ++rawNodes)
                for (std::size_t k = 0; k < 3; ++k) {
                    lo[k] = std::min(lo[k], double(sub.vertices[i + k]));
                    hi[k] = std::max(hi[k], double(sub.vertices[i + k]));
                }
        if (rawNodes == 0) return false;
        const double diagonal = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) +
                                          (hi[2] - lo[2]) * (hi[2] - lo[2]));
        // Render vertices are single precision: weld within a few ulps of the model size.
        const double weldTolerance = std::max(1e-5 * diagonal, 1e-12);

        std::vector<std::array<double, 3>> points;
        std::vector<std::array<int, 3>> triangles;
        std::vector<int> triangleFaceIds;
        {
            std::map<std::array<long long, 3>, std::vector<int>> grid; // cell -> welded point ids
            auto cellOf = [&](const std::array<double, 3>& p) {
                return std::array<long long, 3>{{(long long)std::floor(p[0] / weldTolerance), (long long)std::floor(p[1] / weldTolerance),
                                                 (long long)std::floor(p[2] / weldTolerance)}};
            };
            auto weld = [&](const std::array<double, 3>& p) {
                const auto cell = cellOf(p);
                for (long long dx = -1; dx <= 1; ++dx)
                    for (long long dy = -1; dy <= 1; ++dy)
                        for (long long dz = -1; dz <= 1; ++dz) {
                            const auto found = grid.find({{cell[0] + dx, cell[1] + dy, cell[2] + dz}});
                            if (found == grid.end()) continue;
                            for (const int id : found->second) {
                                const auto& q = points[static_cast<std::size_t>(id)];
                                if (std::fabs(q[0] - p[0]) <= weldTolerance && std::fabs(q[1] - p[1]) <= weldTolerance &&
                                    std::fabs(q[2] - p[2]) <= weldTolerance) return id;
                            }
                        }
                const int id = static_cast<int>(points.size());
                points.push_back(p);
                grid[cell].push_back(id);
                return id;
            };
            std::size_t degenerate = 0;
            for (const auto& sub : renderMeshes) {
                std::vector<int> local(sub.vertices.size() / 3);
                for (std::size_t i = 0; i < local.size(); ++i)
                    local[i] = weld({{sub.vertices[i * 3], sub.vertices[i * 3 + 1], sub.vertices[i * 3 + 2]}});
                for (std::size_t i = 0; i + 2 < sub.indices.size(); i += 3) {
                    const int a = local.at(sub.indices[i]), b = local.at(sub.indices[i + 1]), c = local.at(sub.indices[i + 2]);
                    if (a == b || b == c || a == c) { ++degenerate; continue; }
                    triangles.push_back({{a, b, c}});
                    triangleFaceIds.push_back(sub.sourceFaceId);
                }
            }
            if (degenerate)
                m_diagnostics.push_back("TetGen input: " + std::to_string(degenerate) + " degenerate triangle(s) dropped after vertex welding.");
            m_diagnostics.push_back("TetGen input: " + std::to_string(rawNodes) + " tessellation vertices welded to " +
                                    std::to_string(points.size()) + ".");
        }
        if (triangles.empty()) return false;

        in.numberofpoints = static_cast<int>(points.size());
        in.pointlist = new REAL[points.size() * 3];
        for (std::size_t i = 0; i < points.size(); ++i)
            for (std::size_t k = 0; k < 3; ++k) in.pointlist[i * 3 + k] = points[i][k];
        in.numberoffacets = static_cast<int>(triangles.size());
        in.facetlist = new tetgenio::facet[triangles.size()];
        in.facetmarkerlist = new int[triangles.size()];
        std::map<int, int> markerToCadFace;
        std::map<int, int> cadFaceToMarker;
        // Marker 1 identifies boundary facets whose source face metadata is
        // unavailable. Positive, distinct markers are assigned to known CAD faces.
        markerToCadFace.emplace(1, -1);
        for (const int faceId : triangleFaceIds) {
            if (faceId < 0 || cadFaceToMarker.count(faceId)) continue;
            const int marker = static_cast<int>(markerToCadFace.size()) + 1;
            cadFaceToMarker.emplace(faceId, marker);
            markerToCadFace.emplace(marker, faceId);
        }
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            tetgenio::facet* f = &in.facetlist[i];
            tetgenio::init(f);
            f->numberofpolygons = 1;
            f->polygonlist = new tetgenio::polygon[1];
            tetgenio::init(&f->polygonlist[0]);
            f->polygonlist[0].numberofvertices = 3;
            f->polygonlist[0].vertexlist = new int[3];
            for (std::size_t k = 0; k < 3; ++k) f->polygonlist[0].vertexlist[k] = triangles[i][k];
            const auto marker = triangleFaceIds[i] < 0
                ? 1 : cadFaceToMarker.at(triangleFaceIds[i]);
            in.facetmarkerlist[i] = marker;
        }

        // Stringa di comando per TetGen (es. "p" = PLS, "q" = qualità raggio-limite, "a" = volume massimo)
        // "a" bounds the tetrahedron volume. Derive it from the requested edge length
        // (regular tetrahedron: V = L^3 / (6*sqrt(2))) so that elementSize is a length.
        const double maxVolume = options.elementSize * options.elementSize * options.elementSize / (6.0 * std::sqrt(2.0));
        std::string cmd = "pq1.4fa" + std::to_string(maxVolume);
        if (options.order == 2) cmd += "o2"; // Tetraedri del 2° ordine

        try {
            // Esegue la tetrahedralizzazione Delaunay vincolata
            ::tetrahedralize(const_cast<char*>(cmd.c_str()), &in, &out);

            // Conserva ID esterni separati dagli indici interni 0-based.
            for (int i = 0; i < out.numberofpoints; ++i) {
                outMesh.addNode(static_cast<NodeId>(i + 1),
                    out.pointlist[i * 3 + 0],
                    out.pointlist[i * 3 + 1],
                    out.pointlist[i * 3 + 2]);
            }

            const int nodesPerElem = out.numberofcorners;
            ElementType type = ElementType::Unknown;
            if (nodesPerElem == 4) type = ElementType::Tetrahedron4;
            else if (nodesPerElem == 10) type = ElementType::Tetrahedron10;
            else throw std::runtime_error("Numero di nodi per tetraedro TetGen non supportato.");

            for (int i = 0; i < out.numberoftetrahedra; ++i) {
                std::vector<NodeIndex> connectivity;
                connectivity.reserve(static_cast<std::size_t>(nodesPerElem));
                for (int n = 0; n < nodesPerElem; ++n) {
                    const int nodeIdx = out.tetrahedronlist[i * nodesPerElem + n];
                    if (nodeIdx < 0 || nodeIdx >= out.numberofpoints)
                        throw std::runtime_error("TetGen ha restituito un indice nodale non valido.");
                    connectivity.push_back(static_cast<NodeIndex>(nodeIdx));
                }
                outMesh.addElement("TetGen_Volume", type,
                    static_cast<ElementId>(i + 1), connectivity);
            }

            // TetGen's -f output returns boundary triangles and their propagated
            // facet markers. Store these as genuine 2D mesh elements so the
            // CAD-to-FEM mapping survives independently of node markers.
            const bool quadraticSurface = options.order == 2;
            if (quadraticSurface && !out.o2facelist)
                throw std::runtime_error("TetGen did not return second-order boundary face nodes");
            std::map<int, std::vector<ElementId>> elementsByCadFace;
            std::map<int, std::vector<NodeIndex>> nodesByCadFace;
            const ElementId surfaceIdBase = static_cast<ElementId>(out.numberoftetrahedra) + 1;
            for (int i = 0; i < out.numberoftrifaces; ++i) {
                const int marker = out.trifacemarkerlist ? out.trifacemarkerlist[i] : 0;
                const auto face = markerToCadFace.find(marker);
                // Unmarked interior faces (if emitted by a TetGen build) are
                // not boundary elements and must not enter the surface block.
                if (face == markerToCadFace.end()) continue;
                std::vector<NodeIndex> connectivity;
                connectivity.reserve(quadraticSurface ? 6 : 3);
                for (int n = 0; n < 3; ++n) {
                    const int nodeIdx = out.trifacelist[i * 3 + n];
                    if (nodeIdx < 0 || nodeIdx >= out.numberofpoints)
                        throw std::runtime_error("TetGen boundary face references an invalid node");
                    connectivity.push_back(static_cast<NodeIndex>(nodeIdx));
                }
                if (quadraticSurface) {
                    for (int n = 0; n < 3; ++n) {
                        const int nodeIdx = out.o2facelist[i * 3 + n];
                        if (nodeIdx < 0 || nodeIdx >= out.numberofpoints)
                            throw std::runtime_error("TetGen second-order boundary face references an invalid node");
                        connectivity.push_back(static_cast<NodeIndex>(nodeIdx));
                    }
                }
                const ElementId elementId = surfaceIdBase + static_cast<ElementId>(i);
                outMesh.addElement("TetGen_Surface",
                    quadraticSurface ? ElementType::Triangle6 : ElementType::Triangle3,
                    elementId, connectivity);
                if (face->second >= 0) {
                    elementsByCadFace[face->second].push_back(elementId);
                    auto& nodes = nodesByCadFace[face->second];
                    nodes.insert(nodes.end(), connectivity.begin(), connectivity.end());
                }
            }

            std::vector<CADGroup> requestedFaceGroups;
            if (options.importCadGroups) {
                for (const auto& group : cadModel->getGroups())
                    if (group.dimension == 2) requestedFaceGroups.push_back(group);
            }
            requestedFaceGroups.insert(requestedFaceGroups.end(),
                                       options.additionalFaceGroups.begin(),
                                       options.additionalFaceGroups.end());
            // Group names must be unique across dimensions; disambiguate like the Gmsh
            // adapter instead of aborting the whole meshing run.
            std::set<std::string> usedNames;
            auto uniqueName = [&](std::string name, int dimension) {
                if (name.empty()) name = "CAD_group";
                if (usedNames.insert(name).second) return name;
                name += "_dim" + std::to_string(dimension);
                for (int n = 2; !usedNames.insert(name).second; ++n) name += "_" + std::to_string(n);
                return name;
            };
            for (const auto& group : requestedFaceGroups) {
                if (group.dimension != 2 || group.ids.empty()) continue;
                std::vector<ElementId> elements;
                std::vector<NodeIndex> nodes;
                std::set<int> uniqueFaces(group.ids.begin(), group.ids.end());
                std::vector<int> missingFaces;
                for (const int faceId : uniqueFaces) {
                    const auto e = elementsByCadFace.find(faceId);
                    const auto n = nodesByCadFace.find(faceId);
                    if (e == elementsByCadFace.end() || n == nodesByCadFace.end()) {
                        missingFaces.push_back(faceId);
                        continue;
                    }
                    elements.insert(elements.end(), e->second.begin(), e->second.end());
                    nodes.insert(nodes.end(), n->second.begin(), n->second.end());
                }
                // Incomplete groups are rejected atomically (never emitted partially, as a
                // partial support would silently leave part of a constraint free), but they
                // must not abort the mesh: report them like the Gmsh adapter does.
                if (!missingFaces.empty()) {
                    std::string message = "CAD face group '" + group.name +
                        "' was not mapped completely; missing CAD face id(s):";
                    for (const int id : missingFaces) message += " " + std::to_string(id);
                    m_diagnostics.push_back(message + ". No group was created.");
                    continue;
                }
                if (elements.empty()) continue;
                std::sort(nodes.begin(), nodes.end());
                nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
                const std::string name = uniqueName(group.name.empty() ? "CAD_Face_Group" : group.name, 2);
                outMesh.addMeshGroup(MeshGroup{name, 2, MeshGroupSource::CAD,
                                               std::vector<int>(uniqueFaces.begin(), uniqueFaces.end()),
                                               std::move(elements), std::move(nodes)});
            }
            double xmin = std::numeric_limits<double>::max();
            double ymin = std::numeric_limits<double>::max();
            double zmin = std::numeric_limits<double>::max();
            double xmax = std::numeric_limits<double>::lowest();
            double ymax = std::numeric_limits<double>::lowest();
            double zmax = std::numeric_limits<double>::lowest();
            for (NodeIndex node = 0; node < outMesh.getNumNodes(); ++node) {
                const auto p = outMesh.nodeCoordinates(node);
                xmin = std::min(xmin, p[0]); ymin = std::min(ymin, p[1]); zmin = std::min(zmin, p[2]);
                xmax = std::max(xmax, p[0]); ymax = std::max(ymax, p[1]); zmax = std::max(zmax, p[2]);
            }
            const double dx = xmax - xmin, dy = ymax - ymin, dz = zmax - zmin;
            const double mappingTolerance = std::max(1e-6 * std::sqrt(dx * dx + dy * dy + dz * dz), 1e-9);

            // Edge and vertex groups are transferred as geometric node
            // memberships in TetGen/Netgen. These C adapters do not currently
            // expose stable 1D CAD-curve element ownership.
            if (options.importCadGroups) {
                for (const auto& group : cadModel->getGroups()) {
                    if ((group.dimension != 1 && group.dimension != 0) || group.ids.empty()) continue;
                    const std::set<int> requested(group.ids.begin(), group.ids.end());
                    std::map<int, std::vector<NodeIndex>> nodesByEntity;
                    for (NodeIndex node = 0; node < outMesh.getNumNodes(); ++node) {
                        const auto point = outMesh.nodeCoordinates(node);
                        const auto hits = group.dimension == 1
                            ? cadModel->edgesContainingPoint(point, mappingTolerance)
                            : cadModel->verticesContainingPoint(point, mappingTolerance);
                        for (const int id : hits)
                            if (requested.count(id)) nodesByEntity[id].push_back(node);
                    }
                    std::vector<NodeIndex> nodes;
                    std::vector<int> missing;
                    for (const int id : requested) {
                        const auto found = nodesByEntity.find(id);
                        if (found == nodesByEntity.end() || found->second.empty()) missing.push_back(id);
                        else nodes.insert(nodes.end(), found->second.begin(), found->second.end());
                    }
                    if (!missing.empty()) {
                        std::string message = "CAD " +
                            std::string(group.dimension == 1 ? "edge" : "vertex") +
                            " group '" + group.name + "' could not be mapped completely; missing entity id(s):";
                        for (const int id : missing) message += " " + std::to_string(id);
                        m_diagnostics.push_back(message + ". No group was created.");
                        continue;
                    }
                    std::sort(nodes.begin(), nodes.end());
                    nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
                    const std::string name = uniqueName(group.name, group.dimension);
                    outMesh.addNodeSet(name + "_NODES", nodes);
                    outMesh.addMeshGroup(MeshGroup{name, group.dimension, MeshGroupSource::CAD,
                                                  std::vector<int>(requested.begin(), requested.end()),
                                                  {}, std::move(nodes)});
                }
            }

            if (!requestedFaceGroups.empty() && cadFaceToMarker.empty())
                m_diagnostics.push_back("TetGen input tessellation has no sourceFaceId metadata; CAD face groups cannot be mapped.");

            const auto structuralErrors = outMesh.validate();
            if (!structuralErrors.empty())
                throw std::runtime_error("TetGen mesh failed structural validation: " + structuralErrors.front());
            return true;
        } catch (const std::exception& e) {
            std::cerr << "TetGen Error: " << e.what() << std::endl;
            m_diagnostics.push_back(std::string("TetGen failed: ") + e.what());
            outMesh.clear();
            return false;
        } catch (int code) {
            // TetGen reports fatal input problems by throwing an int (its internal exit code).
            std::cerr << "TetGen Error: exit code " << code << std::endl;
            m_diagnostics.push_back("TetGen failed with code " + std::to_string(code) +
                                    ": the surface is probably not one closed, non-self-intersecting shell "
                                    "(for example several solids that touch or overlap).");
            outMesh.clear();
            return false;
        } catch (...) {
            m_diagnostics.push_back("TetGen failed with an unknown error.");
            outMesh.clear();
            return false;
        }
    
}
