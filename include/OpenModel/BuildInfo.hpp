#pragma once

#include <string>
#include <vector>

/**
 * @file BuildInfo.hpp
 * @brief How the linked OpenModel library was built.
 *
 * The values are fixed when CMake configures the project and the library is compiled, so they describe the
 * library that is actually linked into the running program, not the headers a program was compiled against.
 */

namespace OpenModel {

/** @brief One build setting: the name of a CMake option and the value it had at configure time. */
struct BuildOption {
    std::string name;  ///< CMake option name, for example `OPENMODEL_BUILD_OCC_BACKEND`.
    std::string value; ///< `"ON"` or `"OFF"`.
};

/** @brief Build description of the OpenModel library. */
struct BuildInfo {
    std::string version;                ///< Library version, `"MAJOR.MINOR.PATCH"` (same text as versionString()).
    std::string configuration;          ///< Build configuration (`Release`, `Debug`, ...), `"unspecified"` when CMake had none.
    std::string compiler;               ///< Compiler id and version, for example `"GNU 13.3.0"`.
    std::string cxxStandard;            ///< C++ standard the library was compiled with, for example `"C++17"`.
    std::string system;                 ///< Target operating system and processor, for example `"Linux x86_64"`.
    std::string cmakeVersion;           ///< Version of CMake that configured the build.
    std::vector<BuildOption> options;   ///< The `OPENMODEL_*` CMake options that select modules and backends.
};

/**
 * @brief Build description of the OpenModel library that is linked into the running program.
 * @return A reference to an object with static storage duration; it is never modified after start-up.
 */
const BuildInfo& buildInfo();

} // namespace OpenModel
