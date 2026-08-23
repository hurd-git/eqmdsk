#pragma once

#define EQMDSK_VERSION_MAJOR 0
#define EQMDSK_VERSION_MINOR 9
#define EQMDSK_VERSION_PATCH 2
#define EQMDSK_VERSION_STRING "0.9.2"

namespace eqmdsk {

inline constexpr int version_major = EQMDSK_VERSION_MAJOR;
inline constexpr int version_minor = EQMDSK_VERSION_MINOR;
inline constexpr int version_patch = EQMDSK_VERSION_PATCH;
inline constexpr const char* version_string = EQMDSK_VERSION_STRING;

}  // namespace eqmdsk
