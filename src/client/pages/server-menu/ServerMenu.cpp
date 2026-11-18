// The F8 window (ServerMenu.h). It draws what the server sent (Session::ServerSettings): a setting this build has no
// name for is shown under its key. Its last tab, Cheats, is no setting of the server's: a player's character
// (features/character), read from that player's game and changed there.
#include "pages/server-menu/ServerMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "common/lib/util/Util.h"
#include "features/character/Character.h"
#include "features/weapon-gift/WeaponGift.h"
#include "shared/api/net-session/Session.h"
#include "shared/config/Config.h"
#include "shared/game/Game.h"
#include "shared/lib/core/Core.h"
#include "shared/ui/chat/Chat.h"
#include "shared/ui/input/Input.h"
#include "shared/ui/renderer/Renderer.h"
#include "shared/ui/widgets/Widgets.h"

namespace coop::ui {

namespace {
using proto::Setting;
using w::kSaBlue, w::kSaMuted, w::kSaLine;
constexpr uint32_t kText = 0xFFE6E9EF;

bool g_visible = false;
int g_tab = 0;                                // 0 the server, 1 combat, 2 economy, 3 streets, 4 cheats
uint32_t g_seenRevision = 0;
std::map<std::string, std::wstring> g_edit;   // the text boxes, by key
std::map<std::string, std::string> g_known;   // and the server's value each one was filled from

// a setting's name in the window, by its key in server.ini
std::string NameOf(const Setting& s) {
    const std::string& k = s.key;
    if (k == "name") return TR("Название сервера", "Server name");
    if (k == "password") return TR("Пароль", "Password");
    if (k == "max_players") return TR("Игроков", "Players");
    if (k == "host") return TR("Ник постоянного хоста", "Reserved host nickname");
    if (k == "motd") return TR("Приветствие", "Greeting");
    if (k == "friendly_fire") return TR("Урон по напарнику", "Friendly fire");
    if (k == "partner_teleport") return TR("/tp и /tphere для напарников", "/tp and /tphere for partners");
    if (k == "shared_life") return TR("Общая жизнь", "Shared life");
    if (k == "partner_cheats") return TR("Чит-коды для напарников", "Cheat codes for partners");
    if (k == "admins") return TR("Админы (ники через запятую)", "Admins (nicknames, comma-separated)");
    if (k == "admin_password") return TR("Пароль админа", "Admin password");
    if (k == "snapshot_minutes") return TR("Снимок мира, минут", "World snapshot, minutes");
    if (k == "backup_minutes") return TR("Резервная копия мира, минут", "World backup, minutes");
    if (k == "dedicated") return TR("Отдельный сервер с общим миром", "Dedicated server with a shared world");
    if (k == "internet") return TR("Код приглашения через интернет", "Invite code over the internet");
    if (k == "health_base") return TR("Здоровье врагов", "Enemy health");
    if (k == "health_per_player") return TR("Здоровье врагов, за игрока", "Enemy health, per player");
    if (k == "damage_base") return TR("Урон врагов", "Enemy damage");
    if (k == "damage_per_player") return TR("Урон врагов, за игрока", "Enemy damage, per player");
    if (k == "accuracy_base") return TR("Меткость врагов", "Enemy accuracy");
    if (k == "accuracy_per_player") return TR("Меткость врагов, за игрока", "Enemy accuracy, per player");
    if (k == "gang_density_base") return TR("Плотность банд", "Gang density");
    if (k == "gang_density_per_player") return TR("Плотность банд, за игрока", "Gang density, per player");
    if (k == "mission_health_per_player") return TR("Здоровье врагов миссии, за игрока", "Mission enemy health, per player");
    if (k == "mission_scale_radius_m") return TR("Радиус счёта игроков миссии, м", "Mission player count radius, m");
    if (k == "price_weapons") return TR("Цена оружия", "Weapon price");
    if (k == "price_ammo") return TR("Цена патронов", "Ammo price");
    if (k == "price_armor") return TR("Цена брони", "Armour price");
    if (k == "price_clothes") return TR("Цена одежды", "Clothes price");
    if (k == "price_food") return TR("Цена еды", "Food price");
    if (k == "price_barber_tattoo") return TR("Цена стрижек и тату", "Barber and tattoo price");
    if (k == "price_carmods") return TR("Цена тюнинга", "Car mod price");
    if (k == "payout_mode") return TR("Раздел наград", "Reward sharing");
    if (k == "payout_share") return TR("Доля при scaled", "Share in scaled mode");
    if (k == "sync_driver_leads") return TR("Прохожих и трафик создаёт игра водителя", "Driver's game spawns people and traffic");
    if (k == "sync_instant_hits") return TR("Попадания по чужим людям видны сразу", "Hits on other games' people show at once");
    if (k == "sync_handover") return TR("Люди и машины переходят к игре ближнего игрока", "People and cars pass to the nearer player's game");
    if (k == "spawn_scale") return TR("Дальность людей и машин", "People and traffic range");
    if (k == "ped_density") return TR("Плотность людей", "People density");
    if (k == "car_density") return TR("Плотность машин", "Traffic density");
    return k;
}

// the tab a setting is on: the server's own, the fight (health, damage, accuracy, gangs, missions), the money, the
// streets (how far out and how many)
int TabOf(const Setting& s) {
    if (s.section != "balance") return 0;
    if (s.key == "spawn_scale" || s.key == "ped_density" || s.key == "car_density") return 3;
    return s.key.rfind("price_", 0) == 0 || s.key.rfind("payout_", 0) == 0 ? 2 : 1;
}

// one click of an arrow: fine steps for the multipliers, whole ones for minutes and players, 25 m for the radius
float StepOf(const Setting& s) {
    if (s.kind == proto::SK_INT) return 1.0f;
    if (s.max <= 5.0f) return 0.05f;
    if (s.max <= 20.0f) return 0.1f;
    if (s.max <= 1000.0f) return 0.25f;
    if (s.max <= 1440.0f) return 1.0f;
    return 25.0f;
}

std::string NumberText(float v) {
    std::string t = Format("%.3f", v);
    while (t.back() == '0') t.pop_back();
    if (t.back() == '.') t.pop_back();
    return t;
}

std::string Stepped(const Setting& s, int d) {
    float step = StepOf(s);
    float v = (float)atof(s.value.c_str());
    v = std::round((v + (float)d * step) / step) * step;
    return NumberText(std::clamp(v, s.min, s.max));
}

std::string NextChoice(const Setting& s, int d) {
    std::vector<std::string> all = Split(s.choices, ',');
    if (all.empty()) return s.value;
    size_t i = 0;
    while (i < all.size() && all[i] != s.value) ++i;
    if (i == all.size()) i = 0;
    return all[(i + all.size() + (size_t)(d > 0 ? 1 : all.size() - 1)) % all.size()];
}

std::string Shown(const Setting& s) {
    if (s.kind == proto::SK_BOOL) return s.value == "1" ? TR("да", "on") : TR("нет", "off");
    if (s.kind == proto::SK_SECRET) return s.value.empty() ? TR("нет", "none") : TR("задан", "set");
    return s.value;
}

// the text boxes follow the server: one whose value changed there is filled again, the rest keep what is typed
void SyncEdits(const proto::Settings& all) {
    for (auto& s : all.items) {
        if (s.kind != proto::SK_TEXT && s.kind != proto::SK_SECRET) continue;
        auto it = g_known.find(s.key);
        if (it != g_known.end() && it->second == s.value) continue;
        g_known[s.key] = s.value;
        g_edit[s.key] = s.kind == proto::SK_SECRET ? std::wstring() : Utf8ToWide(s.value);
    }
}

// ---------------------------------------------------------------- the Cheats tab
// A player's character: this game's own, or another player's through the server. A player who may change the server's
// settings may change a character; the others read. A row is a field of features/character.
constexpr int kCheatsTab = 4, kCheatSections = 11, kSectionAnyStat = 9;  // the last section: a script variable
uint8_t g_cheatPlayer = 255;
int g_cheatSection = 0;
std::map<uint16_t, std::wstring> g_cheatEdit;  // the number boxes, by field
std::map<uint16_t, int32_t> g_cheatKnown;      // and the value each one was filled from
std::wstring g_rawStat, g_rawGlobal;           // the number typed for "any stat" and for "script variable"
uint64_t g_cheatAskedMs = 0, g_cheatSinceMs = 0;

struct CheatRow {
    uint16_t field;
    std::string name;
    bool flag = false;  // on or off
};

const char* CheatSectionName(int section) {
    switch (section) {
        case 0: return TR("Здоровье и деньги", "Health and money");
        case 1: return TR("Тело", "Body");
        case 2: return TR("Навыки", "Skills");
        case 3: return TR("Навыки оружия", "Weapon skills");
        case 4: return TR("Награды", "Rewards");
        case 5: return TR("Холодное оружие", "Melee weapons");
        case 6: return TR("Метательное оружие и подарки", "Thrown weapons and gifts");
        case 7: return TR("Огнестрельное оружие", "Guns");
        case 8: return TR("Тяжёлое оружие и снаряжение", "Heavy weapons and gear");
        case kSectionAnyStat: return TR("Любой показатель по номеру", "Any stat by number");
        default: return TR("Переменная скрипта по номеру", "Script variable by number");
    }
}

std::vector<CheatRow> CheatRows(int section) {
    using namespace character;
    std::vector<CheatRow> r;
    auto weapons = [&](int first, int last) {  // a weapon's row is its rounds: 0 takes it away
        for (int t = first; t <= last; ++t)
            if (t != 40) r.push_back({(uint16_t)(F_WEAPON + t), weapon_gift::Name(t)});  // the detonator comes with the satchels
    };
    switch (section) {
        case 0:
            r = {{F_HEALTH, TR("Здоровье", "Health")},
                 {F_ARMOUR, TR("Броня", "Armour")},
                 {24, TR("Максимум здоровья (показатель)", "Max health (stat)")},
                 {F_MAX_ARMOUR, TR("Максимум брони", "Max armour")},
                 {F_MONEY, TR("Деньги", "Money")},
                 {F_WANTED, TR("Уровень розыска", "Wanted level")}};
            break;
        case 1:
            r = {{21, TR("Жир", "Fat")},
                 {22, TR("Выносливость", "Stamina")},
                 {23, TR("Мышцы", "Muscle")},
                 {225, TR("Объём лёгких", "Lung capacity")},
                 {25, TR("Привлекательность", "Sex appeal")},
                 {68, TR("Уважение", "Respect")},
                 {233, TR("Удача", "Luck")},
                 {F_FIGHT_STYLE, TR("Стиль боя (4 обычный, 5 бокс, 6 кунг-фу, 7 кикбоксинг)",
                                    "Fighting style (4 standard, 5 boxing, 6 kung fu, 7 kick boxing)")}};
            break;
        case 2:
            r = {{160, TR("Вождение", "Driving")},
                 {223, TR("Пилотирование", "Flying")},
                 {229, TR("Мотоцикл", "Bike")},
                 {230, TR("Велосипед", "Cycling")},
                 {81, TR("Азартные игры", "Gambling")}};
            break;
        case 3:
            r = {{69, TR("Пистолет", "Pistol")},
                 {70, TR("Пистолет с глушителем", "Silenced pistol")},
                 {71, TR("Desert Eagle", "Desert Eagle")},
                 {72, TR("Дробовик", "Shotgun")},
                 {73, TR("Обрез", "Sawn-off shotgun")},
                 {74, TR("Боевой дробовик", "Combat shotgun")},
                 {75, TR("Пистолет-пулемёт (Uzi, Tec-9)", "Machine pistol (Uzi, Tec-9)")},
                 {76, TR("MP5", "MP5")},
                 {77, TR("АК-47", "AK-47")},
                 {78, TR("M4", "M4")},
                 {79, TR("Винтовка", "Rifle")}};
            break;
        case 4:
            r = {{F_NEVER_TIRED, TR("Бег без усталости (кражи)", "Sprint without tiring (burglary)"), true},
                 {F_FIREPROOF, TR("Огнестойкость (пожарный)", "Fireproof (firefighter)"), true},
                 {F_TAXI_NITRO, TR("Нитро в такси (таксист)", "Nitro in taxis (taxi driver)"), true},
                 {F_PIMP, TR("Награда сутенёра", "Pimp's reward"), true},
                 {F_FAST_RELOAD, TR("Быстрая перезарядка", "Fast reload"), true},
                 {F_HOSPITAL_FREE, TR("Следующая смерть без платы", "Next death without the fee"), true},
                 {F_JAIL_FREE, TR("Следующий арест без платы", "Next arrest without the fee"), true}};
            break;
        case 5: weapons(1, 9); break;
        case 6: weapons(10, 18); break;
        case 7: weapons(22, 34); break;
        case 8: weapons(35, 46); break;
        default: break;
    }
    return r;
}

int CheatRowCount() { return 2 + (g_cheatSection >= kSectionAnyStat ? 2 : (int)CheatRows(g_cheatSection).size()); }

struct CheatLayout {
    float cx, cw, rowH, gap, lh, sh, ctlX, ctlW, s;
};

// the rows from cy down; returns the y under the last one
float DrawCheats(float cy, const CheatLayout& L, bool canEdit) {
    auto& g = Gfx();
    auto& net = Net();
    float s = L.s;
    std::vector<uint8_t> players{net.MyId()};
    for (auto& [id, peer] : net.Peers()) players.push_back(id);
    auto fresh = [] {
        g_cheatEdit.clear();
        g_cheatKnown.clear();
        g_cheatAskedMs = 0;
        g_cheatSinceMs = NowMs();
    };
    if (std::find(players.begin(), players.end(), g_cheatPlayer) == players.end()) g_cheatPlayer = net.MyId(), fresh();
    auto back = [&] { g.Rect(L.cx, cy, L.cw, L.rowH, 0xFF090C10); };
    auto name = [&](const std::string& text) { g.Text(FONT_NORMAL, L.cx + 14 * s, cy + (L.rowH - L.lh) * 0.5f, text, kText, TEXT_SHADOW); };
    auto right = [&](const std::string& text) {
        g.Text(FONT_NORMAL, L.cx + L.cw - 14 * s, cy + (L.rowH - L.lh) * 0.5f, text, kSaMuted, TEXT_RIGHT | TEXT_SHADOW);
    };

    bool mine = g_cheatPlayer == net.MyId();
    back(), name(TR("Игрок", "Player"));
    if (int d = w::Selector("cheat-player", L.ctlX, cy, L.ctlW, L.rowH,
                            mine ? Cfg().nickname + TR(" (эта игра)", " (this game)") : net.PeerName(g_cheatPlayer))) {
        size_t i = std::find(players.begin(), players.end(), g_cheatPlayer) - players.begin();
        g_cheatPlayer = players[(i + (d > 0 ? 1 : players.size() - 1)) % players.size()];
        mine = g_cheatPlayer == net.MyId();
        fresh();
    }
    cy += L.rowH + L.gap;
    back(), name(TR("Раздел", "Section"));
    if (int d = w::Selector("cheat-section", L.ctlX, cy, L.ctlW, L.rowH, CheatSectionName(g_cheatSection)))
        g_cheatSection = (g_cheatSection + kCheatSections + d) % kCheatSections, fresh();
    cy += L.rowH + L.gap;

    std::vector<CheatRow> rows;
    if (g_cheatSection >= kSectionAnyStat) {  // the number, then its value
        bool stat = g_cheatSection == kSectionAnyStat;
        std::wstring& number = stat ? g_rawStat : g_rawGlobal;
        back(), name(stat ? TR("Номер показателя (0-81, 120-342)", "Stat number (0-81, 120-342)") : TR("Номер переменной", "Variable number"));
        w::TextBox(stat ? "cheat-stat" : "cheat-global", L.ctlX, cy + 2 * s, L.ctlW, L.rowH - 4 * s, number, 5, false, true);
        cy += L.rowH + L.gap;
        int n = number.empty() ? -1 : _wtoi(number.c_str()), min = 0, max = 0;
        bool known = n >= 0 && n <= (stat ? (int)character::F_STAT_LAST : 0x3FFF) &&
                     character::Limits((uint16_t)(stat ? n : character::F_GLOBAL + n), min, max);
        if (known) {
            rows.push_back({(uint16_t)(stat ? n : character::F_GLOBAL + n), TR("Значение", "Value")});
        } else {
            back();
            if (!number.empty()) name(TR("Такого номера нет", "No such number"));
            cy += L.rowH + L.gap;
        }
    } else {
        rows = CheatRows(g_cheatSection);
    }

    // another player's values are its game's answers: asked once a second while the tab shows them
    if (!mine && NowMs() - g_cheatAskedMs >= 1000) {
        g_cheatAskedMs = NowMs();
        std::vector<uint16_t> fields;
        for (const CheatRow& row : rows) fields.push_back(row.field);
        character::Ask(g_cheatPlayer, fields);
    }
    const char* waiting = NowMs() - g_cheatSinceMs > 3000 ? TR("нет ответа", "no answer") : "...";
    for (const CheatRow& row : rows) {
        back(), name(row.name);
        int32_t v = 0, min = 0, max = 1;
        character::Limits(row.field, min, max);
        if (!row.flag && min >= 0 && max <= 99999)  // the range a number takes, beside its name
            g.Text(FONT_SMALL, L.cx + 14 * s + g.TextWidth(FONT_NORMAL, row.name) + 12 * s, cy + (L.rowH - L.sh) * 0.5f,
                   Format("%d-%d", min, max), kSaMuted);
        std::string id = Format("cheat-%04X", row.field);
        if (!character::Value(g_cheatPlayer, row.field, v)) {
            right(waiting);
        } else if (!canEdit) {
            right(row.flag ? (v ? TR("да", "on") : TR("нет", "off")) : std::to_string(v));
        } else if (row.flag) {
            bool on = v != 0;
            float box = L.lh * 0.8f;
            if (w::Checkbox(id.c_str(), L.ctlX, cy + (L.rowH - box) * 0.5f, "", on)) character::Change(g_cheatPlayer, row.field, on);
        } else {
            // the box follows the game while nothing is typed anywhere: health and rounds move by themselves
            std::wstring& text = g_cheatEdit[row.field];
            auto known = g_cheatKnown.find(row.field);
            if ((known == g_cheatKnown.end() || known->second != v) && !w::AnyTextFocused())
                text = std::to_wstring(v), g_cheatKnown[row.field] = v;
            if (w::TextBox(id.c_str(), L.ctlX, cy + 2 * s, L.ctlW, L.rowH - 4 * s, text, 11, false, min >= 0)) {
                character::Change(g_cheatPlayer, row.field, (int32_t)std::clamp<long long>(_wtoi64(text.c_str()), min, max));
                g_cheatKnown.erase(row.field);  // filled again from what the game answers
            }
        }
        cy += L.rowH + L.gap;
    }
    return cy;
}
}  // namespace

void SetServerMenuVisible(bool visible) {
    if (visible == g_visible) return;
    g_visible = visible;
    if (visible) {
        g_known.clear();
        g_edit.clear();
        g_seenRevision = 0;
        Net().RequestSettings();
        if (TheChat().IsOpen()) TheChat().Close();
    }
    if (!visible && core::ConnectMenuVisible()) return;  // F9 opened over it keeps the mouse and the keys
    In().SetKeyboardCapture(visible);
    In().SetMouseCapture(visible);
}

bool IsServerMenuVisible() { return g_visible; }

void ToggleServerMenu() {
    if (g_visible) return SetServerMenuVisible(false);
    if (Net().Online() && core::IsPlayerReady() && !game::MenuActive() && !core::ConnectMenuVisible()) SetServerMenuVisible(true);
}

void RenderServerMenu() {
    if (!g_visible) return;
    auto& net = Net();
    if (!net.Online() || !core::IsPlayerReady() || game::MenuActive() || core::ConnectMenuVisible()) return SetServerMenuVisible(false);
    const proto::Settings& all = net.ServerSettings();
    if (net.SettingsRevision() != g_seenRevision) g_seenRevision = net.SettingsRevision(), SyncEdits(all);

    auto& g = Gfx();
    float s = g.Scale();
    float lh = g.LineHeight(FONT_NORMAL), sh = g.LineHeight(FONT_SMALL), mh = g.LineHeight(FONT_MENU);
    float rowH = lh + 12 * s, gap = 4 * s, pad = 32 * s, room = mh * 1.6f;
    int rows = 0;
    for (auto& it : all.items) rows += TabOf(it) == g_tab;
    if (g_tab == kCheatsTab) rows = CheatRowCount();
    rows = std::max(rows, 1);
    float W = std::min(940 * s, g.Width() - 32 * s), cw = W - 2 * pad;
    float H = 24 * s + mh + 14 * s + rowH + 12 * s + rows * (rowH + gap) + 10 * s + sh + 12 * s + rowH + 20 * s;
    float x = (g.Width() - W) * 0.5f, y = std::max(16 * s, (g.Height() - H) * 0.45f);
    float cx = x + pad, cy = y + 24 * s;
    float ctlW = std::min(380 * s, cw * 0.45f), ctlX = cx + cw - ctlW;

    w::BeginFrame();
    g.Rect(x, y, W, H, 0xF0000000);
    g.RectOutline(x, y, W, H, kSaLine, std::max(1.0f, s));
    g.Text(FONT_MENU, cx, cy, TR("Настройки сервера", "Server settings"), kSaBlue, TEXT_SHADOW);
    g.Text(FONT_SMALL, cx + cw, cy + (mh - sh) * 0.5f, net.ServerName(), kSaMuted, TEXT_RIGHT | TEXT_SHADOW);
    cy += mh + 14 * s;

    const char* tabs[] = {TR("Сервер", "Server"), TR("Бой", "Combat"), TR("Деньги", "Economy"), TR("Улицы", "Streets"),
                          TR("Читы", "Cheats")};
    float tx = cx;
    for (int i = 0; i < 5; ++i) {
        float tw = g.TextWidth(FONT_MENU_SMALL, tabs[i]) + 36 * s;
        if (w::Tab(tabs[i], tx, cy, tw, rowH, tabs[i], g_tab == i, true)) g_tab = i;
        tx += tw + 8 * s;
    }
    cy += rowH + 12 * s;

    if (all.items.empty() && g_tab != kCheatsTab) {
        g.Text(FONT_NORMAL, cx, cy + (rowH - lh) * 0.5f, TR("Настройки запрошены у сервера...", "Settings requested from the server..."), kSaMuted,
               TEXT_SHADOW);
        cy += rowH + gap;
    }
    for (auto& it : all.items) {
        if (TabOf(it) != g_tab) continue;
        g.Rect(cx, cy, cw, rowH, 0xFF090C10);
        float ty = cy + (rowH - lh) * 0.5f;
        std::string name = NameOf(it);
        g.Text(FONT_NORMAL, cx + 14 * s, ty, name, kText, TEXT_SHADOW);
        if (it.flags & proto::SF_RESTART)
            g.Text(FONT_SMALL, cx + 14 * s + g.TextWidth(FONT_NORMAL, name) + 12 * s, cy + (rowH - sh) * 0.5f,
                   TR("после перезапуска сервера", "from the server's next start"), kSaMuted);
        std::string id = it.section + "." + it.key;
        std::string send;
        bool changed = false;
        if (!all.canEdit) {
            g.Text(FONT_NORMAL, cx + cw - 14 * s, ty, Shown(it), kSaMuted, TEXT_RIGHT | TEXT_SHADOW);
        } else if (it.kind == proto::SK_BOOL) {
            bool on = it.value == "1";
            float box = lh * 0.8f;
            if (w::Checkbox(id.c_str(), ctlX, cy + (rowH - box) * 0.5f, "", on)) send = on ? "1" : "0", changed = true;
        } else if (it.kind == proto::SK_INT || it.kind == proto::SK_FLOAT) {
            if (int d = w::Selector(id.c_str(), ctlX, cy, ctlW, rowH, it.value)) send = Stepped(it, d), changed = true;
        } else if (it.kind == proto::SK_CHOICE) {
            if (int d = w::Selector(id.c_str(), ctlX, cy, ctlW, rowH, it.value)) send = NextChoice(it, d), changed = true;
        } else {
            bool secret = it.kind == proto::SK_SECRET;
            std::wstring& text = g_edit[it.key];
            const char* hint = secret ? (it.value.empty() ? TR("нет", "none") : TR("задан", "set")) : nullptr;
            if (w::TextBox(id.c_str(), ctlX, cy + 2 * s, ctlW, rowH - 4 * s, text, secret ? 64 : 200, secret, false, hint)) {
                send = WideToUtf8(text), changed = true;
                g_known.erase(it.key);  // filled again from what the server answers
            }
        }
        if (changed) net.SetSetting(it.section, it.key, send);
        cy += rowH + gap;
    }
    if (g_tab == kCheatsTab) cy = DrawCheats(cy, {cx, cw, rowH, gap, lh, sh, ctlX, ctlW, s}, all.canEdit);
    cy += 10 * s;
    const char* note = all.canEdit ? TR("Изменение действует сразу и записывается в server.ini. Текстовое поле: Enter.",
                                        "A change is in effect at once and is written to server.ini. A text field: Enter.")
                                   : TR("Настройки меняет хост или админ сервера.", "Settings are changed by the host or a server admin.");
    if (g_tab == kCheatsTab)
        note = all.canEdit ? TR("Число: Enter. Изменение делает игра выбранного игрока и сохраняет в его профиль на сервере.",
                                "A number: Enter. The change is made by the picked player's game and saved to its profile on the server.")
                           : TR("Персонажей меняет хост или админ сервера.", "Characters are changed by the host or a server admin.");
    g.Text(FONT_SMALL, cx, cy, note, kSaMuted, TEXT_SHADOW);
    cy += sh + 12 * s;
    const char* close = TR("Закрыть", "Close");
    float closeX = cx + cw - g.TextWidth(FONT_MENU, close);
    bool closing = w::Button("close", closeX - room, cy, g.TextWidth(FONT_MENU, close) + 2 * room, rowH, close);
    w::EndFrame();
    w::DrawCursor();
    if (closing) SetServerMenuVisible(false);
}

}  // namespace coop::ui
