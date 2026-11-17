#pragma once
#include <enet/enet.h>

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "common/api/protocol/Balance.h"
#include "common/api/protocol/Protocol.h"
#include "common/api/protocol/Settings.h"
#include "common/lib/buffer/Buffer.h"
#include "features/internet/Internet.h"

namespace coop::server {

struct Config {
    std::string name = "SA:COOP server";
    std::string password;
    std::string motd = "Chat commands: /help.";
    bool friendlyFire = false;  // the players can hurt each other
    bool partnerTeleport = true;  // the partners may use /tp and /tphere
    bool sharedLife = false;      // a partner's death fails the host's story mission
    bool partnerCheats = true;    // the partners' cheat codes work
    uint16_t port = 7788;
    std::string bind;        // the address to listen on (empty: every one of this computer's)
    int maxPlayers = 2;
    std::wstring dataDir;    // profiles/, world/
    std::wstring configPath;  // server.ini: its [balance] section is read again by /reloadbalance
    std::wstring worldFile;  // optional initial world save (passed by the launcher)
    bool newGame = false;    // the host starts a new game: the stored world belongs to an older one
    std::string hostName;    // nickname that gets the host role (empty = first player)
    // A server of its own, running with no host's game beside it (docs/FORK.md): the world it stores is the game's and
    // is replaced only by a game that plays it; admins are named, the host's role gives no rights
    bool dedicated = false;
    std::vector<std::string> admins;  // nicknames, lower case
    std::string adminPassword;        // set: an admin says it once a session (/admin <password>)
    float snapshotMinutes = 5.0f;     // dedicated: the host's game is asked for the world that often (0: never)
    // A replaced world is kept as a backup when the last backup is that old, and at every passed mission ("progress").
    // 0: every replaced world. Snapshots come every few minutes: one backup each would leave ten backups an hour deep.
    float backupMinutes = 30.0f;
    bool internet = true;    // UPnP + STUN + invite code + hole punching
    std::string turn;        // optional TURN relay for friends behind any NAT (see TurnRelay)
};

struct Player {
    bool used = false;
    bool joined = false;  // completed the handshake
    ENetPeer* peer = nullptr;
    uint8_t id = 0;
    std::string name;
    bool isHost = false;
    uint8_t status = proto::STATUS_MENU;
    uint64_t connectTime = 0;
    bool follower = false;  // joined the host after a new game's prologue, in a new game of its own: wants the first save
    // plays a world that may not be the host's game now (the stored one, the host loaded another save): wants the next
    bool staleWorld = false;
    // world upload in progress (host only)
    std::vector<uint8_t> upload;
    uint32_t uploadCrc = 0;
    std::string uploadLabel;
    uint64_t sentBytes = 0;  // what went to it since the last traffic line of the log (relayed, the world)
    uint32_t worldVersion = 0;      // the stored world's version its game holds (sent to it, or uploaded by it); 0: none
    uint64_t acceptWorldUntil = 0;  // an admin's /acceptworld: until then its upload replaces the world whatever it plays
    uint32_t claimedGame = 0;       // the game it says it plays (C_GAME_ID), taken when its world is
    bool admin = false;             // it gave the admin password (/admin)
};

class Server {
public:
    bool Start(const Config& cfg);
    void Run();  // blocks until Stop()
    void Stop() { m_running = false; }

    // Thread-safe: console commands are queued and executed by the network thread.
    void QueueConsoleCommand(const std::string& line);

private:
    void Service();
    void OnConnect(ENetPeer* peer);
    void OnDisconnect(ENetPeer* peer, bool timeout);
    void OnPacket(ENetPeer* peer, uint8_t channel, const uint8_t* data, size_t size, uint32_t flags);

    void HandleHello(Player& p, Reader& r);
    void Join(Player& p, const proto::Hello& h, const std::string& name);  // the hello passed: in the game
    void HandleChat(Player& p, const std::string& text);
    void HandleCommand(Player* p, const std::string& text);  // p == nullptr for the console
    void HandleWorldBegin(Player& p, Reader& r);
    void HandleWorldChunk(Player& p, Reader& r);
    void HandleWorldEnd(Player& p);
    void SendWorld(Player& p);
    void RequestWorld(Player& p);
    void FlushWorldRequests(bool fresh);  // fresh: the host's upload of now, else the stored world
    void HostStartedNewGame();
    void FollowersJoin();
    void TellToWait(Player& p);
    void SendProfile(Player& p);
    void SaveProfile(Player& p, const std::vector<uint8_t>& data);
    void WriteProfiles();      // the profile writer's thread
    void WaitProfileWrites();  // returns once what is queued is on the disk
    void SwitchGame(uint32_t game);  // the host plays another game: its partners' profiles
    bool MayNameGame(const Player& p, uint32_t game) const;  // its C_GAME_ID is taken

