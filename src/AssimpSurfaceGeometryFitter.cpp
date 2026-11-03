#include <OpenModel/AssimpSurfaceGeometryFitter.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace {
using V = std::array<double,3>;
using M = std::array<std::array<double,3>,3>;
constexpr double Pi=3.1415926535897932384626433832795;

V add(const V& a,const V& b){return {{a[0]+b[0],a[1]+b[1],a[2]+b[2]}};}
V sub(const V& a,const V& b){return {{a[0]-b[0],a[1]-b[1],a[2]-b[2]}};}
V mul(const V& a,double s){return {{a[0]*s,a[1]*s,a[2]*s}};}
double dot(const V& a,const V& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
V cross(const V& a,const V& b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
double norm(const V& a){return std::sqrt(dot(a,a));}
bool normalize(V& a){const double n=norm(a);if(!std::isfinite(n)||n<=1e-14)return false;a=mul(a,1.0/n);return true;}
bool finite(const V& a){return std::isfinite(a[0])&&std::isfinite(a[1])&&std::isfinite(a[2]);}
void canonicalize(V& a){for(double x:a)if(std::abs(x)>1e-12){if(x<0)a=mul(a,-1);return;}}

bool solve(double a[4][5],int n,double* x){
    double matrixScale=0.0;
    for(int i=0;i<n;++i)for(int j=0;j<n;++j)matrixScale=std::max(matrixScale,std::abs(a[i][j]));
    if(!std::isfinite(matrixScale)||matrixScale==0.0)return false;
    const double pivotTolerance=std::numeric_limits<double>::epsilon()*matrixScale*64.0;
    for(int col=0;col<n;++col){
        int pivot=col;
        for(int row=col+1;row<n;++row)
            if(std::abs(a[row][col])>std::abs(a[pivot][col]))pivot=row;
        if(std::abs(a[pivot][col])<=pivotTolerance)return false;
        if(pivot!=col)for(int k=col;k<=n;++k)std::swap(a[pivot][k],a[col][k]);
        const double d=a[col][col];
        for(int k=col;k<=n;++k)a[col][k]/=d;
        for(int row=0;row<n;++row)if(row!=col){
            const double f=a[row][col];
            for(int k=col;k<=n;++k)a[row][k]-=f*a[col][k];
        }
    }
    for(int i=0;i<n;++i)x[i]=a[i][n];
    return true;
}

std::array<V,3> principalAxes(const std::vector<V>& points,const V& centroid){
    M a{{{{0,0,0}},{{0,0,0}},{{0,0,0}}}};
    for(const auto& p:points){const V q=sub(p,centroid);for(int i=0;i<3;++i)for(int j=0;j<3;++j)a[i][j]+=q[i]*q[j];}
    M v{{{{1,0,0}},{{0,1,0}},{{0,0,1}}}};
    for(int iter=0;iter<64;++iter){
        int p=0,q=1;
        if(std::abs(a[0][2])>std::abs(a[p][q])){p=0;q=2;}
        if(std::abs(a[1][2])>std::abs(a[p][q])){p=1;q=2;}
        if(std::abs(a[p][q])<1e-14)break;
        const double phi=0.5*std::atan2(2.0*a[p][q],a[q][q]-a[p][p]);
        const double c=std::cos(phi),s=std::sin(phi);
        const double app=c*c*a[p][p]-2*s*c*a[p][q]+s*s*a[q][q];
        const double aqq=s*s*a[p][p]+2*s*c*a[p][q]+c*c*a[q][q];
        for(int k=0;k<3;++k)if(k!=p&&k!=q){
            const double akp=c*a[k][p]-s*a[k][q];
            const double akq=s*a[k][p]+c*a[k][q];
            a[k][p]=a[p][k]=akp;a[k][q]=a[q][k]=akq;
        }
        a[p][p]=app;a[q][q]=aqq;a[p][q]=a[q][p]=0;
        for(int k=0;k<3;++k){
            const double vkp=c*v[k][p]-s*v[k][q];
            const double vkq=s*v[k][p]+c*v[k][q];
            v[k][p]=vkp;v[k][q]=vkq;
        }
    }
    std::array<V,3> axes{};
    for(int col=0;col<3;++col){axes[col]={{v[0][col],v[1][col],v[2][col]}};normalize(axes[col]);canonicalize(axes[col]);}
    return axes;
}

void radialMetrics(const std::vector<double>& residuals,double scale,
                   double& rms,double& maximum,double& relativeRms,double& relativeMaximum){
    double sum=0;maximum=0;
    for(double e:residuals){sum+=e*e;maximum=std::max(maximum,e);}
    rms=std::sqrt(sum/static_cast<double>(residuals.size()));
    relativeRms=rms/scale;relativeMaximum=maximum/scale;
}

double angularCoverage(const std::vector<double>& angles){
    if(angles.size()<2)return 0.0;
    auto sorted=angles;std::sort(sorted.begin(),sorted.end());
    double maxGap=0;
    for(std::size_t i=1;i<sorted.size();++i)maxGap=std::max(maxGap,sorted[i]-sorted[i-1]);
    maxGap=std::max(maxGap,2.0*Pi-sorted.back()+sorted.front());
    return std::max(0.0,2.0*Pi-maxGap)*180.0/Pi;
}

struct Candidate {
    AssimpFittedSurfaceType type=AssimpFittedSurfaceType::Freeform;
    V center{{0,0,0}},axis{{0,0,1}},apex{{0,0,0}};
    double radius=0,angle=0,slope=0,rms=0,max=0,relativeRms=std::numeric_limits<double>::infinity();
    double relativeMax=std::numeric_limits<double>::infinity(),coverage=0;
    bool valid=false;
};

bool circleFit(const std::vector<V>& points,const V& centroid,const V& axis,
               Candidate& candidate){
    V reference=std::abs(axis[0])<=std::abs(axis[1])&&std::abs(axis[0])<=std::abs(axis[2])
        ? V{{1,0,0}} : (std::abs(axis[1])<=std::abs(axis[2])?V{{0,1,0}}:V{{0,0,1}});
    V u=cross(axis,reference);if(!normalize(u))return false;
    V v=cross(axis,u);if(!normalize(v))return false;
    double normal[4][5]{};
    std::vector<std::array<double,3>> projected;projected.reserve(points.size());
    double zMin=std::numeric_limits<double>::max(),zMax=-zMin;
    for(const auto& point:points){
        const V q=sub(point,centroid);
        const double x=dot(q,u),y=dot(q,v),z=dot(q,axis);
        projected.push_back({{x,y,z}});
        zMin=std::min(zMin,z);zMax=std::max(zMax,z);
        const double row[3]={2*x,2*y,1},rhs=x*x+y*y;
        for(int i=0;i<3;++i){normal[i][3]+=row[i]*rhs;for(int j=0;j<3;++j)normal[i][j]+=row[i]*row[j];}
    }
    double solution[4]{};
    if(!solve(normal,3,solution))return false;
    const double radius2=solution[2]+solution[0]*solution[0]+solution[1]*solution[1];
    if(!std::isfinite(radius2)||radius2<=0.0)return false;
    const double radius=std::sqrt(radius2);
    if(zMax-zMin<=radius*1e-6)return false;
    std::vector<double> errors,angles;errors.reserve(points.size());angles.reserve(points.size());
    for(const auto& p:projected){
        errors.push_back(std::abs(std::sqrt((p[0]-solution[0])*(p[0]-solution[0])+
                                             (p[1]-solution[1])*(p[1]-solution[1]))-radius));
        angles.push_back(std::atan2(p[1]-solution[1],p[0]-solution[0]));
    }
    candidate.type=AssimpFittedSurfaceType::Cylindrical;
    candidate.center=add(centroid,add(mul(u,solution[0]),mul(v,solution[1])));
    candidate.axis=axis;candidate.radius=radius;
    candidate.coverage=angularCoverage(angles);
    radialMetrics(errors,radius,candidate.rms,candidate.max,candidate.relativeRms,candidate.relativeMax);
    candidate.valid=true;
    return true;
}

bool coneFit(const std::vector<V>& points,const V& centroid,const V& axis,Candidate& candidate){
    std::vector<std::array<double,2>> zr;zr.reserve(points.size());
    double sumZ=0,sumR=0,sumZZ=0,sumZR=0,zMin=std::numeric_limits<double>::max(),zMax=-zMin;
    V reference=std::abs(axis[0])<0.8?V{{1,0,0}}:V{{0,1,0}};
    V u=cross(axis,reference);if(!normalize(u))return false;
    V v=cross(axis,u);if(!normalize(v))return false;
    std::vector<double> angles;angles.reserve(points.size());
    for(const auto& point:points){
        const V q=sub(point,centroid);const double z=dot(q,axis);
        const V radial=sub(q,mul(axis,z));const double r=norm(radial);
        zr.push_back({{z,r}});sumZ+=z;sumR+=r;sumZZ+=z*z;sumZR+=z*r;
        zMin=std::min(zMin,z);zMax=std::max(zMax,z);
        angles.push_back(std::atan2(dot(radial,v),dot(radial,u)));
    }
    const double n=static_cast<double>(zr.size()),den=n*sumZZ-sumZ*sumZ;
    if(std::abs(den)<=std::numeric_limits<double>::epsilon()*
        std::max(std::abs(n*sumZZ),std::abs(sumZ*sumZ))*64.0 || zMax-zMin<=1e-12)return false;
    const double slope=(n*sumZR-sumZ*sumR)/den;
    const double intercept=(sumR-slope*sumZ)/n;
    if(!std::isfinite(slope)||!std::isfinite(intercept)||intercept<=0.0)return false;
    std::vector<double> errors;errors.reserve(zr.size());
    for(const auto& item:zr)errors.push_back(std::abs(item[1]-(intercept+slope*item[0])));
    candidate.type=std::abs(slope)>1e-3?AssimpFittedSurfaceType::Conical:AssimpFittedSurfaceType::Cylindrical;
    candidate.center=centroid;candidate.axis=axis;candidate.radius=intercept;
    candidate.slope=std::abs(slope)>1e-3?slope:0.0;
    candidate.apex=std::abs(slope)>1e-3 ? add(centroid,mul(axis,-intercept/slope)) : centroid;
    candidate.angle=std::atan(std::abs(slope));candidate.coverage=angularCoverage(angles);
    radialMetrics(errors,intercept,candidate.rms,candidate.max,candidate.relativeRms,candidate.relativeMax);
    candidate.valid=true;return true;
}

bool sphereFit(const std::vector<V>& points,const V& centroid,
               const std::array<V,3>& axes,Candidate& candidate){
    // A sphere fit to only two parallel rings is indistinguishable from a
    // cylinder through those samples. Require three resolved levels along
    // every principal direction before accepting a spherical primitive.
    V lo{{std::numeric_limits<double>::max(),std::numeric_limits<double>::max(),std::numeric_limits<double>::max()}};
    V hi{{-std::numeric_limits<double>::max(),-std::numeric_limits<double>::max(),-std::numeric_limits<double>::max()}};
    for(const auto& p:points)for(int k=0;k<3;++k){lo[k]=std::min(lo[k],p[k]);hi[k]=std::max(hi[k],p[k]);}
    const double scale=norm(sub(hi,lo));
    const double levelTolerance=std::max(scale*1e-6,
        std::numeric_limits<double>::epsilon()*std::max({std::abs(lo[0]),std::abs(lo[1]),std::abs(lo[2]),std::abs(hi[0]),std::abs(hi[1]),std::abs(hi[2])})*64.0);
    for(const auto& axis:axes){
        std::vector<double> levels;levels.reserve(points.size());
        for(const auto& p:points)levels.push_back(dot(sub(p,centroid),axis));
        std::sort(levels.begin(),levels.end());
        std::size_t distinct=0;double previous=0.0;
        for(double value:levels)if(distinct==0||std::abs(value-previous)>levelTolerance){++distinct;previous=value;}
        if(distinct<3)return false;
    }
    double normal[4][5]{};
    for(const auto& point:points){
        const V q=sub(point,centroid);const double row[4]={2*q[0],2*q[1],2*q[2],1};
        const double rhs=dot(q,q);
        for(int i=0;i<4;++i){normal[i][4]+=row[i]*rhs;for(int j=0;j<4;++j)normal[i][j]+=row[i]*row[j];}
    }
    double solution[4]{};
    if(!solve(normal,4,solution))return false;
    const V offset{{solution[0],solution[1],solution[2]}};
    const double radius2=solution[3]+dot(offset,offset);
    if(!std::isfinite(radius2)||radius2<=0.0)return false;
    const double radius=std::sqrt(radius2);
    std::vector<double> errors;errors.reserve(points.size());
    for(const auto& point:points)errors.push_back(std::abs(norm(sub(point,add(centroid,offset)))-radius));
    candidate.type=AssimpFittedSurfaceType::Spherical;candidate.center=add(centroid,offset);
    candidate.radius=radius;candidate.axis={{0,0,1}};
    radialMetrics(errors,radius,candidate.rms,candidate.max,candidate.relativeRms,candidate.relativeMax);
    candidate.valid=true;return true;
}
}

bool AssimpSurfaceGeometryFitter::fit(const std::vector<RenderSubMesh>& parts,
    const std::vector<AssimpSurfacePatch>& patches,const AssimpSurfaceFitOptions& options,
    std::vector<AssimpSurfaceFit>& results,std::vector<std::string>& diagnostics){
    results.clear();diagnostics.clear();
    if(!std::isfinite(options.maxRelativeRmsError)||options.maxRelativeRmsError<0||
       !std::isfinite(options.maxRelativeMaxError)||options.maxRelativeMaxError<0||
       !std::isfinite(options.minAngularCoverageDegrees)||options.minAngularCoverageDegrees<0||
       options.minAngularCoverageDegrees>360||
       !std::isfinite(options.minAxisExtentRatio)||options.minAxisExtentRatio<0||
       options.minSampleCount<8){
        diagnostics.push_back("Invalid Assimp analytic surface fitting options.");return false;
    }
    for(std::size_t patchIndex=0;patchIndex<patches.size();++patchIndex){
        const auto& patch=patches[patchIndex];
        if(patch.partIndex>=parts.size()){
            diagnostics.push_back("Patch refers to an unknown Assimp part.");results.clear();return false;
        }
        const auto& part=parts[patch.partIndex];
        if(part.vertices.size()%3!=0||part.indices.size()%3!=0){
            diagnostics.push_back("Malformed source arrays while fitting Assimp patch.");results.clear();return false;
        }
        std::set<V> unique;
        for(const auto triangle:patch.triangleIndices){
            if(triangle>=part.indices.size()/3){
                diagnostics.push_back("Patch triangle index is outside source part.");results.clear();return false;
            }
            for(std::size_t k=0;k<3;++k){
                const auto vi=static_cast<std::size_t>(part.indices[3*triangle+k]);
                if(vi>=part.vertices.size()/3){
                    diagnostics.push_back("Patch vertex index is outside source part.");results.clear();return false;
                }
                V p{{part.vertices[3*vi],part.vertices[3*vi+1],part.vertices[3*vi+2]}};
                if(!finite(p)){diagnostics.push_back("Non-finite point in Assimp patch.");results.clear();return false;}
                unique.insert(p);
            }
        }
        if(unique.size()<3){diagnostics.push_back("Patch has fewer than three unique vertices.");results.clear();return false;}
        std::vector<V> points(unique.begin(),unique.end());
        V centroid{{0,0,0}};for(const auto& p:points)centroid=add(centroid,p);
        centroid=mul(centroid,1.0/static_cast<double>(points.size()));

        AssimpSurfaceFit result;result.patchIndex=patchIndex;result.center=centroid;
        result.sampleCount=points.size();
        if (points.size()<options.minSampleCount) {
            result.type=patch.kind==AssimpSurfacePatchKind::Planar
                ? AssimpFittedSurfaceType::Planar : AssimpFittedSurfaceType::Freeform;
            result.axis=patch.meanNormal;
            result.maxError=patch.maxPlanarityDeviation;
            result.rmsError=patch.maxPlanarityDeviation;
            const double scale=std::max(norm(sub(patch.boundsMax,patch.boundsMin)),1e-30);
            result.relativeMaxError=result.maxError/scale;
            result.relativeRmsError=result.rmsError/scale;
            results.push_back(result);
            continue;
        }
        if(patch.kind==AssimpSurfacePatchKind::Planar){
            result.type=AssimpFittedSurfaceType::Planar;result.axis=patch.meanNormal;
            if(!normalize(result.axis))result.axis={{0,0,1}};
            std::vector<double> errors;errors.reserve(points.size());
            for(const auto& p:points)errors.push_back(std::abs(dot(sub(p,patch.centroid),result.axis)));
            radialMetrics(errors,1.0,result.rmsError,result.maxError,result.relativeRmsError,result.relativeMaxError);
            const double scale=std::max(norm(sub(patch.boundsMax,patch.boundsMin)),1e-30);
            result.relativeRmsError=result.rmsError/scale;result.relativeMaxError=result.maxError/scale;
            results.push_back(result);continue;
        }

        const auto axes=principalAxes(points,centroid);
        Candidate best;
        auto consider=[&](const Candidate& candidate){
            if(!candidate.valid||candidate.relativeRms>options.maxRelativeRmsError||
               candidate.relativeMax>options.maxRelativeMaxError||
               candidate.coverage<options.minAngularCoverageDegrees)return;
            if(!best.valid||candidate.relativeRms<best.relativeRms)best=candidate;
        };
        Candidate sphere;
        if(sphereFit(points,centroid,axes,sphere)) {
            sphere.coverage=360.0;
            consider(sphere);
        }
        for(auto axis:axes){
            if(!normalize(axis))continue;canonicalize(axis);
            Candidate cylinder;
            if(circleFit(points,centroid,axis,cylinder)){
                const double axialExtent=[&](){
                    double lo=std::numeric_limits<double>::max(),hi=-lo;
                    for(const auto& p:points){const double z=dot(sub(p,centroid),axis);lo=std::min(lo,z);hi=std::max(hi,z);}
                    return hi-lo;
                }();
                if(axialExtent>=cylinder.radius*options.minAxisExtentRatio)consider(cylinder);
            }
            Candidate cone;
            if(coneFit(points,centroid,axis,cone)&&cone.coverage>=options.minAngularCoverageDegrees)
                consider(cone);
        }
        if(best.valid){
            result.type=best.type;result.center=best.center;result.axis=best.axis;
            result.radius=best.radius;result.coneHalfAngleRadians=best.angle;
            result.apex=best.apex;result.coneSlope=best.slope;
            result.rmsError=best.rms;result.maxError=best.max;
            result.relativeRmsError=best.relativeRms;result.relativeMaxError=best.relativeMax;
            result.angularCoverageDegrees=best.coverage;
        }else{
            result.type=AssimpFittedSurfaceType::Freeform;
            result.axis=patch.meanNormal;
            result.rmsError=patch.maxPlanarityDeviation;
            result.maxError=patch.maxPlanarityDeviation;
            const double scale=std::max(norm(sub(patch.boundsMax,patch.boundsMin)),1e-30);
            result.relativeRmsError=result.rmsError/scale;
            result.relativeMaxError=result.maxError/scale;
        }
        results.push_back(result);
    }
    diagnostics.push_back("Fitted "+std::to_string(results.size())+
        " Assimp surface patch(es) using plane, cylinder, sphere and cone models.");
    return true;
}
