#include <OpenModel/GltfWriter.hpp>

std::string GltfWriter::escape(const std::string& s) {
std::ostringstream o;for(unsigned char c:s)switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\b':o<<"\\b";break;case '\f':o<<"\\f";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:if(c<0x20){const char*h="0123456789abcdef";o<<"\\u00"<<h[c>>4]<<h[c&15];}else o<<char(c);}return o.str();
}

void GltfWriter::align4(std::vector<unsigned char>& b) {
while(b.size()%4)b.push_back(0);
}

std::size_t GltfWriter::appendFloats(std::vector<unsigned char>& b, const std::vector<float>& v) {

        align4(b); auto off=b.size();
        // glTF binary data is little-endian regardless of host architecture.
        for(float f:v){std::uint32_t bits=0;static_assert(sizeof(bits)==sizeof(f),"float32 required");std::memcpy(&bits,&f,sizeof(bits));b.push_back(static_cast<unsigned char>(bits&255));b.push_back(static_cast<unsigned char>((bits>>8)&255));b.push_back(static_cast<unsigned char>((bits>>16)&255));b.push_back(static_cast<unsigned char>((bits>>24)&255));}
        return off;
    
}

std::size_t GltfWriter::appendIndices(std::vector<unsigned char>& b, const std::vector<unsigned int>& v) {
align4(b);auto off=b.size();for(auto x:v){std::uint32_t n=x;b.push_back(n&255);b.push_back((n>>8)&255);b.push_back((n>>16)&255);b.push_back((n>>24)&255);}return off;
}

std::vector<float> GltfWriter::normalsFor(const SurfaceMeshPart& p) {
auto n=p.normals;if(!n.empty())return n;n.assign(p.positions.size(),0);for(std::size_t t=0;t<p.indices.size();t+=3){auto a=3ull*p.indices[t],b=3ull*p.indices[t+1],c=3ull*p.indices[t+2];double ax=p.positions[b]-p.positions[a],ay=p.positions[b+1]-p.positions[a+1],az=p.positions[b+2]-p.positions[a+2],bx=p.positions[c]-p.positions[a],by=p.positions[c+1]-p.positions[a+1],bz=p.positions[c+2]-p.positions[a+2];float x=float(ay*bz-az*by),y=float(az*bx-ax*bz),z=float(ax*by-ay*bx);for(auto i:{a,b,c}){n[i]+=x;n[i+1]+=y;n[i+2]+=z;}}for(std::size_t i=0;i<n.size();i+=3){double l=std::sqrt(double(n[i])*n[i]+double(n[i+1])*n[i+1]+double(n[i+2])*n[i+2]);if(l>1e-20)for(int k=0;k<3;++k)n[i+k]=float(n[i+k]/l);else n[i]=n[i+1]=0,n[i+2]=1;}return n;
}

bool GltfWriter::validImage(const std::vector<unsigned char>& d, const std::string& mime) {

        if (d.empty()) return mime.empty();
        if (mime != "image/png" && mime != "image/jpeg") return false;
        const bool png=d.size()>=8&&d[0]==137&&d[1]==80&&d[2]==78&&d[3]==71&&d[4]==13&&d[5]==10&&d[6]==26&&d[7]==10;
        const bool jpg=d.size()>=3&&d[0]==255&&d[1]==216&&d[2]==255;
        return (mime=="image/png"&&png)||(mime=="image/jpeg"&&jpg);
    
}

