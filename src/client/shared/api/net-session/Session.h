#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "common/api/protocol/Balance.h"
#include "common/api/protocol/Protocol.h"
#include "common/api/protocol/Settings.h"
#include "common/lib/buffer/Buffer.h"
#include "shared/api/net-client/NetClient.h"

namespace coop {

struct PeerInfo {
    uint8_t id = 0;
    std::string name;
    bool isHost = false;
    uint16_t ping = 0;
};

// Network session of the local client: connection lifecycle, handshake, player list and
// routing of game messages to the registered handlers.
class Session {
public:
    enum class State { Offline, Connecting, Online, Failed };
    using Handler = void (*)(uint8_t from, Reader& r);

    void Connect(const std::string& host, uint16_t port, const std::string& nick, const std::string& password,
                 bool wantsHost);
    void Disconnect(const std::string& reason = "");
    void Update();  // once per frame, game thread

    State GetState() const { return m_state; }
    bool Online() const { return m_state == State::Online; }
    bool IsHost() const { return Online() && m_hostId == m_myId; }
    uint8_t MyId() const { return m_myId; }
    uint8_t HostId() const { return m_hostId; }
    const std::string& FailReason() const { return m_failReason; }
    const std::string& ServerName() const { return m_serverName; }
    bool FriendlyFire() const { return Online() && m_friendlyFire; }  // the host let the players hurt each other
    bool PartnerTeleport() const { return m_partnerTeleport; }  // the host let its partners use /tp and /tphere
    bool SharedLife() const { return Online() && m_sharedLife; }  // a partner's death fails the host's story mission
    bool PartnerCheats() const { return m_partnerCheats; }  // the host let its partners use cheat codes
    // the server's balance settings (S_BALANCE); neutral offline and until they come. Revision: counts the ones received
    // in this session (0: none yet)
    const proto::Balance& Balance() const { return m_balance; }
    uint32_t BalanceRevision() const { return m_balanceRevision; }
    // The server's settings as it sent them last (S_SETTINGS; empty until asked for), for the F8 window. Revision: counts
    // the ones received in this session. A change goes out one setting at a time and comes back in a new S_SETTINGS.
    const proto::Settings& ServerSettings() const { return m_settings; }
    uint32_t SettingsRevision() const { return m_settingsRevision; }
    void RequestSettings();
    void SetSetting(const std::string& section, const std::string& key, const std::string& value);
    // what the welcome said of the server's own world and of the game its profiles belong to (S_HOST_CHANGED names the
    // game again): a host that joined a server standing on its own plays that world (entities/session)
    bool ServerHasWorld() const { return m_serverHasWorld; }
    uint32_t ServerGameId() const { return m_serverGameId; }
    const std::map<uint8_t, PeerInfo>& Peers() const { return m_peers; }
    const PeerInfo* Peer(uint8_t id) const;
    std::string PeerName(uint8_t id) const;
    uint32_t Rtt() const { return m_net.Rtt(); }
    NetClient& Net() { return m_net; }

    void Send(uint8_t channel, const Writer& w) { m_net.Send(channel, w); }
    void SendChat(const std::string& text);

    void SetHandler(uint8_t msgId, Handler h) { m_handlers[msgId] = h; }

    // Callbacks for the game flow
    std::function<void()> onOnline;                         // handshake completed
    std::function<void(const std::string&)> onOffline;      // connection closed (reason)
    std::function<void(const std::vector<uint8_t>&, const std::string&)> onWorld;  // world save received
    std::function<void()> onWorldNone;                      // server has no world yet
    std::function<void()> onWorldNewGame;                   // the host's prologue is over: start a new game, join it
    std::function<void()> onWorldWait;                      // the host plays a new game's prologue: wait
    std::function<void()> onWorldWanted;                    // host: a partner needs a fresh snapshot
    std::function<void(const std::vector<uint8_t>&, uint32_t)> onProfile;  // the profile, the host's game it is of
    std::function<void(uint8_t id)> onPeerJoin;
    std::function<void(uint8_t id)> onPeerLeave;
    std::function<void(uint8_t newHost)> onHostChanged;
    std::function<void(const std::string&)> onInvite;      // the server learned its invite code
    std::function<void()> onRules;                          // the game's rules changed while it runs (S_RULES)
    uint32_t (*nameColour)(uint8_t id) = nullptr;           // a player's nickname in the chat (ARGB; entities/player)
    const std::string& InviteCode() const { return m_inviteCode; }

    // World transfer progress (0..1) while downloading, -1 when idle.
    float WorldProgress() const { return m_worldExpected ? (float)m_worldRecv / (float)m_worldExpected : -1.0f; }

private:
    void HandleService(uint8_t id, Reader& r);

    NetClient m_net;
    State m_state = State::Offline;
    std::string m_failReason;
    std::string m_nick, m_password;
    bool m_wantsHost = false;
    uint8_t m_myId = 255, m_hostId = 255;
    std::string m_serverName;
    bool m_friendlyFire = false;
    bool m_partnerTeleport = true;
    bool m_sharedLife = false;
    bool m_partnerCheats = true;
    proto::Balance m_balance;
    uint32_t m_balanceRevision = 0;
    proto::Settings m_settings;
    uint32_t m_settingsRevision = 0;
    bool m_serverHasWorld = false;
    uint32_t m_serverGameId = 0;
    std::string m_inviteCode;
    std::map<uint8_t, PeerInfo> m_peers;
    Handler m_handlers[256] = {};
    std::vector<NetClient::Packet> m_inbox;
    // world download
    std::vector<uint8_t> m_world;
    uint32_t m_worldExpected = 0, m_worldRecv = 0, m_worldCrc = 0;
    int m_worldRetries = 0;  // damaged worlds asked for again in a row
    std::string m_worldLabel;
};

Session& Net();

}  // namespace coop
