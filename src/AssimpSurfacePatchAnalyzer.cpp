#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace {
using V = std::array<double, 3>;
struct Cell {
    std::int64_t x, y, z;
    bool operator<(const Cell& rhs) const {
        return std::tie(x, y, z) < std::tie(rhs.x, rhs.y, rhs.z);
    }
};
using Edge = std::pair<std::size_t, std::size_t>;

V sub(const V& a, const V& b) { return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}}; }
V add(const V& a, const V& b) { return {{a[0]+b[0],a[1]+b[1],a[2]+b[2]}}; }
V mul(const V& a, double s) { return {{a[0]*s,a[1]*s,a[2]*s}}; }
double dot(const V& a, const V& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
V cross(const V& a, const V& b) {
    return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};
}
double length2(const V& a) { return dot(a,a); }
double length(const V& a) { return std::sqrt(length2(a)); }
bool finite(const V& a) {
    return std::isfinite(a[0]) && std::isfinite(a[1]) && std::isfinite(a[2]);
}
Cell cellOf(const V& p, double tolerance) {
    const double limit = static_cast<double>(std::numeric_limits<std::int64_t>::max()/4);
    Cell c{};
    for (double v : p)
        if (!std::isfinite(v) || std::abs(v/tolerance) > limit)
            throw std::runtime_error("coordinate/tolerance ratio exceeds spatial hash range");
    c.x=static_cast<std::int64_t>(std::floor(p[0]/tolerance));
    c.y=static_cast<std::int64_t>(std::floor(p[1]/tolerance));
    c.z=static_cast<std::int64_t>(std::floor(p[2]/tolerance));
    return c;
}
Edge edge(std::size_t a, std::size_t b) {
    if (b<a) std::swap(a,b);
    return {a,b};
}
struct DSU {
    std::vector<std::size_t> parent, rank;
    explicit DSU(std::size_t n) : parent(n), rank(n,0) {
        std::iota(parent.begin(),parent.end(),0);
    }
    std::size_t find(std::size_t x) {
        while (parent[x]!=x) { parent[x]=parent[parent[x]]; x=parent[x]; }
        return x;
    }
    void join(std::size_t a,std::size_t b) {
        a=find(a); b=find(b); if(a==b) return;
        if(rank[a]<rank[b]) std::swap(a,b);
        parent[b]=a; if(rank[a]==rank[b]) ++rank[a];
    }
};
struct Triangle {
    V p[3];
    V normal;
    double area=0.0;
    std::size_t sourceIndex=0;
};
}

