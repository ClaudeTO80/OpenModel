#pragma once
/**
 * @file assembly_fixture.hpp
 * @brief Test fixture: an XDE assembly written to STEP.
 *
 * Parts: Base (10x10x10 box at the origin, red), Cover (10x10x5 box on top of it, blue) and Pin (cylinder
 * r=1, h=5) instantiated twice on the top face of the Cover. Base and Cover touch on z = 10; the top face
 * of the Base is on the layer "Interface" and is named "TopFace" on its ADVANCED_FACE entity.
 */
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
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Trsf.hxx>

#include <fstream>
#include <regex>
#include <sstream>
#include <string>

/** @brief Write the fixture assembly to @p path. @return false (with a message on stderr) on failure. */
inline bool writeAssemblyFixture(const std::string& path) {
#define FIXTURE_CHECK(cond) do { if (!(cond)) return false; } while (0)
    {
        Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
        Handle(TDocStd_Document) doc;
        app->NewDocument("MDTV-XCAF", doc);
        const auto shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        const auto colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
        const auto layers = XCAFDoc_DocumentTool::LayerTool(doc->Main());

        BRepPrimAPI_MakeBox baseMaker(10, 10, 10);
        const TDF_Label base = shapes->AddShape(baseMaker.Shape(), Standard_False);
        TDataStd_Name::Set(base, "Base");
        colors->SetColor(base, Quantity_Color(1.0, 0.0, 0.0, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);
        // The top face of the base (z = 10) sits on the layer "Interface". Its name is written into the
        // STEP text below, the way exporters that name ADVANCED_FACE entities do.
        const TDF_Label topFace = shapes->AddSubShape(base, baseMaker.TopFace());
        layers->SetLayer(topFace, TCollection_ExtendedString("Interface"));

        // Lower-dimensional named entities exercise edge and vertex group import.
        TopExp_Explorer edgeExplorer(baseMaker.Shape(), TopAbs_EDGE);
        FIXTURE_CHECK(edgeExplorer.More());
        const TDF_Label namedEdge = shapes->AddSubShape(base, edgeExplorer.Current());
        TDataStd_Name::Set(namedEdge, "BaseEdge");
        layers->SetLayer(namedEdge, TCollection_ExtendedString("EdgeLayer"));
        TopExp_Explorer vertexExplorer(edgeExplorer.Current(), TopAbs_VERTEX);
        FIXTURE_CHECK(vertexExplorer.More());
        const TDF_Label namedVertex = shapes->AddSubShape(base, vertexExplorer.Current());
        TDataStd_Name::Set(namedVertex, "BaseVertex");
        layers->SetLayer(namedVertex, TCollection_ExtendedString("VertexLayer"));

        const TDF_Label cover = shapes->AddShape(BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 10), 10, 10, 5).Shape(), Standard_False);
        TDataStd_Name::Set(cover, "Cover");
        colors->SetColor(cover, Quantity_Color(0.0, 0.0, 1.0, Quantity_TOC_sRGB), XCAFDoc_ColorSurf);

        const TDF_Label pin = shapes->AddShape(BRepPrimAPI_MakeCylinder(1.0, 5.0).Shape(), Standard_False);
        TDataStd_Name::Set(pin, "Pin");

        const TDF_Label assembly = shapes->NewShape();
        TDataStd_Name::Set(assembly, "Assy");
        shapes->AddComponent(assembly, base, TopLoc_Location());
        shapes->AddComponent(assembly, cover, TopLoc_Location());
        gp_Trsf t1, t2;
        t1.SetTranslation(gp_Vec(2, 2, 15));
        t2.SetTranslation(gp_Vec(8, 8, 15));
        shapes->AddComponent(assembly, pin, TopLoc_Location(t1));
        shapes->AddComponent(assembly, pin, TopLoc_Location(t2));
        shapes->UpdateAssemblies();

        STEPCAFControl_Writer writer;
        writer.SetColorMode(Standard_True);
        writer.SetNameMode(Standard_True);
        writer.SetLayerMode(Standard_True);
        FIXTURE_CHECK(writer.Transfer(doc, STEPControl_AsIs));
        FIXTURE_CHECK(writer.Write(path.c_str()) == IFSelect_RetDone);
        app->Close(doc);
    }
    {
        // Name the face that carries the layer on its ADVANCED_FACE entity.
        std::ifstream in(path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        std::smatch layerMatch;
        FIXTURE_CHECK(std::regex_search(text, layerMatch, std::regex("PRESENTATION_LAYER_ASSIGNMENT\\('Interface','visible',\\(#(\\d+)\\)\\)")));
        const std::regex faceRegex("(#" + layerMatch[1].str() + " = ADVANCED_FACE\\()'[^']*'");
        FIXTURE_CHECK(std::regex_search(text, faceRegex));
        text = std::regex_replace(text, faceRegex, "$1'TopFace'");

        // STEPCAFControl does not consistently emit XDE names for edges.
        // Name the EDGE_CURVE referenced by the EdgeLayer assignment so the
        // OCCModel STEP-entity fallback can be tested deterministically.
        std::smatch edgeLayerMatch;
        FIXTURE_CHECK(std::regex_search(text, edgeLayerMatch,
            std::regex("PRESENTATION_LAYER_ASSIGNMENT\\('EdgeLayer','visible',\\(#(\\d+)\\)\\)")));
        const std::regex edgeRegex("(#" + edgeLayerMatch[1].str() + " = EDGE_CURVE\\()'[^']*'");
        FIXTURE_CHECK(std::regex_search(text, edgeRegex));
        text = std::regex_replace(text, edgeRegex, "$1'BaseEdge'");

        std::ofstream out(path, std::ios::trunc);
        out << text;
    }

    return true;
#undef FIXTURE_CHECK
}
