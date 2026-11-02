/**
 * @file make_assembly_fixture.cpp
 * @brief Writes the XDE assembly test fixture (see support/assembly_fixture.hpp) to the given STEP path.
 */
#include "support/assembly_fixture.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: make_assembly_fixture <out.step>\n"; return 2; }
    if (!writeAssemblyFixture(argv[1])) { std::cerr << "could not write the fixture\n"; return 1; }
    return 0;
}
