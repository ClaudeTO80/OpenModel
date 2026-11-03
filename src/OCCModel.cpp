#include <OpenModel/OCCModel.hpp>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <IGESCAFControl_Reader.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <StepData_StepModel.hxx>
#include <StepShape_AdvancedFace.hxx>
#include <TCollection_AsciiString.hxx>
#include <TCollection_HAsciiString.hxx>
#include <TransferBRep.hxx>
#include <Transfer_TransientProcess.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>
#include <TCollection_ExtendedString.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDF_Tool.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFApp_Application.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ColorType.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_LayerTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <map>
#include <set>

namespace {

std::string labelName(const TDF_Label& label) {
    if (label.IsNull()) return {};
    Handle(TDataStd_Name) name;
    if (!label.FindAttribute(TDataStd_Name::GetID(), name)) return {};
    return std::string(TCollection_AsciiString(name->Get()).ToCString());
}

std::string labelKey(const TDF_Label& label) {
    TCollection_AsciiString entry;
    TDF_Tool::Entry(label, entry);
    return std::string(entry.ToCString());
}

/** One visited node of the XDE assembly tree together with its located shape. */
struct WalkNode {
    CADComponent component;
    TopoDS_Shape shape;
};

/** Places where one XDE part label is instantiated, and the label itself. */
struct PartInstances {
    TDF_Label label;
    std::vector<TopLoc_Location> locations;
};

struct Walker {
    Handle(XCAFDoc_ShapeTool) shapes;
    Handle(XCAFDoc_ColorTool) colors;
    std::vector<WalkNode> nodes;
    std::map<std::string, PartInstances> parts;

    bool colorOf(const TDF_Label& label, std::array<float, 3>& rgb) const {
        if (label.IsNull()) return false;
        Quantity_Color c;
        if (!colors->GetColor(label, XCAFDoc_ColorSurf, c) && !colors->GetColor(label, XCAFDoc_ColorGen, c)) return false;
        Standard_Real r = 0, g = 0, b = 0;
        c.Values(r, g, b, Quantity_TOC_sRGB);
        rgb = {{static_cast<float>(r), static_cast<float>(g), static_cast<float>(b)}};
        return true;
    }

    TopoDS_Shape visit(const TDF_Label& part, const TDF_Label& instance, const TopLoc_Location& location, int parent) {
        const int index = static_cast<int>(nodes.size());
        nodes.emplace_back();
        {
            CADComponent& c = nodes[static_cast<std::size_t>(index)].component;
            c.id = index;
            c.parent = parent;
            // Instance names written by OCCT itself look like "=>[0:1:1:1]": use the product name instead.
            c.name = labelName(instance);
            if (c.name.empty() || c.name.rfind("=>[", 0) == 0) c.name = labelName(part);
            c.isAssembly = shapes->IsAssembly(part);
            c.hasColor = colorOf(instance, c.color) || colorOf(part, c.color);
            std::string leaf = c.name.empty() ? "Component_" + std::to_string(index) : c.name;
            // Several instances of one part under the same parent get distinct paths: Name, Name #2, ...
            int duplicates = 0;
            for (int other = 0; other < index; ++other) {
                const CADComponent& o = nodes[static_cast<std::size_t>(other)].component;
                if (o.parent == parent && o.name == c.name) ++duplicates;
            }
            if (duplicates > 0) leaf += " #" + std::to_string(duplicates + 1);
            c.path = parent >= 0 ? nodes[static_cast<std::size_t>(parent)].component.path + "/" + leaf : leaf;
        }
        auto& entry = parts[labelKey(part)];
        entry.label = part;
        entry.locations.push_back(location);

        TopoDS_Shape result;
        if (shapes->IsAssembly(part)) {
            BRep_Builder builder;
            TopoDS_Compound compound;
            builder.MakeCompound(compound);
            TDF_LabelSequence children;
            shapes->GetComponents(part, children);
            for (Standard_Integer i = 1; i <= children.Length(); ++i) {
                const TDF_Label reference = children.Value(i);
                TDF_Label referred;
                if (!shapes->GetReferredShape(reference, referred)) referred = reference;
                const TopoDS_Shape sub = visit(referred, reference, location * XCAFDoc_ShapeTool::GetLocation(reference), index);
                if (!sub.IsNull()) builder.Add(compound, sub);
            }
            result = compound;
        } else {
            const TopoDS_Shape local = XCAFDoc_ShapeTool::GetShape(part);
            if (!local.IsNull()) result = local.Moved(location);
        }
        nodes[static_cast<std::size_t>(index)].shape = result;
        return result;
    }
};

} // namespace

