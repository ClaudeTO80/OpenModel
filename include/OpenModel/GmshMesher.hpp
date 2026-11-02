#pragma once

#include <OpenModel/Mesher.hpp>
#include <OpenModel/MeshValidator.hpp>

#include <gmsh.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Gmsh owns process-global state. Calls made through this adapter are serialized.
// This adapter owns the Gmsh initialize/finalize lifecycle; do not call it while
// another component independently manages a Gmsh session.
class GmshMesher final : public Mesher {
public:
    std::string getName() const override ;

    const std::string& lastError() const noexcept ;

    const std::vector<std::string>& diagnostics() const override { return m_diagnostics; }

    bool buildMesh(const std::shared_ptr<CADModel>& cadModel,
                   const MeshingOptions& options,
                   MeshData& outMesh) override ;

private:
    class GmshSession {
    public:
        ~GmshSession() noexcept ;
        GmshSession(const GmshSession&) = delete;
        GmshSession& operator=(const GmshSession&) = delete;
        GmshSession() = default;
    };

    static std::mutex& gmshMutex() ;

    bool fail(const std::string& message) ;

    static ElementType mapElementType(int gmshType) noexcept ;

    /**
     * @brief Turn the CAD assembly structure into physical groups by matching mesh entities back to CAD solids/faces.
     *
     * Matching is geometric: a Gmsh volume belongs to the CAD solid(s) that contain a sample point of its
     * elements; a Gmsh surface belongs to a CAD face if its sample nodes lie on that face and one of the
     * volumes it bounds belongs to a solid that owns the face. This stays valid after a fragment operation.
     */
    void mapCadGroups(const CADModel& cad, MeshData& mesh,
                      const std::unordered_map<std::size_t, NodeIndex>& nodeTagToIndex,
                      const std::map<std::pair<int, int>, std::vector<ElementId>>& elementIdsByEntity,
                      std::set<std::string>& usedPhysicalNames);

    std::string m_lastError;
    std::vector<std::string> m_diagnostics;
};
