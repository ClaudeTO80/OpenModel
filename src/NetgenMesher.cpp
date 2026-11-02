#include <OpenModel/NetgenMesher.hpp>

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