std::string OCCModel::lowercaseExtension(const std::string& path) {

        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos) return {};
        std::string ext = path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    
}

bool OCCModel::finitePoint(const gp_Pnt& p) {

        return std::isfinite(p.X()) && std::isfinite(p.Y()) && std::isfinite(p.Z());
    
}

void OCCModel::appendNormal(RenderSubMesh& mesh, const gp_Vec& input) {

        gp_Vec n = input;
        if (!std::isfinite(n.X()) || !std::isfinite(n.Y()) || !std::isfinite(n.Z()) ||
            n.SquareMagnitude() <= std::numeric_limits<double>::epsilon()) {
            mesh.normals.insert(mesh.normals.end(), {0.0f, 0.0f, 0.0f});
            return;
        }
        n.Normalize();
        mesh.normals.push_back(static_cast<float>(n.X()));
        mesh.normals.push_back(static_cast<float>(n.Y()));
        mesh.normals.push_back(static_cast<float>(n.Z()));
    
}

bool OCCModel::loadFromFile(const std::string& filepath)  {

        // Transactional load: do not retain a previous shape if a new load fails.
        m_filepath.clear();
        m_lastError.clear();
        m_renderMeshes.clear();
        m_rawShape.Nullify();

        m_components.clear();
        m_groups.clear();
        m_solidMap.Clear();
        m_faceMap.Clear();
        m_solidBoxes.clear();
        m_faceBoxes.clear();

        const auto ext = lowercaseExtension(filepath);
        TopoDS_Shape loaded;
        IFSelect_ReturnStatus status = IFSelect_RetError;

        // First choice: the XDE reader, which keeps product names, colours, layers and the assembly tree.
        // If it fails for any reason, fall back to the plain geometry reader below.
        const bool isStep = ext == ".step" || ext == ".stp";
        const bool isIges = ext == ".iges" || ext == ".igs";
        bool structured = false;
        if (isStep || isIges) {
            try {
                structured = loadWithStructure(filepath, isIges, loaded);
            } catch (const Standard_Failure&) {
                structured = false;
            }
            if (structured) status = IFSelect_RetDone;
            else { m_components.clear(); m_groups.clear(); loaded.Nullify(); }
        }

        if (structured) {
            // loaded already holds the assembled shape.
        } else if (ext == ".step" || ext == ".stp") {
            STEPControl_Reader reader;
            status = reader.ReadFile(filepath.c_str());
            if (status == IFSelect_RetDone) {
                const Standard_Integer transferred = reader.TransferRoots();
                if (transferred > 0) loaded = reader.OneShape();
                else m_lastError = "STEP file was read, but no roots could be transferred.";
            }
        } else if (ext == ".iges" || ext == ".igs") {
            IGESControl_Reader reader;
            status = reader.ReadFile(filepath.c_str());
            if (status == IFSelect_RetDone) {
                const Standard_Integer transferred = reader.TransferRoots();
                if (transferred > 0) loaded = reader.OneShape();
                else m_lastError = "IGES file was read, but no roots could be transferred.";
            }
        } else {
            m_lastError = "Unsupported CAD format. Supported extensions: .step, .stp, .iges, .igs.";
            return false;
        }

        if (status != IFSelect_RetDone) {
            if (m_lastError.empty()) m_lastError = "Open CASCADE failed to read the CAD file.";
            return false;
        }
        if (loaded.IsNull()) {
            if (m_lastError.empty()) m_lastError = "CAD import produced an empty shape.";
            return false;
        }
        if (!BRepCheck_Analyzer(loaded, Standard_True).IsValid()) {
            m_lastError = "Imported B-Rep failed Open CASCADE shape validation.";
            m_components.clear();
            m_groups.clear();
            m_rawShape.Nullify();
            m_solidMap.Clear();
            m_faceMap.Clear();
            m_solidBoxes.clear();
            m_faceBoxes.clear();
            return false;
        }

        m_rawShape = loaded;
        m_filepath = filepath;
        if (!structured) { m_components.clear(); m_groups.clear(); }
        indexShape();
        return true;
    
}

