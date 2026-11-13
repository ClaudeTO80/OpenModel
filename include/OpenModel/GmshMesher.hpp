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

/** Per-Gmsh-surface audit record for CAD Face mapping. */
struct CADFaceMappingRecord {
    int gmshSurfaceTag = -1;
    std::size_t elementCount = 0;
    // Number of 2-D elements whose representative point was checked. Kept
    // under the historical field name for source compatibility.
    std::size_t sampledElementCount = 0;
    // Maximum number of checked elements explained by a single CAD face.
    std::size_t matchedSampleCount = 0;
    // Elements whose representative point could not be projected onto the
    // trimmed Gmsh surface; these remain in the match-ratio denominator.
    std::size_t projectionFailureCount = 0;
    double matchRatio = 0.0;
    std::vector<int> cadFaceIds;
    bool ambiguous = false;
};

/** @brief Version and build description reported by the linked Gmsh library. */
struct GmshLibraryInfo {
    std::string version;   ///< `General.Version` of the library; the API version of the headers when it cannot be read.
    std::string buildInfo; ///< `General.BuildInfo`: licence, build system and options, versions of the libraries it uses. Empty when not available.
};

/**
 * @brief Gmsh adapter. Gmsh owns process-global state, so calls through this
 * adapter are serialized. This adapter owns the initialize/finalize lifecycle;
 * do not use it while another component independently manages a Gmsh session.
 */
class GmshMesher final : public Mesher {
public:
    std::string getName() const override ;

    const std::string& lastError() const noexcept ;

    /**
     * @brief Asks the linked Gmsh library for its version and for how it was built.
     *
     * Starts and finalises a Gmsh session, serialised with the other calls of this adapter, so it must not be
     * called while another component manages a Gmsh session. It never throws: what cannot be read stays empty.
     */
    static GmshLibraryInfo libraryInfo();

    const std::vector<std::string>& diagnostics() const override { return m_diagnostics; }
    /** Gmsh log of the most recent buildMesh call, at the level of MeshingOptions::logLevel. */
    const std::vector<std::string>& log() const noexcept { return m_log; }
    /** Audit data from the most recent buildMesh call, one record per meshed CAD surface. */
    const std::vector<CADFaceMappingRecord>& faceMappingReport() const noexcept { return m_faceMappingReport; }

    /**
     * @brief Pick the CAD solid(s) of a Gmsh volume from the solids found at its sample points.
     *
     * The centroid of a coarse element can fall outside the solid, for example in the bore of a nut, where it
     * lands in the solid filling the bore. Taking the first sample, or the intersection of all of them, then
     * assigns the volume to the wrong solid. The solids that contain the most samples are returned, so
     * coincident solids (all samples shared) are all kept while an outlier sample is outvoted.
     *
     * @param hitsPerSample For every sample point the ids of the CAD solids containing it.
     * @param agree         Set to true when every sample produced the same set of solids.
     * @return Solids with the highest sample count; empty when no sample hit any solid.
     */
    static std::set<int> solidsByMajority(const std::vector<std::vector<int>>& hitsPerSample, bool& agree);

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
     *
     * @param additionalFaceGroups User-defined face groups; always mapped.
     * @param auditAllFaces        When false, surfaces are mapped to CAD faces only if a dimension-2 group
     *                             (native or additional) has to be built.
     * @param importNativeGroups   When false, components and groups stored in the CAD source are not
     *                             transferred; only @p additionalFaceGroups are.
     */
    void mapCadGroups(const CADModel& cad, MeshData& mesh,
                      const std::unordered_map<std::size_t, NodeIndex>& nodeTagToIndex,
                      const std::map<std::pair<int, int>, std::vector<ElementId>>& elementIdsByEntity,
                      std::set<std::string>& usedPhysicalNames,
                      const std::vector<CADGroup>& additionalFaceGroups,
                      bool importNativeGroups = true,
                      bool auditAllFaces = true);

    /** Writes an OpenModel trace line to the Gmsh log when the log level is Info or Debug. */
    void trace(const std::string& message) const;

    std::string m_lastError;
    std::vector<std::string> m_diagnostics;
    std::vector<CADFaceMappingRecord> m_faceMappingReport;
    std::vector<std::string> m_log;
    bool m_trace = false;
};
