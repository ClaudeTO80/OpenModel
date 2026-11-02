#include <OpenModel/Fatigue/MeanStressCorrection.hpp>
#include <cassert>
#include <cmath>
using namespace OpenModel::Fatigue;
int main() {
    MeanStressCorrectionParameters p;
    p.ultimateTensileStrength=600.0; p.yieldStrength=350.0; p.trueFractureStrength=900.0;
    auto g=MeanStressCorrection::apply(MeanStressCorrectionKind::Goodman,100.0,120.0,p);
    assert(g && std::fabs(g.correctedAmplitude-125.0)<1e-12);
    auto ge=MeanStressCorrection::apply(MeanStressCorrectionKind::Gerber,100.0,300.0,p);
    assert(ge && std::fabs(ge.correctedAmplitude-133.3333333333333)<1e-10);
    auto so=MeanStressCorrection::apply(MeanStressCorrectionKind::Soderberg,100.0,70.0,p);
    assert(so && std::fabs(so.correctedAmplitude-125.0)<1e-12);
    auto mo=MeanStressCorrection::apply(MeanStressCorrectionKind::Morrow,100.0,90.0,p);
    assert(mo && std::fabs(mo.correctedAmplitude-111.1111111111111)<1e-10);
    auto swt=MeanStressCorrection::apply(MeanStressCorrectionKind::SmithWatsonTopper,100.0,50.0,p);
    assert(swt && std::fabs(swt.correctedAmplitude-std::sqrt(15000.0))<1e-12);
    assert(MeanStressCorrection::apply(MeanStressCorrectionKind::Goodman,100.0,600.0,p).status==MeanStressCorrectionStatus::OutsideMaterialStrength);
    assert(MeanStressCorrection::apply(MeanStressCorrectionKind::Soderberg,100.0,0.0,{}).status==MeanStressCorrectionStatus::MissingMaterialProperty);
    assert(MeanStressCorrection::apply(MeanStressCorrectionKind::SmithWatsonTopper,100.0,-100.0,p).status==MeanStressCorrectionStatus::NonTensileMaximumStress);
    assert(MeanStressCorrection::apply(MeanStressCorrectionKind::Gerber,100.0,600.0,p).status==MeanStressCorrectionStatus::NonPositiveDenominator);
    return 0;
}
