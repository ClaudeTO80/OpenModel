#pragma once

#include <OpenModel/Mesher.hpp>
#include <OpenModel/MeshValidator.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nglib.h>

// Netgen's OCC functions are available only when Netgen itself was built with
// Open CASCADE support. Enable OPENMODEL_NETGEN_HAS_OCC only for such builds.
#if defined(OPENMODEL_NETGEN_HAS_OCC)
#  include <nglib_occ.h>
#endif

// Optional Netgen backend. Native Netgen state is process-global, therefore
// calls through this adapter are serialized and the adapter owns Ng_Init/Ng_Exit.
// STEP/IGES/BREP import is supported only when Netgen was built with OCC.
class NetgenMesher final : public Mesher {
public:
    std::string getName() const override ;
    const std::string& lastError() const noexcept ;

    bool buildMesh(const std::shared_ptr<CADModel>& cadModel,
                   const MeshingOptions& options,
                   MeshData& outMesh) override ;

private:
    static std::mutex& netgenMutex() ;

    bool fail(const std::string& message) ;

#if defined(OPENMODEL_NETGEN_HAS_OCC)
    class NetgenSession {
    public:
        NetgenSession() = default;
        ~NetgenSession() noexcept ;
        NetgenSession(const NetgenSession&) = delete;
        NetgenSession& operator=(const NetgenSession&) = delete;
    };

    class NetgenMeshHandle {
    public:
        explicit NetgenMeshHandle(Ng_Mesh* value) : m_value(value) ;
        ~NetgenMeshHandle() noexcept ;
        NetgenMeshHandle(const NetgenMeshHandle&) = delete;
        NetgenMeshHandle& operator=(const NetgenMeshHandle&) = delete;
    private:
        Ng_Mesh* m_value;
    };

    class NetgenOccHandle {
    public:
        explicit NetgenOccHandle(Ng_OCC_Geometry* value) : m_value(value) ;
        ~NetgenOccHandle() noexcept ;
        NetgenOccHandle(const NetgenOccHandle&) = delete;
        NetgenOccHandle& operator=(const NetgenOccHandle&) = delete;
    private:
        Ng_OCC_Geometry* m_value;
    };

    static Ng_OCC_Geometry* loadOccGeometry(const std::string& path) ;

    static void checkResult(Ng_Result result, const char* operation) ;
#endif

    static ElementType mapElementType(Ng_Volume_Element_Type type) noexcept ;

    std::string m_lastError;
};
