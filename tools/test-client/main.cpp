// coop-test-client: a headless player for server tests (tools/test_server.py drives it). It joins a server, prints every
// service message it gets as one "EVT ..." line, and takes commands on stdin:
//
//   chat <text>              C_CHAT (a /command too)
//   status <0..3>            C_STATUS (2: in the game)
//   gameid <hex>             C_GAME_ID
//   profile <game hex> <text>   C_PROFILE_SAVE with the text as the profile's bytes
//   upload <file> <label>    the world: C_WORLD_BEGIN, chunks, C_WORLD_END
//   request                  C_WORLD_REQUEST
//   keep | newgame | follow  C_WORLD_KEEP, C_WORLD_NEWGAME, C_WORLD_FOLLOW
//   game <id> <channel> <hex bytes>   a relayed game message with that payload
//   quit                     leaves as a game does (disconnect data 1)
//   drop                     exits without a word (the server sees a lost connection)
//
//   coop-test-client --name <nick> [--host 127.0.0.1] [--port 7788] [--password X] [--wants-host] [--protocol N]
#include <enet/enet.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/api/net/Net.h"
#include "common/api/protocol/Balance.h"
#include "common/api/protocol/Protocol.h"
#include "common/config/version/Version.h"
#include "common/lib/buffer/Buffer.h"
#include "common/lib/util/Util.h"

using namespace coop;
using namespace coop::proto;

static std::mutex g_mutex;
static std::vector<std::string> g_commands;
static bool g_stdinClosed = false;

