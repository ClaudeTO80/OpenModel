/**
 * @file occ_structure_test.cpp
 * @brief Writes an XDE assembly (names, colours, a layer, a named face, two instances of one part)
 *        to STEP, reads it back through OCCModel and checks what survives.
 */
#include "support/assembly_fixture.hpp"

#include <OpenModel/OCCModel.hpp>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <STEPControl_StepModelType.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_LayerTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <iostream>
#include <string>

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "CHECK failed: " #cond " (line " << __LINE__ << ")\n";          \
            return 1;                                                                    \
        }                                                                                \
    } while (0)

namespace {

const CADComponent* findComponent(const std::vector<CADComponent>& all, const std::string& name, int skip = 0) {
    for (const auto& c : all)
        if (c.name == name && skip-- == 0) return &c;
    return nullptr;
}

const CADGroup* findGroup(const std::vector<CADGroup>& all, const std::string& name) {
    for (const auto& g : all)
        if (g.name == name) return &g;
    return nullptr;
}

} // namespace

int main() {
    const std::string path = "openmodel_occ_structure.step";
    CHECK(writeAssemblyFixture(path));

    OCCModel model;
    const bool loaded = model.loadFromFile(path);
    if (!std::getenv("OPENMODEL_KEEP_FIXTURE")) std::remove(path.c_str());
    CHECK(loaded);
    const auto& components = model.getComponents();
    const auto& groups = model.getGroups();
    for (const auto& c : components)
        std::cout << "component " << c.id << " parent=" << c.parent << " '" << c.path << "' solids=" << c.solidIds.size()
                  << " faces=" << c.faceIds.size() << " color=" << (c.hasColor ? "yes" : "no") << '\n';
    for (const auto& g : groups)
        std::cout << "group '" << g.name << "' dim=" << g.dimension << " ids=" << g.ids.size() << '\n';

    const auto* assembly = findComponent(components, "Assy");
    const auto* base = findComponent(components, "Base");
    const auto* cover = findComponent(components, "Cover");
    const auto* pin1 = findComponent(components, "Pin", 0);
    const auto* pin2 = findComponent(components, "Pin", 1);
    CHECK(assembly && base && cover && pin1 && pin2);
    CHECK(components.size() == 5);
    CHECK(assembly->isAssembly && assembly->parent == -1 && assembly->solidIds.size() == 4);
    CHECK(base->parent == assembly->id && base->solidIds.size() == 1 && base->faceIds.size() == 6);
    CHECK(pin1->solidIds.size() == 1 && pin2->solidIds.size() == 1 && pin1->solidIds != pin2->solidIds);
    CHECK(base->path == "Assy/Base");
    CHECK(pin1->path == "Assy/Pin" && pin2->path == "Assy/Pin #2");
    CHECK(base->hasColor && std::fabs(base->color[0] - 1.0f) < 0.01f && base->color[2] < 0.01f);
    CHECK(cover->hasColor && cover->color[2] > 0.99f);

    const auto* layer = findGroup(groups, "Interface");
    const auto* top = findGroup(groups, "TopFace");
    CHECK(layer && layer->dimension == 2 && layer->ids.size() == 1);
    CHECK(top && top->dimension == 2 && top->ids.size() == 1);
    CHECK(layer->ids == top->ids);

    const auto* edgeLayer = findGroup(groups, "EdgeLayer");
    const auto* vertexLayer = findGroup(groups, "VertexLayer");
    const auto* namedEdge = findGroup(groups, "BaseEdge");
    const auto* namedVertex = findGroup(groups, "BaseVertex");
    CHECK(edgeLayer && edgeLayer->dimension == 1 && !edgeLayer->ids.empty());
    CHECK(vertexLayer && vertexLayer->dimension == 0 && !vertexLayer->ids.empty());
    CHECK(namedEdge && namedEdge->dimension == 1 && !namedEdge->ids.empty());
    CHECK(namedVertex && namedVertex->dimension == 0 && !namedVertex->ids.empty());

    // Geometric queries: the named face lies at z = 10; (5,5,12) is inside the cover; (5,5,5) inside the base.
    const double tol = 1e-6;
    const auto onTop = model.facesContainingPoint({{5, 5, 10}}, tol);
    CHECK(std::find(onTop.begin(), onTop.end(), top->ids[0]) != onTop.end());
    CHECK(model.facesContainingPoint({{5, 5, 11}}, tol).empty());
    CHECK(model.solidsContainingPoint({{5, 5, 12}}, tol) == cover->solidIds);
    CHECK(model.solidsContainingPoint({{5, 5, 5}}, tol) == base->solidIds);
    CHECK(model.solidsContainingPoint({{50, 5, 5}}, tol).empty());

    model.tessellate(0.1);
    CHECK(!model.getRenderMeshes().empty());
    int maxFace = -1;
    for (const auto& sub : model.getRenderMeshes()) maxFace = std::max(maxFace, sub.sourceFaceId);
    CHECK(maxFace >= 0);
    std::cout << "occ_structure_test passed\n";
    return 0;
}
