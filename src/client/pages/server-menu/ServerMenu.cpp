// The F8 window (ServerMenu.h). It draws what the server sent (Session::ServerSettings): a setting this build has no
// name for is shown under its key.
#include "pages/server-menu/ServerMenu.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "common/lib/util/Util.h"
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
int g_tab = 0;                                // 0 the server, 1 combat, 2 economy
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
    return k;
}

// the tab a setting is on: the server's own, the fight (health, damage, accuracy, gangs, missions), the money
int TabOf(const Setting& s) {
    if (s.section != "balance") return 0;
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

    const char* tabs[] = {TR("Сервер", "Server"), TR("Бой", "Combat"), TR("Деньги", "Economy")};
    float tx = cx;
    for (int i = 0; i < 3; ++i) {
        float tw = g.TextWidth(FONT_MENU_SMALL, tabs[i]) + 36 * s;
        if (w::Tab(tabs[i], tx, cy, tw, rowH, tabs[i], g_tab == i, true)) g_tab = i;
        tx += tw + 8 * s;
    }
    cy += rowH + 12 * s;

    if (all.items.empty()) {
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
    cy += 10 * s;
    g.Text(FONT_SMALL, cx, cy,
           all.canEdit ? TR("Изменение действует сразу и записывается в server.ini. Текстовое поле: Enter.",
                            "A change is in effect at once and is written to server.ini. A text field: Enter.")
                       : TR("Настройки меняет хост или админ сервера.", "Settings are changed by the host or a server admin."),
           kSaMuted, TEXT_SHADOW);
    cy += sh + 12 * s;
    const char* close = TR("Закрыть", "Close");
    float closeX = cx + cw - g.TextWidth(FONT_MENU, close);
    bool closing = w::Button("close", closeX - room, cy, g.TextWidth(FONT_MENU, close) + 2 * room, rowH, close);
    w::EndFrame();
    w::DrawCursor();
    if (closing) SetServerMenuVisible(false);
}

}  // namespace coop::ui
