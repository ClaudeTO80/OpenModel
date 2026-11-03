#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <tuple>

namespace {
using V = std::array<double, 3>;
V sub(const V& a,const V& b){return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};}
V add(const V& a,const V& b){return {{a[0]+b[0],a[1]+b[1],a[2]+b[2]}};}
V mul(const V& a,double s){return {{a[0]*s,a[1]*s,a[2]*s}};}
double dot(const V& a,const V& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V cross(const V& a,const V& b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
double length(const V& a){return std::sqrt(dot(a,a));}
bool finite(const V& a){return std::isfinite(a[0])&&std::isfinite(a[1])&&std::isfinite(a[2]);}
struct Q { std::int64_t x,y,z; bool operator<(const Q& o) const {return std::tie(x,y,z)<std::tie(o.x,o.y,o.z);} };
struct E { Q a,b; bool operator<(const E& o) const {return std::tie(a,b)<std::tie(o.a,o.b);} };
Q quantize(const V& p,double t){
    const double lim=static_cast<double>(std::numeric_limits<std::int64_t>::max()/4);
    for(double x:p) if(!std::isfinite(x)||std::abs(x/t)>lim) throw std::runtime_error("coordinate/tolerance ratio exceeds quantization range");
    return {static_cast<std::int64_t>(std::llround(p[0]/t)),static_cast<std::int64_t>(std::llround(p[1]/t)),static_cast<std::int64_t>(std::llround(p[2]/t))};
}
E edge(Q a,Q b){if(b<a)std::swap(a,b);return {a,b};}
struct Tri { V p[3]; V normal; double area; std::size_t sourceIndex; };
struct DSU {
    std::vector<std::size_t> p;
    explicit DSU(std::size_t n):p(n){std::iota(p.begin(),p.end(),0);}
    std::size_t find(std::size_t x){while(p[x]!=x){p[x]=p[p[x]];x=p[x];}return x;}
    void join(std::size_t a,std::size_t b){a=find(a);b=find(b);if(a!=b)p[b]=a;}
};
}

bool AssimpSurfacePatchAnalyzer::analyze(const std::vector<RenderSubMesh>& parts,
    double weldTolerance, std::vector<AssimpSurfacePatch>& patches,
    std::vector<std::string>& diagnostics, double maxDihedralAngleDegrees) {
    patches.clear(); diagnostics.clear();
    if(!std::isfinite(weldTolerance)||weldTolerance<=0.0){
        diagnostics.push_back("Weld tolerance must be finite and positive."); return false;
    }
    if(!std::isfinite(maxDihedralAngleDegrees)||maxDihedralAngleDegrees<0.0||maxDihedralAngleDegrees>180.0){
        diagnostics.push_back("Maximum dihedral angle must be in [0, 180] degrees."); return false;
    }
    const double minNormalDot=std::cos(maxDihedralAngleDegrees*3.14159265358979323846/180.0);
    try {
        for(std::size_t pi=0;pi<parts.size();++pi){
            const auto& part=parts[pi];
            if(part.vertices.empty()||part.vertices.size()%3||part.indices.empty()||part.indices.size()%3){
                diagnostics.push_back("Malformed triangle arrays in part "+std::to_string(pi)+"."); return false;
            }
            const std::size_t nv=part.vertices.size()/3, nt=part.indices.size()/3;
            std::vector<Tri> tris; tris.reserve(nt);
            std::vector<std::array<Q,3>> keys; keys.reserve(nt);
            std::map<E,std::vector<std::size_t>> edgeOwners;
            for(std::size_t ti=0;ti<nt;++ti){
                Tri tr{}; std::array<Q,3> q{};
                for(int k=0;k<3;++k){
                    const auto vi=part.indices[3*ti+static_cast<std::size_t>(k)];
                    if(vi>=nv){diagnostics.push_back("Triangle index outside vertex array in part "+std::to_string(pi)+".");return false;}
                    tr.p[k]={{part.vertices[3*vi],part.vertices[3*vi+1],part.vertices[3*vi+2]}};
                    if(!finite(tr.p[k])){diagnostics.push_back("Non-finite vertex in part "+std::to_string(pi)+".");return false;}
                    q[k]=quantize(tr.p[k],weldTolerance);
                }
                const V n=cross(sub(tr.p[1],tr.p[0]),sub(tr.p[2],tr.p[0]));
                const double twice=length(n);
                if(!std::isfinite(twice)||twice<=1e-18) continue;
                tr.area=0.5*twice; tr.normal=mul(n,1.0/twice); tr.sourceIndex=ti;
                const std::size_t validIndex=tris.size();
                tris.push_back(tr); keys.push_back(q);
                for(int k=0;k<3;++k) edgeOwners[edge(q[k],q[(k+1)%3])].push_back(validIndex);
            }
            if(tris.empty()) continue;
            DSU dsu(tris.size());
            for(const auto& entry:edgeOwners) {
                const auto& owners=entry.second;
                for(std::size_t a=0;a<owners.size();++a)
                    for(std::size_t b=a+1;b<owners.size();++b) {
                        const double alignment=std::abs(dot(tris[owners[a]].normal,tris[owners[b]].normal));
                        if(alignment+1e-12>=minNormalDot) dsu.join(owners[a],owners[b]);
                    }
            }

            std::map<std::size_t,std::vector<std::size_t>> components;
            for(std::size_t i=0;i<tris.size();++i) components[dsu.find(i)].push_back(i);
            for(const auto& component:components){
                AssimpSurfacePatch patch; patch.partIndex=pi;
                patch.boundsMin={{std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),std::numeric_limits<double>::max()}};
                patch.boundsMax={{-std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()}};
                V weighted{{0,0,0}}, normalSum{{0,0,0}};
                std::map<E,std::size_t> localEdges;
                for(auto ti:component.second){
                    const auto& tr=tris[ti]; patch.triangleIndices.push_back(tr.sourceIndex);
                    patch.area+=tr.area;
                    V c=mul(add(add(tr.p[0],tr.p[1]),tr.p[2]),1.0/3.0);
                    weighted=add(weighted,mul(c,tr.area));
                    normalSum=add(normalSum,mul(tr.normal,tr.area));
                    for(const auto& p:tr.p) for(int a=0;a<3;++a){
                        patch.boundsMin[a]=std::min(p[a],patch.boundsMin[a]);
                        patch.boundsMax[a]=std::max(patch.boundsMax[a],p[a]);
                    }
                    for(int k=0;k<3;++k) ++localEdges[edge(keys[ti][k],keys[ti][(k+1)%3])];
                }
                patch.centroid=mul(weighted,1.0/patch.area);
                const double nl=length(normalSum);
                if(nl>1e-14) patch.meanNormal=mul(normalSum,1.0/nl);
                for(const auto& e:localEdges) if(e.second==1) ++patch.boundaryEdgeCount;
                if(nl>1e-14) {
                    for(auto ti:component.second) for(const auto& p:tris[ti].p)
                        patch.maxPlanarityDeviation=std::max(patch.maxPlanarityDeviation,
                            std::abs(dot(sub(p,patch.centroid),patch.meanNormal)));
                } else {
                    patch.maxPlanarityDeviation=std::numeric_limits<double>::infinity();
                }
                patches.push_back(std::move(patch));
            }
        }
    } catch(const std::exception& e) {
        patches.clear(); diagnostics.push_back(std::string("Patch analysis failed: ")+e.what()); return false;
    }
    std::sort(patches.begin(),patches.end(),[](const auto& a,const auto& b){
        return std::tie(a.partIndex,a.triangleIndices.front())<std::tie(b.partIndex,b.triangleIndices.front());
    });
    diagnostics.push_back("Detected "+std::to_string(patches.size())+" edge-connected patch(es) with a maximum dihedral angle of "+std::to_string(maxDihedralAngleDegrees)+" degrees.");
    return true;
}