    void Relay(Player& from, uint8_t channel, const uint8_t* data, size_t size, uint32_t flags);
    // to == nullptr: everyone. The server's own texts come in both languages (en empty: the same in English).
    void SendChat(Player* to, uint8_t fromId, uint32_t rgba, const std::string& text, const std::string& en = {});
    void Broadcast(uint8_t channel, const Writer& w, int exceptId = -1);
    void Kick(Player& p, const std::string& reason, const std::string& reasonEn = {});
    // frees the slot, tells the others; a kick's reason in both languages (reasonEn empty: one text for both)
    void Leave(Player& p, proto::LeaveReason why, const std::string& reason = {}, const std::string& reasonEn = {});
    void EnsureHost();
    void SetHost(Player& p);  // the role goes to p: everyone is told, a game behind the stored world gets it first
    Player* Host();
    bool IsAdmin(const Player* p) const;  // the console, a named admin, and outside dedicated mode the host
    Player* FindPlayerArg(const std::string& arg);  // a command's player: a nickname, or a number of /players
    void ServeWaiters();  // the host changed or left: who waits for the world is answered again
    bool MayReplaceWorld(const Player& p) const;
    void StoreWorld(std::vector<uint8_t>&& data, const std::string& label);  // the new world: kept, written, the old one backed up
    void RotateBackups();  // world.b becomes world.b.1, that one .2 ... the tenth goes
    void ClearWorld();
    void SaveGameId();
    void LoadGameId();
    // wrong passwords by address: after 5 of them the address waits (60 s, twice as long each next time)
    bool Blocked(ENetPeer* peer, uint32_t& seconds);
    void WrongPassword(ENetPeer* peer);
    void RightPassword(ENetPeer* peer);
    void SendPings();
    void ReportTraffic();  // every 30 s: what went to each player

    Player* FindByPeer(ENetPeer* peer);
    Player* FindById(int id);
    Player* FindByName(const std::string& name);
    int JoinedCount() const;

    std::wstring ProfilePath(const std::string& name) const;
    std::wstring WorldPath() const { return m_cfg.dataDir + L"world\\world.b"; }
    std::wstring BackupPath(int n) const { return WorldPath() + L"." + std::to_wstring(n); }
    std::wstring GameIdPath() const { return m_cfg.dataDir + L"world\\game.id"; }
    bool LoadWorld(const std::wstring& path, const std::string& label);

    // The settings from the game (Settings.cpp): the F8 window of a player with the admin's rights changes them
    void SendSettings(Player& p);                    // S_SETTINGS as that player may see them
    void HandleSettingsSet(Player& p, Reader& r);    // C_SETTINGS_SET
    void SendRules(Player* to);                      // to == nullptr: everyone
    std::string ServerSettingValue(const std::string& key) const;
    // false: not a value for that key. shown: the value as the chat and the log say it
    bool ApplyServerSetting(const std::string& key, const std::string& value, std::string& stored, std::string& shown);
    void PersistSetting(const std::string& section, const std::string& key, const std::string& value);

    void SendInvite(Player* to);  // to == nullptr: everyone
    void LoadBalanceFiles();       // [balance] of server.ini and balance_exclude.ini, as they are now
    void SendBalance(Player* to);  // to == nullptr: everyone

    Config m_cfg;
    std::map<std::string, std::string> m_restartSettings;  // changed from the game, in effect from the next start: as written
    ENetHost* m_host = nullptr;
    Internet m_internet;
    // the last slot only holds a connection until its hello: a player back after a crash while its old connection still holds
    // a slot (till ENet's timeout) found four players' slots taken (10.11 audit). The hello moves it to a slot of its own.
    std::array<Player, proto::kMaxPlayers + 1> m_players{};
    uint64_t m_trafficMs = 0;
    volatile bool m_running = false;
    std::vector<uint8_t> m_world;  // current world save
    std::string m_worldLabel;
    uint64_t m_lastPings = 0;
    std::vector<uint8_t> m_worldWaiters;   // player ids waiting for the world save
    uint64_t m_worldWantedTime = 0;        // when the host was asked for a fresh snapshot (0 = not asked)
    bool m_prologue = false;               // the host plays a new game's prologue: partners wait for its end
    uint32_t m_gameId = 0;                 // the host's game (C_GAME_ID): whose profiles the partners get
    proto::Balance m_balance;              // the balance settings every player counts with (S_BALANCE)
    uint32_t m_worldVersion = 0;           // counts up with every world that replaces the stored one (0: no world)
    // dedicated: the player whose game gave the stored world when it was not the stored world's own (the first world,
    // /acceptworld): its game names the world's game (C_GAME_ID). 255: none; a server with no world takes its host's.
    uint8_t m_gameIdOwner = 255;
    uint64_t m_snapshotAskedMs = 0;        // dedicated: the host's game was last asked for a scheduled snapshot
    uint64_t m_backupMs = 0;               // when a replaced world was last kept as a backup (0: not yet in this run)
    struct Guard {
        int fails = 0;
        uint32_t blockSeconds = 0;  // the last wait
        uint64_t blockedUntil = 0, lastMs = 0;
    };
    std::map<uint32_t, Guard> m_guards;    // by IPv4 address

    std::mutex m_cmdMutex;
    std::vector<std::string> m_cmdQueue;

    std::mutex m_writeMutex;
    std::map<std::wstring, std::vector<uint8_t>> m_profileWrites;  // the latest for each file, to be written
    bool m_writing = false;                                         // the writer's thread runs
};

}  // namespace coop::server
