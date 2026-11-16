#include "entities/balance/Balance.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <map>
#include <random>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/mission/Mission.h"
#include "entities/ped/Peds.h"
#include "entities/player/Players.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/lib/core/Core.h"

namespace coop::balance {

namespace {
using namespace game;
using namespace proto;

constexpr int WEAPON_RAMMEDBYCAR = 49, WEAPON_RUNOVERBYCAR = 50, WEAPON_DROWNING = 53, WEAPON_FALL = 54;
constexpr int TASK_COMPLEX_KILL_PED_ON_FOOT = 1000;
constexpr uint64_t kInGameMs = 20000;  // a player whose states stopped that long ago loads or sits in the menu
constexpr uint64_t kAttackMs = 600000;  // an attack on a player is remembered that long

std::map<int, uint64_t> g_attackers;  // script handle -> when it last hit a player

bool Active() { return Net().Online() && Net().BalanceRevision() != 0; }

bool InGame(const RemotePlayer& rp) { return rp.hasState && NowMs() - rp.stateRecvMs < kInGameMs; }

CVector PlaceOf(const RemotePlayer& rp) { return rp.ped && IsPedValid(rp.ped) ? rp.ped->GetPosition() : rp.state.pos; }

CPed* PersonOf(CEntity* e) {
    if (!e) return nullptr;
    if (e->IsPed()) return (CPed*)e;
    return e->IsVehicle() ? ((CVehicle*)e)->Driver() : nullptr;
}

bool IsPlayerPed(CPed* ped) { return ped->IsPlayer() || coop::Players::Instance().FindByPed(ped); }

// A whole number of the same average: 0.4 is 1 four times in ten. A burning person's 1 a tick would never round up.
int RoundRandom(float v) {
    static std::minstd_rand rng{0x5ACD00Fu};
    float whole = std::floor(v);
    float part = v - whole;
    return (int)whole + (part > 0.0f && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < part ? 1 : 0);
}

bool ModelExcluded(int model) {
    const auto& list = Settings().excludeModels;
    return std::find(list.begin(), list.end(), (uint16_t)model) != list.end();
}

// A CAcquaintance: u32 respect, like, ignore, dislike, hate, a bit per ped type. PLAYER1 is bit 0, PLAYER2 bit 1; a
// player's copy here is a PLAYER1 too. A person has its own (CPed::m_acquaintance, 0x4E0: CPed::CPed fills it at
// 0x5E835A from its type's), SET_CHAR_RELATIONSHIP changes that one; SET_RELATIONSHIP changes the type's
// (CPedType::ms_apPedTypes, 0x14 bytes a type: 0x608E20), for the people made after it. Main.scm sets Ballas, Vagos and
// GANG8 against the player so at its start (0xDB7D), and the missions their enemies' types (MISSION1 to 3).
constexpr uint32_t kPlayerBits = 3;
bool Against(const uint8_t* acquaintance) {
    return ((*(const uint32_t*)(acquaintance + 12) | *(const uint32_t*)(acquaintance + 16)) & kPlayerBits) != 0;
}
bool TypeHatesPlayer(int type) {
    const uint8_t* types = Ref<const uint8_t*>(0xC0BBE8);  // CPedType::ms_apPedTypes
    return types && type >= 0 && type < 32 && Against(types + (size_t)type * 0x14);
}
bool HatesPlayer(CPed* ped) { return Against((const uint8_t*)ped + 0x4E0) || TypeHatesPlayer(ped->PedType()); }

bool InOurGroup(CPed* ped) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me) return false;
    void* group = Call<0x5F7E80, void*>(ped);  // CPedGroups::GetPedsGroup
    return group && group == Call<0x5F7E80, void*>((CPed*)me);
}

// its kill task's target is a player (CTaskManager::FindActiveTaskByType, the target at +0x10: as Peds.cpp's GymOpponent)
bool HuntsPlayer(CPed* ped) {
    void* intel = ped->Intelligence();
    void* kill = intel ? CallMethod<0x681740, void*>((uint8_t*)intel + 4, TASK_COMPLEX_KILL_PED_ON_FOOT) : nullptr;
    CPed* target = kill ? Field<CPed*>(kill, 0x10) : nullptr;
    return target && IsPedValid(target) && IsPlayerPed(target);
}

