// The balance of a game for several players, applied to the engine (docs/FORK.md; the rules are entities/balance):
//  * shop prices: CShopping::GetPrice is the one place a price is read, for the menu's number and for the purchase
//    (CShopping::Buy calls it at 0x49C000). Its result is multiplied by the category of the price list loaded now;
//    shopping.dat and the lists in memory stay the game's, so the clothes kept in the profiles by their keys do too;
//  * gang members on the streets: CPopCycle::UpdatePercentages counts how many people of each kind the zone should have,
//    the population code then makes the kind furthest below its number. The gangs' number is multiplied after it;
//  * accuracy: the hostile people's CPed::m_nWeaponAccuracy, set over the value their game or script gave them and put
//    back when they stop being hostile or the session ends;
//  * a line in the chat at a story mission's start with the multipliers its enemies have.
// The damage multipliers are applied where the hits are (features/damage), the payouts where the money is
// (features/story, features/money).
#include <algorithm>
#include <cmath>
#include <map>

#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "entities/balance/Balance.h"
#include "entities/session/Flow.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"
#include "shared/ui/chat/Chat.h"

namespace coop {

namespace {
using namespace game;
using namespace proto;
using balance::Stat;

// ---------------------------------------------------------------- shop prices
// ePriceSection, CShopping::ms_priceSectionLoaded (0xA9A7C8): the list of shopping.dat's "prices" in memory now
enum : int32_t {
    SECTION_CAR_MODS = 1, SECTION_CAR_PAINTJOBS = 2, SECTION_CLOTHES = 4, SECTION_HAIRCUTS = 5, SECTION_TATTOOS = 6,
    SECTION_FOOD = 8, SECTION_WEAPONS = 9
};
constexpr uint32_t kArmourKey = 48;  // WEAPON_ARMOUR: CShopping::Buy tells the armour by it (cmp esi, 0x30 at 0x49C20D)
constexpr int kMinPrice = 1, kMaxPrice = 999999;

bool Owns(uint32_t weapon) {
    CPlayerPed* me = FindPlayerPed(0);
    if (!me || !weapon) return false;
    for (int i = 0; i < NUM_WEAPON_SLOTS; ++i)
        if (me->Weapons()[i].type == (int32_t)weapon) return true;
    return false;
}

float PriceMultiplier(int32_t section, uint32_t key) {
    switch (section) {
    case SECTION_CAR_MODS:
    case SECTION_CAR_PAINTJOBS: return balance::Value(BK_PRICE_CARMODS);
    case SECTION_CLOTHES: return balance::Value(BK_PRICE_CLOTHES);
    case SECTION_HAIRCUTS:
    case SECTION_TATTOOS: return balance::Value(BK_PRICE_BARBER_TATTOO);
    case SECTION_FOOD: return balance::Value(BK_PRICE_FOOD);
    case SECTION_WEAPONS:
        // a weapon the player has is bought again for its ammunition: the same item of the list, the ammo's price
        return balance::Value(key == kArmourKey ? BK_PRICE_ARMOR : Owns(key) ? BK_PRICE_AMMO : BK_PRICE_WEAPONS);
    default: return 1.0f;
    }
}

using GetPriceFn = int32_t(__cdecl*)(uint32_t);
GetPriceFn g_origGetPrice = nullptr;
int32_t __cdecl HookGetPrice(uint32_t key) {
    int32_t price = g_origGetPrice(key);
    if (price <= 0 || !Net().Online() || !Net().BalanceRevision()) return price;
    float m = PriceMultiplier(Ref<int32_t>(0xA9A7C8), key);
    if (m == 1.0f) return price;
    return (int32_t)std::clamp(std::llround((double)price * m), (long long)kMinPrice, (long long)kMaxPrice);
}

// ---------------------------------------------------------------- gang members on the streets
using UpdatePercentagesFn = void(__cdecl*)();
UpdatePercentagesFn g_origUpdatePercentages = nullptr;
float g_gangScale = 1.0f;  // as last applied, for /balance
void __cdecl HookUpdatePercentages() {
    g_origUpdatePercentages();
    g_gangScale = Net().Online() && core::IsPlayerReady() ? balance::Scale(Stat::GangDensity, false) : 1.0f;
    if (g_gangScale != 1.0f) Ref<float>(0xC0BC48) *= g_gangScale;  // CPopCycle::m_NumGangs_Peds
}

// ---------------------------------------------------------------- accuracy
struct Accuracy {
    uint8_t own;  // what its game or script gave it
    uint8_t set;  // what is in the ped now, by us
};
std::map<int, Accuracy> g_accuracy;  // by script handle: a pool slot used again is another handle
uint64_t g_accuracyMs = 0;

// world: the peds are there to take their own values back (not while the game is torn down for a load)
void RestoreAccuracy(bool world) {
    if (world && core::IsPlayerReady() && PedPool())
        for (auto& [handle, a] : g_accuracy)
            if (CPed* ped = GetPed(handle); ped && ped->WeaponAccuracy() == a.set) ped->WeaponAccuracy() = a.own;
    g_accuracy.clear();
}

void UpdateAccuracy() {
    CPoolHeader* pool = PedPool();
    if (!pool) return;
    std::map<int, Accuracy> next;
    for (int i = 0; i < pool->size; ++i) {
        if (pool->byteMap[i] < 0) continue;
        CPed* ped = (CPed*)(pool->objects + (size_t)i * kPedPoolElemSize);
        if (ped->IsPlayer()) continue;
        int handle = GetPedRef(ped);
        uint8_t now = ped->WeaponAccuracy();
        auto it = g_accuracy.find(handle);
        // a value other than ours: its script set it since (SET_CHAR_ACCURACY), that is its own now
        uint8_t own = it != g_accuracy.end() && it->second.set == now ? it->second.own : now;
        uint8_t want = balance::AccuracyFor(ped, own);
        if (want != now) ped->WeaponAccuracy() = want;
        if (want != own) next[handle] = {own, want};
    }
    g_accuracy.swap(next);
}

// ---------------------------------------------------------------- the chat's lines
std::string Times(float m) {
    std::string s = Format("%.2f", m);
    if (s.back() == '0') s.pop_back();
    return "x" + s;
}

void TellMissionScale() {
    if (balance::MissionExcluded()) {
        ui::TheChat().AddSystem(TR("Баланс кооператива: в этой миссии выключен.", "Co-op scaling: off for this mission."));
        return;
    }
    float health = balance::Scale(Stat::Health, true), damage = balance::Scale(Stat::Damage, true),
          accuracy = balance::Scale(Stat::Accuracy, true);
    int n = balance::MissionPlayerCount();
    LOGI("Mission scaling (%s): %d players near the host, health x%.2f, damage x%.2f, accuracy x%.2f", balance::StoryMission().c_str(),
         n, health, damage, accuracy);
    if (health == 1.0f && damage == 1.0f && accuracy == 1.0f) return;
    ui::TheChat().AddSystem(Format(TR("Баланс кооператива: игроков %d, здоровье врагов %s, урон %s, меткость %s.",
                                      "Co-op scaling: %d players, enemy health %s, damage %s, accuracy %s."),
                                   n, Times(health).c_str(), Times(damage).c_str(), Times(accuracy).c_str()));
}

void TellBalance() {
    auto& chat = ui::TheChat();
    if (!Net().Online() || !Net().BalanceRevision()) {
        chat.AddSystem(TR("Баланс: сервер не прислал настроек, игра без изменений.", "Balance: no settings from the server, the game is unchanged."));
        return;
    }
    const Balance& b = balance::Settings();
    chat.AddSystem(Format(TR("Баланс: игроков %d, рядом с хостом %d.", "Balance: %d players, %d near the host."), balance::PlayerCount(),
                          balance::MissionPlayerCount()));
    chat.AddSystem(Format(TR("Враги: здоровье %s, урон %s, меткость %s, банды на улицах %s.",
                             "Enemies: health %s, damage %s, accuracy %s, gangs on the streets %s."),
                          Times(balance::Scale(Stat::Health, false)).c_str(), Times(balance::Scale(Stat::Damage, false)).c_str(),
                          Times(balance::Scale(Stat::Accuracy, false)).c_str(), Times(balance::Scale(Stat::GangDensity, false)).c_str()));
    if (balance::StoryMissionOn())
        chat.AddSystem(balance::MissionExcluded()
                           ? std::string(TR("Миссия: баланс выключен.", "Mission: scaling off."))
                           : Format(TR("Миссия: здоровье %s, урон %s, меткость %s.", "Mission: health %s, damage %s, accuracy %s."),
                                    Times(balance::Scale(Stat::Health, true)).c_str(), Times(balance::Scale(Stat::Damage, true)).c_str(),
                                    Times(balance::Scale(Stat::Accuracy, true)).c_str()));
    chat.AddSystem(Format(TR("Цены: оружие %s, патроны %s, броня %s, одежда %s, еда %s, стрижки и тату %s, тюнинг %s.",
                             "Prices: weapons %s, ammo %s, armour %s, clothes %s, food %s, barber and tattoo %s, car mods %s."),
                          Times(b.Get(BK_PRICE_WEAPONS)).c_str(), Times(b.Get(BK_PRICE_AMMO)).c_str(), Times(b.Get(BK_PRICE_ARMOR)).c_str(),
                          Times(b.Get(BK_PRICE_CLOTHES)).c_str(), Times(b.Get(BK_PRICE_FOOD)).c_str(),
                          Times(b.Get(BK_PRICE_BARBER_TATTOO)).c_str(), Times(b.Get(BK_PRICE_CARMODS)).c_str()));
    switch (b.Payout()) {
    case PAYOUT_SPLIT: chat.AddSystem(TR("Награды: делятся поровну между игроками.", "Rewards: split evenly between the players.")); break;
    case PAYOUT_SCALED:
        chat.AddSystem(Format(TR("Награды: каждому %d%% суммы.", "Rewards: %d%% of the amount to each player."),
                              (int)std::lround(b.Get(BK_PAYOUT_SHARE) * 100.0f)));
        break;
    default: chat.AddSystem(TR("Награды: каждому вся сумма.", "Rewards: the whole amount to each player.")); break;
    }
}

class BalanceFeature : public Module {
public:
    const char* Name() const override { return "Balance"; }

