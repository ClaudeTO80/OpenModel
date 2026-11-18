/**
 * @file mesh_runs_test.cpp
 * @brief Session registry of meshing runs: selection, the active mesh, part coverage and removal.
 */
#include <OpenModel/MeshRuns.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace OpenModel;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";        \
        }                                                                                  \
    } while (0)

/** @brief Mesh with one tetrahedron per named body; the body is listed with its element. */
MeshData makeMesh(const std::vector<std::string>& bodyNames) {
    MeshData m;
    m.addNode(1, 0, 0, 0);
    m.addNode(2, 1, 0, 0);
    m.addNode(3, 0, 1, 0);
    m.addNode(4, 0, 0, 1);
    m.addElement("VOL", ElementType::Tetrahedron4, 1, {0, 1, 2, 3});
    std::vector<MeshBody> bodies;
    for (const auto& name : bodyNames) bodies.push_back(MeshBody{name, 3, {1}});
    m.setBodies(bodies);
    return m;
}

} // namespace

int main() {
    MeshRunRegistry reg;
    CHECK(reg.size() == 0);
    CHECK(reg.selected() == MeshRunRegistry::npos);
    bool threw = false;
    try { (void)reg.activeMesh(); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);

    const std::size_t bracket = reg.add({"Bracket, 2 mm", "Bracket", makeMesh({"Bracket"})});
    const std::size_t assembly = reg.add({"Assembly, 3 mm", "", makeMesh({"Bracket", "Bolt"})});
    CHECK(reg.size() == 2);
    CHECK(bracket == 0 && assembly == 1);

    // Coverage: a part run covers its part, an assembly run covers every body it holds.
    CHECK(reg.isMeshed("Bracket"));
    CHECK(reg.isMeshed("Bolt"));
    CHECK(!reg.isMeshed("Plate"));
    CHECK((reg.runsCovering("Bracket") == std::vector<std::size_t>{0, 1}));
    CHECK((reg.runsCovering("Bolt") == std::vector<std::size_t>{1}));

    // Only the selected run is the active mesh; the other one is kept and ignored.
    reg.select(assembly);
    CHECK(reg.selected() == assembly);
    CHECK(&reg.activeMesh() == &reg.run(assembly).mesh);
    CHECK(reg.activeMesh().getBodies().size() == 2);
    CHECK(reg.run(bracket).mesh.getBodies().size() == 1);

    threw = false;
    try { reg.select(7); } catch (const std::out_of_range&) { threw = true; }
    CHECK(threw);
    CHECK(reg.selected() == assembly);

    // Removing an earlier run shifts the selection; removing the selected run leaves nothing selected.
    reg.remove(bracket);
    CHECK(reg.size() == 1);
    CHECK(reg.selected() == 0);
    CHECK(reg.run(0).name == "Assembly, 3 mm");
    reg.remove(0);
    CHECK(reg.size() == 0);
    CHECK(reg.selected() == MeshRunRegistry::npos);

    if (g_failures == 0) std::cout << "mesh_runs_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
