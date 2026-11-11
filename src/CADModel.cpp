#include <OpenModel/CADModel.hpp>

#include <algorithm>

// Backends may  this overload to honor angular deflection. The
    // default preserves compatibility with existing CADModel implementations.
    void CADModel::tessellate(double linearDeflection, double angularDeflection) {

        (void)angularDeflection;
        tessellate(linearDeflection);
    
}

// Diagnostic text from the most recent load/tessellation operation.
    const std::string& CADModel::getLastError() const {

        static const std::string empty;
        return empty;
    
}

const std::vector<CADComponent>& CADModel::getComponents() const {
    static const std::vector<CADComponent> empty;
    return empty;
}

const std::vector<CADGroup>& CADModel::getGroups() const {
    static const std::vector<CADGroup> empty;
    return empty;
}

std::vector<int> CADModel::solidsContainingPoint(const std::array<double, 3>&, double) const { return {}; }

std::vector<int> CADModel::facesContainingPoint(const std::array<double, 3>&, double) const { return {}; }

bool CADModel::faceContainsPoint(int faceId, const std::array<double, 3>& point, double tolerance,
                                 const std::array<double, 2>*) const {
    const auto faces = facesContainingPoint(point, tolerance);
    return std::find(faces.begin(), faces.end(), faceId) != faces.end();
}

std::vector<int> CADModel::edgesContainingPoint(const std::array<double, 3>&, double) const { return {}; }

std::vector<int> CADModel::verticesContainingPoint(const std::array<double, 3>&, double) const { return {}; }