    void OnInstallHooks() override {
        mem::HookFn(0x49AD50, &HookGetPrice, &g_origGetPrice, "CShopping::GetPrice");
        mem::HookFn(0x610770, &HookUpdatePercentages, &g_origUpdatePercentages, "CPopCycle::UpdatePercentages");
    }
    void OnSessionEnd() override { Reset(true); }
    void OnGameShutdown() override { Reset(false); }

    void OnPostProcess() override {
        if (!Net().Online() || !core::IsPlayerReady()) return;
        uint64_t now = NowMs();
        if (uint32_t rev = Net().BalanceRevision(); rev != m_revision) {
            if (m_revision && rev) RestoreAccuracy(true);  // /reloadbalance: from the peds' own values again
            m_revision = rev;
        }
        if (!m_revision) return;
        if (now - g_accuracyMs >= 500) {
            g_accuracyMs = now;
            if (flow::InSharedWorld() && !flow::HostWorldLoading()) UpdateAccuracy();
        }
        // a moment after the start: the partner learns the mission's name then, and the players stand at its marker
        bool mission = balance::StoryMissionOn();
        if (mission && !m_mission) m_tellAt = now + 3000;
        if (!mission) m_tellAt = 0;
        m_mission = mission;
        if (m_tellAt && now >= m_tellAt) {
            m_tellAt = 0;
            TellMissionScale();
        }
    }

    bool OnCommand(const std::string& cmd, const std::vector<std::string>&) override {
        if (cmd != "balance") return false;
        TellBalance();
        return true;
    }

private:
    void Reset(bool world) {
        RestoreAccuracy(world);
        balance::ForgetAttacks();
        m_revision = 0;
        m_mission = false;
        m_tellAt = 0;
    }
    uint32_t m_revision = 0;
    bool m_mission = false;
    uint64_t m_tellAt = 0;
};

struct Reg {
    Reg() {
        static BalanceFeature m;
        core::RegisterModule(&m);
    }
} g_reg;
}  // namespace
}  // namespace coop
