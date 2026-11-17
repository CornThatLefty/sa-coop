// The server's settings from the game: the F8 window of a player with the admin's rights reads them (C_SETTINGS_GET) and
// changes one at a time (C_SETTINGS_SET). A change is in effect at once, goes to server.ini, and is told to everyone.
#include <algorithm>
#include <cstdlib>

#include "app/server/Server.h"
#include "common/api/net/Net.h"
#include "common/lib/ini/Ini.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"
#include "features/balance/BalanceConfig.h"

namespace coop::server {

using namespace proto;

namespace {

constexpr uint32_t kColorServer = 0xFFC864FF, kColorError = 0xFF6464FF;

struct Def {
    const char* key;
    uint8_t kind;
    float min, max;
    uint8_t flags;
    bool adminsOnly;  // not shown to a player without the rights
};
// [server] of server.ini, in the window's order. port, bind and turn stay the file's alone: a hosting panel sets the
// first two, and none of them can change under a running server.
const Def kServerDefs[] = {
    {"name", SK_TEXT, 0, 0, 0, false},
    {"password", SK_SECRET, 0, 0, 0, true},
    {"max_players", SK_INT, 2, (float)kMaxPlayers, 0, false},
    {"host", SK_TEXT, 0, 0, 0, false},
    {"motd", SK_TEXT, 0, 0, 0, false},
    {"friendly_fire", SK_BOOL, 0, 1, 0, false},
    {"partner_teleport", SK_BOOL, 0, 1, 0, false},
    {"shared_life", SK_BOOL, 0, 1, 0, false},
    {"partner_cheats", SK_BOOL, 0, 1, 0, false},
    {"admins", SK_TEXT, 0, 0, 0, true},
    {"admin_password", SK_SECRET, 0, 0, 0, true},
    {"snapshot_minutes", SK_FLOAT, 0, 1440, 0, false},
    {"backup_minutes", SK_FLOAT, 0, 1440, 0, false},
    {"dedicated", SK_BOOL, 0, 1, SF_RESTART, false},
    {"internet", SK_BOOL, 0, 1, SF_RESTART, false},
};

const Def* FindDef(const std::string& key) {
    for (auto& d : kServerDefs)
        if (key == d.key) return &d;
    return nullptr;
}

std::string Flag(bool b) { return b ? "1" : "0"; }

bool ParseFlag(const std::string& text, bool& out) {
    std::string t = ToLower(Trim(text));
    if (t == "1" || t == "true" || t == "yes" || t == "on") return out = true, true;
    if (t == "0" || t == "false" || t == "no" || t == "off") return out = false, true;
    return false;
}

bool ParseNumber(const std::string& text, float& out) {
    std::string t = Trim(text);
    std::replace(t.begin(), t.end(), ',', '.');  // a decimal comma typed in a Russian layout
    char* end = nullptr;
    float v = strtof(t.c_str(), &end);
    if (end == t.c_str() || *end || !(v == v)) return false;
    return out = v, true;
}

std::string Number(float v) {
    std::string s = Format("%.3f", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s;
}

}  // namespace

std::string Server::ServerSettingValue(const std::string& key) const {
    if (auto it = m_restartSettings.find(key); it != m_restartSettings.end()) return it->second;
    if (key == "name") return m_cfg.name;
    if (key == "password") return m_cfg.password;
    if (key == "max_players") return std::to_string(m_cfg.maxPlayers);
    if (key == "host") return m_cfg.hostName;
    if (key == "motd") return m_cfg.motd;
    if (key == "friendly_fire") return Flag(m_cfg.friendlyFire);
    if (key == "partner_teleport") return Flag(m_cfg.partnerTeleport);
    if (key == "shared_life") return Flag(m_cfg.sharedLife);
    if (key == "partner_cheats") return Flag(m_cfg.partnerCheats);
    if (key == "admin_password") return m_cfg.adminPassword;
    if (key == "snapshot_minutes") return Number(m_cfg.snapshotMinutes);
    if (key == "backup_minutes") return Number(m_cfg.backupMinutes);
    if (key == "dedicated") return Flag(m_cfg.dedicated);
    if (key == "internet") return Flag(m_cfg.internet);
    if (key == "admins") {
        std::string list;
        for (auto& a : m_cfg.admins) list += (list.empty() ? "" : ", ") + a;
        return list;
    }
    return {};
}

bool Server::ApplyServerSetting(const std::string& key, const std::string& value, std::string& stored, std::string& shown) {
    const Def* d = FindDef(key);
    if (!d) return false;
    bool flag = false;
    float number = 0;
    if (d->kind == SK_BOOL && !ParseFlag(value, flag)) return false;
    if ((d->kind == SK_INT || d->kind == SK_FLOAT) && (!ParseNumber(value, number) || number < d->min || number > d->max)) return false;
    if (d->kind == SK_INT) number = (float)(int)number;
    stored = d->kind == SK_BOOL ? Flag(flag) : d->kind == SK_INT || d->kind == SK_FLOAT ? Number(number) : value;
    shown = stored;
    if (d->kind == SK_SECRET) shown = value.empty() ? "-" : "***";

    if (d->flags & SF_RESTART) {  // what the running server is stays; the file and the window have the new value
        m_restartSettings[key] = stored;
    } else if (key == "name") {
        if (value.empty() || value.size() > 64) return false;
        m_cfg.name = value;
    } else if (key == "password") {
        if (value.size() > 64) return false;
        m_cfg.password = value;
    } else if (key == "max_players") {
        m_cfg.maxPlayers = (int)number;
    } else if (key == "host") {
        if (!value.empty() && SanitizeNickname(value) != value) return false;
        m_cfg.hostName = value;
    } else if (key == "motd") {
        m_cfg.motd = value;
    } else if (key == "friendly_fire") {
        m_cfg.friendlyFire = flag;
    } else if (key == "partner_teleport") {
        m_cfg.partnerTeleport = flag;
    } else if (key == "shared_life") {
        m_cfg.sharedLife = flag;
    } else if (key == "partner_cheats") {
        m_cfg.partnerCheats = flag;
    } else if (key == "admin_password") {
        if (value.size() > 64) return false;
        m_cfg.adminPassword = value;
    } else if (key == "snapshot_minutes") {
        m_cfg.snapshotMinutes = number;
    } else if (key == "backup_minutes") {
        m_cfg.backupMinutes = number;
    } else if (key == "admins") {
        m_cfg.admins.clear();
        for (auto& nick : Split(value, ','))
            if (std::string n = ToLower(Trim(nick)); !n.empty()) m_cfg.admins.push_back(n);
        stored = shown = ServerSettingValue("admins");
    }
    return true;
}

void Server::PersistSetting(const std::string& section, const std::string& key, const std::string& value) {
    if (m_cfg.configPath.empty()) return;
    Ini ini;
    ini.Load(m_cfg.configPath);
    ini.Set(section, key, value);
    if (!ini.Save(m_cfg.configPath)) LOGW("server.ini could not be written: %s = %s holds until the server stops", key.c_str(), value.c_str());
}

void Server::SendSettings(Player& p) {
    Settings s;
    s.canEdit = IsAdmin(&p);
    for (auto& d : kServerDefs) {
        if (d.adminsOnly && !s.canEdit) continue;
        Setting it;
        it.section = "server";
        it.key = d.key;
        it.kind = d.kind;
        it.flags = d.flags;
        it.min = d.min;
        it.max = d.max;
        it.value = ServerSettingValue(d.key);
        if (d.kind == SK_SECRET) it.value = it.value.empty() ? "" : "1";
        s.items.push_back(std::move(it));
    }
    for (int i = 0; i < BK_COUNT; ++i) {
        Setting it;
        it.section = "balance";
        it.key = kBalanceKeys[i].name;
        it.kind = i == BK_PAYOUT_MODE ? SK_CHOICE : BalanceKeyIsFlag(i) ? SK_BOOL : SK_FLOAT;
        if (i == BK_PAYOUT_MODE) it.choices = "full,split,scaled";
        it.min = kBalanceKeys[i].min;
        it.max = kBalanceKeys[i].max;
        it.value = BalanceValueText(i, m_balance.v[i]);
        s.items.push_back(std::move(it));
    }
    Writer w(S_SETTINGS);
    s.Write(w);
    net::Send(p.peer, CH_RELIABLE, w);
}

void Server::SendRules(Player* to) {
    Rules r;
    r.serverName = m_cfg.name;
    r.friendlyFire = m_cfg.friendlyFire;
    r.partnerTeleport = m_cfg.partnerTeleport;
    r.sharedLife = m_cfg.sharedLife;
    r.partnerCheats = m_cfg.partnerCheats;
    Writer w(S_RULES);
    r.Write(w);
    if (to) net::Send(to->peer, CH_RELIABLE, w);
    else Broadcast(CH_RELIABLE, w);
}

void Server::HandleSettingsSet(Player& p, Reader& r) {
    std::string section = ToLower(Trim(r.Str(16))), key = ToLower(Trim(r.Str(40))), value = r.Str(512);
    if (!r.Ok()) return;
    for (char& c : value)
        if ((unsigned char)c < 0x20) c = ' ';  // a line of the file
    value = Trim(value);
    if (!IsAdmin(&p)) {
        SendChat(&p, kServerId, kColorError, "Настройки сервера меняет хост или админ.", "The server's settings are changed by the host or an admin.");
        return SendSettings(p);
    }
    std::string stored, shown;
    bool ok = false;
    if (section == "balance") {
        float v = 0;
        if (int k = BalanceKeyByName(key); k >= 0 && ParseBalanceValue(k, value, v) && v >= kBalanceKeys[k].min && v <= kBalanceKeys[k].max) {
            m_balance.v[k] = v;
            stored = shown = BalanceValueText(k, v);
            ok = true;
        }
    } else if (section == "server") {
        ok = ApplyServerSetting(key, value, stored, shown);
    }
    if (!ok) {
        SendChat(&p, kServerId, kColorError, Format("Настройка %s: значение не подходит.", key.c_str()),
                 Format("Setting %s: not a value for it.", key.c_str()));
        return SendSettings(p);  // the window shows what holds
    }
    PersistSetting(section, key, stored);
    LOGI("Setting [%s] %s = %s by %s", section.c_str(), key.c_str(), shown.c_str(), p.name.c_str());
    const Def* d = section == "server" ? FindDef(key) : nullptr;
    bool restart = d && (d->flags & SF_RESTART);
    SendChat(nullptr, kServerId, kColorServer,
             Format("Настройка сервера: %s = %s (%s)%s.", key.c_str(), shown.c_str(), p.name.c_str(), restart ? ", после перезапуска сервера" : ""),
             Format("Server setting: %s = %s (%s)%s.", key.c_str(), shown.c_str(), p.name.c_str(), restart ? ", from the server's next start" : ""));
    if (section == "balance") SendBalance(nullptr);
    else SendRules(nullptr);
    for (auto& o : m_players)
        if (o.used && o.joined) SendSettings(o);  // every open window shows the same
}

}  // namespace coop::server
