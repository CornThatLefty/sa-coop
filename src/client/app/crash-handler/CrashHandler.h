#pragma once
#include <string>

namespace coop {

// Writes coop\crash.log (exception, module+offset, registers, probable call stack) for fatal
// exceptions. First-chance access violations are logged too (limited), because mods and the game
// sometimes swallow them and the real crash happens later.
void InstallCrashHandler(const std::wstring& coopDir);
// Plugins loaded after coop (CrashInfo.SA, modloader, SilentPatch) put their own filter over it, and the process stayed
// after a crash (the server it started on the port) when theirs did not call coop's: coop's goes back on top, theirs
// after it. Called once the plugins are loaded.
void ReclaimCrashFilter();

// Hang detector: the main thread calls Heartbeat() regularly (frame hooks, skeleton events);
// if no heartbeat arrives for `seconds`, the main thread stack is dumped into crash.log once.
void StartHangDetector(unsigned seconds);
void Heartbeat();

// The process's memory, in MB: its address space in all (2047 for gta_sa.exe as it comes, 4095 with the large-address
// flag in its header) and free, the largest free block (0 unless asked for: a walk over every region), and what the
// process has committed.
struct MemoryState {
    unsigned totalMb = 0, freeMb = 0, largestMb = 0, usedMb = 0;
};
MemoryState ReadMemoryState(bool largest);

}  // namespace coop
