#pragma once
// The balance settings of a game: combat scaling, shop prices, payouts (docs/FORK.md). The server reads them from the
// [balance] section of server.ini and from balance_exclude.ini beside it, and sends them to every player in S_BALANCE,
// so that every game counts with the same numbers. A game that got no S_BALANCE keeps the neutral values: the game as it
// is without the fork's balance.
#include <cstdint>
#include <string>
#include <vector>

#include "common/lib/buffer/Buffer.h"

namespace coop::proto {

enum BalanceKey : uint8_t {
    BK_HEALTH_BASE,
    BK_HEALTH_PER_PLAYER,
    BK_DAMAGE_BASE,
    BK_DAMAGE_PER_PLAYER,
    BK_ACCURACY_BASE,
    BK_ACCURACY_PER_PLAYER,
    BK_GANG_DENSITY_BASE,
    BK_GANG_DENSITY_PER_PLAYER,
    BK_MISSION_HEALTH_PER_PLAYER,
    BK_MISSION_SCALE_RADIUS_M,
    BK_PRICE_WEAPONS,
    BK_PRICE_AMMO,
    BK_PRICE_ARMOR,
    BK_PRICE_CLOTHES,
    BK_PRICE_FOOD,
    BK_PRICE_BARBER_TATTOO,
    BK_PRICE_CARMODS,
    BK_PAYOUT_MODE,   // PayoutMode
    BK_PAYOUT_SHARE,  // PAYOUT_SCALED: each recipient's part of what was earned
    // Two switches of how the games share the world (0 or 1), sent with the balance so that every game follows the
    // same rule (docs/FORK.md, "Ownership")
    BK_SYNC_DRIVER_LEADS,  // in a car two players share, the driver's game makes the people and traffic around it
    BK_SYNC_INSTANT_HITS,  // a player's hit on another game's person shows in its own game at once
    // The streets (docs/FORK.md, "Wider streets"). The same in every game: the games share their people and cars by
    // distances that follow from the engine's.
    BK_SPAWN_SCALE,  // how many times farther out the engine makes and removes its people and traffic
    BK_PED_DENSITY,  // people on the streets
    BK_CAR_DENSITY,  // traffic
    BK_COUNT
};

enum PayoutMode : uint8_t { PAYOUT_FULL = 0, PAYOUT_SPLIT = 1, PAYOUT_SCALED = 2 };

struct BalanceKeyInfo {
    const char* name;  // the key in server.ini
    float neutral;     // the game without the fork's balance (no S_BALANCE)
    float preset;      // the server's value when server.ini does not set the key
    float min, max;
};

// in the order of BalanceKey
inline constexpr BalanceKeyInfo kBalanceKeys[BK_COUNT] = {
    {"health_base", 1.0f, 1.0f, 0.1f, 20.0f},
    {"health_per_player", 0.0f, 0.5f, 0.0f, 5.0f},
    {"damage_base", 1.0f, 1.0f, 0.1f, 20.0f},
    {"damage_per_player", 0.0f, 0.15f, 0.0f, 5.0f},
    {"accuracy_base", 1.0f, 1.0f, 0.1f, 5.0f},
    {"accuracy_per_player", 0.0f, 0.1f, 0.0f, 5.0f},
    {"gang_density_base", 1.0f, 1.0f, 0.0f, 10.0f},
    {"gang_density_per_player", 0.0f, 0.25f, 0.0f, 5.0f},
    {"mission_health_per_player", 0.0f, 0.5f, 0.0f, 5.0f},
    {"mission_scale_radius_m", 300.0f, 300.0f, 10.0f, 10000.0f},
    {"price_weapons", 1.0f, 2.0f, 0.01f, 1000.0f},
    {"price_ammo", 1.0f, 2.0f, 0.01f, 1000.0f},
    {"price_armor", 1.0f, 3.0f, 0.01f, 1000.0f},
    {"price_clothes", 1.0f, 1.5f, 0.01f, 1000.0f},
    {"price_food", 1.0f, 1.0f, 0.01f, 1000.0f},
    {"price_barber_tattoo", 1.0f, 1.0f, 0.01f, 1000.0f},
    {"price_carmods", 1.0f, 1.5f, 0.01f, 1000.0f},
    {"payout_mode", (float)PAYOUT_FULL, (float)PAYOUT_SPLIT, 0.0f, 2.0f},
    {"payout_share", 1.0f, 0.5f, 0.0f, 1.0f},
    {"sync_driver_leads", 0.0f, 1.0f, 0.0f, 1.0f},
    {"sync_instant_hits", 0.0f, 1.0f, 0.0f, 1.0f},
    // past 2 the cars would be made beyond the road nodes the game keeps loaded around its player
    {"spawn_scale", 1.0f, 1.0f, 1.0f, 2.0f},
    {"ped_density", 1.0f, 1.0f, 0.0f, 4.0f},
    {"car_density", 1.0f, 1.0f, 0.0f, 4.0f},
};

// a key that is a switch: 0 or 1 in server.ini, a checkbox in the F8 window
inline bool BalanceKeyIsFlag(int key) { return key == BK_SYNC_DRIVER_LEADS || key == BK_SYNC_INSTANT_HITS; }

inline int BalanceKeyByName(const std::string& lowerName) {
    for (int i = 0; i < BK_COUNT; ++i)
        if (lowerName == kBalanceKeys[i].name) return i;
    return -1;
}

inline float ClampBalance(int key, float v) {
    const BalanceKeyInfo& k = kBalanceKeys[key];
    if (!(v == v)) return k.neutral;  // NaN
    return v < k.min ? k.min : v > k.max ? k.max : v;
}

// one value of [mission:<name>] in balance_exclude.ini: that key while that story mission runs
struct MissionOverride {
    std::string mission;  // the mission's script name, lower case ("sweet1")
    uint8_t key = 0;
    float value = 0;
};

struct Balance {
    float v[BK_COUNT];
    std::vector<uint16_t> excludeModels;       // ped models never scaled
    std::vector<std::string> excludeMissions;  // story missions (script names, lower case) whose people are never scaled
    std::vector<MissionOverride> overrides;

