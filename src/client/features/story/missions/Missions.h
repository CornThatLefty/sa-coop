#pragma once
#include <cstdint>
#include <string>

namespace coop {

// a mission marker waits for everybody: "Waiting for your partner" for the host, "<host> is waiting for you..." for the partner, the
// partner's call from one for the host (its keys in hint); empty otherwise
std::string MissionWaitText(std::string& hint);

// someone wants to skip the cutscene: the line for the box ("Tester1 wants to skip the cutscene") and the keys to
// answer (empty when this player said yes already); empty when no vote is on
std::string CutsceneSkipText(std::string& hint);
// Esc: this player wants to watch on. False when there was nothing to answer (the key goes on to the game).
bool CutsceneSkipDecline();
// what the skip button does in a cutscene (debug bot)
void CutsceneSkipPress();
// the mission flag was raised by hand for the story, not by a side activity's script (debug bot)
void ClaimMissionFlag();

// a side activity's call to play it together: the line for the box ("%s invites you to a joint taxi job",
// "Invite %s to a joint taxi job?") and the keys to answer; empty when there is nothing to say
std::string SideCallText(std::string& hint);
// Y (yes) or N: answers the call (a side activity's, or the partner's from a mission's start point), or makes it. False
// when there was nothing to answer (the key goes on to the game).
bool SideCallKey(bool yes);
// the call of this player's side activity to play it together can be made now (its box shows); withdrawn: not any more
// (it is played as a contest)
bool SideCallOpen();
void SideCallWithdraw();

// a meter of this game's own player (a bar of COnscreenTimer on the script variable at varOffset, its GXT label) added in
// a free counter or taken away: kept over the counters the other game's mission sends (its own meter of its player)
void ShowOwnCounter(uint32_t varOffset, const char* label, bool on);

}  // namespace coop