void OCCModel::tessellate(double tolerance)  {

        tessellate(tolerance, 0.5);
    
}

void OCCModel::tessellate(double tolerance, double angularTolerance)  {

        m_renderMeshes.clear();
        m_lastError.clear();

        if (m_rawShape.IsNull()) {
            m_lastError = "Cannot tessellate: no valid CAD shape has been loaded.";
            return;
        }
        if (!std::isfinite(tolerance) || tolerance <= 0.0) {
            m_lastError = "Tessellation tolerance must be a finite positive value.";
            return;
        }
        if (!std::isfinite(angularTolerance) || angularTolerance <= 0.0 || angularTolerance > 3.141592653589793) {
            m_lastError = "Angular tessellation tolerance must be in (0, pi].";
            return;
        }

        try {
            BRepMesh_IncrementalMesh mesher(m_rawShape, tolerance, Standard_False, angularTolerance, Standard_True);
            if (!mesher.IsDone()) {
                m_lastError = "Open CASCADE tessellation did not complete.";
                return;
            }

            // Faces are enumerated once through the unique face map, so a face shared by two solids is
            // tessellated once. A face is attributed to the first solid that owns it.
            std::vector<int> facePartIds(static_cast<std::size_t>(m_faceMap.Extent()), -1);
            for (Standard_Integer si = 1; si <= m_solidMap.Extent(); ++si) {
                for (TopExp_Explorer fx(m_solidMap(si), TopAbs_FACE); fx.More(); fx.Next()) {
                    const Standard_Integer faceIndex = m_faceMap.FindIndex(fx.Current());
                    if (faceIndex > 0 && facePartIds[static_cast<std::size_t>(faceIndex - 1)] < 0)
                        facePartIds[static_cast<std::size_t>(faceIndex - 1)] = si - 1;
                }
            }

            for (Standard_Integer faceId = 0; faceId < m_faceMap.Extent(); ++faceId) {
                const TopoDS_Face face = TopoDS::Face(m_faceMap(faceId + 1));
                TopLoc_Location location;
                const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
                if (!triangulation.IsNull()) {
                    RenderSubMesh sub;
                    sub.sourceFaceId = faceId;
                    sub.sourcePartId = facePartIds[static_cast<std::size_t>(faceId)];
                    sub.name = (sub.sourcePartId >= 0 ? "Solid_" + std::to_string(sub.sourcePartId) : "Part_0") +
                               "_Face_" + std::to_string(faceId);

                    const Standard_Integer nodeCount = triangulation->NbNodes();
                    const Standard_Integer triangleCount = triangulation->NbTriangles();
                    sub.vertices.reserve(static_cast<std::size_t>(nodeCount) * 3);
                    sub.normals.reserve(static_cast<std::size_t>(nodeCount) * 3);
                    if (triangulation->HasUVNodes()) sub.texcoords.reserve(static_cast<std::size_t>(nodeCount) * 2);
                    sub.indices.reserve(static_cast<std::size_t>(triangleCount) * 3);

                    const gp_Trsf transform = location.Transformation();
                    for (Standard_Integer ni = 1; ni <= nodeCount; ++ni) {
                        gp_Pnt p = triangulation->Node(ni);
                        p.Transform(transform);
                        if (!finitePoint(p)) {
                            m_lastError = "Tessellation produced a non-finite vertex on face " + std::to_string(faceId) + ".";
                            m_renderMeshes.clear();
                            return;
                        }
                        sub.vertices.push_back(static_cast<float>(p.X()));
                        sub.vertices.push_back(static_cast<float>(p.Y()));
                        sub.vertices.push_back(static_cast<float>(p.Z()));
                        if (triangulation->HasUVNodes()) {
                            const auto uv = triangulation->UVNode(ni);
                            sub.texcoords.push_back(static_cast<float>(uv.X()));
                            sub.texcoords.push_back(static_cast<float>(uv.Y()));
                        }
                    }

                    const bool reversed = face.Orientation() == TopAbs_REVERSED;
                    // Prefer OCCT's triangulation normals when available; otherwise
                    // accumulate geometric triangle normals per vertex.
                    if (triangulation->HasNormals()) {
                        for (Standard_Integer ni = 1; ni <= nodeCount; ++ni) {
                            gp_Vec normal(triangulation->Normal(ni));
                            normal.Transform(transform);
                            if (reversed) normal.Reverse();
                            appendNormal(sub, normal);
                        }
                    } else {
                        std::vector<gp_Vec> accumulated(static_cast<std::size_t>(nodeCount), gp_Vec(0, 0, 0));
                        for (Standard_Integer ti = 1; ti <= triangleCount; ++ti) {
                            Standard_Integer a, b, c;
                            triangulation->Triangle(ti).Get(a, b, c);
                            const auto pointAt = [&](Standard_Integer index) {
                                gp_Pnt p = triangulation->Node(index); p.Transform(transform); return p;
                            };
                            const gp_Pnt pa = pointAt(a), pb = pointAt(b), pc = pointAt(c);
                            gp_Vec normal(gp_Vec(pa, pb).Crossed(gp_Vec(pa, pc)));
                            if (reversed) normal.Reverse();
                            if (normal.SquareMagnitude() > std::numeric_limits<double>::epsilon()) {
                                accumulated[static_cast<std::size_t>(a - 1)] += normal;
                                accumulated[static_cast<std::size_t>(b - 1)] += normal;
                                accumulated[static_cast<std::size_t>(c - 1)] += normal;
                            }
                        }
                        for (const auto& normal : accumulated) appendNormal(sub, normal);
                    }

                    for (Standard_Integer ti = 1; ti <= triangleCount; ++ti) {
                        Standard_Integer a, b, c;
                        triangulation->Triangle(ti).Get(a, b, c);
                        if (reversed) std::swap(b, c);
                        sub.indices.push_back(static_cast<unsigned int>(a - 1));
                        sub.indices.push_back(static_cast<unsigned int>(b - 1));
                        sub.indices.push_back(static_cast<unsigned int>(c - 1));
                    }
                    m_renderMeshes.push_back(std::move(sub));
                }
            }
            if (m_renderMeshes.empty()) m_lastError = "No triangulated faces were produced by Open CASCADE.";
        } catch (const Standard_Failure& failure) {
            m_renderMeshes.clear();
            m_lastError = std::string("Open CASCADE tessellation failed: ") + failure.GetMessageString();
        } catch (const std::exception& failure) {
            m_renderMeshes.clear();
            m_lastError = std::string("Tessellation failed: ") + failure.what();
        }
    
}

