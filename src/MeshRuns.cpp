/**
 * @file MeshRuns.cpp
 * @brief Implementation of the session registry of meshing runs.
 */
#include <OpenModel/MeshRuns.hpp>

#include <algorithm>
#include <stdexcept>

namespace OpenModel {

std::size_t MeshRunRegistry::add(MeshRun run) {
    m_runs.push_back(std::move(run));
    return m_runs.size() - 1;
}

std::size_t MeshRunRegistry::size() const noexcept {
    return m_runs.size();
}

const MeshRun& MeshRunRegistry::run(std::size_t index) const {
    if (index >= m_runs.size()) throw std::out_of_range("MeshRunRegistry: no run at this index");
    return m_runs[index];
}

void MeshRunRegistry::select(std::size_t index) {
    if (index >= m_runs.size()) throw std::out_of_range("MeshRunRegistry: no run at this index");
    m_selected = index;
}

std::size_t MeshRunRegistry::selected() const noexcept {
    return m_selected;
}

const MeshData& MeshRunRegistry::activeMesh() const {
    if (m_selected == npos) throw std::logic_error("MeshRunRegistry: no mesh is selected");
    return m_runs[m_selected].mesh;
}

std::vector<std::size_t> MeshRunRegistry::runsCovering(const std::string& part) const {
    std::vector<std::size_t> covering;
    for (std::size_t i = 0; i < m_runs.size(); ++i) {
        const MeshRun& run = m_runs[i];
        if (!run.part.empty()) {
            if (run.part == part) covering.push_back(i);
            continue;
        }
        const auto& bodies = run.mesh.getBodies();
        const bool holdsPart = std::any_of(bodies.begin(), bodies.end(),
                                           [&part](const MeshBody& b) { return b.name == part; });
        if (holdsPart) covering.push_back(i);
    }
    return covering;
}

bool MeshRunRegistry::isMeshed(const std::string& part) const {
    return !runsCovering(part).empty();
}

void MeshRunRegistry::remove(std::size_t index) {
    if (index >= m_runs.size()) throw std::out_of_range("MeshRunRegistry: no run at this index");
    m_runs.erase(m_runs.begin() + static_cast<std::ptrdiff_t>(index));
    if (m_selected == index) m_selected = npos;
    else if (m_selected != npos && m_selected > index) --m_selected;
}

} // namespace OpenModel
