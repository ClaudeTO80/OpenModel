#include <OpenModel/AssimpPatchGroupBuilder.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {
double dot3(const std::array<double,3>& a,const std::array<double,3>& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
double norm3(const std::array<double,3>& a) { return std::hypot(a[0],a[1],a[2]); }
bool finite3(const std::array<double,3>& a) {
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}
std::array<double,3> subtract3(const std::array<double,3>& a,
                                  const std::array<double,3>& b) {
    return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};
}
std::array<double,3> cross3(const std::array<double,3>& a,
                            const std::array<double,3>& b) {
    return {{a[1]*b[2]-a[2]*b[1],
             a[2]*b[0]-a[0]*b[2],
             a[0]*b[1]-a[1]*b[0]}};
}
bool validPatch(const AssimpSurfacePatch& patch) {
    if (!std::isfinite(patch.area) || patch.area<=0.0 ||
        !finite3(patch.centroid) || !finite3(patch.meanNormal) ||
        !finite3(patch.boundsMin) || !finite3(patch.boundsMax) ||
        std::isnan(patch.maxPlanarityDeviation) || patch.maxPlanarityDeviation<0.0 ||
        std::isnan(patch.relativePlanarityDeviation) || patch.relativePlanarityDeviation<0.0 ||
        !std::isfinite(patch.normalCoherence) || patch.normalCoherence<0.0 ||
        patch.normalCoherence>1.0 ||
        (patch.kind!=AssimpSurfacePatchKind::Planar &&
         patch.kind!=AssimpSurfacePatchKind::NonPlanar)) return false;
    for (std::size_t axis=0;axis<3;++axis)
        if (patch.boundsMin[axis]>patch.boundsMax[axis]) return false;
    return !patch.triangleIndices.empty();
}
bool matchesFilter(const AssimpSurfacePatch& patch,const AssimpPatchFilter& filter,
                   const std::array<double,3>& unitDirection) {
    if (filter.planarOnly && patch.kind!=AssimpSurfacePatchKind::Planar) return false;
    if (patch.area<filter.minArea || patch.area>filter.maxArea ||
        patch.maxPlanarityDeviation>filter.maxPlanarityDeviation ||
        patch.normalCoherence<filter.minNormalCoherence) return false;
    if (filter.constrainCentroid)
        for (std::size_t axis=0;axis<3;++axis)
            if (patch.centroid[axis]<filter.centroidMin[axis] ||
                patch.centroid[axis]>filter.centroidMax[axis]) return false;
    if (filter.constrainNormal &&
        std::abs(dot3(patch.meanNormal,unitDirection))+1e-12<filter.minNormalAlignment)
        return false;
    return true;
}
} // namespace

