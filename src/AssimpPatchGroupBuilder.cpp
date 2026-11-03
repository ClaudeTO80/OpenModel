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
std::string blockName(std::size_t index, const std::string& name) {
    std::ostringstream out;
    out << "ASSIMP_SURFACE_" << index;
    if (!name.empty()) out << "_" << name;
    return out.str();
}
double dot3(const std::array<double,3>& a,const std::array<double,3>& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
double norm3(const std::array<double,3>& a) { return std::sqrt(dot3(a,a)); }
bool finite3(const std::array<double,3>& a) {
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
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
double triangleArea2(const RenderSubMesh& part, std::size_t triangle) {
    const auto ia=static_cast<std::size_t>(part.indices[3*triangle]);
    const auto ib=static_cast<std::size_t>(part.indices[3*triangle+1]);
    const auto ic=static_cast<std::size_t>(part.indices[3*triangle+2]);
    const auto coord=[&](std::size_t i,std::size_t a){return part.vertices[3*i+a];};
    const double abx=coord(ib,0)-coord(ia,0), aby=coord(ib,1)-coord(ia,1), abz=coord(ib,2)-coord(ia,2);
    const double acx=coord(ic,0)-coord(ia,0), acy=coord(ic,1)-coord(ia,1), acz=coord(ic,2)-coord(ia,2);
    const double nx=aby*acz-abz*acy, ny=abz*acx-abx*acz, nz=abx*acy-aby*acx;
    return std::sqrt(nx*nx+ny*ny+nz*nz);
}
}

bool AssimpPatchGroupBuilder::apply(const std::vector<RenderSubMesh>& parts,
    const std::vector<AssimpSurfacePatch>& patches,
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
                if (!finite3(direction) || norm3(direction)<=1e-14) {
                    diagnostics.push_back("Invalid normal direction in patch filter '"+definition.name+"'.");
                    return false;
                }
                const double n=norm3(direction);
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
        resolvedPatchIndices.push_back(std::move(selected));
    }

    // Build source-triangle -> generated ElementId maps. The importer preserves
    // source triangle order and skips only degenerate triangles.
    std::vector<std::vector<ElementId>> sourceElementIds(parts.size());
    for (std::size_t pi=0;pi<parts.size();++pi) {
        const auto& part=parts[pi];
        if (part.vertices.empty() || part.vertices.size()%3 || part.indices.empty() || part.indices.size()%3) {
            diagnostics.push_back("Malformed source part "+std::to_string(pi)+".");
            return false;
        }
        const auto blockIt=std::find_if(mesh.getElementBlocks().begin(),mesh.getElementBlocks().end(),
            [&](const ElementBlock& block){return block.name==blockName(pi,part.name);});
        if (blockIt==mesh.getElementBlocks().end() || blockIt->type!=ElementType::Triangle3) {
            diagnostics.push_back("Missing Triangle3 element block for Assimp part "+std::to_string(pi)+".");
            return false;
        }
        sourceElementIds[pi].assign(part.indices.size()/3,0);
        std::size_t blockElement=0;
        for (std::size_t ti=0;ti<part.indices.size()/3;++ti) {
            for (std::size_t k=0;k<3;++k)
                if (part.indices[3*ti+k]>=part.vertices.size()/3) {
                    diagnostics.push_back("Invalid source triangle index in part "+std::to_string(pi)+".");
                    return false;
                }
            const double twiceArea=triangleArea2(part,ti);
            if (!std::isfinite(twiceArea) || twiceArea<=1e-18) continue;
            if (blockElement>=blockIt->ids.size()) {
                diagnostics.push_back("Source triangle count does not match imported element block.");
                return false;
            }
            sourceElementIds[pi][ti]=blockIt->ids[blockElement++];
        }
        if (blockElement!=blockIt->ids.size()) {
            diagnostics.push_back("Imported element block contains elements not accounted for by source triangles.");
            return false;
        }
    }

    std::unordered_map<ElementId,std::vector<NodeIndex>> connectivity;
    for (const auto& block:mesh.getElementBlocks()) {
        if (block.nodesPerElement==0 || block.connectivity.size()!=block.ids.size()*block.nodesPerElement) {
            diagnostics.push_back("Malformed element connectivity in imported mesh.");
            return false;
        }
        for (std::size_t i=0;i<block.ids.size();++i) {
            const auto begin=block.connectivity.begin()+static_cast<std::ptrdiff_t>(i*block.nodesPerElement);
            connectivity.emplace(block.ids[i],std::vector<NodeIndex>(begin,begin+static_cast<std::ptrdiff_t>(block.nodesPerElement)));
        }
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
                if (triangle>=sourceElementIds[patch.partIndex].size() ||
                    sourceElementIds[patch.partIndex][triangle]==0) {
                    diagnostics.push_back("Patch refers to a missing or degenerate imported triangle.");
                    return false;
                }
                elements.insert(sourceElementIds[patch.partIndex][triangle]);
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
            nodes.insert(found->second.begin(),found->second.end());
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
