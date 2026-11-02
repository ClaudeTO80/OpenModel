#include <OpenModel/SurfaceRefiner.hpp>

SurfaceRefiner::Vec3 SurfaceRefiner::read3(const std::vector<float>& v, std::size_t i) {
 return {v[3*i],v[3*i+1],v[3*i+2]}; 
}

SurfaceRefiner::Vec3 SurfaceRefiner::normalized(Vec3 v) {

        double n=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(n>1e-30){for(auto& x:v)x/=n;} else v={0,0,1}; return v;
    
}

double SurfaceRefiner::length(const Vec3& a, const Vec3& b) {
 double x=a[0]-b[0],y=a[1]-b[1],z=a[2]-b[2];return std::sqrt(x*x+y*y+z*z); 
}

std::vector<float> SurfaceRefiner::makeNormals(const SurfaceMeshPart& p) {

        std::vector<float> n(p.positions.size(),0.0f);
        for(std::size_t t=0;t<p.indices.size();t+=3){
            auto a=3ull*p.indices[t],b=3ull*p.indices[t+1],c=3ull*p.indices[t+2];
            double ax=p.positions[b]-p.positions[a],ay=p.positions[b+1]-p.positions[a+1],az=p.positions[b+2]-p.positions[a+2];
            double bx=p.positions[c]-p.positions[a],by=p.positions[c+1]-p.positions[a+1],bz=p.positions[c+2]-p.positions[a+2];
            float x=float(ay*bz-az*by),y=float(az*bx-ax*bz),z=float(ax*by-ay*bx);
            for(auto i:{a,b,c}){n[i]+=x;n[i+1]+=y;n[i+2]+=z;}
        }
        for(std::size_t i=0;i<n.size();i+=3){auto q=normalized({n[i],n[i+1],n[i+2]});for(int k=0;k<3;++k)n[i+k]=float(q[k]);} return n;
    
}

bool SurfaceRefiner::valid(const SurfaceMeshPart& p, std::string& e) {

        if(p.name.empty()||p.positions.empty()||p.positions.size()%3||p.indices.empty()||p.indices.size()%3){e="Malformed surface part '"+p.name+"'.";return false;}
        auto nv=p.positions.size()/3;
        for(float x:p.positions)if(!std::isfinite(x)){e="Non-finite position in '"+p.name+"'.";return false;}
        for(auto i:p.indices)if(i>=nv){e="Out-of-range index in '"+p.name+"'.";return false;}
        if(!p.normals.empty()&&p.normals.size()!=p.positions.size()){e="Normals size mismatch in '"+p.name+"'.";return false;}
        if(!p.texcoords.empty()&&p.texcoords.size()!=nv*2){e="UV size mismatch in '"+p.name+"'.";return false;}
        for(float x:p.texcoords)if(!std::isfinite(x)){e="Non-finite UV in '"+p.name+"'.";return false;}
        return true;
    
}

double SurfaceRefiner::angle(const Vec3& a, const Vec3& b) {
return std::acos(std::clamp(a[0]*b[0]+a[1]*b[1]+a[2]*b[2],-1.0,1.0))*180.0/3.14159265358979323846;
}

std::map<SurfaceRefiner::Edge,bool> SurfaceRefiner::edgesToSplit(const SurfaceMeshPart& p, const SurfaceRefineOptions& o) {

        std::map<Edge,bool> split;
        auto mark=[&](unsigned int a,unsigned int b){Edge e=std::minmax(a,b);split[e]=true;};
        const bool haveNormals=p.normals.size()==p.positions.size();
        for(std::size_t t=0;t<p.indices.size();t+=3){unsigned int ids[3]={p.indices[t],p.indices[t+1],p.indices[t+2]};
            for(int k=0;k<3;++k){auto a=ids[k],b=ids[(k+1)%3];bool need=false;
                if(o.maxEdgeLength>0&&length(read3(p.positions,a),read3(p.positions,b))>o.maxEdgeLength)need=true;
                if(o.maxNormalAngleDegrees>0&&haveNormals&&angle(normalized(read3(p.normals,a)),normalized(read3(p.normals,b)))>o.maxNormalAngleDegrees)need=true;
                if(need)mark(a,b);
            }
        } return split;
    
}