bool Attacked(CPed* ped) {
    auto it = g_attackers.find(GetPedRef(ped));
    return it != g_attackers.end() && NowMs() - it->second < kAttackMs;
}

}  // namespace

const proto::Balance& Settings() { return Net().Balance(); }

float Value(BalanceKey key, bool mission) {
    const Balance& b = Settings();
    if (mission && !b.overrides.empty()) {
        std::string name = StoryMission();
        if (!name.empty())
            for (const MissionOverride& o : b.overrides)
                if (o.key == key && o.mission == name) return o.value;
    }
    return b.v[key];
}

int PlayerCount() {
    if (!Net().Online()) return 1;
    int n = 1;
    for (auto& [id, rp] : coop::Players::Instance().All()) n += InGame(rp);
    return n;
}

int MissionPlayerCount() {
    if (!Net().Online()) return 1;
    CPlayerPed* me = FindPlayerPed(0);
    CVector host;
    if (Net().IsHost()) {
        if (!me) return 1;
        host = me->GetPosition();
    } else {
        const RemotePlayer* rp = coop::Players::Instance().Get(Net().HostId());
        if (!rp || !InGame(*rp)) return PlayerCount();
        host = PlaceOf(*rp);
    }
    float radius = Value(BK_MISSION_SCALE_RADIUS_M, true);
    int n = 1;  // the host
    if (!Net().IsHost() && me && (me->GetPosition() - host).Length() <= radius) ++n;
    for (auto& [id, rp] : coop::Players::Instance().All())
        if (id != Net().HostId() && InGame(rp) && (PlaceOf(rp) - host).Length() <= radius) ++n;
    return n;
}

bool StoryMissionOn() {
    if (!Net().Online()) return false;
    if (!Net().IsHost()) return HostMissionRecent(0);
    void* s = RunningMission();
    return s && coop::StoryMission(s);
}

std::string StoryMission() {
    if (!Net().Online()) return {};
    if (!Net().IsHost()) return ToLower(HostMissionName());
    void* s = RunningMission();
    if (!s || !coop::StoryMission(s)) return {};
    const char* name = Field<char[8]>(s, 0x08);  // CRunningScript::m_szName
    return ToLower(std::string(name, strnlen(name, 8)));
}

bool MissionExcluded() {
    const auto& list = Settings().excludeMissions;
    if (list.empty()) return false;
    std::string name = StoryMission();
    return !name.empty() && std::find(list.begin(), list.end(), name) != list.end();
}

float Scale(Stat stat, bool mission) {
    if (!Active()) return 1.0f;
    if (mission && MissionExcluded()) return 1.0f;
    int n = std::max(1, mission ? MissionPlayerCount() : PlayerCount());
    BalanceKey base, per;
    switch (stat) {
    case Stat::Health: base = BK_HEALTH_BASE, per = mission ? BK_MISSION_HEALTH_PER_PLAYER : BK_HEALTH_PER_PLAYER; break;
    case Stat::Damage: base = BK_DAMAGE_BASE, per = BK_DAMAGE_PER_PLAYER; break;
    case Stat::Accuracy: base = BK_ACCURACY_BASE, per = BK_ACCURACY_PER_PLAYER; break;
    default: base = BK_GANG_DENSITY_BASE, per = BK_GANG_DENSITY_PER_PLAYER; break;
    }
    return Value(base, mission) * (1.0f + Value(per, mission) * (float)(n - 1));
}

bool MissionPerson(CPed* ped) {
    if (!ped || !StoryMissionOn()) return false;
    NetPed* np = Peds::Instance().Find(ped);
    if (np && np->owner != Net().MyId()) return np->owner == Net().HostId();  // a copy: the host's people in its mission
    return Net().IsHost() && ped->CreatedBy() == PED_MISSION;
}

bool Hostile(CPed* ped) {
    if (!ped || IsPlayerPed(ped) || ModelExcluded(ped->ModelIndex())) return false;
    // a copy is a civilian or a gang member of its owner's type (Peds::SpawnMirror): told by the type its owner gave
    NetPed* np = Peds::Instance().Find(ped);
    bool copy = np && np->owner != Net().MyId();
    int type = copy ? np->pedType : ped->PedType();
    if (Attacked(ped)) return true;
    // a copy has none of its owner's relationships and tasks: police, a type set against the player in this game too, or
    // a mission's type (the host's mission sets those against the player in the host's game alone)
    if (copy) return type == PED_TYPE_COP || TypeHatesPlayer(type) || type >= PED_TYPE_MISSION1;
    if (InOurGroup(ped)) return false;
    return type == PED_TYPE_COP || HatesPlayer(ped) || HuntsPlayer(ped);
}

