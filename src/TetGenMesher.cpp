#include <OpenModel/TetGenMesher.hpp>

#include <algorithm>
#include <array>
#include <map>

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
        if (options.importCadGroups && (!cadModel->getComponents().empty() || !cadModel->getGroups().empty()))
            m_diagnostics.push_back("TetGen backend does not transfer CAD components/groups: the mesh is a single block.");
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
        for (std::size_t i = 0; i < triangles.size(); ++i) {
            tetgenio::facet* f = &in.facetlist[i];
            tetgenio::init(f);
            f->numberofpolygons = 1;
            f->polygonlist = new tetgenio::polygon[1];
            tetgenio::init(&f->polygonlist[0]);
            f->polygonlist[0].numberofvertices = 3;
            f->polygonlist[0].vertexlist = new int[3];
            for (std::size_t k = 0; k < 3; ++k) f->polygonlist[0].vertexlist[k] = triangles[i][k];
            in.facetmarkerlist[i] = 1;
        }

        // Stringa di comando per TetGen (es. "p" = PLS, "q" = qualità raggio-limite, "a" = volume massimo)
        // "a" bounds the tetrahedron volume. Derive it from the requested edge length
        // (regular tetrahedron: V = L^3 / (6*sqrt(2))) so that elementSize is a length.
        const double maxVolume = options.elementSize * options.elementSize * options.elementSize / (6.0 * std::sqrt(2.0));
        std::string cmd = "pq1.4a" + std::to_string(maxVolume);
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
