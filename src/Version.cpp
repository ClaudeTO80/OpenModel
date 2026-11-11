#include <OpenModel/Version.hpp>

namespace OpenModel {

Version version() noexcept {
    return Version{OPENMODEL_VERSION_MAJOR, OPENMODEL_VERSION_MINOR, OPENMODEL_VERSION_PATCH};
}

const char* versionString() noexcept {
    return OPENMODEL_VERSION_STRING;
}

} // namespace OpenModel
