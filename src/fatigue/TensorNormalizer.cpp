#include <OpenModel/Fatigue/TensorNormalizer.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
namespace OpenModel { namespace Fatigue { namespace {
constexpr double tol=1e-6;
std::string compact(const std::string& x) {
 std::string y; for(unsigned char c:x) if(std::isalnum(c)) y.push_back(static_cast<char>(std::toupper(c))); return y;
}
int tensorIndex(const std::string& label) {
 const auto x=compact(label); if(x.size()<2) return -1; const auto q=x.substr(x.size()-2);
 if(q=="XX"||q=="11") return 0; if(q=="YY"||q=="22") return 1; if(q=="ZZ"||q=="33") return 2;
 if(q=="XY"||q=="YX"||q=="12"||q=="21") return 3;
 if(q=="XZ"||q=="ZX"||q=="13"||q=="31") return 4;
 if(q=="YZ"||q=="ZY"||q=="23"||q=="32") return 5; return -1;
}
int vectorIndex(const std::string& label) {
 const auto x=compact(label); if(x.empty()) return -1; const char c=x.back();
 if(c=='1'||c=='X') return 0; if(c=='2'||c=='Y') return 1; if(c=='3'||c=='Z') return 2; return -1;
}
bool validRotation(const CoordinateTransform& t) {
 if(!t.available) return false; const auto& r=t.localToGlobal;
 for(double x:r) if(!std::isfinite(x)) return false;
 for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
  double d=0; for(int k=0;k<3;++k) d+=r[k*3+i]*r[k*3+j];
  if(std::fabs(d-(i==j?1.0:0.0))>tol) return false;
 }
 const double det=r[0]*(r[4]*r[8]-r[5]*r[7])-r[1]*(r[3]*r[8]-r[5]*r[6])+r[2]*(r[3]*r[7]-r[4]*r[6]);
 return std::fabs(det-1.0)<=tol;
}
Tensor3 trans(const Tensor3& a) { Tensor3 b; for(int i=0;i<3;++i) for(int j=0;j<3;++j)b.values[i*3+j]=a.values[j*3+i]; return b; }
Tensor3 mul(const Tensor3& a,const Tensor3& b) {
 Tensor3 c; c.values.fill(0); for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)c.values[i*3+j]+=a.values[i*3+k]*b.values[k*3+j]; return c;
}
TensorNormalizationStatus transform(Tensor3& a,const ResultSample& s,ReferenceFrame target,std::string& why) {
 if(s.frame==ReferenceFrame::Unknown||target==ReferenceFrame::Unknown){why="Source and target frames must be known.";return TensorNormalizationStatus::UnsupportedCoordinateFrame;}
 if(s.frame==target)return TensorNormalizationStatus::Success;
 if(!s.coordinateTransform.available){why="A local-to-global coordinate transform is required.";return TensorNormalizationStatus::MissingCoordinateTransform;}
 if(!validRotation(s.coordinateTransform)){why="Coordinate transform is not a proper orthonormal rotation.";return TensorNormalizationStatus::InvalidCoordinateTransform;}
 Tensor3 r; r.values=s.coordinateTransform.localToGlobal;
 if(s.frame==ReferenceFrame::Global) a=mul(mul(trans(r),a),r);
 else if(target==ReferenceFrame::Global) a=mul(mul(r,a),trans(r));
 else {why="Only transforms to/from the global frame are supported.";return TensorNormalizationStatus::UnsupportedCoordinateFrame;}
 return TensorNormalizationStatus::Success;
}
TensorNormalizationResult fail(TensorNormalizationStatus s,const std::string& d){TensorNormalizationResult r;r.status=s;r.diagnostic=d;return r;}
VectorNormalizationResult vfail(TensorNormalizationStatus s,const std::string& d){VectorNormalizationResult r;r.status=s;r.diagnostic=d;return r;}
} // anonymous
TensorNormalizationResult TensorNormalizer::normalizeTensor(const ResultSample& s,ReferenceFrame target) {
 if(s.componentKind!=ComponentKind::SymmetricTensor)return fail(TensorNormalizationStatus::UnsupportedComponentKind,"Sample is not declared as a symmetric tensor.");
 if(s.componentLabels.size()!=6||s.components.size()!=6)return fail(TensorNormalizationStatus::InvalidComponentCount,"A 3D symmetric tensor requires six labelled components.");
 double c[6]={}; bool seen[6]={};
 for(std::size_t i=0;i<6;++i){
  if(!std::isfinite(s.components[i]))return fail(TensorNormalizationStatus::NonFiniteValue,"Tensor contains a non-finite component.");
  const int k=tensorIndex(s.componentLabels[i]); if(k<0)return fail(TensorNormalizationStatus::UnknownComponentLabel,"Unsupported tensor component label: "+s.componentLabels[i]);
  if(seen[k])return fail(TensorNormalizationStatus::DuplicateComponent,"Tensor component labels are duplicated."); seen[k]=true;c[k]=s.components[i];
 }
 for(bool b:seen)if(!b)return fail(TensorNormalizationStatus::MissingComponent,"Tensor component set is incomplete.");
 if(s.quantity==QuantityKind::Strain){
  if(s.shearConvention!=ShearStrainConvention::Tensorial&&s.shearConvention!=ShearStrainConvention::Engineering)
   return fail(TensorNormalizationStatus::UnknownShearConvention,"Strain shear convention must be Tensorial or Engineering.");
  if(s.shearConvention==ShearStrainConvention::Engineering){c[3]*=0.5;c[4]*=0.5;c[5]*=0.5;}
 } else if(s.quantity!=QuantityKind::Stress&&s.quantity!=QuantityKind::Unknown)
  return fail(TensorNormalizationStatus::UnsupportedQuantity,"Only stress and strain tensors are supported.");
 Tensor3 a; a.values={{c[0],c[3],c[4],c[3],c[1],c[5],c[4],c[5],c[2]}};
 std::string why; const auto status=transform(a,s,target,why); if(status!=TensorNormalizationStatus::Success)return fail(status,why);
 TensorNormalizationResult out;out.status=TensorNormalizationStatus::Success;out.tensor=a;return out;
}
VectorNormalizationResult TensorNormalizer::normalizeVector(const ResultSample& s,ReferenceFrame target) {
 if(s.componentKind!=ComponentKind::Vector)return vfail(TensorNormalizationStatus::UnsupportedComponentKind,"Sample is not declared as a vector.");
 if(s.componentLabels.size()!=3||s.components.size()!=3)return vfail(TensorNormalizationStatus::InvalidComponentCount,"A 3D vector requires three labelled components.");
 Vector3 v;bool seen[3]={};
 for(std::size_t i=0;i<3;++i){
  if(!std::isfinite(s.components[i]))return vfail(TensorNormalizationStatus::NonFiniteValue,"Vector contains a non-finite component.");
  const int k=vectorIndex(s.componentLabels[i]);if(k<0)return vfail(TensorNormalizationStatus::UnknownComponentLabel,"Unsupported vector component label: "+s.componentLabels[i]);
  if(seen[k])return vfail(TensorNormalizationStatus::DuplicateComponent,"Vector component labels are duplicated.");seen[k]=true;v.values[k]=s.components[i];
 }
 for(bool b:seen)if(!b)return vfail(TensorNormalizationStatus::MissingComponent,"Vector component set is incomplete.");
 if(s.frame==ReferenceFrame::Unknown||target==ReferenceFrame::Unknown)return vfail(TensorNormalizationStatus::UnsupportedCoordinateFrame,"Source and target frames must be known.");
 if(s.frame!=target){
  if(!s.coordinateTransform.available)return vfail(TensorNormalizationStatus::MissingCoordinateTransform,"A local-to-global coordinate transform is required.");
  if(!validRotation(s.coordinateTransform))return vfail(TensorNormalizationStatus::InvalidCoordinateTransform,"Coordinate transform is not a proper orthonormal rotation.");
  const auto& r=s.coordinateTransform.localToGlobal;double q[3]={};
  if(s.frame==ReferenceFrame::Global){for(int i=0;i<3;++i)for(int j=0;j<3;++j)q[i]+=r[j*3+i]*v.values[j];}
  else if(target==ReferenceFrame::Global){for(int i=0;i<3;++i)for(int j=0;j<3;++j)q[i]+=r[i*3+j]*v.values[j];}
  else return vfail(TensorNormalizationStatus::UnsupportedCoordinateFrame,"Only transforms to/from the global frame are supported.");
  for(int i=0;i<3;++i)v.values[i]=q[i];
 }
 VectorNormalizationResult out;out.status=TensorNormalizationStatus::Success;out.vector=v;return out;
}
double TensorNormalizer::vonMisesStress(const Tensor3& s) noexcept {
 const double x=s.values[0],y=s.values[4],z=s.values[8],xy=s.values[1],xz=s.values[2],yz=s.values[5];
 const double d=0.5*((x-y)*(x-y)+(y-z)*(y-z)+(z-x)*(z-x))+3.0*(xy*xy+xz*xz+yz*yz);
 return std::sqrt(std::max(0.0,d));
}
bool TensorNormalizer::principalValues(const Tensor3& a,std::array<double,3>& v) noexcept {
 for(double x:a.values)if(!std::isfinite(x))return false;
 const double b01=.5*(a.values[1]+a.values[3]),b02=.5*(a.values[2]+a.values[6]),b12=.5*(a.values[5]+a.values[7]);
 const double q=(a.values[0]+a.values[4]+a.values[8])/3.0;
 const double p2=(a.values[0]-q)*(a.values[0]-q)+(a.values[4]-q)*(a.values[4]-q)+(a.values[8]-q)*(a.values[8]-q)+2*(b01*b01+b02*b02+b12*b12);
 const double p=std::sqrt(p2/6.0);
 if(p<=std::numeric_limits<double>::epsilon()*std::max(1.0,std::fabs(q))){v={{q,q,q}};return true;}
 Tensor3 b;b.values={{(a.values[0]-q)/p,b01/p,b02/p,b01/p,(a.values[4]-q)/p,b12/p,b02/p,b12/p,(a.values[8]-q)/p}};
 const double det=b.values[0]*(b.values[4]*b.values[8]-b.values[5]*b.values[7])-b.values[1]*(b.values[3]*b.values[8]-b.values[5]*b.values[6])+b.values[2]*(b.values[3]*b.values[7]-b.values[4]*b.values[6]);
 const double phi=std::acos(std::max(-1.0,std::min(1.0,det/2.0)))/3.0, twoP=2*p;
 v[0]=q+twoP*std::cos(phi);v[2]=q+twoP*std::cos(phi+2.09439510239319549);v[1]=3*q-v[0]-v[2];std::sort(v.begin(),v.end(),std::greater<double>());return true;
}
} }
