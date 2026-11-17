#pragma once
// Internal: co-op session flow (host / join / world loading) shared by Core and the connect menu.
#include <cstdint>
#include <string>
#include <vector>

namespace coop::flow {

enum class Phase {
    Idle,            // not in a session
    StartingServer,  // local server process is starting
    Connecting,
    WaitingWorld,    // joined as partner, waiting for the host's save
    LoadingWorld,    // a save is being loaded
    Playing,         // in the shared world
};

struct SaveSlotInfo {
    int slot = 0;          // 1..8
    bool exists = false;
    std::string title;     // date and in-game info
};

Phase GetPhase();
std::string StatusText();          // human readable status for the UI
const std::string& LastError();

void Host(int slot, uint16_t port, const std::string& password);  // slot 0 = current game (in-game hosting)
void Join(const std::string& host, uint16_t port, const std::string& password);
void Leave();
void SaveConnectSettings();  // the menu's choices (nickname, mode, address, language, the host's options, keys) to coop.ini

std::vector<SaveSlotInfo> ListSaveSlots();
void Update();             // per frame
void OnGameStarted();
void OnGameShutdown();
void OnWorldReceived(const std::vector<uint8_t>& data);
void RequestFreshWorld();  // host: the server asked for a fresh snapshot
void OnHostInPrologue();   // partner: the host plays a new game's prologue alone, the partner waits for its end
void OnWorldNone();        // partner: no world to load yet, the server sends it when the host has its game
void OnNewGameFromHost();  // partner: the prologue is over (no save of it yet): a new game of its own, next to the host
bool IsFollower();         // partner: in that new game until the host's first save arrives
bool InHostWorld();
// host: the game that starts now is the world a server standing on its own keeps (asked for on joining it), not a save
// of this player's. True once per such load: the caller is the one that names the game to the server (features/progress).
bool TakeHostServerWorld();
bool HostWorldWaiting();  // partner: the host's world after its progress waits to load (at the next mission's start point)
bool HostWorldLoading();  // partner: and loads now (the screen going dark for it, or the load itself)
// online and in the world both games share: the host always, a partner once it plays the host's world (before that
// its pickups, purchases, cheats and the host's ones belonged to two different worlds)
bool InSharedWorld();        // partner: playing the host's world (not its own game while it waits, not during a prologue)
void WorldProgressed();    // host: the story moved on, the partners in the game get the world again
// The game's rules changed while it runs (the F8 window): the host of a game started from the F9 window keeps them as
// that window's choices for the next game, since its server starts with what coop.ini says
void OnRulesChanged();

// partner: where it stood before a reload of the host's world after its progress, put back after the load
struct SavedSpot {
    float x = 0, y = 0, z = 0, heading = 0, health = 100, armour = 0;
    int8_t area = 0;
    bool byDoor = false;  // indoors: the street door it came in by (one room has many: all the Bincos)
    float doorX = 0, doorY = 0;
    int activeWeapon = 0;
    int weapons[13] = {}, ammo[13] = {};
    // the car the partner drove last, when it stands close by: it is in the new world too
    int carModel = 0;
    float carX = 0, carY = 0, carZ = 0, carHeading = 0, carHealth = 1000;
    uint8_t carColors[4] = {};
};
bool TakeSavedSpot(SavedSpot& out);

// Path of the local world copy used by the partner (written into the redirected save slot).
std::wstring PartnerWorldPath();
constexpr int kRedirectSlot = 7;   // 0-based slot index whose file name is redirected (slot 8 in the menu)

}  // namespace coop::flow
