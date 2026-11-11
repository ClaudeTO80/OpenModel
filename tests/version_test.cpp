#include <OpenModel/Version.hpp>

#include <cassert>
#include <string>

#ifndef OPENMODEL_EXPECTED_VERSION
#error "CMake must define OPENMODEL_EXPECTED_VERSION (the configured project version)"
#endif

int main() {
    const std::string expected = OPENMODEL_EXPECTED_VERSION;

    // The macros come from the generated header; they must agree with each other and with CMake.
    const std::string fromMacros = std::to_string(OPENMODEL_VERSION_MAJOR) + "." +
                                   std::to_string(OPENMODEL_VERSION_MINOR) + "." +
                                   std::to_string(OPENMODEL_VERSION_PATCH);
    assert(fromMacros == expected);
    assert(std::string(OPENMODEL_VERSION_STRING) == expected);
    static_assert(OPENMODEL_VERSION_NUMBER ==
                      OPENMODEL_VERSION_MAJOR * 10000 + OPENMODEL_VERSION_MINOR * 100 + OPENMODEL_VERSION_PATCH,
                  "OPENMODEL_VERSION_NUMBER must be major*10000 + minor*100 + patch");
    static_assert(OPENMODEL_VERSION_MINOR >= 0 && OPENMODEL_VERSION_MINOR <= 99, "minor must fit two digits");
    static_assert(OPENMODEL_VERSION_PATCH >= 0 && OPENMODEL_VERSION_PATCH <= 99, "patch must fit two digits");

    // The runtime API comes from the linked library and must report the same version.
    const OpenModel::Version v = OpenModel::version();
    assert(v.major == OPENMODEL_VERSION_MAJOR);
    assert(v.minor == OPENMODEL_VERSION_MINOR);
    assert(v.patch == OPENMODEL_VERSION_PATCH);
    assert(expected == OpenModel::versionString());
    assert(OpenModel::versionString() == OpenModel::versionString()); // static storage, stable pointer
    return 0;
}
