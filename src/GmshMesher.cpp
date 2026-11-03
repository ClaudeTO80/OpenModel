#include <OpenModel/GmshMesher.hpp>

std::string GmshMesher::getName() const  {
 return "Gmsh Mesher"; 
}

const std::string& GmshMesher::lastError() const noexcept {
 return m_lastError; 
}

bool GmshMesher::buildMesh(const std::shared_ptr<CADModel>& cadModel, const MeshingOptions& options, MeshData& outMesh)  {

        m_lastError.clear();
        m_diagnostics.clear();
        if (!cadModel) return fail("CAD model pointer is null");
        if (cadModel->getFilePath().empty()) return fail("CAD file path is empty");
        const double baseSize = options.maxElementSize > 0.0 ? options.maxElementSize : options.elementSize;
        if (!std::isfinite(options.elementSize) || options.elementSize <= 0.0)
            return fail("elementSize must be finite and greater than zero");
        if (!std::isfinite(baseSize) || baseSize <= 0.0)
            return fail("maxElementSize (or elementSize fallback) must be finite and positive");
        if (!std::isfinite(options.minElementSize) || options.minElementSize < 0.0 ||
            (options.minElementSize > 0.0 && options.minElementSize > baseSize))
            return fail("minElementSize must be zero or positive and no greater than maxElementSize");
        if (!std::isfinite(options.curvaturePointsPerCircle) || options.curvaturePointsPerCircle < 3.0)
            return fail("curvaturePointsPerCircle must be at least 3");
        if (options.order != 1 && options.order != 2)
            return fail("Only first- and second-order elements are supported");

        std::lock_guard<std::mutex> lock(gmshMutex());
        try {
            gmsh::initialize();
            GmshSession session;

            gmsh::option::setNumber("General.Terminal", 0);
            // A zero minimum means "let Gmsh adapt down to its backend default".
            // Setting the minimum equal to the maximum unintentionally disables
            // local refinement and curvature-driven sizing for the common case.
            gmsh::option::setNumber("Mesh.MeshSizeMin",
                options.minElementSize > 0.0 ? options.minElementSize : 0.0);
            gmsh::option::setNumber("Mesh.MeshSizeMax", baseSize);
            gmsh::option::setNumber("Mesh.MeshSizeFromPoints", 1.0);
            gmsh::option::setNumber("Mesh.MeshSizeFromCurvature",
                options.curvatureAdaptation ? options.curvaturePointsPerCircle : 0.0);
            gmsh::option::setNumber("Mesh.ElementOrder", options.order);

            gmsh::model::add("OpenModelModel");
            std::vector<std::pair<int, int>> importedEntities;
            gmsh::model::occ::importShapes(cadModel->getFilePath(), importedEntities, true);
            gmsh::model::occ::synchronize();

            const int meshDimension = (options.dimension == MeshDimension::Volume && options.generateTetrahedra) ? 3 : 2;

            if (options.interfaceMode == InterfaceMode::Conformal) {
                std::vector<std::pair<int, int>> solids;
                gmsh::model::getEntities(solids, 3);
                if (meshDimension != 3) {
                    m_diagnostics.push_back("Conformal interfaces apply to volume meshes only; ignored for a surface mesh.");
                } else if (solids.size() < 2) {
                    m_diagnostics.push_back("Conformal interfaces requested, but the CAD contains a single solid: nothing to fragment.");
                } else {
                    // Fragment makes touching/overlapping solids share their common faces, so the surface mesh of
                    // an interface is generated once and both sides use the same nodes.
                    std::vector<std::pair<int, int>> objects(solids.begin(), solids.begin() + 1);
                    std::vector<std::pair<int, int>> tools(solids.begin() + 1, solids.end());
                    std::vector<std::pair<int, int>> fragments;
                    std::vector<std::vector<std::pair<int, int>>> fragmentMap;
                    gmsh::model::occ::fragment(objects, tools, fragments, fragmentMap);
                    gmsh::model::occ::synchronize();
                    std::vector<std::pair<int, int>> after;
                    gmsh::model::getEntities(after, 3);
                    m_diagnostics.push_back("Conformal interfaces: " + std::to_string(solids.size()) + " solids fragmented into " +
                                            std::to_string(after.size()) + " volumes.");
                }
            }
            std::vector<std::pair<int, int>> entities;
            gmsh::model::getEntities(entities, meshDimension);
            if (entities.empty()) {
                return fail(meshDimension == 3
                    ? "CAD file contains no 3D volume entities"
                    : "CAD file contains no 2D surface entities");
            }

            gmsh::model::mesh::generate(meshDimension);

            MeshData candidate;
            std::vector<std::size_t> nodeTags;
            std::vector<double> nodeCoordinates;
            std::vector<double> parametricCoordinates;
            gmsh::model::mesh::getNodes(nodeTags, nodeCoordinates, parametricCoordinates);
            if (nodeCoordinates.size() != nodeTags.size() * 3)
                throw std::runtime_error("Gmsh returned an inconsistent node-coordinate array");
            if (nodeTags.empty())
                throw std::runtime_error("Gmsh generated no nodes");

            std::unordered_map<std::size_t, NodeIndex> nodeTagToIndex;
            nodeTagToIndex.reserve(nodeTags.size());
            for (std::size_t i = 0; i < nodeTags.size(); ++i) {
                const auto inserted = nodeTagToIndex.emplace(
                    nodeTags[i], candidate.addNode(static_cast<NodeId>(nodeTags[i]),
                        nodeCoordinates[3 * i], nodeCoordinates[3 * i + 1], nodeCoordinates[3 * i + 2]));
                if (!inserted.second)
                    throw std::runtime_error("Gmsh returned a duplicate node tag");
            }

            // Extract every mesh dimension up to the requested top dimension.
            // This is required to retain boundary elements and physical groups.
            std::map<std::pair<int, int>, std::vector<ElementId>> elementIdsByEntity;
            std::size_t extractedTopElements = 0;
            std::size_t extractedElements = 0;
            // Keep top-dimensional elements and their codimension-one boundary
            // elements. Point elements and 3-D curve elements are not part of
            // this adapter's current FEM transfer contract.
            const int minimumExtractedDimension = (options.importCadGroups && meshDimension == 3) ? 0 : meshDimension - 1;
            for (int dim = minimumExtractedDimension; dim <= meshDimension; ++dim) {
                std::vector<std::pair<int, int>> dimensionEntities;
                gmsh::model::getEntities(dimensionEntities, dim);
                for (const auto& entity : dimensionEntities) {
                    std::vector<int> elementTypes;
                    std::vector<std::vector<std::size_t>> elementTags;
                    std::vector<std::vector<std::size_t>> elementNodeTags;
                    gmsh::model::mesh::getElements(elementTypes, elementTags, elementNodeTags,
                                                   dim, entity.second);
                    for (std::size_t t = 0; t < elementTypes.size(); ++t) {
                        const auto& tags = elementTags[t];
                        const auto& nodes = elementNodeTags[t];
                        if (tags.empty()) continue;
                        if (nodes.size() % tags.size() != 0)
                            throw std::runtime_error("Gmsh element connectivity has an inconsistent length");
                        const std::size_t nodesPerElement = nodes.size() / tags.size();
                        const ElementType type = mapElementType(elementTypes[t]);
                        if (type == ElementType::Unknown || elementNodeCount(type) != nodesPerElement) {
                            throw std::runtime_error("Unsupported Gmsh element type " +
                                                     std::to_string(elementTypes[t]) + " (" +
                                                     std::to_string(nodesPerElement) + " nodes) in dimension " +
                                                     std::to_string(dim));
                        }
                        const char* prefix = dim == 3 ? "Volume_" : dim == 2 ? "Surface_" :
                                             dim == 1 ? "Curve_" : "Point_";
                        const std::string blockName = std::string(prefix) +
                            std::to_string(entity.second) + "_type_" + std::to_string(elementTypes[t]);
                        auto& entityIds = elementIdsByEntity[{dim, entity.second}];
                        for (std::size_t e = 0; e < tags.size(); ++e) {
                            std::vector<NodeIndex> connectivity;
                            connectivity.reserve(nodesPerElement);
                            for (std::size_t n = 0; n < nodesPerElement; ++n) {
                                const auto found = nodeTagToIndex.find(nodes[e * nodesPerElement + n]);
                                if (found == nodeTagToIndex.end())
                                    throw std::runtime_error("Element refers to an unknown Gmsh node tag");
                                connectivity.push_back(found->second);
                            }
                            const auto elementId = static_cast<ElementId>(tags[e]);
                            candidate.addElement(blockName, type, elementId, connectivity);
                            entityIds.push_back(elementId);
                            ++extractedElements;
                            if (dim == meshDimension) ++extractedTopElements;
                        }
                    }
                }
            }
            if (extractedTopElements == 0)
                throw std::runtime_error("Gmsh generated no elements in the requested dimension");
            if (extractedElements == 0)
                throw std::runtime_error("Gmsh generated no elements in the requested dimensions");

            // Physical groups are dimension-scoped in Gmsh. MeshData requires
            // globally unique names, so disambiguate only when source names collide.
            std::set<std::string> usedPhysicalNames;
            std::vector<std::pair<int, int>> physicalGroups;
            gmsh::model::getPhysicalGroups(physicalGroups);
            for (const auto& physical : physicalGroups) {
                const int dim = physical.first;
                const int tag = physical.second;
                if (dim < minimumExtractedDimension || dim > meshDimension) continue;
                std::string name;
                gmsh::model::getPhysicalName(dim, tag, name);
                if (name.empty()) name = "PhysicalGroup_" + std::to_string(dim) + "_" + std::to_string(tag);
                if (!usedPhysicalNames.insert(name).second) {
                    name += "_dim" + std::to_string(dim) + "_tag" + std::to_string(tag);
                    if (!usedPhysicalNames.insert(name).second)
                        throw std::runtime_error("Could not create a unique name for Gmsh physical group");
                }
                std::vector<int> groupEntities;
                gmsh::model::getEntitiesForPhysicalGroup(dim, tag, groupEntities);
                std::vector<ElementId> groupElementIds;
                for (const int entityTag : groupEntities) {
                    const auto found = elementIdsByEntity.find({dim, entityTag});
                    if (found != elementIdsByEntity.end()) {
                        groupElementIds.insert(groupElementIds.end(),
                                               found->second.begin(), found->second.end());
                    }
                }
                candidate.addPhysicalGroup(name, dim, tag, groupElementIds);
            }

            if (options.importCadGroups && meshDimension == 3)
                mapCadGroups(*cadModel, candidate, nodeTagToIndex, elementIdsByEntity, usedPhysicalNames,
                             options.additionalFaceGroups);

            const auto structuralErrors = candidate.validate();
            if (!structuralErrors.empty())
                throw std::runtime_error("Extracted mesh failed structural validation: " + structuralErrors.front());

            // Keep the caller's mesh intact unless a complete, structurally valid result exists.
            outMesh = std::move(candidate);
            return true;
        } catch (const std::exception& e) {
            return fail(e.what());
        } catch (...) {
            return fail("Unknown non-standard exception raised by Gmsh");
        }
    
}