void SurfaceRefiner::applySplits(SurfaceMeshPart& p, const std::map<Edge,bool>& split) {

        std::map<Edge,unsigned int> mids;
        auto mid=[&](unsigned int a,unsigned int b)->unsigned int{
            Edge e=std::minmax(a,b);auto found=mids.find(e);if(found!=mids.end())return found->second;
            unsigned int id=static_cast<unsigned int>(p.positions.size()/3);
            for(int k=0;k<3;++k)p.positions.push_back((p.positions[3ull*a+k]+p.positions[3ull*b+k])*0.5f);
            Vec3 n=normalized({(p.normals[3ull*a]+p.normals[3ull*b])*0.5,(p.normals[3ull*a+1]+p.normals[3ull*b+1])*0.5,(p.normals[3ull*a+2]+p.normals[3ull*b+2])*0.5});
            for(double x:n)p.normals.push_back(float(x));
            if(!p.texcoords.empty()){p.texcoords.push_back((p.texcoords[2ull*a]+p.texcoords[2ull*b])*0.5f);p.texcoords.push_back((p.texcoords[2ull*a+1]+p.texcoords[2ull*b+1])*0.5f);}
            mids.emplace(e,id);return id;
        };
        std::vector<unsigned int> out;out.reserve(p.indices.size()*2);
        for(std::size_t t=0;t<p.indices.size();t+=3){
            unsigned int a=p.indices[t],b=p.indices[t+1],c=p.indices[t+2];
            bool sab=split.count(std::minmax(a,b)),sbc=split.count(std::minmax(b,c)),sca=split.count(std::minmax(c,a));
            if(!sab&&!sbc&&!sca){out.insert(out.end(),{a,b,c});continue;}
            unsigned int ab=sab?mid(a,b):0,bc=sbc?mid(b,c):0,ca=sca?mid(c,a):0;
            if(sab&&sbc&&sca){out.insert(out.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});}
            else if(sab&&sbc){out.insert(out.end(),{b,bc,ab,a,ab,c,ab,bc,c});}
            else if(sab&&sca){out.insert(out.end(),{a,ab,ca,ab,b,c,ab,c,ca});}
            else if(sbc&&sca){out.insert(out.end(),{c,ca,bc,a,b,bc,a,bc,ca});}
            else if(sab){out.insert(out.end(),{a,ab,c,ab,b,c});}
            else if(sbc){out.insert(out.end(),{b,bc,a,bc,c,a});}
            else {out.insert(out.end(),{c,ca,b,ca,a,b});}
        }
        p.indices=std::move(out);
    
}

SurfaceRefineReport SurfaceRefiner::refine(SurfaceMesh& mesh, const SurfaceRefineOptions& o) const {

        SurfaceRefineReport r;r.parts=mesh.parts.size();r.verticesBefore=mesh.vertexCount();r.trianglesBefore=mesh.triangleCount();
        if(mesh.empty()){r.error="Cannot refine an empty surface mesh.";return r;}
        if(!std::isfinite(o.maxEdgeLength)||o.maxEdgeLength<0||!std::isfinite(o.maxNormalAngleDegrees)||o.maxNormalAngleDegrees<0||o.maxNormalAngleDegrees>180){r.error="Invalid refinement threshold.";return r;}
        if(o.maxEdgeLength==0&&o.maxNormalAngleDegrees==0){r.error="At least one refinement criterion must be enabled.";return r;}
        SurfaceMesh candidate=mesh;
        for(auto& p:candidate.parts){if(!valid(p,r.error))return r;if(p.normals.empty())p.normals=makeNormals(p);}
        for(unsigned level=0;level<o.maxLevels;++level){
            bool any=false;
            for(auto& p:candidate.parts){auto split=edgesToSplit(p,o);if(!split.empty()){any=true;if(p.positions.size()/3+split.size()>std::numeric_limits<unsigned int>::max()){r.error="Refinement exceeds 32-bit vertex index capacity.";return r;}applySplits(p,split);}}
            if(!any)break;++r.levelsApplied;
        }
        for(const auto& p:candidate.parts)if(!edgesToSplit(p,o).empty())r.limitReached=true;
        mesh=std::move(candidate);r.success=true;r.verticesAfter=mesh.vertexCount();r.trianglesAfter=mesh.triangleCount();return r;
    
}
