// The streets' range and numbers (docs/FORK.md, "Wider streets"): the server's spawn_scale, ped_density, car_density.
//
// Range. Every distance the engine makes and removes its people and traffic at is a number of metres times
// TheCamera.m_fGenerationDistMultiplier (0xB6F11C: CCamera::Process sets it to 70 / FOV at 0x52C9F1, so a sniper scope
// gives 4.7 in the game as it is):
//   people   made 42.5-50.5 m out (CPopulation::Update), removed past 54.5 or 65 m (ManagePed, 0x6120F9);
//   traffic  made 120-160 m out (CCarCtrl::GenerateOneRandomCar, 0x430407, 0x431A1C, 0x431A74), removed past 170 m
//            (PossiblyRemoveVehicle, 0x4250E8);
//   parked   CCarGenerator::Process, 160 and 240 m (0x6F3004, 0x6F30B7);
//   drawn    a vehicle within 150 m x sqrt 2 (CVisibilityPlugins::SetRenderWareCamera, 0x732938).
// It is multiplied once, after CCamera::Process. A person is drawn within 60 m x sqrt 2 by the other multiplier, the
// LOD one (ms_pedLodDist, 0xC88034, read by RenderPedCB at 0x7335EF): that distance is multiplied too, or the people
// past it would walk unseen.
// A new car's spot is found by walking the road links out from the player's nearest nodes, and the walk stops after
// 230 m of links (CCarCtrl::GenerateCarCreationCoors2: "fcomp [0x858F80]" at 0x4244F4): a ring 160 m x 1.5 out was
// never reached and no car made. That instruction reads our own number, 230 m x the range. The walk also stops after
// 30 nodes (0x424AD2, an array on its stack), which stays: where the nodes stand close, a range near 2 makes fewer cars.
//
// Numbers. The engine makes people while there are fewer than min(MaxNumberOfPedsInUse, the zone's numbers) x the
// script's density (CPopulation::AddToPopulation, 0x6148D1), and cars while fewer than min(MaxNumberOfCarsInUse, the
// zone's numbers) x the script's density (GenerateOneRandomCar, 0x43011F) and fewer than 45 random ones
// (GenerateRandomCars, 0x434222). The zone's numbers are multiplied after CPopCycle::UpdatePercentages, the three limits
// with them. The scripts' own densities (SET_PED_DENSITY_MULTIPLIER, SET_CAR_DENSITY_MULTIPLIER) stay theirs: a mission
// that empties a street still does.
// A range of 2 covers 4 times the ground, so the numbers go by the range squared: the street looks as full as before.
// They stop at a part of the game's pools, whatever size those have (the game's own 140 people and 110 vehicles, or a
// limit adjuster's): the rest is for the scripts' people and the other games' copies.
#include "features/balance/Streets.h"

#include <algorithm>
#include <cmath>

#include "common/lib/log/Log.h"
#include "entities/balance/Balance.h"
#include "shared/api/net-session/Session.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/lib/memory/Memory.h"