const std::vector<RenderSubMesh>& OCCModel::getRenderMeshes() const  {
 return m_renderMeshes; 
}

std::string OCCModel::getFilePath() const  {
 return m_filepath; 
}

const std::string& OCCModel::getLastError() const  {
 return m_lastError; 
}

const TopoDS_Shape& OCCModel::getRawShape() const {
 return m_rawShape; 
}

bool OCCModel::hasShape() const {
 return !m_rawShape.IsNull(); 
}

const std::vector<CADComponent>& OCCModel::getComponents() const { return m_components; }

const std::vector<CADGroup>& OCCModel::getGroups() const { return m_groups; }

void OCCModel::indexShape() {
    m_solidMap.Clear();
    m_faceMap.Clear();
    m_edgeMap.Clear();
    m_vertexMap.Clear();
    m_solidBoxes.clear();
    m_faceBoxes.clear();
    m_edgeBoxes.clear();
    m_vertexBoxes.clear();
    if (m_rawShape.IsNull()) return;
    TopExp::MapShapes(m_rawShape, TopAbs_SOLID, m_solidMap);
    TopExp::MapShapes(m_rawShape, TopAbs_FACE, m_faceMap);
    TopExp::MapShapes(m_rawShape, TopAbs_EDGE, m_edgeMap);
    TopExp::MapShapes(m_rawShape, TopAbs_VERTEX, m_vertexMap);
    // Geometry-based boxes (no triangulation) so they stay conservative supersets of the true extent.
    for (Standard_Integer i = 1; i <= m_solidMap.Extent(); ++i) {
        Bnd_Box box;
        BRepBndLib::Add(m_solidMap(i), box, Standard_False);
        m_solidBoxes.push_back(box);
    }
    for (Standard_Integer i = 1; i <= m_faceMap.Extent(); ++i) {
        Bnd_Box box;
        BRepBndLib::Add(m_faceMap(i), box, Standard_False);
        m_faceBoxes.push_back(box);
    }
    for (Standard_Integer i = 1; i <= m_edgeMap.Extent(); ++i) {
        Bnd_Box box;
        BRepBndLib::Add(m_edgeMap(i), box, Standard_False);
        m_edgeBoxes.push_back(box);
    }
    for (Standard_Integer i = 1; i <= m_vertexMap.Extent(); ++i) {
        Bnd_Box box;
        BRepBndLib::Add(m_vertexMap(i), box, Standard_False);
        m_vertexBoxes.push_back(box);
    }
}

