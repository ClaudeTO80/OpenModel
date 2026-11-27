// Runs a heat 2d script from a file and prints the CSV of the result on stdout.
#include <OpenModel/Heat2DScript.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream in(argv[1]);
    std::stringstream ss; ss << in.rdbuf();
    OpenModel::Heat2D::ScriptProblem p;
    std::vector<std::string> errors;
    if (!OpenModel::Heat2D::parseScript(ss.str(), p, errors)) {
        for (const auto& e : errors) std::cerr << e << "\n";
        return 1;
    }
    auto r = OpenModel::Heat2D::runScript(p);
    if (!r.error.empty()) { std::cerr << r.error << "\n"; return 1; }
    std::cout << OpenModel::Heat2D::resultToCsv(r, p);
    return 0;
}