bool GltfWriter::validate(const SurfaceMeshPart& p, std::string& e) {

        if(p.name.empty()||p.positions.empty()||p.positions.size()%3||p.indices.empty()||p.indices.size()%3){e="Malformed surface part '"+p.name+"'.";return false;}
        const auto nv=p.positions.size()/3;if(nv>std::numeric_limits<std::uint32_t>::max()){e="Part exceeds glTF index limit.";return false;}
        for(float x:p.positions)if(!std::isfinite(x)){e="Non-finite position.";return false;}
        for(auto i:p.indices)if(i>=nv){e="Out-of-range index.";return false;}
        if(!p.normals.empty()&&p.normals.size()!=p.positions.size()){e="Normals size mismatch.";return false;}
        for(float x:p.normals)if(!std::isfinite(x)){e="Non-finite normal.";return false;}
        if(!p.texcoords.empty()&&p.texcoords.size()!=nv*2){e="UV size mismatch.";return false;}
        for(float x:p.texcoords)if(!std::isfinite(x)){e="Non-finite UV.";return false;}
        const bool anyTexture=!p.baseColorImage.empty()||!p.normalImage.empty()||!p.metallicRoughnessImage.empty()||!p.emissiveImage.empty();
        if(anyTexture&&p.texcoords.empty()){e="Textures require UV0 coordinates.";return false;}
        if(!validImage(p.baseColorImage,p.baseColorImageMimeType)||!validImage(p.normalImage,p.normalImageMimeType)||!validImage(p.metallicRoughnessImage,p.metallicRoughnessImageMimeType)||!validImage(p.emissiveImage,p.emissiveImageMimeType)){e="Texture data must be a valid PNG or JPEG matching its MIME type.";return false;}
        for(float x:p.baseColorFactor)if(!std::isfinite(x)||x<0||x>1){e="baseColorFactor values must be finite and in [0,1].";return false;}
        for(float x:p.emissiveFactor)if(!std::isfinite(x)||x<0){e="emissiveFactor values must be finite and non-negative.";return false;}
        if(!std::isfinite(p.metallicFactor)||p.metallicFactor<0||p.metallicFactor>1||!std::isfinite(p.roughnessFactor)||p.roughnessFactor<0||p.roughnessFactor>1){e="Metallic and roughness factors must be in [0,1].";return false;}
        if(p.alphaMode!="OPAQUE"&&p.alphaMode!="MASK"&&p.alphaMode!="BLEND"){e="alphaMode must be OPAQUE, MASK or BLEND.";return false;}
        if(!std::isfinite(p.alphaCutoff)||p.alphaCutoff<0||p.alphaCutoff>1){e="alphaCutoff must be finite and in [0,1].";return false;}
        return true;
    
}

std::string GltfWriter::encode64(const std::vector<unsigned char>& d) {
static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string o;o.reserve((d.size()+2)/3*4);for(std::size_t i=0;i<d.size();i+=3){unsigned a=d[i],b=i+1<d.size()?d[i+1]:0,c=i+2<d.size()?d[i+2]:0,n=(a<<16)|(b<<8)|c;o+=t[(n>>18)&63];o+=t[(n>>12)&63];o+=i+1<d.size()?t[(n>>6)&63]:'=';o+=i+2<d.size()?t[n&63]:'=';}return o;
}

bool GltfWriter::decode64(const std::string& s, std::vector<unsigned char>& out) {
std::uint32_t val=0;int bits=-8;for(unsigned char c:s){if(c=='=')break;const std::string tab="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";auto p=tab.find(char(c));if(p==std::string::npos)return false;val=(val<<6)|std::uint32_t(p);bits+=6;if(bits>=0){out.push_back((val>>bits)&255);bits-=8;}}return true;
}

void GltfWriter::u32(std::ofstream& o, std::uint32_t v) {
char b[4]={char(v&255),char((v>>8)&255),char((v>>16)&255),char((v>>24)&255)};o.write(b,4);
}

