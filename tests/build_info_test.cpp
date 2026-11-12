#include <OpenModel/BuildInfo.hpp>
#include <OpenModel/Version.hpp>

#include <cassert>
#include <set>
#include <string>

#ifndef OPENMODEL_EXPECTED_BUILD_SHARED
#error "CMake must define OPENMODEL_EXPECTED_BUILD_SHARED (1 or 0, the value of OPENMODEL_BUILD_SHARED)"
#endif
#ifndef OPENMODEL_EXPECTED_CONFIGURATION
#error "CMake must define OPENMODEL_EXPECTED_CONFIGURATION (the build configuration, possibly empty)"
#endif

int main() {
    const OpenModel::BuildInfo& info = OpenModel::buildInfo();

    // The same object every time, and consistent with the version functions.
    assert(&info == &OpenModel::buildInfo());
    assert(info.version == OpenModel::versionString());

    // Everything the About dialog prints must be present.
    assert(!info.compiler.empty() && info.compiler != " ");
    assert(!info.system.empty() && info.system != " ");
    assert(!info.cmakeVersion.empty());
    assert(info.cxxStandard == "C++17" || info.cxxStandard == "C++20" || info.cxxStandard == "C++23");

    // The configuration is what CMake generated for this target ("unspecified" when it had none).
    const std::string expectedConfiguration = OPENMODEL_EXPECTED_CONFIGURATION;
    assert(info.configuration == (expectedConfiguration.empty() ? "unspecified" : expectedConfiguration));

    // Options: unique names, only ON or OFF, and the ones this test knows the value of.
    assert(!info.options.empty());
    std::set<std::string> names;
    std::string tests, shared;
    for (const OpenModel::BuildOption& option : info.options) {
        assert(option.name.rfind("OPENMODEL_", 0) == 0);
        assert(option.value == "ON" || option.value == "OFF");
        assert(names.insert(option.name).second);
        if (option.name == "OPENMODEL_BUILD_TESTS") tests = option.value;
        if (option.name == "OPENMODEL_BUILD_SHARED") shared = option.value;
    }
    assert(tests == "ON"); // this test only exists when the tests are built
    assert(shared == (OPENMODEL_EXPECTED_BUILD_SHARED ? "ON" : "OFF"));
    assert(names.count("OPENMODEL_BUILD_OCC_BACKEND") == 1);
    assert(names.count("OPENMODEL_BUILD_GMSH_BACKEND") == 1);
    return 0;
}
