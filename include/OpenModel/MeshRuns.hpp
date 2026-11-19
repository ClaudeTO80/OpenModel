/**
 * @file MeshRuns.hpp
 * @brief Session registry of the meshes made from one CAD model: each run is the whole assembly or one part.
 *
 * Several runs can be kept at once. Exactly one of them is the active mesh: the one the viewer shows, the analysis
 * solves and the exporters write. The others stay in memory and are ignored until they are selected.
 */
#pragma once

#include <OpenModel/MeshData.hpp>

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace OpenModel {

/**
 * @brief One meshing run: the whole assembly, or one CAD part of it.
 */
struct MeshRun {
    std::string name;   ///< Label shown in the model tree, e.g. "Bracket, 2 mm".
    std::string part;   ///< CAD part that was meshed; empty when the run is the whole assembly.
    MeshData mesh;      ///< The mesh produced by the run.
};

/**
 * @brief Runs of one session, with one of them selected as the active mesh.
 *
 * Indices are positions in the order the runs were added; a removed run shifts the later ones down.
 */
class MeshRunRegistry {
public:
    /** Value of selected() when no run is active. */
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    /**
     * @brief Keeps a run.
     * @param run the run to keep; it is moved in
     * @return index of the run
     */
    std::size_t add(MeshRun run);

    /** @brief Number of runs kept. */
    std::size_t size() const noexcept;

    /**
     * @brief Run at an index.
     * @throws std::out_of_range when the index is not a run
     */
    const MeshRun& run(std::size_t index) const;

    /**
     * @brief Makes a run the active mesh.
     * @throws std::out_of_range when the index is not a run
     */
    void select(std::size_t index);

    /** @brief Index of the active run, or npos when none is active. */
    std::size_t selected() const noexcept;

    /**
     * @brief The mesh that is shown, solved and exported.
     * @throws std::logic_error when no run is active
     */
    const MeshData& activeMesh() const;

    /**
     * @brief Indices of the runs that cover a CAD part: its own run, and every assembly run that holds a body of
     *        that name.
     */
    std::vector<std::size_t> runsCovering(const std::string& part) const;

    /** @brief True when at least one run covers the part. */
    bool isMeshed(const std::string& part) const;

    /**
     * @brief Removes a run. The active run stays active; when it is the removed one, nothing is active.
     * @throws std::out_of_range when the index is not a run
     */
    void remove(std::size_t index);

private:
    std::vector<MeshRun> m_runs;
    std::size_t m_selected = npos;
};

} // namespace OpenModel
