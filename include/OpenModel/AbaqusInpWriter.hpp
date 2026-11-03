#pragma once

#include <OpenModel/MeshData.hpp>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

struct AbaqusWriteResult {
    bool success = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::size_t nodesWritten = 0;
    std::size_t elementsWritten = 0;
    std::size_t elementSetsWritten = 0;
    std::size_t nodeSetsWritten = 0;
};

struct AbaqusWriteOptions {
    bool writeHeading = true;
    bool writeNodeComments = false;
};

// Writes the MeshData model to the Abaqus/CalculiX .inp keyword format.
// ElementBlock names and physical/generic mesh-group memberships are exported as
// *ELSETs; generic group node memberships become <name>_NODES *NSETs. Node IDs
// and element IDs are preserved; connectivity is translated from NodeIndex to NodeId.
class AbaqusInpWriter {
public:
    static AbaqusWriteResult writeFile(const std::string& path,
                                       const MeshData& mesh,
                                       const AbaqusWriteOptions& options = {});

    static std::string abaqusElementType(ElementType type) ;
    /// Name used for an element block or group in the file (upper case, only A-Z 0-9 and '_').
    static std::string sanitizeName(const std::string& input) ;
};
