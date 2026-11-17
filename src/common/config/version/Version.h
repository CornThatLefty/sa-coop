#pragma once

#define COOP_NAME "SA:COOP"
// tools/build.py gives the version resource its numbers from the digits; "-fork.N" names the fork's test build
#define COOP_VERSION_STR "0.3.4-fork.10"

#ifndef RC_INVOKED  // the resource compiler reads only the macros above (VersionInfo.rc)
#include <cstdint>

namespace coop {
// Bumped on any incompatible change of the network protocol.
constexpr uint32_t kProtocolVersion = 65;  // 64: the fork's S_BALANCE, payouts by mode; 65: S_SETTINGS, S_RULES
constexpr uint16_t kDefaultPort = 7788;
// Size of gta_sa.exe 1.0 US (HOODLUM) — the only supported executable.
constexpr uint32_t kSupportedExeSize = 14383616;
}  // namespace coop
#endif
