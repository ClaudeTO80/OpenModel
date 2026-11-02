#pragma once
#include <OpenModel/SurfaceMesh.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

struct GltfWriteResult {
    bool success=false; std::string error;
    std::size_t meshesWritten=0,verticesWritten=0,trianglesWritten=0,bytesWritten=0;
};
struct GltfWriteOptions { std::string assetGenerator="OpenModel"; };

class GltfWriter {
    struct Prim { std::string name; std::size_t nv=0,ni=0,pv=0,nvView=0,uvView=0,iv=0,pa=0,na=0,ua=0,ia=0,material=0; int image=-1,normalImage=-1,mrImage=-1,emissiveImage=-1; bool hasUv=false; std::array<float,3> lo{},hi{}; std::array<float,4> color{}; std::array<float,3> emissive{}; float metallic=0,roughness=0.8f,alphaCutoff=0.5f; std::string alphaMode="OPAQUE"; bool doubleSided=false; };
    static std::string escape(const std::string& s);
    static void align4(std::vector<unsigned char>& b);
    static std::size_t appendFloats(std::vector<unsigned char>& b,const std::vector<float>& v);
    static std::size_t appendIndices(std::vector<unsigned char>& b,const std::vector<unsigned int>& v);
    static std::vector<float> normalsFor(const SurfaceMeshPart& p);
    static bool validImage(const std::vector<unsigned char>& d, const std::string& mime) ;
    static bool validate(const SurfaceMeshPart& p,std::string& e);
    static std::string encode64(const std::vector<unsigned char>& d);
    static bool decode64(const std::string& s,std::vector<unsigned char>& out);
    static void u32(std::ofstream& o,std::uint32_t v);
public:
    static GltfWriteResult writeFile(const std::string& path,const SurfaceMesh& mesh,const GltfWriteOptions& options={});
    static GltfWriteResult writeGlbFile(const std::string& path,const SurfaceMesh& mesh,const GltfWriteOptions& options={});
};