std::vector<int> OCCModel::solidsContainingPoint(const std::array<double, 3>& point, double tolerance) const {
    std::vector<int> result;
    const gp_Pnt p(point[0], point[1], point[2]);
    for (Standard_Integer i = 1; i <= m_solidMap.Extent(); ++i) {
        Bnd_Box box = m_solidBoxes[static_cast<std::size_t>(i - 1)];
        box.Enlarge(tolerance);
        if (box.IsOut(p)) continue;
        BRepClass3d_SolidClassifier classifier(TopoDS::Solid(m_solidMap(i)), p, tolerance);
        if (classifier.State() == TopAbs_IN) result.push_back(i - 1);
    }
    return result;
}

std::vector<int> OCCModel::facesContainingPoint(const std::array<double, 3>& point, double tolerance) const {
    std::vector<int> result;
    const gp_Pnt p(point[0], point[1], point[2]);
    const TopoDS_Vertex vertex = BRepBuilderAPI_MakeVertex(p);
    for (Standard_Integer i = 1; i <= m_faceMap.Extent(); ++i) {
        Bnd_Box box = m_faceBoxes[static_cast<std::size_t>(i - 1)];
        box.Enlarge(tolerance);
        if (box.IsOut(p)) continue;
        BRepExtrema_DistShapeShape distance(vertex, m_faceMap(i));
        if (distance.IsDone() && distance.Value() <= tolerance) result.push_back(i - 1);
    }
    return result;
}

std::vector<int> OCCModel::edgesContainingPoint(const std::array<double, 3>& point, double tolerance) const {
    std::vector<int> result;
    if (!std::isfinite(tolerance) || tolerance < 0.0) return result;
    const gp_Pnt p(point[0], point[1], point[2]);
    const TopoDS_Vertex query = BRepBuilderAPI_MakeVertex(p);
    for (Standard_Integer i = 1; i <= m_edgeMap.Extent(); ++i) {
        Bnd_Box box = m_edgeBoxes[static_cast<std::size_t>(i - 1)];
        box.Enlarge(tolerance);
        if (box.IsOut(p)) continue;
        BRepExtrema_DistShapeShape distance(query, m_edgeMap(i));
        if (distance.IsDone() && distance.Value() <= tolerance) result.push_back(i - 1);
    }
    return result;
}

std::vector<int> OCCModel::verticesContainingPoint(const std::array<double, 3>& point, double tolerance) const {
    std::vector<int> result;
    if (!std::isfinite(tolerance) || tolerance < 0.0) return result;
    const gp_Pnt p(point[0], point[1], point[2]);
    for (Standard_Integer i = 1; i <= m_vertexMap.Extent(); ++i) {
        Bnd_Box box = m_vertexBoxes[static_cast<std::size_t>(i - 1)];
        box.Enlarge(tolerance);
        if (box.IsOut(p)) continue;
        const gp_Pnt vertexPoint = BRep_Tool::Pnt(TopoDS::Vertex(m_vertexMap(i)));
        if (vertexPoint.Distance(p) <= tolerance) result.push_back(i - 1);
    }
    return result;
}

