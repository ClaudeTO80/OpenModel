#include <OpenModel/Fatigue/TensorNormalizer.hpp>
#include <cassert>
#include <cmath>
using namespace OpenModel::Fatigue;
static bool near(double a,double b){return std::fabs(a-b)<1e-10*(1+std::fabs(a)+std::fabs(b));}
int main(){
 ResultSample s;s.quantity=QuantityKind::Stress;s.componentKind=ComponentKind::SymmetricTensor;s.frame=ReferenceFrame::Global;
 s.componentLabels={"SXY","SZZ","SXX","SYZ","SYY","SXZ"};s.components={4,30,10,6,20,5};
 auto n=TensorNormalizer::normalizeTensor(s);assert(n);assert(near(n.tensor(0,0),10)&&near(n.tensor(1,1),20)&&near(n.tensor(2,2),30));assert(near(n.tensor(0,1),4)&&near(n.tensor(0,2),5)&&near(n.tensor(1,2),6));
 s.frame=ReferenceFrame::Local;s.coordinateTransform.available=true;s.coordinateTransform.localToGlobal={{0,-1,0,1,0,0,0,0,1}};
 n=TensorNormalizer::normalizeTensor(s);assert(n);assert(near(n.tensor(0,0),20)&&near(n.tensor(1,1),10));assert(near(n.tensor(0,1),-4)&&near(n.tensor(0,2),-6)&&near(n.tensor(1,2),5));
 ResultSample e;e.quantity=QuantityKind::Strain;e.componentKind=ComponentKind::SymmetricTensor;e.frame=ReferenceFrame::Global;e.shearConvention=ShearStrainConvention::Engineering;
 e.componentLabels={"E11","E22","E33","E12","E13","E23"};e.components={.1,.2,.3,.04,.06,.08};
 auto en=TensorNormalizer::normalizeTensor(e);assert(en);assert(near(en.tensor(0,1),.02)&&near(en.tensor(0,2),.03)&&near(en.tensor(1,2),.04));
 e.shearConvention=ShearStrainConvention::Tensorial;en=TensorNormalizer::normalizeTensor(e);assert(en&&near(en.tensor(0,1),.04));
 e.shearConvention=ShearStrainConvention::Unknown;assert(TensorNormalizer::normalizeTensor(e).status==TensorNormalizationStatus::UnknownShearConvention);
 Tensor3 d;d.values={{100,0,0,0,50,0,0,0,0}};assert(near(TensorNormalizer::vonMisesStress(d),std::sqrt(7500.0)));
 std::array<double,3> p{};assert(TensorNormalizer::principalValues(d,p));assert(near(p[0],100)&&near(p[1],50)&&near(p[2],0));
 Tensor3 sh;sh.values={{0,25,0,25,0,0,0,0,0}};assert(near(TensorNormalizer::vonMisesStress(sh),25*std::sqrt(3.0)));assert(TensorNormalizer::principalValues(sh,p));assert(near(p[0],25)&&near(p[1],0)&&near(p[2],-25));
 s.coordinateTransform.available=false;assert(TensorNormalizer::normalizeTensor(s).status==TensorNormalizationStatus::MissingCoordinateTransform);
 ResultSample v;v.componentKind=ComponentKind::Vector;v.frame=ReferenceFrame::Local;v.componentLabels={"U3","U1","U2"};v.components={3,1,2};v.coordinateTransform=s.coordinateTransform;v.coordinateTransform.available=true;
 auto vn=TensorNormalizer::normalizeVector(v);assert(vn);assert(near(vn.vector[0],-2)&&near(vn.vector[1],1)&&near(vn.vector[2],3));
 v.componentLabels[2]="U1";assert(TensorNormalizer::normalizeVector(v).status==TensorNormalizationStatus::DuplicateComponent);
 return 0;
}