    Balance() {
        for (int i = 0; i < BK_COUNT; ++i) v[i] = kBalanceKeys[i].neutral;
    }
    static Balance Preset() {
        Balance b;
        for (int i = 0; i < BK_COUNT; ++i) b.v[i] = kBalanceKeys[i].preset;
        return b;
    }
    float Get(BalanceKey k) const { return v[k]; }
    bool On(BalanceKey k) const { return v[k] >= 0.5f; }
    PayoutMode Payout() const { return (PayoutMode)(int)(v[BK_PAYOUT_MODE] + 0.5f); }
    bool Neutral() const {
        for (int i = 0; i < BK_COUNT; ++i)
            if (v[i] != kBalanceKeys[i].neutral) return false;
        return true;
    }

    // The values go by name: a key one side does not know is skipped, one it misses keeps its neutral value.
    void Write(Writer& w) const {
        w.U8(BK_COUNT);
        for (int i = 0; i < BK_COUNT; ++i) {
            w.Str(kBalanceKeys[i].name, 40);
            w.F32(v[i]);
        }
        w.U16((uint16_t)excludeModels.size());
        for (uint16_t m : excludeModels) w.U16(m);
        w.U16((uint16_t)excludeMissions.size());
        for (auto& m : excludeMissions) w.Str(m, 8);
        w.U16((uint16_t)overrides.size());
        for (auto& o : overrides) {
            w.Str(o.mission, 8);
            w.Str(kBalanceKeys[o.key].name, 40);
            w.F32(o.value);
        }
    }
    bool Read(Reader& r) {
        *this = Balance();
        uint8_t n = r.U8();
        for (uint8_t i = 0; i < n && r.Ok(); ++i) {
            std::string name = r.Str(40);
            float value = r.F32();
            if (int k = BalanceKeyByName(name); k >= 0 && r.Ok()) v[k] = ClampBalance(k, value);
        }
        uint16_t models = r.U16();
        for (uint16_t i = 0; i < models && r.Ok(); ++i) excludeModels.push_back(r.U16());
        uint16_t missions = r.U16();
        for (uint16_t i = 0; i < missions && r.Ok(); ++i) excludeMissions.push_back(r.Str(8));
        uint16_t over = r.U16();
        for (uint16_t i = 0; i < over && r.Ok(); ++i) {
            MissionOverride o;
            o.mission = r.Str(8);
            std::string name = r.Str(40);
            o.value = r.F32();
            if (int k = BalanceKeyByName(name); k >= 0 && r.Ok()) {
                o.key = (uint8_t)k;
                o.value = ClampBalance(k, o.value);
                overrides.push_back(o);
            }
        }
        if (!r.Ok()) *this = Balance();
        return r.Ok();
    }
};

}  // namespace coop::proto
