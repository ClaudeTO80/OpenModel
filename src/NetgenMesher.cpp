#include <OpenModel/NetgenMesher.hpp>

#include <algorithm>
#include <map>
#include <iterator>
#include <set>
#include <unordered_map>

std::string NetgenMesher::getName() const  {
 return "Netgen Mesher"; 
}

const std::string& NetgenMesher::lastError() const noexcept {
 return m_lastError; 
}

bool NetgenMesher::buildMesh(const std::shared_ptr<CADModel>& cadModel, const MeshingOptions& options, MeshData& outMesh)  {

        m_lastError.clear();
        if (!cadModel) return fail("CAD model pointer is null");
        if (cadModel->getFilePath().empty()) return fail("CAD file path is empty");
        if (!std::isfinite(options.elementSize) || options.elementSize <= 0.0)
            return fail("elementSize must be finite and greater than zero");
        if (options.order != 1 && options.order != 2)
            return fail("Only first- and second-order elements are supported");
        if (!options.generateTetrahedra)
            return fail("Netgen adapter currently supports volumetric 3D meshes only");

#if !defined(OPENMODEL_NETGEN_HAS_OCC)
        return fail("This Netgen build has no OCC adapter enabled; STEP/IGES/BREP input requires a Netgen build with OCCGEOMETRY");
#else
        std::lock_guard<std::mutex> lock(netgenMutex());
        try {
            Ng_Init();
            NetgenSession session;

            Ng_Mesh* mesh = Ng_NewMesh();
            if (!mesh) return fail("Netgen failed to allocate a mesh");
            NetgenMeshHandle meshHandle(mesh);

            const std::string path = cadModel->getFilePath();
            Ng_OCC_Geometry* geometry = loadOccGeometry(path);
            if (!geometry)
                return fail("Netgen could not load CAD file: " + path);
            NetgenOccHandle geometryHandle(geometry);

            Ng_Meshing_Parameters parameters;
            parameters.maxh = options.elementSize;
            parameters.minh = 0.0;
            parameters.fineness = 0.5;
            parameters.second_order = options.order == 2 ? 1 : 0;
            parameters.uselocalh = 1;
            parameters.optsurfmeshenable = 1;
            parameters.optvolmeshenable = 1;

            checkResult(Ng_OCC_GenerateEdgeMesh(geometry, mesh, &parameters),
                        "generating CAD edge mesh");
            checkResult(Ng_OCC_GenerateSurfaceMesh(geometry, mesh, &parameters),
                        "generating CAD surface mesh");
            checkResult(Ng_GenerateVolumeMesh(mesh, &parameters),
                        "generating tetrahedral volume mesh");

            // Convert into a temporary candidate. The caller's output is not
            // modified unless extraction and structural validation both succeed.
            MeshData candidate;
            const int pointCount = Ng_GetNP(mesh);
            if (pointCount <= 0) throw std::runtime_error("Netgen generated no nodes");
            if (pointCount < 0) throw std::runtime_error("Netgen returned a negative node count");

            for (int i = 1; i <= pointCount; ++i) {
                double xyz[3] = {0.0, 0.0, 0.0};
                Ng_GetPoint(mesh, i, xyz);
                candidate.addNode(static_cast<NodeId>(i), xyz[0], xyz[1], xyz[2]);
            }

            const int elementCount = Ng_GetNE(mesh);
            if (elementCount <= 0) throw std::runtime_error("Netgen generated no volume elements");
            for (int i = 1; i <= elementCount; ++i) {
                int pointIds[NG_VOLUME_ELEMENT_MAXPOINTS] = {};
                const auto nativeType = Ng_GetVolumeElement(mesh, i, pointIds);
                const ElementType type = mapElementType(nativeType);
                const std::size_t count = elementNodeCount(type);
                if (type == ElementType::Unknown || count == 0)
                    throw std::runtime_error("Netgen returned an unsupported volume element type");

                std::vector<NodeIndex> connectivity;
                connectivity.reserve(count);
                for (std::size_t local = 0; local < count; ++local) {
                    // Netgen's point numbers are 1-based; MeshData stores 0-based indices.
                    if (pointIds[local] < 1 || pointIds[local] > pointCount)
                        throw std::runtime_error("Netgen element references an invalid point number");
                    connectivity.push_back(static_cast<NodeIndex>(pointIds[local] - 1));
                }
                candidate.addElement("Netgen_Volume", type,
                                     static_cast<ElementId>(i), connectivity);
            }

            // Export Netgen's boundary elements as first-class OpenModel
            // elements. The C API does not expose a stable CAD face ID for
            // each surface element, so map element centroids through CADModel.
            const int surfaceCount = Ng_GetNSE(mesh);
            if (surfaceCount < 0) throw std::runtime_error("Netgen returned a negative surface-element count");
            std::map<int, std::vector<ElementId>> elementsByCadFace;
            std::map<int, std::vector<NodeIndex>> nodesByCadFace;
            std::array<double, 3> lower{{1e300, 1e300, 1e300}};
            std::array<double, 3> upper{{-1e300, -1e300, -1e300}};
            for (int i = 1; i <= pointCount; ++i) {
                double xyz[3] = {};
                Ng_GetPoint(mesh, i, xyz);
                for (int k = 0; k < 3; ++k) {
                    lower[static_cast<std::size_t>(k)] = std::min(lower[static_cast<std::size_t>(k)], xyz[k]);
                    upper[static_cast<std::size_t>(k)] = std::max(upper[static_cast<std::size_t>(k)], xyz[k]);
                }
            }
            const double dx = upper[0] - lower[0], dy = upper[1] - lower[1], dz = upper[2] - lower[2];
            const double mappingTolerance = std::max(1e-6 * std::sqrt(dx * dx + dy * dy + dz * dz), 1e-9);

            for (int i = 1; i <= surfaceCount; ++i) {
                int pointIds[NG_SURFACE_ELEMENT_MAXPOINTS] = {};
                const auto nativeType = Ng_GetSurfaceElement(mesh, i, pointIds);
                ElementType type = ElementType::Unknown;
                std::size_t cornerCount = 0;
                switch (nativeType) {
                case NG_TRIG: type = ElementType::Triangle3; cornerCount = 3; break;
                case NG_QUAD: type = ElementType::Quadrilateral4; cornerCount = 4; break;
                case NG_TRIG6: type = ElementType::Triangle6; cornerCount = 3; break;
                case NG_QUAD8: type = ElementType::Quadrilateral8; cornerCount = 4; break;
                case NG_QUAD6:
                    throw std::runtime_error("Netgen returned NG_QUAD6, which has no matching OpenModel element topology");
                default:
                    throw std::runtime_error("Netgen returned an unsupported surface element type");
                }
                const auto nodeCount = elementNodeCount(type);
                if (nodeCount == 0) throw std::runtime_error("Netgen surface element topology is unsupported");
                std::vector<NodeIndex> connectivity;
                connectivity.reserve(nodeCount);
                std::set<int> commonFaces;
                bool firstCorner = true;
                for (std::size_t local = 0; local < nodeCount; ++local) {
                    if (pointIds[local] < 1 || pointIds[local] > pointCount)
                        throw std::runtime_error("Netgen surface element references an invalid point number");
                    const auto index = static_cast<NodeIndex>(pointIds[local] - 1);
                    connectivity.push_back(index);
                    if (local < cornerCount) {
                        // Surface mesh corner nodes lie on the CAD support surface.
                        // Intersecting their face candidates is more reliable than
                        // testing a chord centroid on curved geometry.
                        const auto xyz = candidate.nodeCoordinates(index);
                        const auto hitsVector = cadModel->facesContainingPoint(xyz, mappingTolerance);
                        const std::set<int> hits(hitsVector.begin(), hitsVector.end());
                        if (firstCorner) {
                            commonFaces = hits;
                            firstCorner = false;
                        } else {
                            std::set<int> intersection;
                            std::set_intersection(commonFaces.begin(), commonFaces.end(),
                                                  hits.begin(), hits.end(),
                                                  std::inserter(intersection, intersection.begin()));
                            commonFaces = std::move(intersection);
                        }
                    }
                }
                const ElementId surfaceId = static_cast<ElementId>(elementCount) + static_cast<ElementId>(i);
                candidate.addElement("Netgen_Surface", type, surfaceId, connectivity);
                for (const int faceId : commonFaces) {
                    elementsByCadFace[faceId].push_back(surfaceId);
                    auto& faceNodes = nodesByCadFace[faceId];
                    faceNodes.insert(faceNodes.end(), connectivity.begin(), connectivity.end());
                }
            }

            std::vector<CADGroup> requestedFaceGroups;
            for (const auto& group : cadModel->getGroups())
                if (group.dimension == 2) requestedFaceGroups.push_back(group);
            requestedFaceGroups.insert(requestedFaceGroups.end(),
                                       options.additionalFaceGroups.begin(),
                                       options.additionalFaceGroups.end());
            std::set<std::string> usedGroupNames;
            for (const auto& group : requestedFaceGroups) {
                if (group.dimension != 2 || group.ids.empty()) continue;
                const std::set<int> faceIds(group.ids.begin(), group.ids.end());
                std::vector<ElementId> elementIds;
                std::vector<NodeIndex> nodeIndices;
                std::vector<int> missing;
                for (const int faceId : faceIds) {
                    const auto elements = elementsByCadFace.find(faceId);
                    const auto nodes = nodesByCadFace.find(faceId);
                    if (elements == elementsByCadFace.end() || nodes == nodesByCadFace.end()) {
                        missing.push_back(faceId);
                        continue;
                    }
                    elementIds.insert(elementIds.end(), elements->second.begin(), elements->second.end());
                    nodeIndices.insert(nodeIndices.end(), nodes->second.begin(), nodes->second.end());
                }
                if (!missing.empty()) {
                    std::string message = "Netgen could not map every CAD face in group '" + group.name + "'. Missing face IDs:";
                    for (const int id : missing) message += " " + std::to_string(id);
                    throw std::runtime_error(message);
                }
                if (elementIds.empty()) continue;
                std::sort(nodeIndices.begin(), nodeIndices.end());
                nodeIndices.erase(std::unique(nodeIndices.begin(), nodeIndices.end()), nodeIndices.end());
                const std::string name = group.name.empty() ? "CAD_Face_Group" : group.name;
                if (!usedGroupNames.insert(name).second)
                    throw std::runtime_error("Netgen CAD face groups contain duplicate names: " + name);
                candidate.addMeshGroup(MeshGroup{name, 2, MeshGroupSource::CAD,
                                                  std::vector<int>(faceIds.begin(), faceIds.end()),
                                                  std::move(elementIds), std::move(nodeIndices)});
            }
            if (!requestedFaceGroups.empty() && elementsByCadFace.empty())
                throw std::runtime_error("Netgen surface mesh could not be mapped to CAD face IDs.");

            const auto structuralErrors = candidate.validate();
            if (!structuralErrors.empty())
                throw std::runtime_error("Netgen mesh failed structural validation: " + structuralErrors.front());
            const auto geometricReport = MeshValidator::validate(candidate);
            if (geometricReport.hasErrors())
                throw std::runtime_error("Netgen mesh failed geometric validation: " +
                                         geometricReport.issues.front().message);

            outMesh = std::move(candidate);
            return true;
        } catch (const std::exception& e) {
            return fail(e.what());
        } catch (...) {
            return fail("Unknown non-standard exception raised by Netgen");
        }
#endif
    
}