bool AssimpSurfacePatchAnalyzer::analyze(const std::vector<RenderSubMesh>& parts,
    double weldTolerance, std::vector<AssimpSurfacePatch>& patches,
    std::vector<std::string>& diagnostics, double maxDihedralAngleDegrees) {
    patches.clear();
    diagnostics.clear();
    if (!std::isfinite(weldTolerance) || weldTolerance<=0.0) {
        diagnostics.push_back("Weld tolerance must be finite and positive.");
        return false;
    }
    if (!std::isfinite(maxDihedralAngleDegrees) ||
        maxDihedralAngleDegrees<0.0 || maxDihedralAngleDegrees>90.0) {
        diagnostics.push_back("Maximum winding-independent dihedral angle must be in [0, 90] degrees.");
        return false;
    }

    const double tolerance2=weldTolerance*weldTolerance;
    if (!std::isfinite(tolerance2)) {
        diagnostics.push_back("Weld tolerance is too large to square safely.");
        return false;
    }
    const double minNormalDot=std::cos(maxDihedralAngleDegrees*3.14159265358979323846/180.0);
    std::size_t degenerateCount=0, nonManifoldEdges=0;
    try {
        for (std::size_t partIndex=0; partIndex<parts.size(); ++partIndex) {
            const auto& part=parts[partIndex];
            if (part.vertices.empty() || part.vertices.size()%3!=0 ||
                part.indices.empty() || part.indices.size()%3!=0) {
                diagnostics.push_back("Malformed triangle arrays in part "+std::to_string(partIndex)+".");
                return false;
            }
            const std::size_t vertexCount=part.vertices.size()/3;
            std::vector<V> points(vertexCount);
            for (std::size_t i=0;i<vertexCount;++i) {
                points[i]={{part.vertices[3*i],part.vertices[3*i+1],part.vertices[3*i+2]}};
                if (!finite(points[i])) {
                    diagnostics.push_back("Non-finite vertex in part "+std::to_string(partIndex)+".");
                    return false;
                }
            }

            // Spatial welding checks the 27 neighbouring cells and verifies the
            // actual Euclidean distance. This avoids quantization-bin boundary misses.
            DSU vertexSets(vertexCount);
            std::map<Cell,std::vector<std::size_t>> buckets;
            for (std::size_t vi=0;vi<vertexCount;++vi) {
                const Cell cell=cellOf(points[vi],weldTolerance);
                for (int dx=-1;dx<=1;++dx)
                    for (int dy=-1;dy<=1;++dy)
                        for (int dz=-1;dz<=1;++dz) {
                            const Cell neighbour{cell.x+dx,cell.y+dy,cell.z+dz};
                            const auto found=buckets.find(neighbour);
                            if (found==buckets.end()) continue;
                            for (const auto other:found->second)
                                if (length2(sub(points[vi],points[other]))<=tolerance2)
                                    vertexSets.join(vi,other);
                        }
                buckets[cell].push_back(vi);
            }

            std::vector<Triangle> triangles;
            triangles.reserve(part.indices.size()/3);
            std::map<Edge,std::vector<std::size_t>> edgeOwners;
            for (std::size_t ti=0;ti<part.indices.size()/3;++ti) {
                Triangle tr;
                tr.sourceIndex=ti;
                std::size_t welded[3]{};
                for (std::size_t k=0;k<3;++k) {
                    const auto vi=static_cast<std::size_t>(part.indices[3*ti+k]);
                    if (vi>=vertexCount) {
                        diagnostics.push_back("Triangle index outside vertex array in part "+std::to_string(partIndex)+".");
                        return false;
                    }
                    tr.p[k]=points[vi];
                    welded[k]=vertexSets.find(vi);
                }
                const V edge01=sub(tr.p[1],tr.p[0]);
                const V edge02=sub(tr.p[2],tr.p[0]);
                const V edge12=sub(tr.p[2],tr.p[1]);
                const double maxEdgeSquared=std::max({length2(edge01),length2(edge02),length2(edge12)});
                const V raw=cross(edge01,edge02);
                const double twice=length(raw);
                // Compare area against the square of the local edge scale.
                // An absolute area threshold incorrectly discarded valid
                // small-unit meshes (e.g. micron-scale geometry).
                const double degeneracyThreshold=
                    std::numeric_limits<double>::epsilon()*maxEdgeSquared*64.0;
                if (!std::isfinite(twice) || !std::isfinite(maxEdgeSquared) ||
                    maxEdgeSquared<=0.0 || twice<=degeneracyThreshold) {
                    ++degenerateCount;
                    continue;
                }
                tr.area=0.5*twice;
                tr.normal=mul(raw,1.0/twice);
                const std::size_t validIndex=triangles.size();
                triangles.push_back(tr);
                for (std::size_t k=0;k<3;++k)
                    edgeOwners[edge(welded[k],welded[(k+1)%3])].push_back(validIndex);
            }
            if (triangles.empty()) continue;

            DSU triangleSets(triangles.size());
            for (const auto& item:edgeOwners) {
                const auto& owners=item.second;
                if (owners.size()>2) ++nonManifoldEdges;
                for (std::size_t a=0;a<owners.size();++a)
                    for (std::size_t b=a+1;b<owners.size();++b) {
                        const double alignment=std::abs(dot(triangles[owners[a]].normal,
                                                             triangles[owners[b]].normal));
                        if (alignment+1e-12>=minNormalDot)
                            triangleSets.join(owners[a],owners[b]);
                    }
            }

            std::map<std::size_t,std::vector<std::size_t>> components;
            for (std::size_t i=0;i<triangles.size();++i)
                components[triangleSets.find(i)].push_back(i);

            for (const auto& component:components) {
                AssimpSurfacePatch patch;
                patch.partIndex=partIndex;
                patch.boundsMin={{std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),std::numeric_limits<double>::max()}};
                patch.boundsMax={{-std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()}};
                V weighted{{0,0,0}}, normalSum{{0,0,0}};
                const V referenceNormal=triangles[component.second.front()].normal;
                std::map<Edge,std::size_t> localEdges;
                for (const auto ti:component.second) {
                    const auto& tr=triangles[ti];
                    patch.triangleIndices.push_back(tr.sourceIndex);
                    patch.area+=tr.area;
                    const V centroid=mul(add(add(tr.p[0],tr.p[1]),tr.p[2]),1.0/3.0);
                    weighted=add(weighted,mul(centroid,tr.area));
                    // Align normals to a reference hemisphere so inconsistent
                    // source winding does not cancel the patch normal statistics.
                    const V oriented=dot(tr.normal,referenceNormal)<0.0 ? mul(tr.normal,-1.0) : tr.normal;
                    normalSum=add(normalSum,mul(oriented,tr.area));
                    for (const auto& p:tr.p)
                        for (std::size_t axis=0;axis<3;++axis) {
                            patch.boundsMin[axis]=std::min(patch.boundsMin[axis],p[axis]);
                            patch.boundsMax[axis]=std::max(patch.boundsMax[axis],p[axis]);
                        }
                    for (std::size_t k=0;k<3;++k) {
                        const auto ia=static_cast<std::size_t>(part.indices[3*tr.sourceIndex+k]);
                        const auto ib=static_cast<std::size_t>(part.indices[3*tr.sourceIndex+(k+1)%3]);
                        localEdges[edge(vertexSets.find(ia),vertexSets.find(ib))]++;
                    }
                }
                patch.centroid=mul(weighted,1.0/patch.area);
                const double normalLength=length(normalSum);
                patch.normalCoherence=std::max(0.0,std::min(1.0,normalLength/patch.area));
                if (std::isfinite(normalLength) && normalLength>0.0)
                    patch.meanNormal=mul(normalSum,1.0/normalLength);
                for (const auto& e:localEdges) {
                    const auto global=edgeOwners.find(e.first);
                    if (global!=edgeOwners.end() && global->second.size()>2)
                        ++patch.nonManifoldEdgeCount;
                    else if (e.second==1)
                        ++patch.boundaryEdgeCount;
                }
                if (std::isfinite(normalLength) && normalLength>0.0) {
                    for (const auto ti:component.second)
                        for (const auto& p:triangles[ti].p)
                            patch.maxPlanarityDeviation=std::max(patch.maxPlanarityDeviation,
                                std::abs(dot(sub(p,patch.centroid),patch.meanNormal)));
                } else {
                    patch.maxPlanarityDeviation=std::numeric_limits<double>::infinity();
                }
                const double diagonal=length(sub(patch.boundsMax,patch.boundsMin));
                patch.relativePlanarityDeviation=diagonal>0.0
                    ? patch.maxPlanarityDeviation/diagonal
                    : std::numeric_limits<double>::infinity();
                double coordinateScale=diagonal;
                for (std::size_t axis=0;axis<3;++axis)
                    coordinateScale=std::max(coordinateScale,std::max(std::abs(patch.boundsMin[axis]),std::abs(patch.boundsMax[axis])));
                const double limit=std::max(diagonal*1e-6,
                    std::numeric_limits<double>::epsilon()*coordinateScale*64.0);
                patch.kind=(patch.maxPlanarityDeviation<=limit && patch.normalCoherence>=0.999999)
                    ? AssimpSurfacePatchKind::Planar : AssimpSurfacePatchKind::NonPlanar;
                std::sort(patch.triangleIndices.begin(),patch.triangleIndices.end());
                patches.push_back(std::move(patch));
            }
        }
    } catch (const std::exception& error) {
        patches.clear();
        diagnostics.push_back(std::string("Patch analysis failed: ")+error.what());
        return false;
    }

    std::sort(patches.begin(),patches.end(),[](const auto& a,const auto& b) {
        return std::tie(a.partIndex,a.triangleIndices.front())<
               std::tie(b.partIndex,b.triangleIndices.front());
    });
    diagnostics.push_back("Detected "+std::to_string(patches.size())+
        " edge-connected patch(es) with maximum dihedral angle "+
        std::to_string(maxDihedralAngleDegrees)+" degrees.");
    if (degenerateCount)
        diagnostics.push_back("Skipped "+std::to_string(degenerateCount)+" degenerate triangle(s).");
    if (nonManifoldEdges)
        diagnostics.push_back("Detected "+std::to_string(nonManifoldEdges)+
            " non-manifold edge(s) shared by more than two triangles.");
    return true;
}
