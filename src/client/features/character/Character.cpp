// A player's character as numbered values (Character.h). The game that owns the character reads and sets them: another
// game asks (G_CHAR_GET), gets the answer (G_CHAR_VALUES) and sets through the server (G_CHAR_SET, passed on only from
// a player who may change the server's settings).
#include "features/character/Character.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "common/api/protocol/Protocol.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/profile/Profile.h"
#include "entities/session/Flow.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/game/Script.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"

namespace coop::character {

namespace {
using namespace game;
using game::script::Cmd;

constexpr uintptr_t kStatsInt = 0xB79000, kStatsFloat = 0xB79380;  // CStats::StatTypesInt (from id 120), StatTypesFloat
constexpr uintptr_t kCheats = 0x969130;                           // CCheat::m_aCheatsActive
constexpr int kMaxAmmo = 99999;                                   // CWeapon's own limit

std::map<uint8_t, std::map<uint16_t, int32_t>> g_values;  // the other players' answers
std::map<uint8_t, uint64_t> g_toldMs;                     // the chat said that player changed this character

bool FloatStat(uint16_t f) { return f < 82; }
bool IntStat(uint16_t f) { return f >= 120 && f <= F_STAT_LAST; }
// the stats the game keeps within 0..1000: the body, respect, the weapon skills, the vehicle skills, lungs, luck
bool Capped(uint16_t f) {
    return (f >= 21 && f <= 25) || (f >= 64 && f <= 81) || f == 160 || f == 223 || f == 225 || f == 229 || f == 230 || f == 233;
}
bool Weapon(uint16_t f) {  // eWeaponType has no 19-21
    int t = f - F_WEAPON;
    return f >= F_WEAPON && t >= 1 && t <= 46 && (t < 19 || t > 21);
}
// the globals are the first part of main.scm: it starts with a jump over them (02 00 01 <offset>)
int32_t Globals() { return std::clamp(Ref<int32_t>(addr::ScriptSpace + 3) / 4, 0, 0x4000); }
bool GlobalField(uint16_t f) { return f >= F_GLOBAL && f - F_GLOBAL < Globals(); }

uint8_t* Perk(uint16_t f) {
    uint8_t* perks = (uint8_t*)&PlayerInfos()[0] + 0x14C;  // m_bDoesNotGetTired, m_bFastReload, m_bFireProof, ...
    switch (f) {
        case F_NEVER_TIRED: return perks;
        case F_FAST_RELOAD: return perks + 1;
        case F_FIREPROOF: return perks + 2;
        case F_JAIL_FREE: return perks + 5;      // m_bGetOutOfJailFree
        case F_HOSPITAL_FREE: return perks + 6;  // m_bFreeHealthCare
        case F_TAXI_NITRO: return &Ref<uint8_t>(kCheats + 91);
        case F_PIMP: return &Ref<uint8_t>(kCheats + 90);
        default: return nullptr;
    }
}

bool Get(uint16_t f, int32_t& v) {
    int32_t min, max;
    if (!core::IsPlayerReady() || !Limits(f, min, max)) return false;
    CPlayerPed* me = FindPlayerPed(0);
    CPlayerInfo& info = PlayerInfos()[0];
    if (FloatStat(f)) v = (int32_t)std::lround(std::clamp(Ref<float>(kStatsFloat + f * 4u), -2.0e9f, 2.0e9f));
    else if (IntStat(f)) v = Ref<int32_t>(kStatsInt + (f - 120u) * 4u);
    else if (f == F_MONEY) v = info.money;
    else if (f == F_HEALTH) v = (int32_t)std::lround(me->Health());
    else if (f == F_ARMOUR) v = (int32_t)std::lround(me->Armour());
    else if (f == F_MAX_ARMOUR) v = info.maxArmour;
    else if (f == F_WANTED) v = me->PlayerData() && me->Wanted() ? std::clamp(Field<int32_t>(me->Wanted(), 0x2C), 0, 6) : 0;
    else if (f == F_FIGHT_STYLE) v = me->FightingStyle();
    else if (uint8_t* perk = Perk(f)) v = *perk != 0;
    else if (Weapon(f)) {
        v = 0;
        for (int i = 0; i < NUM_WEAPON_SLOTS; ++i)
            if (me->Weapons()[i].type == f - F_WEAPON) v = std::max(me->Weapons()[i].ammoTotal, 1);
    } else v = Ref<int32_t>(addr::ScriptSpace + (uintptr_t)(f - F_GLOBAL) * 4);
    return true;
}

bool Set(uint16_t f, int32_t v) {
    int32_t min, max;
    if (!core::IsPlayerReady() || !Limits(f, min, max)) return false;
    v = std::clamp(v, min, max);
    CPlayerPed* me = FindPlayerPed(0);
    CPlayerInfo& info = PlayerInfos()[0];
    int h = GetPedRef(me);
    if (FloatStat(f)) {
        Ref<float>(kStatsFloat + f * 4u) = (float)v;
        // fat and muscle show on the body at its next rebuild: now, unless at the wheel (the rebuild swaps the clump)
        if ((f == 21 || f == 23) && me->PedState() != 50) Call<0x5A82C0>(me, false);  // CClothes::RebuildPlayer
    } else if (IntStat(f)) Ref<int32_t>(kStatsInt + (f - 120u) * 4u) = v;
    else if (f == F_MONEY) info.money = info.displayMoney = v;
    else if (f == F_HEALTH) me->Health() = (float)v;
    else if (f == F_ARMOUR) me->Armour() = (float)v;
    else if (f == F_MAX_ARMOUR) info.maxArmour = (uint8_t)v;
    else if (f == F_WANTED && v) Cmd(0x010D).I(0).I(v).Run();  // ALTER_WANTED_LEVEL
    else if (f == F_WANTED) Cmd(0x0110).I(0).Run();            // CLEAR_WANTED_LEVEL
    else if (f == F_FIGHT_STYLE) me->FightingStyle() = (uint8_t)v;
    else if (uint8_t* perk = Perk(f)) *perk = v != 0;
    else if (Weapon(f)) {
        int type = f - F_WEAPON;
        if (v <= 0) {
            Cmd(0x0555).I(h).I(type).Run();  // REMOVE_WEAPON_FROM_CHAR
        } else {
            int model = -1;
            Cmd(0x0781).I(type).Out(&model).Run();  // GET_WEAPONTYPE_MODEL
            if (model > 0) LoadModelNow(model);
            Cmd(0x01B2).I(h).I(type).I(1).Run();    // GIVE_WEAPON_TO_CHAR (it adds rounds to one carried)
            Cmd(0x017B).I(h).I(type).I(v).Run();    // SET_CHAR_AMMO: the number asked for
        }
    } else Ref<int32_t>(addr::ScriptSpace + (uintptr_t)(f - F_GLOBAL) * 4) = v;
    SaveProfileNow();  // on the server at once: a world reload puts the profile back
    return true;
}

void Answer(uint8_t to, const std::vector<uint16_t>& fields) {
    std::vector<std::pair<uint16_t, int32_t>> out;
    for (uint16_t f : fields)
        if (int32_t v; Get(f, v)) out.push_back({f, v});
    Writer w(proto::G_CHAR_VALUES);
    w.U8(to);
    w.U8((uint8_t)out.size());
    for (auto [f, v] : out) w.U16(f), w.I32(v);
    Net().Send(proto::CH_RELIABLE, w);
}

void HandleGet(uint8_t from, Reader& r) {
    uint8_t target = r.U8();
    std::vector<uint16_t> fields(r.U8());
    for (uint16_t& f : fields) f = r.U16();
    if (r.Ok() && target == Net().MyId() && from != Net().MyId()) Answer(from, fields);
}

void HandleValues(uint8_t from, Reader& r) {
    uint8_t to = r.U8(), n = r.U8();
    if (!r.Ok() || to != Net().MyId() || from == Net().MyId()) return;
    auto& values = g_values[from];  // an empty answer counts too: that game is there, and not ready
    for (int i = 0; i < n && r.Ok(); ++i) {
        uint16_t f = r.U16();
        int32_t v = r.I32();
        if (r.Ok()) values[f] = v;
    }
}

// The server passed it on: its sender may change the server's settings
void HandleSet(uint8_t from, Reader& r) {
    uint8_t target = r.U8();
    uint16_t f = r.U16();
    int32_t v = r.I32();
    if (!r.Ok() || target != Net().MyId() || from == Net().MyId()) return;
    int32_t was = 0;
    Get(f, was);
    if (!Set(f, v)) return Answer(from, {});
    LOGI("Character field %04X set to %d by player %d (was %d)", f, v, from, was);
    if (uint64_t& told = g_toldMs[from]; NowMs() - told > 30000) {
        told = NowMs();
        ui::TheChat().AddSystem(Format(TR("%s меняет этого персонажа из вкладки «Читы» (F8).",
                                          "%s changes this character from the Cheats tab (F8)."),
                                       Net().PeerName(from).c_str()));
    }
    Answer(from, {f});
}

class Character : public Module {
public:
    const char* Name() const override { return "Character"; }
    void OnInstallHooks() override {
        Net().SetHandler(proto::G_CHAR_GET, &HandleGet);
        Net().SetHandler(proto::G_CHAR_VALUES, &HandleValues);
        Net().SetHandler(proto::G_CHAR_SET, &HandleSet);
    }
    void OnSessionEnd() override {
        g_values.clear();
        g_toldMs.clear();
    }
    void OnPeerLeave(uint8_t id) override {
        g_values.erase(id);
        g_toldMs.erase(id);
    }
    static Character& Instance() {
        static Character c;
        return c;
    }
};

struct Reg {
    Reg() { core::RegisterModule(&Character::Instance()); }
} g_reg;
}  // namespace

bool Limits(uint16_t f, int32_t& min, int32_t& max) {
    min = 0, max = 1;
    if (FloatStat(f)) max = Capped(f) ? 1000 : 1000000000;
    else if (IntStat(f)) min = Capped(f) ? 0 : -1000000000, max = Capped(f) ? 1000 : 1000000000;
    else if (f == F_MONEY) max = 999999999;
    else if (f == F_HEALTH) min = 1, max = 255;
    else if (f == F_ARMOUR || f == F_MAX_ARMOUR) max = 255;
    else if (f == F_WANTED) max = 6;
    else if (f == F_FIGHT_STYLE) min = 4, max = 7;
    else if (f >= F_NEVER_TIRED && f <= F_PIMP) max = 1;
    else if (Weapon(f)) max = kMaxAmmo;
    else if (GlobalField(f)) min = INT32_MIN, max = INT32_MAX;
    else return false;
    return true;
}

bool Value(uint8_t player, uint16_t field, int32_t& value) {
    if (player == Net().MyId()) return Get(field, value);
    auto p = g_values.find(player);
    if (p == g_values.end()) return false;
    auto v = p->second.find(field);
    if (v == p->second.end()) return false;
    value = v->second;
    return true;
}

void Ask(uint8_t player, const std::vector<uint16_t>& fields) {
    if (player == Net().MyId() || fields.empty() || !Net().Online()) return;
    Writer w(proto::G_CHAR_GET);
    w.U8(player);
    w.U8((uint8_t)std::min<size_t>(fields.size(), 255));
    for (size_t i = 0; i < fields.size() && i < 255; ++i) w.U16(fields[i]);
    Net().Send(proto::CH_RELIABLE, w);
}

void Change(uint8_t player, uint16_t field, int32_t value) {
    if (!Net().Online()) return;
    if (player == Net().MyId()) {
        int32_t was = 0;
        Get(field, was);
        if (Set(field, value)) LOGI("Character field %04X set to %d from the Cheats tab (was %d)", field, value, was);
        return;
    }
    Writer w(proto::G_CHAR_SET);
    w.U8(player);
    w.U16(field);
    w.I32(value);
    Net().Send(proto::CH_RELIABLE, w);
}

bool Answered(uint8_t player) { return player == Net().MyId() || g_values.count(player) != 0; }

}  // namespace coop::character