GmshMesher::GmshSession::~GmshSession() noexcept {

            try { gmsh::finalize(); } catch (...) { /* no exceptions from a destructor */ }
        
}

std::mutex& GmshMesher::gmshMutex() {

        static std::mutex mutex;
        return mutex;
    
}

bool GmshMesher::fail(const std::string& message) {

        m_lastError = message;
        return false;
    
}

ElementType GmshMesher::mapElementType(int gmshType) noexcept {

        switch (gmshType) {
        case 15: return ElementType::Point1;
        case 1: return ElementType::Line2;
        case 8: return ElementType::Line3;
        case 2: return ElementType::Triangle3;
        case 9: return ElementType::Triangle6;
        case 3: return ElementType::Quadrilateral4;
        case 16: return ElementType::Quadrilateral8;
        case 10: return ElementType::Quadrilateral9;
        case 4: return ElementType::Tetrahedron4;
        case 11: return ElementType::Tetrahedron10;
        case 5: return ElementType::Hexahedron8;
        case 17: return ElementType::Hexahedron20;
        case 12: return ElementType::Hexahedron27;
        case 6: return ElementType::Prism6;
        case 18: return ElementType::Prism15;
        case 7: return ElementType::Pyramid5;
        default: return ElementType::Unknown;
        }
    
}

