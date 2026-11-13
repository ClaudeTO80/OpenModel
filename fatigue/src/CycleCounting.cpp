#include <OpenModel/Fatigue/CycleCounting.hpp>

#include <algorithm>
#include <cmath>

namespace OpenModel {
namespace Fatigue {
namespace {

struct Point {
    double time;
    double value;
    std::size_t index;
};

double projectedValue(const Tensor3& tensor, const std::array<double,3>& n) {
    double value = 0.0;
    for (std::size_t i=0;i<3;++i)
        for (std::size_t j=0;j<3;++j)
            value += n[i] * tensor(i,j) * n[j];
    return value;
}

bool validDirection(const std::array<double,3>& n) {
    double norm2=0.0;
    for(double x:n) {
        if(!std::isfinite(x)) return false;
        norm2+=x*x;
    }
    return std::fabs(norm2-1.0)<=1e-8;
}

void appendCycle(std::vector<LoadCycle>& out, const Point& a, const Point& b, double count) {
    LoadCycle cycle;
    cycle.start={a.time,a.value,a.index};
    cycle.end={b.time,b.value,b.index};
    cycle.range=std::fabs(b.value-a.value);
    cycle.mean=0.5*(a.value+b.value);
    cycle.count=count;
    out.push_back(cycle);
}

std::vector<Point> reversals(const StressHistory& history, const std::array<double,3>& direction) {
    std::vector<Point> points;
    points.reserve(history.size());
    for(std::size_t i=0;i<history.size();++i) {
        const auto& sample=history.samples()[i];
        const double value=projectedValue(sample.prepared.tensor,direction);
        if(points.empty() || value!=points.back().value)
            points.push_back({sample.time,value,i});
        else
            points.back()={sample.time,value,i}; // plateau: retain its last physical occurrence
    }
    if(points.size()<3) return points;
    std::vector<Point> result;
    result.reserve(points.size());
    result.push_back(points.front());
    for(std::size_t i=1;i+1<points.size();++i) {
        const double before=points[i].value-points[i-1].value;
        const double after=points[i+1].value-points[i].value;
        if((before>0.0 && after<0.0)||(before<0.0 && after>0.0))
            result.push_back(points[i]);
    }
    result.push_back(points.back());
    return result;
}

} // namespace

CycleCountingResult CycleCounting::rainflow(
    const StressHistory& history, const StressProjection& projection) {
    CycleCountingResult result;
    if(history.size()<2) {
        result.diagnostic="At least two history samples are required.";
        return result;
    }
    if(!validDirection(projection.direction)) {
        result.diagnostic="Projection direction must be a finite unit vector in the history frame.";
        return result;
    }
    for(const auto& sample:history.samples()) {
        if(!sample.prepared || !std::isfinite(sample.time)) {
            result.diagnostic="History contains an invalid prepared sample or time.";
            return result;
        }
    }

    const auto turns=reversals(history,projection.direction);
    if(turns.size()<2) {
        result.success=true;
        return result;
    }

    std::vector<Point> stack;
    stack.reserve(turns.size());
    for(const auto& point:turns) {
        stack.push_back(point);
        while(stack.size()>=3) {
            const std::size_t n=stack.size();
            const double older=std::fabs(stack[n-2].value-stack[n-3].value);
            const double newer=std::fabs(stack[n-1].value-stack[n-2].value);
            if(newer<older) break;

            if(n==3) {
                appendCycle(result.cycles,stack[n-3],stack[n-2],0.5);
                stack.erase(stack.begin());
            } else {
                appendCycle(result.cycles,stack[n-3],stack[n-2],1.0);
                stack.erase(stack.end()-3,stack.end()-1);
            }
        }
    }
    for(std::size_t i=1;i<stack.size();++i)
        appendCycle(result.cycles,stack[i-1],stack[i],0.5);

    result.success=true;
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