void NoteAttack(CEntity* by) {
    CPed* ped = PersonOf(by);
    if (!ped || !Net().Online() || IsPlayerPed(ped)) return;
    if (g_attackers.size() > 400) {
        uint64_t now = NowMs();
        for (auto it = g_attackers.begin(); it != g_attackers.end();)
            it = now - it->second > kAttackMs || !GetPed(it->first) ? g_attackers.erase(it) : std::next(it);
    }
    g_attackers[GetPedRef(ped)] = NowMs();
}

void ForgetAttacks() { g_attackers.clear(); }

int DamageToPlayer(CEntity* by, int weapon, int damage) {
    if (!Active() || damage <= 0 || weapon == WEAPON_RAMMEDBYCAR || weapon == WEAPON_RUNOVERBYCAR || weapon == WEAPON_DROWNING ||
        weapon == WEAPON_FALL)
        return damage;
    CPed* ped = PersonOf(by);
    if (!ped || !IsPedValid(ped) || IsPlayerPed(ped)) return damage;
    NetPed* np = Peds::Instance().Find(ped);
    bool copy = np && np->owner != Net().MyId();
    // one of ours by mistake: Sweet's stray bullet stays what it was
    if (ModelExcluded(ped->ModelIndex())) return damage;
    if (copy ? np->pedType == PED_TYPE_GANG2 : InOurGroup(ped) || (ped->PedType() == PED_TYPE_GANG2 && !HatesPlayer(ped))) return damage;
    float m = Scale(Stat::Damage, MissionPerson(ped));
    if (m == 1.0f) return damage;
    return std::max(1, RoundRandom((float)damage * m));
}

int DamageToNpc(CPed* victim, int weapon, int damage) {
    if (!Active() || damage <= 0 || !victim || weapon == WEAPON_RAMMEDBYCAR || weapon == WEAPON_RUNOVERBYCAR ||
        weapon == WEAPON_DROWNING || weapon == WEAPON_FALL)
        return damage;
    NetPed* np = Peds::Instance().Find(victim);
    if (np && np->owner != Net().MyId()) return damage;  // a copy: its owner decides
    if (!Hostile(victim)) return damage;
    float m = Scale(Stat::Health, MissionPerson(victim));
    if (m == 1.0f || m <= 0.0f) return damage;
    return RoundRandom((float)damage / m);
}

uint8_t AccuracyFor(CPed* ped, uint8_t own) {
    if (!Active() || !Hostile(ped)) return own;
    float m = Scale(Stat::Accuracy, MissionPerson(ped));
    if (m == 1.0f) return own;
    return (uint8_t)std::clamp((int)std::lround((float)own * m), 0, std::max<int>(own, 100));
}

int32_t Share(int32_t earned, int recipients) {
    if (earned <= 0 || !Active()) return earned;
    switch (Settings().Payout()) {
    case PAYOUT_SPLIT: return earned / std::max(1, recipients);
    case PAYOUT_SCALED: return (int32_t)((double)earned * Settings().Get(BK_PAYOUT_SHARE));
    default: return earned;
    }
}

int32_t TakeBack(int32_t earned, int32_t share) {
    int32_t back = earned - share;
    if (back <= 0 || !core::IsPlayerReady()) return 0;
    PlayerInfos()[0].money -= back;
    LOGI("Payout: $%d earned, $%d kept, $%d taken back (mode %d)", earned, share, back, (int)Settings().Payout());
    return back;
}

std::string ShareNote(int recipients, uint8_t mode) {
    if (mode == PAYOUT_SPLIT && recipients > 1) return Format(TR(" (поделено на %d)", " (split %d ways)"), recipients);
    if (mode == PAYOUT_SCALED) return Format(" (%d%%)", (int)std::lround(Settings().Get(BK_PAYOUT_SHARE) * 100.0f));
    return {};
}

}  // namespace coop::balance