void GmshMesher::mapCadGroups(const CADModel& cad, MeshData& mesh,
                              const std::unordered_map<std::size_t, NodeIndex>& nodeTagToIndex,
                              const std::map<std::pair<int, int>, std::vector<ElementId>>& elementIdsByEntity,
                              std::set<std::string>& usedPhysicalNames,
                              const std::vector<CADGroup>& additionalFaceGroups) {
    const auto& components = cad.getComponents();
    const auto& cadGroups = cad.getGroups();
    if (components.empty() && cadGroups.empty() && additionalFaceGroups.empty()) {
        m_diagnostics.push_back("The CAD model carries no assembly structure or groups: no CAD groups were created.");
        return;
    }

    double xmin, ymin, zmin, xmax, ymax, zmax;
    gmsh::model::getBoundingBox(-1, -1, xmin, ymin, zmin, xmax, ymax, zmax);
    const double diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) + (zmax - zmin) * (zmax - zmin));
    const double tolerance = std::max(1e-6 * diagonal, 1e-9);

    auto coordinates = [&](std::size_t nodeTag) {
        return mesh.nodeCoordinates(nodeTagToIndex.at(nodeTag));
    };

    // ---- Volumes -> CAD solids -------------------------------------------------------------
    std::vector<std::pair<int, int>> volumes;
    gmsh::model::getEntities(volumes, 3);
    std::map<int, std::set<int>> solidsOfVolume;
    std::size_t unmatchedVolumes = 0, ambiguousVolumes = 0, overlapVolumes = 0;
    for (const auto& volume : volumes) {
        std::vector<int> types;
        std::vector<std::vector<std::size_t>> tags, nodeTags;
        gmsh::model::mesh::getElements(types, tags, nodeTags, 3, volume.second);
        if (types.empty() || tags[0].empty()) continue;
        std::string typeName;
        int dim = 0, order = 0, numNodes = 0, numPrimary = 0;
        std::vector<double> localCoordinates;
        gmsh::model::mesh::getElementProperties(types[0], typeName, dim, order, numNodes, localCoordinates, numPrimary);
        const std::size_t count = tags[0].size();
        std::set<int> common;
        bool first = true, agree = true;
        for (const std::size_t sample : {std::size_t(0), count / 2, count - 1}) {
            std::array<double, 3> centroid{{0, 0, 0}};
            for (int n = 0; n < numPrimary; ++n) {
                const auto c = coordinates(nodeTags[0][sample * static_cast<std::size_t>(numNodes) + static_cast<std::size_t>(n)]);
                for (int k = 0; k < 3; ++k) centroid[static_cast<std::size_t>(k)] += c[static_cast<std::size_t>(k)] / numPrimary;
            }
            const auto hit = cad.solidsContainingPoint(centroid, tolerance);
            const std::set<int> hits(hit.begin(), hit.end());
            if (first) { common = hits; first = false; continue; }
            if (hits != common) agree = false;
            std::set<int> both;
            std::set_intersection(common.begin(), common.end(), hits.begin(), hits.end(), std::inserter(both, both.begin()));
            if (!both.empty()) common = both;
        }
        if (common.empty()) { ++unmatchedVolumes; continue; }
        if (!agree) ++ambiguousVolumes;
        if (common.size() > 1) ++overlapVolumes;
        solidsOfVolume[volume.second] = common;
    }
    if (unmatchedVolumes)
        m_diagnostics.push_back(std::to_string(unmatchedVolumes) + " Gmsh volume(s) could not be matched to any CAD solid (left out of the CAD groups).");
    if (ambiguousVolumes)
        m_diagnostics.push_back(std::to_string(ambiguousVolumes) + " Gmsh volume(s) gave inconsistent solid matches across sample points.");
    if (overlapVolumes)
        m_diagnostics.push_back(std::to_string(overlapVolumes) + " Gmsh volume(s) lie inside more than one CAD solid (overlapping solids): they belong to several groups.");

    auto nextTag = [&](int dimension) {
        int tag = 0;
        for (const auto& group : mesh.getPhysicalGroups())
            if (group.dimension == dimension) tag = std::max(tag, group.tag);
        return tag + 1;
    };
    auto uniqueName = [&](std::string name, int dimension) {
        if (name.empty()) name = "CAD_group";
        if (usedPhysicalNames.insert(name).second) return name;
        name += "_dim" + std::to_string(dimension);
        for (int n = 2; !usedPhysicalNames.insert(name).second; ++n) name += "_" + std::to_string(n);
        return name;
    };
    auto collect = [&](int dimension, const std::set<int>& entityTags) {
        std::vector<ElementId> ids;
        for (const int entityTag : entityTags) {
            const auto found = elementIdsByEntity.find({dimension, entityTag});
            if (found != elementIdsByEntity.end()) ids.insert(ids.end(), found->second.begin(), found->second.end());
        }
        return ids;
    };

    // Resolve element membership to node membership without making node tags
    // the canonical CAD mapping: a node may belong to several face groups.
    auto collectNodes = [&](const std::vector<ElementId>& elementIds) {
        const std::unordered_set<ElementId> wanted(elementIds.begin(), elementIds.end());
        std::unordered_set<NodeIndex> seen;
        std::vector<NodeIndex> nodes;
        for (const auto& block : mesh.getElementBlocks()) {
            for (std::size_t e = 0; e < block.ids.size(); ++e) {
                if (!wanted.count(block.ids[e])) continue;
                const auto begin = e * block.nodesPerElement;
                for (std::size_t n = 0; n < block.nodesPerElement; ++n) {
                    const auto index = block.connectivity[begin + n];
                    if (seen.insert(index).second) nodes.push_back(index);
                }
            }
        }
        return nodes;
    };

    // Component groups: leaf components only. Sub-assemblies stay implicit in the "A/B/C" path of the name,
    // so groups of one dimension do not overlap each other.
    std::size_t componentGroups = 0;
    for (const auto& component : components) {
        if (component.isAssembly || component.solidIds.empty()) continue;
        const std::set<int> solids(component.solidIds.begin(), component.solidIds.end());
        std::set<int> volumeTags;
        for (const auto& [volumeTag, volumeSolids] : solidsOfVolume)
            for (const int s : volumeSolids)
                if (solids.count(s)) { volumeTags.insert(volumeTag); break; }
        const auto ids = collect(3, volumeTags);
        if (ids.empty()) continue;
        const auto groupName = uniqueName(component.path, 3);
        mesh.addPhysicalGroup(groupName, 3, nextTag(3), ids);
        mesh.addMeshGroup(MeshGroup{groupName, 3, MeshGroupSource::CAD,
                                    std::vector<int>(solids.begin(), solids.end()), ids, collectNodes(ids)});
        ++componentGroups;
    }

    // Layers / named shapes made of solids.
    std::size_t solidGroups = 0, faceGroups = 0;
    for (const auto& group : cadGroups) {
        if (group.dimension != 3) continue;
        const std::set<int> solids(group.ids.begin(), group.ids.end());
        std::set<int> volumeTags;
        for (const auto& [volumeTag, volumeSolids] : solidsOfVolume)
            for (const int s : volumeSolids)
                if (solids.count(s)) { volumeTags.insert(volumeTag); break; }
        const auto ids = collect(3, volumeTags);
        if (ids.empty()) continue;
        const auto groupName = uniqueName(group.name, 3);
        mesh.addPhysicalGroup(groupName, 3, nextTag(3), ids);
        mesh.addMeshGroup(MeshGroup{groupName, 3, MeshGroupSource::CAD,
                                    group.ids, ids, collectNodes(ids)});
        ++solidGroups;
    }

    // ---- Surfaces -> CAD faces -------------------------------------------------------------------
    bool anyFaceGroup = !additionalFaceGroups.empty();
    for (const auto& group : cadGroups) anyFaceGroup = anyFaceGroup || group.dimension == 2;
    if (anyFaceGroup) {
        // Which leaf components own each CAD face, to tell apart coincident faces of different solids.
        std::map<int, std::vector<const CADComponent*>> ownersOfFace;
        for (const auto& component : components)
            if (!component.isAssembly)
                for (const int f : component.faceIds) ownersOfFace[f].push_back(&component);

        std::vector<std::pair<int, int>> surfaces;
        gmsh::model::getEntities(surfaces, 2);
        std::map<int, std::set<int>> facesOfSurface;
        for (const auto& surface : surfaces) {
            std::vector<int> upward, downward;
            gmsh::model::getAdjacencies(2, surface.second, upward, downward);
            std::set<int> adjacentSolids;
            for (const int v : upward) {
                const auto found = solidsOfVolume.find(v);
                if (found != solidsOfVolume.end()) adjacentSolids.insert(found->second.begin(), found->second.end());
            }
            if (adjacentSolids.empty()) continue;

            std::vector<std::size_t> nodeTags;
            std::vector<double> xyz, parametric;
            gmsh::model::mesh::getNodes(nodeTags, xyz, parametric, 2, surface.second, true, false);
            if (nodeTags.empty()) continue;
            // At most 12 evenly spread sample nodes; the faces containing all of them are the candidates.
            const std::size_t samples = std::min<std::size_t>(12, nodeTags.size());
            std::set<int> common;
            bool first = true;
            for (std::size_t k = 0; k < samples && (first || !common.empty()); ++k) {
                const std::size_t i = samples == 1 ? 0 : k * (nodeTags.size() - 1) / (samples - 1);
                const auto hit = cad.facesContainingPoint({{xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]}}, tolerance);
                const std::set<int> hits(hit.begin(), hit.end());
                if (first) { common = hits; first = false; continue; }
                std::set<int> both;
                std::set_intersection(common.begin(), common.end(), hits.begin(), hits.end(), std::inserter(both, both.begin()));
                common = std::move(both);
            }
            for (const int f : common) {
                const auto owners = ownersOfFace.find(f);
                // Some neutral CAD files have no assembly/component metadata. In
                // that case geometric face matching plus adjacency to a CAD solid
                // is the available evidence; otherwise use ownership to disambiguate
                // coincident faces belonging to different component instances.
                if (owners == ownersOfFace.end()) {
                    if (components.empty()) facesOfSurface[surface.second].insert(f);
                    continue;
                }
                bool owned = false;
                for (const CADComponent* component : owners->second)
                    for (const int s : component->solidIds)
                        if (adjacentSolids.count(s)) { owned = true; break; }
                if (owned) facesOfSurface[surface.second].insert(f);
            }
        }
        for (const auto& group : cadGroups) {
            if (group.dimension != 2) continue;
            const std::set<int> faces(group.ids.begin(), group.ids.end());
            std::set<int> surfaceTags;
            for (const auto& [surfaceTag, surfaceFaces] : facesOfSurface)
                for (const int f : surfaceFaces)
                    if (faces.count(f)) { surfaceTags.insert(surfaceTag); break; }
            const auto ids = collect(2, surfaceTags);
            if (ids.empty()) {
                m_diagnostics.push_back("CAD face group '" + group.name + "' matched no mesh surface (not created).");
                continue;
            }
            const auto groupName = uniqueName(group.name, 2);
            mesh.addPhysicalGroup(groupName, 2, nextTag(2), ids);
            mesh.addMeshGroup(MeshGroup{groupName, 2, MeshGroupSource::CAD,
                                        group.ids, ids, collectNodes(ids)});
            ++faceGroups;
        }

        // User-defined groups are strict: do not create a partial physical group.
        // A partial support would silently leave part of a requested constraint free.
        for (const auto& group : additionalFaceGroups) {
            if (group.dimension != 2 || group.ids.empty()) {
                m_diagnostics.push_back("User CAD face group '" + group.name + "' is empty or has an invalid dimension.");
                continue;
            }
            const std::set<int> requestedFaces(group.ids.begin(), group.ids.end());
            std::set<int> mappedFaces;
            std::set<int> surfaceTags;
            for (const auto& [surfaceTag, surfaceFaces] : facesOfSurface) {
                for (const int faceId : surfaceFaces) {
                    if (requestedFaces.count(faceId)) {
                        mappedFaces.insert(faceId);
                        surfaceTags.insert(surfaceTag);
                    }
                }
            }
            if (mappedFaces.size() != requestedFaces.size()) {
                std::string missing;
                for (const int faceId : requestedFaces)
                    if (!mappedFaces.count(faceId))
                        missing += (missing.empty() ? "" : ", ") + std::to_string(faceId);
                m_diagnostics.push_back("User CAD face group '" + group.name +
                    "' was not mapped completely; missing CAD face id(s): " + missing + ". No physical group was created.");
                continue;
            }
            const auto ids = collect(2, surfaceTags);
            if (ids.empty()) {
                m_diagnostics.push_back("User CAD face group '" + group.name + "' matched no boundary elements.");
                continue;
            }
            const auto groupName = uniqueName(group.name, 2);
            mesh.addPhysicalGroup(groupName, 2, nextTag(2), ids);
            mesh.addMeshGroup(MeshGroup{groupName, 2, MeshGroupSource::CAD,
                                        group.ids, ids, collectNodes(ids)});
            ++faceGroups;
        }
    }
    // ---- Curves -> CAD edges ---------------------------------------------------------------
    std::size_t edgeGroups = 0, vertexGroups = 0;
    bool anyEdgeGroup = false;
    for (const auto& group : cadGroups) anyEdgeGroup = anyEdgeGroup || group.dimension == 1;
    if (anyEdgeGroup) {
        std::vector<std::pair<int, int>> curves;
        gmsh::model::getEntities(curves, 1);
        std::map<int, std::set<int>> edgesOfCurve;
        for (const auto& curve : curves) {
            std::vector<std::size_t> tags;
            std::vector<double> xyz, parametric;
            // Use interior curve nodes only. Endpoints are shared by adjacent
            // CAD edges and would make a coarse, unsplit curve ambiguous.
            gmsh::model::mesh::getNodes(tags, xyz, parametric, 1, curve.second, false, false);
            if (tags.empty()) continue;
            std::set<int> common;
            bool first = true;
            for (std::size_t i = 0; i < tags.size(); ++i) {
                const auto hit = cad.edgesContainingPoint({{xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]}}, tolerance);
                const std::set<int> hits(hit.begin(), hit.end());
                if (first) { common = hits; first = false; }
                else {
                    std::set<int> intersection;
                    std::set_intersection(common.begin(), common.end(), hits.begin(), hits.end(),
                                          std::inserter(intersection, intersection.begin()));
                    common = std::move(intersection);
                }
                if (common.empty()) break;
            }
            if (!common.empty()) edgesOfCurve[curve.second] = std::move(common);
        }

        for (const auto& group : cadGroups) {
            if (group.dimension != 1 || group.ids.empty()) continue;
            const std::set<int> requested(group.ids.begin(), group.ids.end());
            std::set<int> mapped;
            std::set<int> curveTags;
            for (const auto& [curveTag, edgeIds] : edgesOfCurve)
                for (const int edgeId : edgeIds)
                    if (requested.count(edgeId)) { mapped.insert(edgeId); curveTags.insert(curveTag); }
            if (mapped.size() != requested.size()) {
                std::string missing;
                for (const int id : requested)
                    if (!mapped.count(id)) missing += (missing.empty() ? "" : ", ") + std::to_string(id);
                m_diagnostics.push_back("CAD edge group '" + group.name +
                    "' could not be mapped completely; missing edge id(s): " + missing + ".");
                continue;
            }
            const auto ids = collect(1, curveTags);
            if (ids.empty()) {
                m_diagnostics.push_back("CAD edge group '" + group.name + "' matched no 1D mesh elements.");
                continue;
            }
            const auto groupName = uniqueName(group.name, 1);
            mesh.addPhysicalGroup(groupName, 1, nextTag(1), ids);
            const auto edgeNodes = collectNodes(ids);
            mesh.addNodeSet(groupName + "_NODES", edgeNodes);
            mesh.addMeshGroup(MeshGroup{groupName, 1, MeshGroupSource::CAD,
                                        group.ids, ids, edgeNodes});
            ++edgeGroups;
        }
    }

    // ---- CAD vertices -> FEM nodes ----------------------------------------------------------
    bool anyVertexGroup = false;
    for (const auto& group : cadGroups) anyVertexGroup = anyVertexGroup || group.dimension == 0;
    if (anyVertexGroup) {
        std::map<int, std::vector<NodeIndex>> nodesOfVertex;
        for (const auto& [nodeTag, nodeIndex] : nodeTagToIndex) {
            const auto p = mesh.nodeCoordinates(nodeIndex);
            const auto hits = cad.verticesContainingPoint(p, tolerance);
            for (const int vertexId : hits) nodesOfVertex[vertexId].push_back(nodeIndex);
        }
        for (const auto& group : cadGroups) {
            if (group.dimension != 0 || group.ids.empty()) continue;
            const std::set<int> requested(group.ids.begin(), group.ids.end());
            std::vector<NodeIndex> nodes;
            std::vector<int> missing;
            for (const int vertexId : requested) {
                const auto found = nodesOfVertex.find(vertexId);
                if (found == nodesOfVertex.end() || found->second.empty()) missing.push_back(vertexId);
                else nodes.insert(nodes.end(), found->second.begin(), found->second.end());
            }
            if (!missing.empty()) {
                std::string message;
                for (const int id : missing) message += (message.empty() ? "" : ", ") + std::to_string(id);
                m_diagnostics.push_back("CAD vertex group '" + group.name +
                    "' could not be mapped completely; missing vertex id(s): " + message + ".");
                continue;
            }
            std::sort(nodes.begin(), nodes.end());
            nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
            const auto groupName = uniqueName(group.name, 0);
            mesh.addNodeSet(groupName + "_NODES", nodes);
            mesh.addMeshGroup(MeshGroup{groupName, 0, MeshGroupSource::CAD,
                                        group.ids, {}, std::move(nodes)});
            ++vertexGroups;
        }
    }

    m_diagnostics.push_back("CAD groups created: " + std::to_string(componentGroups) + " components, " +
                            std::to_string(solidGroups) + " solid groups, " + std::to_string(faceGroups) + " face groups, " + std::to_string(edgeGroups) + " edge groups and " + std::to_string(vertexGroups) + " vertex groups.");
}
