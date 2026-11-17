#pragma once
// The server's settings as the game shows and changes them (the F8 window, docs/FORK.md "Server settings window").
// The server names every setting it has, with its kind and limits: the window draws what it is sent, so a setting a
// later server adds shows up in an older game under its key's name.
#include <cstdint>
#include <string>
#include <vector>

#include "common/lib/buffer/Buffer.h"

namespace coop::proto {

enum SettingKind : uint8_t {
    SK_BOOL = 0,    // "0" or "1"
    SK_INT = 1,     // a whole number from min to max
    SK_FLOAT = 2,   // a number from min to max
    SK_TEXT = 3,
    SK_SECRET = 4,  // a password: the server sends "1" for one that is set and "" for none, never the text
    SK_CHOICE = 5,  // one of choices ("full,split,scaled")
};
enum SettingFlag : uint8_t {
    SF_RESTART = 1,  // written to server.ini at once, in effect from the server's next start
};

struct Setting {
    std::string section, key;  // as in server.ini: "server" or "balance", "friendly_fire"
    std::string value;
    std::string choices;
    uint8_t kind = SK_TEXT, flags = 0;
    float min = 0, max = 0;
};

// S_SETTINGS
struct Settings {
    bool canEdit = false;  // the player it goes to may change them (C_SETTINGS_SET)
    std::vector<Setting> items;

    void Write(Writer& w) const {
        w.Bool(canEdit);
        w.U8((uint8_t)items.size());
        for (auto& s : items) {
            w.Str(s.section, 16);
            w.Str(s.key, 40);
            w.Str(s.value, 512);
            w.Str(s.choices, 128);
            w.U8(s.kind);
            w.U8(s.flags);
            w.F32(s.min);
            w.F32(s.max);
        }
    }
    bool Read(Reader& r) {
        items.clear();
        canEdit = r.Bool();
        uint8_t n = r.U8();
        for (uint8_t i = 0; i < n && r.Ok(); ++i) {
            Setting s;
            s.section = r.Str(16);
            s.key = r.Str(40);
            s.value = r.Str(512);
            s.choices = r.Str(128);
            s.kind = r.U8();
            s.flags = r.U8();
            s.min = r.F32();
            s.max = r.F32();
            if (r.Ok()) items.push_back(std::move(s));
        }
        if (!r.Ok()) items.clear();
        return r.Ok();
    }
};

// S_RULES: what a welcome says of the game's rules, again when one of them changes while the game runs
struct Rules {
    std::string serverName;
    bool friendlyFire = false, partnerTeleport = true, sharedLife = false, partnerCheats = true;

    void Write(Writer& w) const {
        w.Str(serverName, 64);
        w.Bool(friendlyFire);
        w.Bool(partnerTeleport);
        w.Bool(sharedLife);
        w.Bool(partnerCheats);
    }
    bool Read(Reader& r) {
        serverName = r.Str(64);
        friendlyFire = r.Bool();
        partnerTeleport = r.Bool();
        sharedLife = r.Bool();
        partnerCheats = r.Bool();
        return r.Ok();
    }
};

}  // namespace coop::proto
