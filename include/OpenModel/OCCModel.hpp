#pragma once

#include <OpenModel/CADModel.hpp>

#include <BRepCheck_Analyzer.hxx>
#include <Bnd_Box.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TDF_Label.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <IGESControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <Standard_Failure.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Open CASCADE Technology adapter. Keeps the native B-Rep shape separate from
// the render triangulation; this class does not perform volume meshing.
class OCCModel final : public CADModel {
private:
    std::string m_filepath;
    std::string m_lastError;
    TopoDS_Shape m_rawShape;
    std::vector<RenderSubMesh> m_renderMeshes;
    std::vector<CADComponent> m_components;
    std::vector<CADGroup> m_groups;
    TopTools_IndexedMapOfShape m_solidMap; ///< Unique solids of m_rawShape; id = index - 1.
    TopTools_IndexedMapOfShape m_faceMap;  ///< Unique faces of m_rawShape; id = index - 1.
    TopTools_IndexedMapOfShape m_edgeMap;  ///< Unique edges of m_rawShape; id = index - 1.
    TopTools_IndexedMapOfShape m_vertexMap;///< Unique vertices of m_rawShape; id = index - 1.
    std::vector<Bnd_Box> m_solidBoxes;
    std::vector<Bnd_Box> m_faceBoxes;
    /** Per-face projector onto the underlying surface (null when the face has no surface); used by the fast point-on-face test. */
    std::vector<opencascade::handle<ShapeAnalysis_Surface>> m_faceProjectors;
    std::vector<Bnd_Box> m_edgeBoxes;
    std::vector<Bnd_Box> m_vertexBoxes;

    /** @brief Fill unique solid/face/edge/vertex maps and their bounding boxes from m_rawShape. */
    void indexShape();

    /**
     * @brief Read a STEP/IGES file through the XDE (XCAF) reader to keep names, colours, layers and the assembly tree.
     * @param path   File to read.
     * @param iges   True for IGES, false for STEP.
     * @param shape  Receives the assembled, located shape on success.
     * @return True when the document was transferred and contains at least one free shape.
     */
    bool loadWithStructure(const std::string& path, bool iges, TopoDS_Shape& shape);

    static std::string lowercaseExtension(const std::string& path) ;

    static bool finitePoint(const gp_Pnt& p) ;

    static void appendNormal(RenderSubMesh& mesh, const gp_Vec& input) ;

public:
    OCCModel() = default;
    ~OCCModel() override = default;

    bool loadFromFile(const std::string& filepath) override ;

    void tessellate(double tolerance = 0.1) override ;

    void tessellate(double tolerance, double angularTolerance) override ;

    const std::vector<RenderSubMesh>& getRenderMeshes() const override ;
    std::string getFilePath() const override ;
    const std::string& getLastError() const override ;
    const TopoDS_Shape& getRawShape() const ;
    const std::vector<CADComponent>& getComponents() const override;
    const std::vector<CADGroup>& getGroups() const override;
    std::vector<int> solidsContainingPoint(const std::array<double, 3>& point, double tolerance) const override;
    std::vector<int> facesContainingPoint(const std::array<double, 3>& point, double tolerance) const override;
    std::vector<int> edgesContainingPoint(const std::array<double, 3>& point, double tolerance) const override;
    std::vector<int> verticesContainingPoint(const std::array<double, 3>& point, double tolerance) const override;
    bool hasShape() const ;
};