bool OCCModel::loadWithStructure(const std::string& path, bool iges, TopoDS_Shape& shape) {
    Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
    Handle(TDocStd_Document) doc;
    app->NewDocument("MDTV-XCAF", doc);
    struct Closer {
        Handle(XCAFApp_Application) application;
        Handle(TDocStd_Document) document;
        ~Closer() { try { application->Close(document); } catch (...) {} }
    } closer{app, doc};

    std::vector<std::pair<std::string, TopoDS_Shape>> stepFaceNames;
    if (iges) {
        IGESCAFControl_Reader reader;
        reader.SetColorMode(Standard_True);
        reader.SetNameMode(Standard_True);
        reader.SetLayerMode(Standard_True);
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone || !reader.Transfer(doc)) return false;
    } else {
        STEPCAFControl_Reader reader;
        reader.SetColorMode(Standard_True);
        reader.SetNameMode(Standard_True);
        reader.SetLayerMode(Standard_True);
        if (reader.ReadFile(path.c_str()) != IFSelect_RetDone || !reader.Transfer(doc)) return false;
        // XDE keeps face names only for SHAPE_ASPECT sub-shapes. Many exporters write the name directly on
        // the ADVANCED_FACE entity, so read those names from the STEP model and bind them to the B-Rep faces.
        const Handle(StepData_StepModel) stepModel = reader.Reader().StepModel();
        const Handle(Transfer_TransientProcess) process = reader.Reader().WS()->TransferReader()->TransientProcess();
        if (!stepModel.IsNull() && !process.IsNull()) {
            for (Standard_Integer i = 1; i <= stepModel->NbEntities(); ++i) {
                const Handle(StepShape_AdvancedFace) face = Handle(StepShape_AdvancedFace)::DownCast(stepModel->Entity(i));
                if (face.IsNull() || face->Name().IsNull() || face->Name()->Length() == 0) continue;
                const TopoDS_Shape bound = TransferBRep::ShapeResult(process, face);
                if (!bound.IsNull() && bound.ShapeType() == TopAbs_FACE)
                    stepFaceNames.emplace_back(std::string(face->Name()->ToCString()), bound);
            }
        }
    }

    Walker walker;
    walker.shapes = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    walker.colors = XCAFDoc_DocumentTool::ColorTool(doc->Main());
    const Handle(XCAFDoc_LayerTool) layers = XCAFDoc_DocumentTool::LayerTool(doc->Main());

    TDF_LabelSequence freeShapes;
    walker.shapes->GetFreeShapes(freeShapes);
    BRep_Builder builder;
    TopoDS_Compound root;
    builder.MakeCompound(root);
    int rootCount = 0;
    for (Standard_Integer i = 1; i <= freeShapes.Length(); ++i) {
        const TopoDS_Shape sub = walker.visit(freeShapes.Value(i), TDF_Label(), TopLoc_Location(), -1);
        if (!sub.IsNull()) { builder.Add(root, sub); ++rootCount; }
    }
    if (rootCount == 0) return false;
    shape = root;

    // The id spaces (solid/face) are defined by the unique maps of the final shape.
    m_rawShape = shape;
    indexShape();

    for (auto& node : walker.nodes) {
        std::set<int> solidIds, faceIds;
        for (TopExp_Explorer x(node.shape, TopAbs_SOLID); x.More(); x.Next()) {
            const Standard_Integer index = m_solidMap.FindIndex(x.Current());
            if (index > 0) solidIds.insert(index - 1);
        }
        for (TopExp_Explorer x(node.shape, TopAbs_FACE); x.More(); x.Next()) {
            const Standard_Integer index = m_faceMap.FindIndex(x.Current());
            if (index > 0) faceIds.insert(index - 1);
        }
        node.component.solidIds.assign(solidIds.begin(), solidIds.end());
        node.component.faceIds.assign(faceIds.begin(), faceIds.end());
        m_components.push_back(node.component);
    }

    // Shapes of a layer / named sub-shape, placed in every instance of their part.
    auto locatedShapes = [&](const TDF_Label& label) {
        std::vector<TopoDS_Shape> located;
        auto found = walker.parts.find(labelKey(label));
        if (found == walker.parts.end() && !label.Father().IsNull()) found = walker.parts.find(labelKey(label.Father()));
        if (found == walker.parts.end()) return located;
        const TopoDS_Shape local = XCAFDoc_ShapeTool::GetShape(label);
        if (local.IsNull()) return located;
        for (const auto& location : found->second.locations) located.push_back(local.Moved(location));
        return located;
    };

    struct EntityIds {
        std::set<int> solids, faces, edges, vertices;
    };
    // Keep the highest-dimensional entities in a named shape. A solid layer
    // remains a solid group; a wire becomes an edge group; a point becomes a
    // vertex group. This avoids expanding every solid layer into all its edges.
    auto classify = [&](const TopoDS_Shape& shapeIn, EntityIds& ids) {
        for (TopExp_Explorer x(shapeIn, TopAbs_SOLID); x.More(); x.Next()) {
            const Standard_Integer index = m_solidMap.FindIndex(x.Current());
            if (index > 0) ids.solids.insert(index - 1);
        }
        if (!ids.solids.empty()) return;
        for (TopExp_Explorer x(shapeIn, TopAbs_FACE); x.More(); x.Next()) {
            const Standard_Integer index = m_faceMap.FindIndex(x.Current());
            if (index > 0) ids.faces.insert(index - 1);
        }
        if (!ids.faces.empty()) return;
        for (TopExp_Explorer x(shapeIn, TopAbs_EDGE); x.More(); x.Next()) {
            const Standard_Integer index = m_edgeMap.FindIndex(x.Current());
            if (index > 0) ids.edges.insert(index - 1);
        }
        if (!ids.edges.empty()) return;
        for (TopExp_Explorer x(shapeIn, TopAbs_VERTEX); x.More(); x.Next()) {
            const Standard_Integer index = m_vertexMap.FindIndex(x.Current());
            if (index > 0) ids.vertices.insert(index - 1);
        }
    };
    auto pushGroup = [&](const std::string& name, CADGroupKind kind, const EntityIds& ids) {
        auto add = [&](const std::string& groupName, int dimension, const std::set<int>& values) {
            if (values.empty()) return;
            CADGroup group;
            group.name = groupName;
            group.kind = kind;
            group.dimension = dimension;
            group.ids.assign(values.begin(), values.end());
            m_groups.push_back(std::move(group));
        };
        add(name, 3, ids.solids);
        add(name + " (faces)", 2, ids.faces);
        add(name + " (edges)", 1, ids.edges);
        add(name + " (vertices)", 0, ids.vertices);
    };

    TDF_LabelSequence layerLabels;
    layers->GetLayerLabels(layerLabels);
    for (Standard_Integer i = 1; i <= layerLabels.Length(); ++i) {
        TCollection_ExtendedString layerName;
        layers->GetLayer(layerLabels.Value(i), layerName);
        std::string name = std::string(TCollection_AsciiString(layerName).ToCString());
        if (name.empty()) name = "Layer_" + std::to_string(i);
        TDF_LabelSequence members;
        layers->GetShapesOfLayer(layerLabels.Value(i), members);
        EntityIds ids;
        for (Standard_Integer m = 1; m <= members.Length(); ++m)
            for (const auto& located : locatedShapes(members.Value(m))) classify(located, ids);
        pushGroup(name, CADGroupKind::Layer, ids);
    }

    // Named sub-shapes: faces, wires/edges and vertices are retained at their
    // own topological dimension. Repeated names across instances are merged.
    std::map<std::string, EntityIds> named;
    for (const auto& entry : walker.parts) {
        TDF_LabelSequence subLabels;
        walker.shapes->GetSubShapes(entry.second.label, subLabels);
        for (Standard_Integer i = 1; i <= subLabels.Length(); ++i) {
            const std::string name = labelName(subLabels.Value(i));
            if (name.empty()) continue;
            auto& target = named[name];
            for (const auto& located : locatedShapes(subLabels.Value(i))) classify(located, target);
        }
    }
    // Faces named on the ADVANCED_FACE entity: find them inside each non-assembly part and place every instance.
    if (!stepFaceNames.empty()) {
        for (const auto& entry : walker.parts) {
            if (walker.shapes->IsAssembly(entry.second.label)) continue;
            const TopoDS_Shape local = XCAFDoc_ShapeTool::GetShape(entry.second.label);
            if (local.IsNull()) continue;
            for (TopExp_Explorer x(local, TopAbs_FACE); x.More(); x.Next()) {
                for (const auto& [faceName, bound] : stepFaceNames) {
                    if (!x.Current().IsPartner(bound)) continue;
                    auto& target = named[faceName];
                    for (const auto& location : entry.second.locations)
                        classify(x.Current().Moved(location), target);
                }
            }
        }
    }
    for (const auto& item : named) pushGroup(item.first, CADGroupKind::NamedShape, item.second);
    return true;
}
