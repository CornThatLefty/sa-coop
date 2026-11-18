#pragma once
// A player's character as numbered values: read and set in this game, or through the server in another player's
// (the Cheats tab of the F8 window, docs/FORK.md "Cheats tab"). A value is an int: a stat that is a float is rounded.
#include <cstdint>
#include <vector>

namespace coop::character {

// A field's number, as it goes over the network (G_CHAR_GET, G_CHAR_VALUES, G_CHAR_SET)
enum : uint16_t {
    F_STAT_LAST = 342,  // 0..342: a stat (CStats: 0..81 floats, 120..342 ints)
    F_MONEY = 0x1000,
    F_HEALTH,
    F_ARMOUR,
    F_MAX_ARMOUR,   // CPlayerInfo::m_nMaxArmour (max health follows stat 24, the engine sets it each frame)
    F_WANTED,
    F_FIGHT_STYLE,  // 4 standard, 5 boxing, 6 kung fu, 7 kick boxing
    F_NEVER_TIRED = 0x1010,
    F_FAST_RELOAD,
    F_FIREPROOF,
    F_JAIL_FREE,      // the next arrest takes no fee (CPlayerInfo::m_bGetOutOfJailFree: the game clears it then)
    F_HOSPITAL_FREE,  // the next death takes no fee (m_bFreeHealthCare)
    F_TAXI_NITRO,
    F_PIMP,
    F_WEAPON = 0x2000,  // + eWeaponType 1..46: its rounds (1 for one without rounds), 0: not carried
    F_GLOBAL = 0x4000,  // + the number of a main.scm global
};

// the range a field takes; false: no such field in this game
bool Limits(uint16_t field, int32_t& min, int32_t& max);

// A player's value: this game's own is read from the game, another's is the last answer its game gave (false: none
// yet). Ask requests those fields of another player's game; its answer comes within a frame or two of the network.
bool Value(uint8_t player, uint16_t field, int32_t& value);
void Ask(uint8_t player, const std::vector<uint16_t>& fields);
// Sets it: at once in this game, through the server in another's (the server passes it on only from a player who may
// change its settings). The new value comes back as an answer.
void Change(uint8_t player, uint16_t field, int32_t value);
// that player's game answered at least once this session (a game older than test build 14 never does)
bool Answered(uint8_t player);

}  // namespace coop::character