namespace coop {

namespace {
using namespace game;
using namespace proto;

constexpr int kStockPeds = 25;        // CPopulation::MaxNumberOfPedsInUse (0x8D2538)
constexpr int kStockCars = 12;        // CCarCtrl::MaxNumberOfCarsInUse (0x8A5B24)
constexpr int kStockRandomCars = 45;  // "cmp eax, 45" of CCarCtrl::GenerateRandomCars (0x434222: 83 F8 2D)
constexpr float kPedRoom = 0.45f, kCarRoom = 0.4f;  // the part of a pool the streets may take
constexpr float kMaxMultiplier = 5.0f;              // a scope's own 4.7 is not multiplied past this

constexpr float kStockWalk = 230.0f;
float g_walk = kStockWalk;  // read by the game's code (OnInstallHooks)

float g_range = 1.0f, g_people = 1.0f, g_traffic = 1.0f;
bool g_limitsSet = false;
int g_randomCars = kStockRandomCars;

bool On() { return Net().Online() && Net().BalanceRevision() != 0 && core::IsPlayerReady(); }

using CameraProcessFn = void(__fastcall*)(void*, void*);
CameraProcessFn g_origCameraProcess = nullptr;
void __fastcall HookCameraProcess(void* camera, void* edx) {
    g_origCameraProcess(camera, edx);
    float range = On() ? Net().SpawnScale() : 1.0f;
    if (range != g_range) {
        LOGI("Streets: people and traffic are made and removed %.2f times as far out as in the game itself", range);
        g_range = range;
        g_walk = kStockWalk * range;
    }
    if (range == 1.0f) return;
    float& gen = Field<float>(camera, 0xF4);  // CCamera::m_fGenerationDistMultiplier
    gen = std::max(gen, std::min(gen * range, kMaxMultiplier));
}

using SetCameraFn = void(__cdecl*)(void*);
SetCameraFn g_origSetRenderWareCamera = nullptr;
void __cdecl HookSetRenderWareCamera(void* camera) {
    g_origSetRenderWareCamera(camera);
    if (g_range != 1.0f) Ref<float>(0xC88034) *= g_range * g_range;  // ms_pedLodDist, a squared distance
}

// more than the game's own number only as far as the pool has room
float Roomy(float want, CPoolHeader* pool, int stock, float part) {
    if (want <= 1.0f || !pool) return want;
    return std::max(1.0f, std::min(want, (float)pool->size * part / (float)stock));
}
}  // namespace

namespace streets {

void ScaleCounts() {
    float people = 1.0f, traffic = 1.0f;
    if (On()) {
        float range = Net().SpawnScale();
        people = Roomy(balance::Value(BK_PED_DENSITY) * range * range, PedPool(), kStockPeds, kPedRoom);
        traffic = Roomy(balance::Value(BK_CAR_DENSITY) * range * range, VehiclePool(), kStockCars, kCarRoom);
    }
    if (people != g_people || traffic != g_traffic) {
        LOGI("Streets: people x%.2f (up to %ld), traffic x%.2f (up to %ld); pools of %d people, %d vehicles", people,
             std::lround(kStockPeds * people), traffic, std::lround(kStockCars * traffic), PedPool() ? PedPool()->size : 0,
             VehiclePool() ? VehiclePool()->size : 0);
        g_people = people, g_traffic = traffic;
    }
    // CPopCycle::m_NumOther_Peds, m_NumCops_Peds, m_NumGangs_Peds, m_NumDealers_Peds, and the same of cars
    if (people != 1.0f)
        for (uintptr_t a : {0xC0BC40u, 0xC0BC44u, 0xC0BC48u, 0xC0E978u}) Ref<float>(a) *= people;
    if (traffic != 1.0f)
        for (uintptr_t a : {0xC0BC30u, 0xC0BC34u, 0xC0BC38u, 0xC0BC3Cu}) Ref<float>(a) *= traffic;
    // The limits are the game's own constants (it writes 25 and 12 again at 0x56D166 and in CPopulation::Initialise):
    // written while ours differ, and put back once when they stop to
    bool set = people != 1.0f || traffic != 1.0f;
    if (set || g_limitsSet) {
        Ref<uint32_t>(0x8D2538) = (uint32_t)std::lround(kStockPeds * people);
        Ref<uint32_t>(0x8A5B24) = (uint32_t)std::lround(kStockCars * traffic);
        int randomCars = traffic > 1.0f ? (int)std::min(127L, std::lround(kStockRandomCars * traffic)) : kStockRandomCars;  // imm8
        if (randomCars != g_randomCars) mem::Put<uint8_t>(0x434224, (uint8_t)randomCars), g_randomCars = randomCars;
        g_limitsSet = set;
    }
}

float Range() { return g_range; }
float People() { return g_people; }
float Traffic() { return g_traffic; }

}  // namespace streets

namespace {
class StreetsFeature : public Module {
public:
    const char* Name() const override { return "Streets"; }

    void OnInstallHooks() override {
        const uint8_t cmp45[] = {0x83, 0xF8, 0x2D};
        if (!mem::Check(0x434222, cmp45, sizeof cmp45)) LOGW("CCarCtrl::GenerateRandomCars is not the game's own at 0x434222");
        const uint8_t walk[] = {0xD8, 0x1D, 0x80, 0x8F, 0x85, 0x00};  // fcomp dword ptr [0x858F80]
        if (mem::Check(0x4244F4, walk, sizeof walk)) mem::Put<uint32_t>(0x4244F6, (uint32_t)(uintptr_t)&g_walk);
        else LOGW("CCarCtrl::GenerateCarCreationCoors2 is not the game's own at 0x4244F4: a range past 1.4 makes few cars");
        mem::HookFn(0x52B730, &HookCameraProcess, &g_origCameraProcess, "CCamera::Process");
        mem::HookFn(0x7328C0, &HookSetRenderWareCamera, &g_origSetRenderWareCamera, "CVisibilityPlugins::SetRenderWareCamera");
    }

    void OnGameStarted() override {
        LOGI("Pools: %d people, %d vehicles, %d objects", PedPool() ? PedPool()->size : 0, VehiclePool() ? VehiclePool()->size : 0,
             ObjectPool() ? ObjectPool()->size : 0);
    }
};

struct Reg {
    Reg() {
        static StreetsFeature m;
        core::RegisterModule(&m);
    }
} g_reg;
}  // namespace
}  // namespace coop
