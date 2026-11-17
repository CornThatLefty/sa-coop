#include "features/balance/BalanceConfig.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "common/lib/ini/Ini.h"
#include "common/lib/log/Log.h"
#include "common/lib/util/Util.h"

namespace coop::server {

using namespace proto;

namespace {

// payout_mode is a word in the file
bool ParseValue(int key, const std::string& text, float& out) {
    std::string t = ToLower(Trim(text));
    if (t.empty()) return false;
    if (key == BK_PAYOUT_MODE) {
        if (t == "full") return out = PAYOUT_FULL, true;
        if (t == "split") return out = PAYOUT_SPLIT, true;
        if (t == "scaled") return out = PAYOUT_SCALED, true;
    }
    char* end = nullptr;
    float v = strtof(t.c_str(), &end);
    if (end == t.c_str()) return false;
    if (key == BK_PAYOUT_MODE) v = (float)(int)v;
    out = v;
    return true;
}

std::string ShowValue(int key, float v) {
    if (key == BK_PAYOUT_MODE) return v < 0.5f ? "full" : v < 1.5f ? "split" : "scaled";
    if (BalanceKeyIsFlag(key)) return v >= 0.5f ? "1" : "0";
    std::string s = Format("%.3f", v);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s += '0';
    return s;
}

std::vector<std::string> List(const std::string& text) {
    std::vector<std::string> out;
    std::string t = text;
    std::replace(t.begin(), t.end(), ';', ',');
    for (auto& part : Split(t, ','))
        if (std::string p = ToLower(Trim(part)); !p.empty()) out.push_back(p);
    return out;
}

}  // namespace

std::string BalanceValueText(int key, float value) { return ShowValue(key, value); }
bool ParseBalanceValue(int key, const std::string& text, float& out) { return ParseValue(key, text, out); }

std::wstring BalanceExcludePath(const std::wstring& serverIni) {
    size_t slash = serverIni.find_last_of(L"\\/");
    return (slash == std::wstring::npos ? std::wstring() : serverIni.substr(0, slash + 1)) + L"balance_exclude.ini";
}

proto::Balance LoadBalance(const std::wstring& serverIni, std::string& summary) {
    Balance b = Balance::Preset();
    Ini ini;
    ini.Load(serverIni);
    for (auto& [key, value] : ini.Items("balance")) {
        int k = BalanceKeyByName(ToLower(key));
        float v = 0;
        if (k < 0) {
            LOGW("server.ini [balance]: unknown key \"%s\"", key.c_str());
        } else if (!ParseValue(k, value, v)) {
            if (!Trim(value).empty()) LOGW("server.ini [balance]: %s = \"%s\" is not a value, the preset %s stays", key.c_str(),
                                           value.c_str(), ShowValue(k, b.v[k]).c_str());
        } else {
            b.v[k] = ClampBalance(k, v);
            if (b.v[k] != v) LOGW("server.ini [balance]: %s = %s is out of range, %s is used", key.c_str(), value.c_str(),
                                  ShowValue(k, b.v[k]).c_str());
        }
    }

    Ini ex;
    if (ex.Load(BalanceExcludePath(serverIni))) {
        for (auto& m : List(ex.Get("exclude", "models"))) {
            long id = strtol(m.c_str(), nullptr, 10);
            if (id >= 0 && id < 20000 && b.excludeModels.size() < 500) b.excludeModels.push_back((uint16_t)id);
            else LOGW("balance_exclude.ini [exclude] models: \"%s\" is not a model number", m.c_str());
        }
        for (auto& m : List(ex.Get("exclude", "missions")))
            if (b.excludeMissions.size() < 500) b.excludeMissions.push_back(m.substr(0, 8));
        for (auto& section : ex.Sections()) {
            std::string s = ToLower(section);
            if (s.rfind("mission:", 0) != 0) continue;
            std::string mission = Trim(s.substr(8)).substr(0, 8);
            if (mission.empty()) continue;
            for (auto& [key, value] : ex.Items(section)) {
                int k = BalanceKeyByName(ToLower(key));
                float v = 0;
                if (k < 0 || !ParseValue(k, value, v)) {
                    LOGW("balance_exclude.ini [%s]: \"%s = %s\" is not a balance key with a value", section.c_str(), key.c_str(),
                         value.c_str());
                    continue;
                }
                if (b.overrides.size() < 2000) b.overrides.push_back({mission, (uint8_t)k, ClampBalance(k, v)});
            }
        }
    }

    summary.clear();
    for (int i = 0; i < BK_COUNT; ++i)
        if (b.v[i] != kBalanceKeys[i].neutral) summary += Format("%s%s=%s", summary.empty() ? "" : ", ", kBalanceKeys[i].name, ShowValue(i, b.v[i]).c_str());
    if (summary.empty()) summary = "all neutral";
    if (!b.excludeModels.empty() || !b.excludeMissions.empty() || !b.overrides.empty())
        summary += Format("; excluded: %zu models, %zu missions; %zu mission overrides", b.excludeModels.size(), b.excludeMissions.size(),
                          b.overrides.size());
    return b;
}

void WriteBalanceDefaults(const std::wstring& serverIni) {
    std::vector<uint8_t> bytes;
    ReadFileBytes(serverIni, bytes);
    std::string text(bytes.begin(), bytes.end());
    if (ToLower(text).find("[balance]") == std::string::npos) {
        if (!text.empty() && text.back() != '\n') text += "\r\n";
        text +=
            "\r\n"
            "; Balance of the fork (docs/FORK.md). Sent to every player on join; /reloadbalance applies an edit to a running game.\r\n"
            "; A scaled value is: base x (1 + per_player x (players - 1)).\r\n"
            "[balance]\r\n"
            "; Damage needed to bring a hostile person down (police, enemy gangs, mission enemies).\r\n"
            "health_base = 1.0\r\n"
            "health_per_player = 0.5\r\n"
            "; Damage a non-player person deals to a player.\r\n"
            "damage_base = 1.0\r\n"
            "damage_per_player = 0.15\r\n"
            "; Weapon accuracy of hostile people, up to 100.\r\n"
            "accuracy_base = 1.0\r\n"
            "accuracy_per_player = 0.1\r\n"
            "; Gang members among the people on the streets.\r\n"
            "gang_density_base = 1.0\r\n"
            "gang_density_per_player = 0.25\r\n"
            "; A story mission's enemies: health per player, counting the players within the radius (metres) of the host.\r\n"
            "mission_health_per_player = 0.5\r\n"
            "mission_scale_radius_m = 300\r\n"
            "; Shop price multipliers. price_ammo: a weapon already owned, bought again for its ammunition.\r\n"
            "price_weapons = 2.0\r\n"
            "price_ammo = 2.0\r\n"
            "price_armor = 3.0\r\n"
            "price_clothes = 1.5\r\n"
            "price_food = 1.0\r\n"
            "price_barber_tattoo = 1.0\r\n"
            "price_carmods = 1.5\r\n"
            "; Mission and joint job rewards. full: every player gets the whole amount. split: the amount divided by the\r\n"
            "; number of players paid. scaled: every player gets the amount x payout_share.\r\n"
            "payout_mode = split\r\n"
            "payout_share = 0.5\r\n"
            "; In a car two players share, the driver's game makes the people and traffic around it (1) or the host's does (0).\r\n"
            "sync_driver_leads = 1\r\n"
            "; A player's hit on a person of another player's game shows in its own game at once (1) or when that game answers (0).\r\n"
            "sync_instant_hits = 1\r\n"
            "; People and traffic: how many times farther out the game makes and removes them (1 to 2), and how many.\r\n"
            "spawn_scale = 1.0\r\n"
            "ped_density = 1.0\r\n"
            "car_density = 1.0\r\n"
            "; A passer-by or car far from its game's player goes to the game of the player nearer it (1) or stays (0).\r\n"
            "sync_handover = 1\r\n";
        if (WriteFileBytes(serverIni, text.data(), text.size())) LOGI("server.ini: the [balance] section added with its presets");
    }
    std::wstring exclude = BalanceExcludePath(serverIni);
    if (!FileExists(exclude)) {
        const char* ex =
            "; What the balance of server.ini leaves alone (docs/FORK.md). /reloadbalance applies an edit to a running game.\r\n"
            "[exclude]\r\n"
            "; Ped model numbers never scaled, separated by commas.\r\n"
            "models =\r\n"
            "; Story missions (script names as tools/re/scm_missions.py lists them) whose people are never scaled.\r\n"
            "missions =\r\n"
            "\r\n"
            "; Balance keys for one story mission, in a section named after its script:\r\n"
            "; [mission:sweet1]\r\n"
            "; mission_health_per_player = 0.25\r\n"
            "; damage_per_player = 0\r\n";
        WriteFileBytes(exclude, ex, strlen(ex));
    }
}

}  // namespace coop::server