GltfWriteResult GltfWriter::writeFile(const std::string& path,const SurfaceMesh& mesh,const GltfWriteOptions& options) {

        GltfWriteResult r;if(path.empty()){r.error="Output path is empty.";return r;}if(mesh.empty()){r.error="Cannot export an empty surface mesh.";return r;}
        std::vector<unsigned char> bin;std::vector<Prim> ps;std::vector<std::size_t> offsets;
        std::vector<std::array<int,4>> textureIndices; int nextImage=0;
        for(const auto& part:mesh.parts){std::array<int,4> ids{{-1,-1,-1,-1}};const std::vector<const std::vector<unsigned char>*> imgs{{&part.baseColorImage,&part.normalImage,&part.metallicRoughnessImage,&part.emissiveImage}};for(std::size_t k=0;k<imgs.size();++k)if(!imgs[k]->empty())ids[k]=nextImage++;textureIndices.push_back(ids);}
        for(std::size_t partIndex=0;partIndex<mesh.parts.size();++partIndex){const auto& p=mesh.parts[partIndex];if(!validate(p,r.error))return r;Prim q;q.name=p.name;q.nv=p.positions.size()/3;q.ni=p.indices.size();q.hasUv=!p.texcoords.empty();q.material=ps.size();q.image=textureIndices[partIndex][0];q.normalImage=textureIndices[partIndex][1];q.mrImage=textureIndices[partIndex][2];q.emissiveImage=textureIndices[partIndex][3];q.color=p.baseColorFactor;q.emissive=p.emissiveFactor;q.metallic=p.metallicFactor;q.roughness=p.roughnessFactor;q.alphaMode=p.alphaMode;q.alphaCutoff=p.alphaCutoff;q.doubleSided=p.doubleSided;q.lo={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::max()};q.hi={std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()};for(std::size_t i=0;i<p.positions.size();i+=3)for(int k=0;k<3;++k){q.lo[k]=std::min(q.lo[k],p.positions[i+k]);q.hi[k]=std::max(q.hi[k],p.positions[i+k]);}
            q.pv=offsets.size();offsets.push_back(appendFloats(bin,p.positions));q.nvView=offsets.size();offsets.push_back(appendFloats(bin,normalsFor(p)));if(q.hasUv){q.uvView=offsets.size();offsets.push_back(appendFloats(bin,p.texcoords));}q.iv=offsets.size();offsets.push_back(appendIndices(bin,p.indices));ps.push_back(q);}
        // Accessors are assigned in primitive order; optional UV accessors shift only later entries.
        std::size_t ac=0;for(auto& p:ps){p.pa=ac++;p.na=ac++;if(p.hasUv)p.ua=ac++;p.ia=ac++;}
        std::ostringstream j;j<<std::setprecision(9)<<"{\"asset\":{\"version\":\"2.0\",\"generator\":\""<<escape(options.assetGenerator)<<"\"},\"scene\":0,\"scenes\":[{\"nodes\":[";for(std::size_t i=0;i<ps.size();++i){if(i)j<<',';j<<i;}j<<"]}],\"nodes\":[";for(std::size_t i=0;i<ps.size();++i){if(i)j<<',';j<<"{\"name\":\""<<escape(ps[i].name)<<"\",\"mesh\":"<<i<<'}';}j<<"],\"meshes\":[";
        for(std::size_t i=0;i<ps.size();++i){const auto&p=ps[i];if(i)j<<',';j<<"{\"name\":\""<<escape(p.name)<<"\",\"primitives\":[{\"attributes\":{\"POSITION\":"<<p.pa<<",\"NORMAL\":"<<p.na;if(p.hasUv)j<<",\"TEXCOORD_0\":"<<p.ua;j<<"},\"indices\":"<<p.ia<<",\"material\":"<<p.material<<",\"mode\":4}]}";}j<<"],\"materials\":[";
        for(std::size_t i=0;i<ps.size();++i){const auto&p=ps[i];if(i)j<<',';j<<"{\"name\":\""<<escape(p.name)<<"_Material\",\"pbrMetallicRoughness\":{\"baseColorFactor\":["<<p.color[0]<<','<<p.color[1]<<','<<p.color[2]<<','<<p.color[3]<<"],\"metallicFactor\":"<<p.metallic<<",\"roughnessFactor\":"<<p.roughness;if(p.image>=0)j<<",\"baseColorTexture\":{\"index\":"<<p.image<<'}';if(p.mrImage>=0)j<<",\"metallicRoughnessTexture\":{\"index\":"<<p.mrImage<<'}';j<<'}';if(p.normalImage>=0)j<<",\"normalTexture\":{\"index\":"<<p.normalImage<<'}';if(p.emissiveImage>=0)j<<",\"emissiveTexture\":{\"index\":"<<p.emissiveImage<<'}';if(p.emissive[0]!=0||p.emissive[1]!=0||p.emissive[2]!=0)j<<",\"emissiveFactor\":["<<p.emissive[0]<<','<<p.emissive[1]<<','<<p.emissive[2]<<']';j<<",\"alphaMode\":\""<<p.alphaMode<<"\",\"alphaCutoff\":"<<p.alphaCutoff<<",\"doubleSided\":"<<(p.doubleSided?"true":"false")<<'}';}j<<']';
        std::size_t imageCount=static_cast<std::size_t>(nextImage);
        if(imageCount){j<<",\"images\":[";bool firstImage=true;auto emitImage=[&](const std::vector<unsigned char>& data,const std::string& mime,const std::string& name,const std::string& fallback){if(data.empty())return;if(!firstImage)j<<',';firstImage=false;j<<"{\"name\":\""<<escape(name.empty()?fallback:name)<<"\",\"uri\":\"data:"<<mime<<";base64,"<<encode64(data)<<"\"}";};for(const auto& part:mesh.parts){emitImage(part.baseColorImage,part.baseColorImageMimeType,part.baseColorImageName,part.name+"_BaseColor");emitImage(part.normalImage,part.normalImageMimeType,part.normalImageName,part.name+"_Normal");emitImage(part.metallicRoughnessImage,part.metallicRoughnessImageMimeType,part.metallicRoughnessImageName,part.name+"_MetallicRoughness");emitImage(part.emissiveImage,part.emissiveImageMimeType,part.emissiveImageName,part.name+"_Emissive");}j<<"],\"textures\":[";for(std::size_t k=0;k<imageCount;++k){if(k)j<<',';j<<"{\"source\":"<<k<<'}';}j<<']';}
        j<<",\"buffers\":[{\"byteLength\":"<<bin.size()<<",\"uri\":\"data:application/octet-stream;base64,"<<encode64(bin)<<"\"}],\"bufferViews\":[";
        for(std::size_t i=0;i<offsets.size();++i){if(i)j<<',';std::size_t end=(i+1<offsets.size()?offsets[i+1]:bin.size()); // offsets are already aligned; view byteLength excludes alignment padding
            std::size_t len=end-offsets[i];if(i+1==offsets.size())len=bin.size()-offsets[i];
            // For each buffer view, compute payload size from the corresponding accessor/attribute.
            std::size_t partIndex=0;while(partIndex+1<ps.size() && i>=ps[partIndex+1].pv)++partIndex;const auto&p=ps[partIndex];
            if(i==p.pv||i==p.nvView)len=p.nv*3*sizeof(float);else if(p.hasUv&&i==p.uvView)len=p.nv*2*sizeof(float);else if(i==p.iv)len=p.ni*sizeof(std::uint32_t);
            j<<"{\"buffer\":0,\"byteOffset\":"<<offsets[i]<<",\"byteLength\":"<<len<<",\"target\":"<<(i==p.iv?34963:34962)<<'}';}
        j<<"],\"accessors\":[";bool first=true;auto sep=[&](){if(!first)j<<',';first=false;};for(const auto&p:ps){sep();j<<"{\"bufferView\":"<<p.pv<<",\"componentType\":5126,\"count\":"<<p.nv<<",\"type\":\"VEC3\",\"min\":["<<p.lo[0]<<','<<p.lo[1]<<','<<p.lo[2]<<"],\"max\":["<<p.hi[0]<<','<<p.hi[1]<<','<<p.hi[2]<<"]}";sep();j<<"{\"bufferView\":"<<p.nvView<<",\"componentType\":5126,\"count\":"<<p.nv<<",\"type\":\"VEC3\"}";if(p.hasUv){sep();j<<"{\"bufferView\":"<<p.uvView<<",\"componentType\":5126,\"count\":"<<p.nv<<",\"type\":\"VEC2\"}";}sep();j<<"{\"bufferView\":"<<p.iv<<",\"componentType\":5125,\"count\":"<<p.ni<<",\"type\":\"SCALAR\"}";}j<<"]}";
        auto text=j.str();std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out){r.error="Cannot open output file: "+path;return r;}out.write(text.data(),static_cast<std::streamsize>(text.size()));out.close();if(!out){r.error="Failed writing output file.";return r;}r.success=true;r.meshesWritten=ps.size();r.verticesWritten=mesh.vertexCount();r.trianglesWritten=mesh.triangleCount();r.bytesWritten=text.size();return r;
    
}

