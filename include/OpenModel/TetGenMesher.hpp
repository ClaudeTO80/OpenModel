#pragma once
#include <OpenModel/Mesher.hpp>
#include <OpenModel/MeshTopology.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

// Header TetGen
#define TETLIBRARY
#include <tetgen.h>

class TetGenMesher : public Mesher {
public:
    std::string getName() const override ;

    bool buildMesh(const std::shared_ptr<CADModel>& cadModel, 
                   const MeshingOptions& options, 
                   MeshData& outMesh) override 
    ;

    const std::vector<std::string>& diagnostics() const override { return m_diagnostics; }

private:
    std::vector<std::string> m_diagnostics;
};