static void Say(const std::string& line) {
    fputs(line.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static std::string Flat(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

static void Send(ENetPeer* peer, uint8_t channel, const Writer& w) { net::Send(peer, channel, w); }

static void OnService(uint8_t id, Reader& r, std::vector<uint8_t>& world, uint32_t& worldCrc) {
    switch (id) {
    case S_WELCOME: {
        Welcome w;
        bool ok = w.Read(r);
        std::string players;
        for (auto& p : w.players) players += Format(" %d:%s%s", p.id, p.name.c_str(), p.isHost ? "*" : "");
        Say(Format("EVT WELCOME ok=%d id=%d host=%d world=%d players=[%s ] dedicated=%d game=%08X version=%u", ok, w.yourId, w.hostId,
                   w.worldAvailable, players.c_str(), w.dedicated, w.gameId, w.worldVersion));
        break;
    }
    case S_REJECT:
    case S_KICKED: {
        std::string ru = r.Str(), en = r.Str();
        Say(Format("EVT %s %s", id == S_REJECT ? "REJECT" : "KICKED", Flat(en.empty() ? ru : en).c_str()));
        break;
    }
    case S_PLAYER_JOIN: {
        uint8_t pid = r.U8();
        std::string name = r.Str(64);
        bool host = r.Bool();
        Say(Format("EVT JOIN id=%d name=%s host=%d", pid, name.c_str(), host));
        break;
    }
    case S_PLAYER_LEAVE: {
        uint8_t pid = r.U8(), why = r.U8();
        Say(Format("EVT LEAVE id=%d reason=%d", pid, why));
        break;
    }
    case S_CHAT: {
        uint8_t from = r.U8();
        r.U32();
        std::string text = r.Str(), en = r.Str();
        Say(Format("EVT CHAT from=%d %s", from, Flat(en.empty() ? text : en).c_str()));
        break;
    }
    case S_PROFILE: {
        auto blob = r.Bytes(64 * 1024);
        uint32_t game = r.U32();
        Say(Format("EVT PROFILE size=%zu game=%08X text=%s", blob.size(), game, Flat(std::string(blob.begin(), blob.end())).c_str()));
        break;
    }
    case S_WORLD_BEGIN: {
        uint32_t size = r.U32();
        worldCrc = r.U32();
        std::string label = r.Str(128);
        uint32_t version = r.Remaining() >= 4 ? r.U32() : 0;
        world.assign(size <= 8u * 1024 * 1024 ? size : 0, 0);
        for (char& c : label)
            if (c == ' ') c = '_';
        Say(Format("EVT WORLD_BEGIN size=%u crc=%08X label=%s version=%u", size, worldCrc, label.c_str(), version));
        break;
    }
    case S_WORLD_CHUNK: {
        uint32_t off = r.U32();
        auto bytes = r.Bytes(kWorldChunk);
        if (r.Ok() && off <= world.size() && bytes.size() <= world.size() - off) memcpy(world.data() + off, bytes.data(), bytes.size());
        break;
    }
    case S_WORLD_END:
        Say(Format("EVT WORLD_END size=%zu crc=%08X ok=%d", world.size(), Crc32(world.data(), world.size()),
                   Crc32(world.data(), world.size()) == worldCrc));
        break;
    case S_WORLD_NONE: Say("EVT WORLD_NONE"); break;
    case S_HOST_CHANGED: {
        uint8_t host = r.U8();
        uint32_t game = r.Remaining() >= 4 ? r.U32() : 0;
        Say(Format("EVT HOST id=%d game=%08X", host, game));
        break;
    }
    case S_PINGS: break;
    case S_WORLD_WANTED: Say("EVT WORLD_WANTED"); break;
    case S_INVITE: Say(Format("EVT INVITE %s", r.Str(32).c_str())); break;
    case S_WORLD_NEWGAME: Say("EVT WORLD_NEWGAME"); break;
    case S_WORLD_WAIT: Say("EVT WORLD_WAIT"); break;
    case S_BALANCE: {
        Balance b;
        bool ok = b.Read(r);
        std::string line = Format("EVT BALANCE ok=%d", ok);
        for (int i = 0; i < BK_COUNT; ++i) line += Format(" %s=%g", kBalanceKeys[i].name, b.v[i]);
        line += " models=[";
        for (uint16_t m : b.excludeModels) line += Format(" %u", m);
        line += " ] missions=[";
        for (auto& m : b.excludeMissions) line += " " + m;
        line += " ] overrides=[";
        for (auto& o : b.overrides) line += Format(" %s.%s=%g", o.mission.c_str(), kBalanceKeys[o.key].name, o.value);
        Say(line + " ]");
        break;
    }
    default: Say(Format("EVT SERVICE id=%d size=%zu", id, r.Remaining())); break;
    }
}

static void Upload(ENetPeer* peer, const std::string& path, const std::string& label) {
    std::vector<uint8_t> data;
    if (!ReadFileBytes(Utf8ToWide(path), data)) return Say("ERR cannot read " + path);
    Writer b(C_WORLD_BEGIN);
    b.U32((uint32_t)data.size());
    b.U32(Crc32(data.data(), data.size()));
    b.Str(label, 128);
    Send(peer, CH_BULK, b);
    for (size_t off = 0; off < data.size(); off += kWorldChunk) {
        size_t n = std::min(kWorldChunk, data.size() - off);
        Writer c(C_WORLD_CHUNK);
        c.U32((uint32_t)off);
        c.Bytes(data.data() + off, n);
        Send(peer, CH_BULK, c);
    }
    Send(peer, CH_BULK, Writer(C_WORLD_END));
    Say(Format("OUT UPLOAD size=%zu crc=%08X label=%s", data.size(), Crc32(data.data(), data.size()), label.c_str()));
}

// false: leave the loop (graceful: as a game quits)
static bool Run(ENetPeer* peer, const std::string& line, bool& graceful) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::string rest;
    std::getline(in, rest);
    rest = Trim(rest);
    if (cmd == "chat") {
        Writer w(C_CHAT);
        w.Str(rest, kMaxChatLen * 4);
        Send(peer, CH_RELIABLE, w);
    } else if (cmd == "status") {
        Writer w(C_STATUS);
        w.U8((uint8_t)atoi(rest.c_str()));
        Send(peer, CH_RELIABLE, w);
    } else if (cmd == "gameid") {
        Writer w(C_GAME_ID);
        w.U32((uint32_t)strtoul(rest.c_str(), nullptr, 16));
        Send(peer, CH_RELIABLE, w);
    } else if (cmd == "profile") {
        std::istringstream p(rest);
        std::string game, text;
        p >> game;
        std::getline(p, text);
        text = Trim(text);
        Writer w(C_PROFILE_SAVE);
        w.Bytes(text.data(), text.size());
        w.U32((uint32_t)strtoul(game.c_str(), nullptr, 16));
        Send(peer, CH_RELIABLE, w);
    } else if (cmd == "upload") {
        std::istringstream p(rest);
        std::string path, label;
        p >> path >> label;
        Upload(peer, path, label);
    } else if (cmd == "request") {
        Send(peer, CH_BULK, Writer(C_WORLD_REQUEST));
    } else if (cmd == "keep") {
        Send(peer, CH_RELIABLE, Writer(C_WORLD_KEEP));
    } else if (cmd == "newgame") {
        Send(peer, CH_RELIABLE, Writer(C_WORLD_NEWGAME));
    } else if (cmd == "follow") {
        Send(peer, CH_RELIABLE, Writer(C_WORLD_FOLLOW));
    } else if (cmd == "game") {
        std::istringstream p(rest);
        int id = 0, channel = 0;
        std::string hex;
        p >> id >> channel >> hex;
        Writer w((uint8_t)id);
        for (size_t i = 0; i + 1 < hex.size(); i += 2) w.U8((uint8_t)strtoul(hex.substr(i, 2).c_str(), nullptr, 16));
        Send(peer, (uint8_t)channel, w);
    } else if (cmd == "quit") {
        graceful = true;
        return false;
    } else if (cmd == "drop") {
        graceful = false;
        return false;
    } else if (!cmd.empty()) {
        Say("ERR unknown command " + cmd);
    }
    return true;
}

int main(int argc, char** argv) {
    std::string host = "127.0.0.1", name = "Tester", password;
    int port = kDefaultPort;
    uint32_t protocol = kProtocolVersion;
    bool wantsHost = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--host") host = next();
        else if (a == "--port") port = atoi(next().c_str());
        else if (a == "--name") name = next();
        else if (a == "--password") password = next();
        else if (a == "--protocol") protocol = (uint32_t)atoi(next().c_str());
        else if (a == "--wants-host") wantsHost = true;
    }
    if (!net::Init()) return Say("ERR enet"), 2;
    ENetHost* client = enet_host_create(nullptr, 1, CH_COUNT, 0, 0);
    if (!client) return Say("ERR host"), 2;
    enet_host_compress_with_range_coder(client);  // the server's packets are compressed so: without it every one is dropped
    client->mtu = net::kMtu;
    ENetAddress addr{};
    enet_address_set_host(&addr, host.c_str());
    addr.port = (enet_uint16)port;
    ENetPeer* peer = enet_host_connect(client, &addr, CH_COUNT, 0);
    if (!peer) return Say("ERR connect"), 2;

    std::thread([] {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_commands.push_back(line);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_stdinClosed = true;
    }).detach();

    std::vector<uint8_t> world;
    uint32_t worldCrc = 0;
    bool connected = false, running = true, graceful = true;
    uint64_t start = NowMs();
    while (running) {
        ENetEvent ev;
        while (enet_host_service(client, &ev, 10) > 0) {
            if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                connected = true;
                Hello h;
                h.protocol = protocol;
                h.version = COOP_VERSION_STR;
                h.name = name;
                h.password = password;
                h.wantsHost = wantsHost;
                h.exeSize = kSupportedExeSize;
                Writer w(C_HELLO);
                h.Write(w);
                Send(peer, CH_RELIABLE, w);
                Say("EVT CONNECTED");
            } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                Say("EVT DISCONNECTED");
                running = false;
                peer = nullptr;
            } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                if (ev.packet->dataLength) {
                    uint8_t id = ev.packet->data[0];
                    if (IsRelayed(id)) {
                        Say(Format("EVT GAME id=%d from=%d channel=%d size=%zu", id, ev.packet->dataLength > 1 ? ev.packet->data[1] : -1,
                                   ev.channelID, ev.packet->dataLength - 2));
                    } else {
                        Reader r(ev.packet->data + 1, ev.packet->dataLength - 1);
                        OnService(id, r, world, worldCrc);
                    }
                }
                enet_packet_destroy(ev.packet);
            }
        }
        if (!connected && NowMs() - start > 8000) {
            Say("ERR no answer from the server");
            return 3;
        }
        if (!connected || !peer) continue;
        std::vector<std::string> commands;
        bool closed;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            commands.swap(g_commands);
            closed = g_stdinClosed;
        }
        for (auto& c : commands)
            if (running && !Run(peer, Trim(c), graceful)) running = false;
        if (closed && commands.empty()) running = false;  // the driver went away
    }
    if (peer && graceful) {
        enet_peer_disconnect(peer, 1);
        uint64_t until = NowMs() + 1000;
        ENetEvent ev;
        while (NowMs() < until && enet_host_service(client, &ev, 50) >= 0)
            if (ev.type == ENET_EVENT_TYPE_DISCONNECT) break;
            else if (ev.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(ev.packet);
    }
    enet_host_destroy(client);
    net::Shutdown();
    return 0;
}
