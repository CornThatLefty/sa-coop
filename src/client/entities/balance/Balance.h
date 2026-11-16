#pragma once
// The balance of a game for several players (docs/FORK.md): what the server's settings (S_BALANCE) make of the enemies'
// strength and of the rewards, by the number of players. Every game counts with the same settings; with none received
// (offline, an older server) every multiplier is 1 and every reward whole.
//
// A scaled value is base x (1 + per_player x (players - 1)).
//  * Health is scaled as damage taken: a hostile person's health and the scripts' own health values and checks stay as
//    they are, and every hit on it in its owner's game counts for 1 / multiplier. A mission's SET_CHAR_HEALTH after its
//    CREATE_CHAR, a handover to another game and the 255 a copy's health goes up to change nothing in it.
//  * Damage is scaled where a player is hit: in that player's own game.
//  * Accuracy is the person's own (CPed::m_nWeaponAccuracy), in every game that shows it: a copy shoots at this game's
//    player with the accuracy it has here.
#include <cstdint>
#include <string>

#include "common/api/protocol/Balance.h"
#include "shared/game/Game.h"

namespace coop::balance {

const proto::Balance& Settings();
// one of the settings; mission: for a person of the story mission that runs (its [mission:<name>] value when it has one)
float Value(proto::BalanceKey key, bool mission = false);

// the players in the game now, this one among them (a player in the menu or loading is not in it)
int PlayerCount();
// the players within mission_scale_radius_m of the host, the host among them: who a story mission's enemies are scaled for
int MissionPlayerCount();

// the host's story mission runs (in the host's game and in its partners')
bool StoryMissionOn();
std::string StoryMission();  // its script name in lower case ("sweet1"), empty for none or not known yet
bool MissionExcluded();      // its people are never scaled (balance_exclude.ini)

enum class Stat { Health, Damage, Accuracy, GangDensity };
// the multiplier now; mission: for the story mission's people (MissionPlayerCount and the mission's own values)
float Scale(Stat stat, bool mission);

// ---- enemies ----
// A person of the story mission that runs: the host's own, or its copy in a partner's game.
bool MissionPerson(game::CPed* ped);
// Police, one who hates or dislikes the player (the enemy gangs as main.scm sets them, a mission's enemies), one whose
// kill task is after a player, or one who has hit a player. Never a player, one of our player's group or an excluded
// model.
bool Hostile(game::CPed* ped);
// `by` (a person, or the vehicle it drives) hit a player: it counts as hostile from now on
void NoteAttack(game::CEntity* by);
// a hit of a non-player person on this game's player: its damage by the damage multiplier
int DamageToPlayer(game::CEntity* by, int weapon, int damage);
// a hit on a person this game owns: its damage by 1 / the health multiplier when the person is hostile
int DamageToNpc(game::CPed* victim, int weapon, int damage);
// the accuracy a person has here now, from the one its game or script gave it (0..100)
uint8_t AccuracyFor(game::CPed* ped, uint8_t own);
void ForgetAttacks();  // the session or the game ended

// ---- rewards ----
// What each of `recipients` players gets of an amount one of them earned, by payout_mode. The earner keeps this much
// too: the rest of what the script paid it is taken back (TakeBack).
int32_t Share(int32_t earned, int recipients);
// takes the part the earner does not keep out of this game's player's money; the amount taken
int32_t TakeBack(int32_t earned, int32_t share);
// " (split 4 ways)", " (50%)" or empty for a whole reward: goes after the amount in a chat line
std::string ShareNote(int recipients, uint8_t mode);
inline std::string ShareNote(int recipients) { return ShareNote(recipients, (uint8_t)Settings().Payout()); }

}  // namespace coop::balance