bool AssimpPatchGroupBuilder::apply(const std::vector<RenderSubMesh>& parts,
    const std::vector<AssimpSurfacePatch>& patches,
    const std::vector<std::vector<ElementId>>& sourceTriangleElementIds,
    const std::vector<AssimpPatchGroupDefinition>& definitions,
    MeshData& mesh, std::vector<std::string>& diagnostics) {
    diagnostics.clear();
    if (mesh.isEmpty() || parts.empty()) {
        diagnostics.push_back("A populated Assimp surface mesh and source parts are required.");
        return false;
    }

    std::unordered_set<std::string> existingNames;
    for (const auto& group:mesh.getMeshGroupRegistry().groups()) existingNames.insert(group.name);
    for (const auto& group:mesh.getPhysicalGroups()) existingNames.insert(group.name);
    for (const auto& item:mesh.getNodeSets()) existingNames.insert(item.first);
    std::unordered_set<std::string> reservedNames;
    std::vector<std::vector<std::size_t>> resolvedPatchIndices;
    resolvedPatchIndices.reserve(definitions.size());
    for (const auto& definition:definitions) {
        if (definition.name.empty() || existingNames.count(definition.name) ||
            existingNames.count(definition.name+"_NODES") ||
            !reservedNames.insert(definition.name).second ||
            !reservedNames.insert(definition.name+"_NODES").second ||
            (definition.selectByFilter && !definition.patchIndices.empty()) ||
            (!definition.selectByFilter && definition.patchIndices.empty())) {
            diagnostics.push_back("Invalid, empty, or conflicting patch group name/definition: '"+definition.name+"'.");
            return false;
        }
        std::vector<std::size_t> selected;
        if (definition.selectByFilter) {
            const auto& filter=definition.filter;
            if (!std::isfinite(filter.minArea) || filter.minArea<0.0 ||
                std::isnan(filter.maxArea) || filter.maxArea<filter.minArea ||
                std::isnan(filter.maxPlanarityDeviation) || filter.maxPlanarityDeviation<0.0 ||
                !std::isfinite(filter.minNormalCoherence) || filter.minNormalCoherence<0.0 ||
                filter.minNormalCoherence>1.0 ||
                !std::isfinite(filter.minNormalAlignment) || filter.minNormalAlignment<0.0 ||
                filter.minNormalAlignment>1.0) {
                diagnostics.push_back("Invalid geometric patch filter for group '"+definition.name+"'.");
                return false;
            }
            std::array<double,3> direction=filter.direction;
            if (filter.constrainNormal) {
                if (!finite3(direction) || norm3(direction)==0.0) {
                    diagnostics.push_back("Invalid normal direction in patch filter '"+definition.name+"'.");
                    return false;
                }
                const double n=norm3(direction);
                if (!std::isfinite(n) || n<=0.0) {
                    diagnostics.push_back("Patch filter normal direction is outside the supported numeric range for group '"+definition.name+"'.");
                    return false;
                }
                for (double& value:direction) value/=n;
            }
            if (filter.constrainCentroid) {
                if (!finite3(filter.centroidMin) || !finite3(filter.centroidMax) ||
                    filter.centroidMin[0]>filter.centroidMax[0] ||
                    filter.centroidMin[1]>filter.centroidMax[1] ||
                    filter.centroidMin[2]>filter.centroidMax[2]) {
                    diagnostics.push_back("Invalid centroid bounds in patch filter '"+definition.name+"'.");
                    return false;
                }
            }
            for (std::size_t i=0;i<patches.size();++i)
                if (matchesFilter(patches[i],filter,direction)) selected.push_back(i);
        } else {
            selected=definition.patchIndices;
            for (const auto index:selected)
                if (index>=patches.size()) {
                    diagnostics.push_back("Patch index outside analysis result for group '"+definition.name+"'.");
                    return false;
                }
        }
        if (selected.empty()) {
            diagnostics.push_back("Patch filter for group '"+definition.name+"' matched no patches.");
            return false;
        }
        for (const auto index:selected) {
            if (!validPatch(patches[index]) || patches[index].partIndex>=parts.size()) {
                diagnostics.push_back("Patch analysis contains invalid geometry or source-part metadata.");
                return false;
            }
            std::unordered_set<std::size_t> uniqueTriangles;
            for (const auto triangle:patches[index].triangleIndices) {
                if (triangle>=parts[patches[index].partIndex].indices.size()/3 ||
                    !uniqueTriangles.insert(triangle).second) {
                    diagnostics.push_back("Patch contains an invalid or duplicate source triangle index.");
                    return false;
                }
            }
        }
        resolvedPatchIndices.push_back(std::move(selected));
    }

    if (sourceTriangleElementIds.size()!=parts.size()) {
        diagnostics.push_back("Source triangle-to-element mapping does not match the Assimp parts.");
        return false;
    }
    for (std::size_t pi=0;pi<parts.size();++pi) {
        if (parts[pi].vertices.size()%3!=0 || parts[pi].indices.size()%3!=0 ||
            sourceTriangleElementIds[pi].size()!=parts[pi].indices.size()/3) {
            diagnostics.push_back("Source triangle mapping has an invalid size for part "+std::to_string(pi)+".");
            return false;
        }
        const std::size_t vertexCount=parts[pi].vertices.size()/3;
        for (std::size_t vi=0;vi<vertexCount;++vi) {
            const std::array<double,3> point{{parts[pi].vertices[3*vi],
                parts[pi].vertices[3*vi+1],parts[pi].vertices[3*vi+2]}};
            if (!finite3(point)) {
                diagnostics.push_back("Source part "+std::to_string(pi)+" contains a non-finite vertex.");
                return false;
            }
        }
        for (std::size_t ti=0;ti<parts[pi].indices.size()/3;++ti) {
            const auto ia=static_cast<std::size_t>(parts[pi].indices[3*ti]);
            const auto ib=static_cast<std::size_t>(parts[pi].indices[3*ti+1]);
            const auto ic=static_cast<std::size_t>(parts[pi].indices[3*ti+2]);
            if (ia>=vertexCount || ib>=vertexCount || ic>=vertexCount) {
                diagnostics.push_back("Source part "+std::to_string(pi)+" contains an out-of-range triangle index.");
                return false;
            }
            const auto point=[&](std::size_t vi) {
                return std::array<double,3>{{parts[pi].vertices[3*vi],
                    parts[pi].vertices[3*vi+1],parts[pi].vertices[3*vi+2]}};
            };
            const auto a=point(ia),b=point(ib),d=point(ic);
            const auto ab=subtract3(b,a),ac=subtract3(d,a),bc=subtract3(d,b);
            const double maxEdgeSquared=std::max({dot3(ab,ab),dot3(ac,ac),dot3(bc,bc)});
            const auto raw=cross3(ab,ac);
            const double twiceArea=norm3(raw);
            const double threshold=std::numeric_limits<double>::epsilon()*maxEdgeSquared*64.0;
            const bool degenerate=!std::isfinite(twiceArea)||!std::isfinite(maxEdgeSquared)||
                maxEdgeSquared<=0.0||twiceArea<=threshold;
            const ElementId mapped=sourceTriangleElementIds[pi][ti];
            if (degenerate ? mapped!=0 : mapped==0) {
                diagnostics.push_back("Source triangle map does not agree with scale-relative degeneracy classification.");
                return false;
            }
        }
    }

    // Resolve only genuine Triangle3 elements. A corrupt or stale source map
    // must never silently attach a patch to an unrelated element that happens
    // to reuse the same numeric ID in another block.
    std::unordered_map<ElementId,std::vector<NodeIndex>> connectivity;
    std::unordered_set<ElementId> triangleElementIds;
    for (const auto& block:mesh.getElementBlocks()) {
        if (block.nodesPerElement==0 || block.connectivity.size()!=block.ids.size()*block.nodesPerElement) {
            diagnostics.push_back("Malformed element connectivity in imported mesh.");
            return false;
        }
        for (std::size_t i=0;i<block.ids.size();++i) {
            const auto begin=block.connectivity.begin()+static_cast<std::ptrdiff_t>(i*block.nodesPerElement);
            const auto inserted=connectivity.emplace(block.ids[i],
                std::vector<NodeIndex>(begin,begin+static_cast<std::ptrdiff_t>(block.nodesPerElement)));
            if (!inserted.second) {
                diagnostics.push_back("Mesh contains duplicate element identifiers; source mapping is ambiguous.");
                return false;
            }
            if (block.type==ElementType::Triangle3 && block.nodesPerElement==3)
                triangleElementIds.insert(block.ids[i]);
        }
    }
    std::unordered_set<ElementId> mappedElementIds;
    for (std::size_t pi=0;pi<sourceTriangleElementIds.size();++pi) {
        const auto& partMap=sourceTriangleElementIds[pi];
        for (std::size_t ti=0;ti<partMap.size();++ti) {
            const auto id=partMap[ti];
            if (id==0) continue; // zero is the documented skipped-degenerate sentinel
            if (!triangleElementIds.count(id) || !mappedElementIds.insert(id).second) {
                diagnostics.push_back("Source triangle mapping contains a missing, non-triangle, or duplicate element ID.");
                return false;
            }
            const auto found=connectivity.find(id);
            if (found==connectivity.end() || found->second.size()!=3) {
                diagnostics.push_back("Source triangle mapping cannot resolve a Triangle3 connectivity record.");
                return false;
            }
            // Verify provenance geometrically. Element IDs are local to a
            // MeshData instance and may be reused by another import; comparing
            // the ordered vertex coordinates prevents a stale map from silently
            // grouping unrelated triangles with the same numeric IDs.
            for (std::size_t k=0;k<3;++k) {
                const auto node=found->second[k];
                if (node>=mesh.getNumNodes()) {
                    diagnostics.push_back("Mapped Triangle3 connectivity references an invalid node.");
                    return false;
                }
                const auto vertex=static_cast<std::size_t>(parts[pi].indices[3*ti+k]);
                const auto actual=mesh.nodeCoordinates(node);
                for (std::size_t axis=0;axis<3;++axis) {
                    const double expected=static_cast<double>(parts[pi].vertices[3*vertex+axis]);
                    if (actual[axis]!=expected) {
                        diagnostics.push_back("Source triangle map belongs to a different geometry or import ordering.");
                        return false;
                    }
                }
            }
        }
    }
    if (mappedElementIds.empty() || mappedElementIds.size()!=triangleElementIds.size()) {
        diagnostics.push_back("Source triangle mapping does not cover every imported Triangle3 element exactly.");
        return false;
    }

    long long nextTag=1;
    for (const auto& group:mesh.getPhysicalGroups())
        if (static_cast<long long>(group.tag)>=nextTag) nextTag=static_cast<long long>(group.tag)+1;
    if (!definitions.empty() && nextTag+static_cast<long long>(definitions.size())-1LL > std::numeric_limits<int>::max()) {
        diagnostics.push_back("Physical group tag range exhausted.");
        return false;
    }
    struct Prepared {
        MeshPhysicalGroup physical;
        MeshGroup group;
        std::vector<NodeIndex> nodes;
        std::string nodeSetName;
    };
    std::vector<Prepared> prepared;
    prepared.reserve(definitions.size());
    for (std::size_t definitionIndex=0;definitionIndex<definitions.size();++definitionIndex) {
        const auto& definition=definitions[definitionIndex];
        std::set<ElementId> elements;
        for (const auto patchIndex:resolvedPatchIndices[definitionIndex]) {
            const auto& patch=patches[patchIndex];
            if (patch.partIndex>=parts.size()) {
                diagnostics.push_back("Patch refers to an unknown Assimp part.");
                return false;
            }
            for (const auto triangle:patch.triangleIndices) {
                if (triangle>=sourceTriangleElementIds[patch.partIndex].size() ||
                    sourceTriangleElementIds[patch.partIndex][triangle]==0) {
                    diagnostics.push_back("Patch refers to a missing or degenerate imported triangle.");
                    return false;
                }
                elements.insert(sourceTriangleElementIds[patch.partIndex][triangle]);
            }
        }
        if (elements.empty()) {
            diagnostics.push_back("Patch group '"+definition.name+"' contains no imported elements.");
            return false;
        }
        std::set<NodeIndex> nodes;
        for (const auto id:elements) {
            const auto found=connectivity.find(id);
            if (found==connectivity.end()) {
                diagnostics.push_back("Cannot resolve connectivity for an imported element.");
                return false;
            }
            for (const auto node:found->second) {
                if (node>=mesh.getNumNodes()) {
                    diagnostics.push_back("Imported element connectivity references an invalid node index.");
                    return false;
                }
                nodes.insert(node);
            }
        }
        Prepared item;
        item.physical.name=definition.name;
        item.physical.dimension=2;
        item.physical.tag=static_cast<int>(nextTag++);
        item.physical.elementIds.assign(elements.begin(),elements.end());
        item.physical.materialName=definition.materialName;
        item.group.name=definition.name;
        item.group.dimension=2;
        item.group.source=MeshGroupSource::Imported;
        item.group.elementIds=item.physical.elementIds;
        item.group.nodeIndices.assign(nodes.begin(),nodes.end());
        item.nodes=item.group.nodeIndices;
        item.nodeSetName=definition.name+"_NODES";
        prepared.push_back(std::move(item));
    }

    for (auto& item:prepared) {
        mesh.addPhysicalGroup(item.physical.name,item.physical.dimension,item.physical.tag,
                              item.physical.elementIds,item.physical.materialName);
        mesh.addNodeSet(item.nodeSetName,item.nodes);
        mesh.addMeshGroup(std::move(item.group));
        diagnostics.push_back("Created patch group '"+item.physical.name+"' with "+
            std::to_string(item.physical.elementIds.size())+" element(s) and "+
            std::to_string(item.nodes.size())+" node(s).");
    }
    return true;
}