GltfWriteResult GltfWriter::writeGlbFile(const std::string& path,const SurfaceMesh& mesh,const GltfWriteOptions& options) {

        GltfWriteResult r;if(path.empty()){r.error="Output path is empty.";return r;}std::string tmp=path+".openmodel_tmp.gltf";auto base=writeFile(tmp,mesh,options);if(!base.success)return base;
        std::ifstream in(tmp,std::ios::binary);std::string json((std::istreambuf_iterator<char>(in)),{});in.close();std::remove(tmp.c_str());const std::string marker="data:application/octet-stream;base64,";auto uri=json.find(marker);if(uri==std::string::npos){r.error="Internal GLB conversion could not find embedded buffer.";return r;}auto start=uri+marker.size(),end=json.find('"',start);if(end==std::string::npos){r.error="Malformed embedded buffer URI.";return r;}std::vector<unsigned char> bin;if(!decode64(json.substr(start,end-start),bin)){r.error="Invalid embedded Base64 buffer.";return r;}
        auto uriKey=json.rfind(",\"uri\":\"",uri);if(uriKey==std::string::npos){r.error="Malformed glTF buffer object.";return r;}json.erase(uriKey,end-uriKey+1);while(json.size()%4)json.push_back(' ');while(bin.size()%4)bin.push_back(0);
        const std::uint64_t total=12+8+json.size()+8+bin.size();if(total>std::numeric_limits<std::uint32_t>::max()){r.error="GLB exceeds 4 GiB format limit.";return r;}std::ofstream out(path,std::ios::binary|std::ios::trunc);if(!out){r.error="Cannot open output file: "+path;return r;}u32(out,0x46546c67);u32(out,2);u32(out,static_cast<std::uint32_t>(total));u32(out,static_cast<std::uint32_t>(json.size()));u32(out,0x4e4f534a);out.write(json.data(),static_cast<std::streamsize>(json.size()));u32(out,static_cast<std::uint32_t>(bin.size()));u32(out,0x004e4942);out.write(reinterpret_cast<const char*>(bin.data()),static_cast<std::streamsize>(bin.size()));out.close();if(!out){r.error="Failed writing GLB file.";return r;}base.bytesWritten=static_cast<std::size_t>(total);return base;
    
}
