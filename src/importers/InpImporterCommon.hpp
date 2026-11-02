#pragma once
#include <OpenModel/MeshImporter.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace OpenModel::detail {
inline std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}
inline std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}
inline std::vector<std::string> splitComma(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) out.push_back(trim(item));
    if (!line.empty() && line.back() == ',') out.emplace_back();
    return out;
}
inline bool parseU64(const std::string& s, std::uint64_t& value) {
    try {
        std::size_t used = 0;
        const auto v = std::stoull(trim(s), &used);
        if (used != trim(s).size()) return false;
        value = static_cast<std::uint64_t>(v); return true;
    } catch (...) { return false; }
}
inline bool parseDouble(const std::string& s, double& value) {
    try {
        std::size_t used = 0; const auto t = trim(s);
        value = std::stod(t, &used); return used == t.size();
    } catch (...) { return false; }
}
inline std::unordered_map<std::string, std::string> keywordOptions(const std::string& line) {
    std::unordered_map<std::string, std::string> out;
    const auto parts = splitComma(line);
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto eq = parts[i].find('=');
        if (eq == std::string::npos) out[upper(trim(parts[i]))] = {};
        else out[upper(trim(parts[i].substr(0, eq)))] = trim(parts[i].substr(eq + 1));
    }
    return out;
}
inline bool elementType(const std::string& raw, ElementType& type) {
    const auto t = upper(raw);
    if (t == "S3" || t == "CPS3" || t == "CPE3") type = ElementType::Triangle3;
    else if (t == "S6" || t == "CPS6" || t == "CPE6") type = ElementType::Triangle6;
    else if (t == "S4" || t == "S4R" || t == "CPS4" || t == "CPE4") type = ElementType::Quadrilateral4;
    else if (t == "S8" || t == "S8R" || t == "CPS8" || t == "CPE8") type = ElementType::Quadrilateral8;
    else if (t == "S9" || t == "CPS9" || t == "CPE9") type = ElementType::Quadrilateral9;
    else if (t == "C3D4" || t == "C3D4H") type = ElementType::Tetrahedron4;
    else if (t == "C3D10" || t == "C3D10H") type = ElementType::Tetrahedron10;
    else if (t == "C3D8" || t == "C3D8R" || t == "C3D8I") type = ElementType::Hexahedron8;
    else if (t == "C3D20" || t == "C3D20R") type = ElementType::Hexahedron20;
    else if (t == "C3D6") type = ElementType::Prism6;
    else if (t == "C3D15") type = ElementType::Prism15;
    else if (t == "C3D5") type = ElementType::Pyramid5;
    else return false;
    return true;
}
inline MeshImportResult readInp(const std::filesystem::path& path, MeshData& destination,
                                const MeshImportOptions& options, const std::string& dialect) {
    MeshImportResult result;
    std::ifstream in(path);
    if (!in) { result.addError(ImportStatus::IoError, "IO_OPEN", "Cannot open input file: " + path.string()); return result; }
    MeshData temporary;
    enum class Section { None, Nodes, Elements, NodeSet, ElementSet, Other } section = Section::None;
    std::string activeSet, activeBlock;
    ElementType activeType = ElementType::Unknown;
    bool generate = false;
    std::unordered_map<std::string, std::vector<std::uint64_t>> pendingNodeSets, pendingElementSets;
    std::unordered_map<std::uint64_t, int> elementDimensions;
    std::string line;
    std::size_t lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line.rfind("**", 0) == 0) continue;
        if (line[0] == '*') {
            const auto keyEnd = line.find(',');
            const auto keyword = upper(trim(line.substr(0, keyEnd)));
            const auto opts = keywordOptions(line);
            section = Section::Other; activeSet.clear(); activeBlock.clear(); generate = false;
            if (keyword == "*NODE") section = Section::Nodes;
            else if (keyword == "*ELEMENT") {
                const auto it = opts.find("TYPE");
                const auto setIt = opts.find("ELSET");
                if (it == opts.end() || !elementType(it->second, activeType)) {
                    result.addError(ImportStatus::UnsupportedFeature, "ELEMENT_TYPE", "Unsupported or missing element TYPE in " + dialect, lineNo); break;
                }
                activeBlock = setIt == opts.end() ? (it->second + "_BLOCK") : setIt->second;
                section = Section::Elements;
            } else if (keyword == "*NSET" && options.importNodeSets) {
                const auto it = opts.find("NSET");
                if (it == opts.end() || it->second.empty()) { result.addError(ImportStatus::InvalidInput, "NSET_NAME", "NSET keyword has no name", lineNo); break; }
                activeSet = it->second; section = Section::NodeSet; generate = opts.count("GENERATE") != 0;
            } else if ((keyword == "*ELSET" || keyword == "*ELEMENTSET") && options.importElementSets) {
                const auto it = opts.find("ELSET");
                if (it == opts.end() || it->second.empty()) { result.addError(ImportStatus::InvalidInput, "ELSET_NAME", "ELSET keyword has no name", lineNo); break; }
                activeSet = it->second; section = Section::ElementSet; generate = opts.count("GENERATE") != 0;
            }
            continue;
        }
        const auto fields = splitComma(line);
        if (section == Section::Nodes) {
            std::uint64_t id{}; double xyz[3]{};
            if (fields.size() < 3 || !parseU64(fields[0], id) || !parseDouble(fields[1], xyz[0]) || !parseDouble(fields[2], xyz[1]) || (fields.size() > 3 && !parseDouble(fields[3], xyz[2]))) {
                result.addError(ImportStatus::InvalidInput, "NODE_RECORD", "Invalid node record", lineNo); break;
            }
            try { temporary.addNode(id, xyz[0], xyz[1], fields.size() > 3 ? xyz[2] : 0.0); }
            catch (const std::exception& e) { result.addError(ImportStatus::InvalidInput, "NODE_RECORD", e.what(), lineNo); break; }
        } else if (section == Section::Elements) {
            std::uint64_t id{};
            if (fields.size() < 2 || !parseU64(fields[0], id)) { result.addError(ImportStatus::InvalidInput, "ELEMENT_RECORD", "Invalid element record", lineNo); break; }
            const auto count = elementNodeCount(activeType);
            if (fields.size() != count + 1) { result.addError(ImportStatus::InvalidInput, "ELEMENT_ARITY", "Wrong number of nodes for element type", lineNo); break; }
            std::vector<NodeIndex> conn; conn.reserve(count);
            bool bad = false;
            for (std::size_t i = 1; i < fields.size(); ++i) {
                std::uint64_t nodeId{};
                if (!parseU64(fields[i], nodeId) || !temporary.hasNodeId(nodeId)) { bad = true; break; }
                conn.push_back(temporary.nodeIndex(nodeId));
            }
            if (bad) { result.addError(ImportStatus::InvalidInput, "ELEMENT_NODE_REF", "Invalid or forward-referenced node ID", lineNo); break; }
            try {
                temporary.addElement(activeBlock, activeType, id, conn);
                elementDimensions[id] = elementDimension(activeType);
            } catch (const std::exception& e) { result.addError(ImportStatus::InvalidInput, "ELEMENT_RECORD", e.what(), lineNo); break; }
        } else if (section == Section::NodeSet || section == Section::ElementSet) {
            std::vector<std::uint64_t> ids;
            for (const auto& field : fields) {
                if (field.empty()) continue;
                std::uint64_t id{};
                if (!parseU64(field, id)) { result.addError(ImportStatus::InvalidInput, "SET_RECORD", "Invalid set ID", lineNo); break; }
                ids.push_back(id);
            }
            if (result.status != ImportStatus::Success) break;
            auto& sets = section == Section::NodeSet ? pendingNodeSets : pendingElementSets;
            auto& target = sets[activeSet];
            if (generate) {
                if (ids.size() != 3 || ids[2] == 0 || ids[1] < ids[0]) { result.addError(ImportStatus::InvalidInput, "SET_GENERATE", "GENERATE expects start, end, increment", lineNo); break; }
                for (auto v = ids[0]; v <= ids[1];) {
                    target.push_back(v);
                    if (ids[2] > ids[1] - v) break;
                    v += ids[2];
                }
            } else target.insert(target.end(), ids.begin(), ids.end());
        }
    }
    if (result.status != ImportStatus::Success) return result;
    if (!in.eof() && in.fail()) { result.addError(ImportStatus::IoError, "IO_READ", "Error while reading input file", lineNo); return result; }
    if (temporary.getNumNodes() == 0) { result.addError(ImportStatus::InvalidInput, "EMPTY_NODES", "No nodes were imported"); return result; }
    for (const auto& set : pendingNodeSets) {
        std::vector<NodeIndex> indices; indices.reserve(set.second.size());
        for (const auto id : set.second) {
            if (!temporary.hasNodeId(id)) { result.addError(ImportStatus::InvalidInput, "NSET_NODE_REF", "Node set references unknown node ID " + std::to_string(id)); return result; }
            indices.push_back(temporary.nodeIndex(id));
        }
        temporary.addNodeSet(set.first, indices);
    }
    int physicalGroupTag = 1;
    for (const auto& set : pendingElementSets) {
        std::vector<ElementId> ids; ids.reserve(set.second.size());
        int dimension = -1;
        for (const auto id : set.second) {
            const auto it = elementDimensions.find(id);
            if (it == elementDimensions.end()) { result.addError(ImportStatus::InvalidInput, "ELSET_ELEMENT_REF", "Element set references unknown element ID " + std::to_string(id)); return result; }
            if (dimension == -1) dimension = it->second;
            else if (dimension != it->second) dimension = -2;
            ids.push_back(id);
        }
        if (options.importPhysicalGroups) {
            if (dimension < 0 || dimension > 3 || ids.empty()) {
                result.addWarning("ELSET_NOT_PHYSICAL_GROUP",
                    "Element set '" + set.first + "' is empty or mixes element dimensions; preserved set membership is not representable as one physical group");
            } else {
                try {
                    temporary.addPhysicalGroup(set.first, dimension, physicalGroupTag++, ids);
                } catch (const std::exception& e) {
                    result.addError(ImportStatus::InvalidInput, "ELSET_GROUP", e.what());
                    return result;
                }
            }
        }
    }
    const auto validation = temporary.validate();
    if (!validation.empty()) { result.addError(ImportStatus::InvalidInput, "MESH_VALIDATION", validation.front()); return result; }
    result.nodesRead = temporary.getNumNodes(); result.elementsRead = temporary.getNumElements();
    result.nodeSetsRead = pendingNodeSets.size(); result.elementSetsRead = pendingElementSets.size();
    destination = std::move(temporary);
    return result;
}
} // namespace OpenModel::detail