std::mutex& NetgenMesher::netgenMutex() {

        static std::mutex mutex;
        return mutex;
    
}

bool NetgenMesher::fail(const std::string& message) {

        m_lastError = message;
        return false;
    
}

NetgenMesher::NetgenSession::~NetgenSession() noexcept {
 try { Ng_Exit(); } catch (...) {} 
}

explicit NetgenMesher::NetgenMeshHandle::NetgenMeshHandle(Ng_Mesh* value) : m_value(value) {

}

NetgenMesher::NetgenMeshHandle::~NetgenMeshHandle() noexcept {
 if (m_value) Ng_DeleteMesh(m_value); 
}

explicit NetgenMesher::NetgenOccHandle::NetgenOccHandle(Ng_OCC_Geometry* value) : m_value(value) {

}

NetgenMesher::NetgenOccHandle::~NetgenOccHandle() noexcept {
 if (m_value) { try { Ng_OCC_DeleteGeometry(m_value); } catch (...) {} } 
}

Ng_OCC_Geometry* NetgenMesher::loadOccGeometry(const std::string& path) {

        const auto dot = path.find_last_of('.');
        std::string extension = dot == std::string::npos ? std::string{} : path.substr(dot);
        for (auto& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension == ".step" || extension == ".stp") return Ng_OCC_Load_STEP(path.c_str());
        if (extension == ".iges" || extension == ".igs") return Ng_OCC_Load_IGES(path.c_str());
        if (extension == ".brep" || extension == ".brp") return Ng_OCC_Load_BREP(path.c_str());
        throw std::runtime_error("Unsupported Netgen OCC input format; use STEP, IGES or BREP");
    
}

void NetgenMesher::checkResult(Ng_Result result, const char* operation) {

        if (result != NG_OK)
            throw std::runtime_error(std::string("Netgen failed while ") + operation +
                                     " (status " + std::to_string(static_cast<int>(result)) + ")");
    
}

#endif

    ElementType NetgenMesher::mapElementType(Ng_Volume_Element_Type type) noexcept {

        switch (type) {
        case NG_TET: return ElementType::Tetrahedron4;
        case NG_TET10: return ElementType::Tetrahedron10;
        case NG_PRISM: return ElementType::Prism6;
        case NG_PYRAMID: return ElementType::Pyramid5;
        default: return ElementType::Unknown;
        }
    
}
