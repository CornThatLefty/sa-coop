# SA:COOP fork: what differs from upstream

This fork of [SA:COOP](https://github.com/Karimov-Akbar/sa-coop) adds co-op balancing, and later a dedicated Linux
server, host migration and a higher player cap. Each section says what changed, why, and which settings control it.
`docs/ARCHITECTURE.md` (Russian) stays the reference for everything inherited from upstream.

All players and the server must run the same fork build: the protocol version differs from upstream's (64 against 63).

## Contents

- [Balance settings and how they reach the players](#balance-settings-and-how-they-reach-the-players)
- [Combat scaling](#combat-scaling)
- [Shop prices](#shop-prices)
- [Payouts](#payouts)
- [Name tags switch](#name-tags-switch)
- [Server additions](#server-additions)
- [Dedicated mode](#dedicated-mode)
- [Tests without the game](#tests-without-the-game)
- [Engine addresses added by the fork](#engine-addresses-added-by-the-fork)
- [Fixes to inherited behaviour](#fixes-to-inherited-behaviour)
- [Release files](#release-files)
- [Known issues and limits](#known-issues-and-limits)

## Balance settings and how they reach the players

Balance settings live on the server, in the `[balance]` section of `server.ini` and in `balance_exclude.ini` beside it.
A game hosted from the F9 window uses `coop\server.ini` in the game folder. The server adds the section and the exclude
file with their presets the first time it starts.

The server sends the settings to every player in a reliable message, `S_BALANCE`, right after the welcome. The host (or
the server console) re-reads both files with `/reloadbalance`, and every player gets the new values at once. Clients
never read balance values from their own files, so every game counts with the same numbers.

A client that received no `S_BALANCE` keeps the neutral values: every multiplier 1.0, payouts whole.

`/balance` in the chat lists the values in effect: player counts, enemy multipliers, price multipliers, payout mode.

**Scaling formula.** `value = base × (1 + per_player × (n − 1))`, where `n` is the number of players in the game. A
player in the menu or loading is not counted. For a story mission's people, `n` counts only the players within
`mission_scale_radius_m` of the host, the host included.

| Key | Preset | Neutral | Range | Effect |
| --- | --- | --- | --- | --- |
| `health_base` | 1.0 | 1.0 | 0.1–20 | Enemy health multiplier with one player |
| `health_per_player` | 0.5 | 0 | 0–5 | Added per extra player, outside story missions |
| `damage_base` | 1.0 | 1.0 | 0.1–20 | Multiplier of damage a non-player person deals to a player |
| `damage_per_player` | 0.15 | 0 | 0–5 | Added per extra player |
| `accuracy_base` | 1.0 | 1.0 | 0.1–5 | Multiplier of hostile people's weapon accuracy |
| `accuracy_per_player` | 0.1 | 0 | 0–5 | Added per extra player |
| `gang_density_base` | 1.0 | 1.0 | 0–10 | Multiplier of the gang members the streets should hold |
| `gang_density_per_player` | 0.25 | 0 | 0–5 | Added per extra player |
| `mission_health_per_player` | 0.5 | 0 | 0–5 | `health_per_player` for a story mission's people |
| `mission_scale_radius_m` | 300 | 300 | 10–10000 | Radius around the host that counts players for a story mission |
| `price_weapons` | 2.0 | 1.0 | 0.01–1000 | Ammu-Nation: a weapon the player does not have |
| `price_ammo` | 2.0 | 1.0 | 0.01–1000 | Ammu-Nation: a weapon the player already has, bought again for ammunition |
| `price_armor` | 3.0 | 1.0 | 0.01–1000 | Body armour |
| `price_clothes` | 1.5 | 1.0 | 0.01–1000 | Clothes shops |
| `price_food` | 1.0 | 1.0 | 0.01–1000 | Restaurants |
| `price_barber_tattoo` | 1.0 | 1.0 | 0.01–1000 | Barbers and tattoo parlours |
| `price_carmods` | 1.5 | 1.0 | 0.01–1000 | Mod shop parts |
| `payout_mode` | `split` | `full` | `full`, `split`, `scaled` | How a reward is shared, see [Payouts](#payouts) |
| `payout_share` | 0.5 | 1.0 | 0–1 | Each player's part of the amount in `scaled` mode |

"Preset" is what the server uses when `server.ini` does not set the key. "Neutral" is the unmodified game.

**`balance_exclude.ini`**

```ini
[exclude]
; Ped model numbers never scaled, separated by commas.
models = 0, 271
; Story missions (script names) whose people are never scaled.
missions = sweet1, cesar1

; Balance keys for one story mission, in a section named after its script.
[mission:ryder1]
mission_health_per_player = 0.25
damage_per_player = 0
```

Mission names are script names as `python tools/re/scm_missions.py <path to main.scm>` lists them, in any letter case.
A `[mission:<name>]` value replaces the `server.ini` value for that mission's people while the mission runs.

Source: `src/common/api/protocol/Balance.h` (keys, presets, wire format), `src/server/features/balance` (files),
`src/client/entities/balance` (rules), `src/client/features/balance` (engine hooks).

## Combat scaling

**Who counts as hostile.** Police; anyone who hates or dislikes the player, which covers the enemy gangs as `main.scm`
sets them (Ballas, Vagos, and gang type 8) and mission enemies whose type the mission sets against the player; anyone
whose kill task targets a player; anyone who has hit a player in the last 10 minutes. Never a player, a member of the
local player's group (recruited Grove members, mission companions that follow), or an excluded model.

**Enemy health** is scaled as damage taken instead of by writing health. A hit on a hostile person counts for
`1 / multiplier` in the game that owns the person, with random rounding so that small repeated damage keeps its average.

Why not write health and max health as first planned:

- Missions set health with `SET_CHAR_HEALTH` after `CREATE_CHAR`, which would overwrite a value set at creation.
- A person's relationship to the player is usually set after creation, so hostility is not known at creation.
- Health travels between games as one byte (0–255), so a scaled value would be cut on an ownership handover and could
  then be scaled a second time.
- Scripts that test a person's health against a fixed number keep working.

Collisions, run-overs, falls and drowning are not scaled. A headshot kills as in the unmodified game.

**Enemy damage** is scaled in the game of the player who is hit: a hit of a non-player person on the local player is
multiplied before the engine applies it. Hits from the local player's own group and from Grove members who do not hate
the player are not scaled. Players' damage to each other and to anything else is unchanged.

**Enemy accuracy** is the person's `CPed::m_nWeaponAccuracy` (60 by default, 0–100). Every game sets it on the hostile
people it shows, copies of other games' people included, because a copy fires at the local player with the accuracy it
has locally. The value a script sets later (`SET_CHAR_ACCURACY`) is picked up as the new base. The base value is put
back when a person stops being hostile, on `/reloadbalance` and when the session ends.

**Gang members on the streets.** `CPopCycle::UpdatePercentages` computes how many people of each kind a zone should
hold. The fork multiplies the gang number after it, in every game that generates population. Gang cars are unchanged.

**Story missions.** A story mission's people use `mission_health_per_player` and the player count near the host. In a
partner's game every copy of the host's people counts as the mission's while a story mission runs. Three seconds after
a story mission starts, each game prints one chat line, for example
`Co-op scaling: 3 players, enemy health x2.0, damage x1.3, accuracy x1.2.`, or `Co-op scaling: off for this mission.`

Mission reinforcements (extra enemies) are not built yet.

## Shop prices

`CShopping::GetPrice` is the one place the engine reads a price, for the number a shop menu shows and for the purchase.
The fork multiplies its result by the category of the price list in memory. `shopping.dat` and the in-memory lists are
untouched, so the item list and the clothes kept in profiles by their `shopping.dat` keys are unaffected. A changed
value applies from the next price the game reads, also after `/reloadbalance`.

| `shopping.dat` price section | Key |
| --- | --- |
| `CarMods`, mod shop paint jobs | `price_carmods` |
| `Clothes` | `price_clothes` |
| `Haircuts`, `Tattoos` | `price_barber_tattoo` |
| `Food` | `price_food` |
| `Weapons`: `armour` | `price_armor` |
| `Weapons`: a weapon the player has | `price_ammo` |
| `Weapons`: a weapon the player does not have | `price_weapons` |

A scaled price is clamped to $1–$999,999. A price of $0 stays $0.

Not covered, because the price does not come from `shopping.dat`: vending machines, Pay 'n' Spray, mod shop colours,
property prices (`main.scm`), and anything else a script charges with a fixed amount.

## Payouts

Upstream pays every player the full amount of a reward, so a mission for four creates four times the money.
`payout_mode` changes what each player gets:

| `payout_mode` | Each player gets |
| --- | --- |
| `full` | The whole amount (upstream behaviour) |
| `split` | `amount / n`, rounded down; `n` is the number of players paid, the earner among them |
| `scaled` | `amount × payout_share` |

The game's script pays the earner the whole amount first. At the moment the reward goes out to the others, the fork
takes the difference back from the earner's wallet once, and writes a `Payout:` line to `coop.log`.

| Reward | Earner | Players paid |
| --- | --- | --- |
| Story mission passed | Host | Every player in the game |
| Joint side job (taxi, vigilante, paramedic, firefighter, burglary, …) | Job owner | Owner and everyone who joined |
| Own taxi job next to a joined one | The driver | Driver and the job owner |
| Big Smoke's courier | Host | Host and partners within 300 m |
| Docks export delivery | Player who delivered | Every player in the game |

Chat lines: `Mission reward: $1250 (split 4 ways).` for every player; `Job pay: $50 of $100 (split 2 ways).`,
`Courier cash: …` and `Export pay: …` for the earner when its part is smaller than the amount.

The reward of a story mission or of a joint side job is the money its own script pays the earner: the `ADD_SCORE`
commands (opcode `0109`) the script runs for the local player, counted as they run (`NoteScriptPay` in
`features/story/missions`). Money the script takes counts against it. Cash picked up meanwhile is no part of the reward
and stays with the player who picked it up. A mission whose script pays nothing sends no reward: `coop.log` of the
host then says `Mission reward: none`.

Upstream measures the reward as everything the earner's wallet gained from the start to the end, pickups included;
test builds 1 and 2 did the same. Checked against the game's `main.scm` and `script.img`: 72 story missions and every
joint side job pay through `ADD_SCORE` in their own script; the early Los Santos missions that give respect alone
have none.

In `split` and `scaled` modes a player who joins a side job late shares only the money earned after joining. In `full`
mode the late joiner still receives everything earned since the job began, as upstream.

Unchanged: gifts between players (F2), pool stakes, contest winnings, and non-money completion rewards of side jobs
(max health, armour, fireproof and the like), which every player still receives.

## Name tags switch

The names, health and armour bars over the other players could be turned off only by `name_tags = 0` in `[game]` of
`coop\coop.ini`, read at the game's start. Test build 4 adds two switches that act at once and write the same key:

- the `Name tags over players` checkbox of the F9 window, shown while connected, beside the mark colour;
- `/names` in the chat (turns them over), `/names on`, `/names off`.

The setting is each player's own: it changes what that player's game draws and is not sent to anyone. Source:
`Players::SetNameTags` in `src/client/entities/player`.

## Server additions

These apply to every server, a game hosted from the F9 window included.

| Addition | Use |
| --- | --- |
| `[balance]` in `server.ini`, `balance_exclude.ini` | See above |
| `/reloadbalance` (host, admin or console) | Re-reads both files and sends them to every player |
| `bind =` in `[server]`, `--bind <address>` | Address to listen on; empty listens on every address |
| World backups | The world a new upload replaces is kept: `world\world.b.1` (latest) to `world.b.10` |
| `/restoreworld [number]` (admin) | Without a number lists the backups; with one, makes that backup the world and sends it to the players in the game. The world it replaces becomes backup 1 |
| Game id kept over a restart | `world\game.id` holds the id of the stored world's game, so partners get that game's profiles from their first join after a restart |
| World waiters | A partner waiting for the world is answered when the host leaves or changes: the new host's game is asked, or the stored world is sent. A repeated request is answered each time |
| Host choice | When the host leaves, the role goes to a player in the game holding the stored world's version, then to any player in the game, then to the longest connected |
| Password cooldown | After 5 wrong passwords an address waits 60 s; each further block doubles, up to 1 hour. Covers the server password and `/admin` |
| `admins =`, `admin_password =` in `[server]` | Named admins, see below |
| `/host <player>` (admin) | Gives the host role to that player |

**Admins.** The server console is always an admin. `admins = Alice, Bob` names admins by nickname. With no
`admin_password`, a named nickname is an admin as soon as it joins, so anyone who takes the nickname is one: set a
password on a server open to strangers. With `admin_password` set, a named player types `/admin <password>` once per
session; with a password and an empty `admins` list, any player who knows the password can. On a server hosted from
the F9 window the host is an admin too, as before.

**Wire additions** (protocol 64, appended fields that an older fork build skips): `S_WELCOME` carries `dedicated`, the
game id and the world version; `S_WORLD_BEGIN` the world version; `S_HOST_CHANGED` the game id.

## Dedicated mode

`dedicated = 1` in `[server]` (or `--dedicated`) is for a server that runs on its own, with no host's game beside it.
The world it stores is the game, whoever hosts.

The client side of this (joining a dedicated server as host, profiles for the host, moving the host role without a
reload) is not built yet. Until it is, `dedicated = 1` is for server tests; games hosted from the F9 window run with
`dedicated = 0` and behave as before apart from the table above.

| Rule in dedicated mode | Detail |
| --- | --- |
| World lock | An upload replaces the world only when the server has no world, when the uploading game holds the stored world's version (it received that world from the server, or uploaded it), or within 5 minutes of an admin's `/acceptworld` for that player. Anything else is refused and the uploader is told in the chat |
| World version | Counts up with every world that replaces the stored one. A player holds the version last sent to it or uploaded by it |
| `/acceptworld [player]` (admin) | Lets the host's next upload replace the world, and asks its game for a snapshot. The player must be the host |
| `/newgame` (admin) | Clears the stored world (kept as backup 1) and the game id. The host's next upload becomes the world. A host's game starting a new story on its own does not clear the world |
| `--world <file>` | Seeds a server that has no world. Ignored, with a log line, when a world is stored |
| Stored world while the host is in the menu | Sent to a partner at once; outside dedicated mode a partner waits for the host's game |
| Game id | Named by the game the world came from: the first host's on an empty server, or the game accepted by `/acceptworld`. A later host's game cannot change it, with 0 or with another id |
| New host behind the world | A player made host while holding an older version is sent the stored world |
| `snapshot_minutes` (preset 5, 0 off) | The server asks the host's game for a snapshot that often, when the host is in the game and holds the stored version. The game answers when it can save. Fractions are allowed |
| Rights | The host role gives no admin rights |

After `/restoreworld` the game id is kept: a backup from another game needs `/newgame` or `/acceptworld` instead.

## Tests without the game

```
python tools/build.py server testclient
python tools/test_server.py
```

`coop-test-client` (`tools/test-client`) is a headless player: it joins a server, prints each service message as a
line and takes commands on stdin. `tools/test_server.py` starts a server per test in a temporary folder, bound to
127.0.0.1 with `internet = 0`, and checks the exchange. Add `-v` for the full output or a word to filter tests by name.

Covered: balance files and `/reloadbalance`, protocol mismatch, host role on join and leave, world upload and
download, profiles per game, game id over a restart, waiters when the host leaves, repeated world requests, password
cooldown, the dedicated world lock, seeding, `/acceptworld`, `/restoreworld`, `/newgame`, `/host`, `/kick` rights,
host choice by world version, and scheduled snapshot requests.

## Engine addresses added by the fork

All for `gta_sa.exe` 1.0 US, taken from gta-reversed and checked in the executable's disassembly with
`tools/re/exe.py` (set `GTA_SA_EXE` to the executable's path; it needs the `capstone` Python package).

| Address | What | Checked by |
| --- | --- | --- |
| `0x49AD50` | `CShopping::GetPrice(key)`, cdecl | Its body reads `ms_numPrices` and `ms_prices`; called from `CShopping::Buy` at `0x49C000` and from the script command at `0x46EBBE`, nowhere else |
| `0xA9A7C8` | `CShopping::ms_priceSectionLoaded` | Written by `LoadPrices` and `RemoveLoadedPrices` |
| key 48 | `WEAPON_ARMOUR` as a `Weapons` item | `cmp esi, 0x30` in `CShopping::Buy` at `0x49C20D` |
| `0x610770` | `CPopCycle::UpdatePercentages()`, cdecl, no arguments | Called once from `CPopCycle::Update` at `0x610CAB` |
| `0xC0BC48` | `CPopCycle::m_NumGangs_Peds` (float) | Stored at `0x610B21`, between the dealers' and the cops' numbers |
| `CPed + 0x71A` | `m_nWeaponAccuracy` (byte) | `CPed::CPed` writes 60 at `0x5E83EB` |
| `CPed + 0x4E0` | `m_acquaintance` (5 × u32: respect, like, ignore, dislike, hate) | `CPed::CPed` at `0x5E835A`; bit `1 << pedType` per `0x608830` |
| `0xC0BBE8` | `CPedType::ms_apPedTypes` (pointer, 0x14 bytes per type) | `CPedType::SetPedTypeAsAcquaintance` at `0x608E20` |

No address is marked `TODO(verify)`.

## Fixes to inherited behaviour

**A revived ped's copy (test build 2).** A copy that died stayed dead for good in the other games: `NetPed::deadApplied`
was never taken back. When a paramedic revived the ped in its owner's game, its copy elsewhere received the owner's
health, place and animations as a dead ped: it slid about lying down, took none of the local player's hits, and once
the replayed animations ended it had none left. `CPed::PlayFootSteps` (`0x5E57F0`) reads the first animation of a
ped's clump unchecked and crashed at `0x5E5815` reading address `0x2C`.

- `Peds::ApplyMirror` now takes a dead copy whose owner's states say alive for half a second as revived: the body is
  deleted and a new copy is made, as for a ped that comes into view. A body the game had already removed is made again
  the same way.
- The call of `CPed::PlayFootSteps` in `CPed::ProcessControl` (`0x5E92C8`, its only caller) goes through a check: a ped
  whose clump has no animation makes no steps. `coop.log` says so the first five times.

Found from a tester's `crash.log`; not reproduced by the author. Upstream has the same code.

**Picked-up cash shared as a reward (test build 3).** See [Payouts](#payouts): the reward is now what the script pays,
not what the wallet gained. Found in a tester's `coop.log`: `Mission reward: $21`, `$141` and `$58` after `drugs3`,
`drugs1` and `music3`, three missions that pay no money.

## Release files

Upstream ships one archive of about 515 MB. `python tools/build.py dist` in the fork writes three files, and a tagged
release carries all three:

| File | Contents |
| --- | --- |
| `dist/SA-COOP.zip` | The mod: `coop.asi`, `coop\coop-server.exe`, `coop\server.ini`, `coop\balance_exclude.ini`, the ASI loader, the animation packs, `install.bat`, licenses. About 6 MB |
| `dist/SA-COOP-extras.zip` | `coop\skinpacks` and `coop\voices` from `package/`, about 505 MB zipped. The mod runs without them: the F4 window then lists only the game's own people, and a player in a skin speaks with CJ's voice |
| `dist/coop.pdb` | The client's symbols for reading a `crash.log` of that build. Not installed |

Both archives unpack over the game folder. The folders that go to the extras are listed in `EXTRAS` of `tools/build.py`.

`coop\server.ini` and `coop\balance_exclude.ini` are in the package so that the settings can be edited before the
server's first start. The build makes them by running `coop-server.exe --write-config <path>`, which writes the files
exactly as a first start does, so the package and the server never hold two versions of the text. Unpacking the
archive over an earlier install replaces both files; skip them to keep edited ones.

## Known issues and limits

- Nothing in the client could be run in the game by the author of these changes. Every client-side item needs the
  in-game checks listed in the release notes.
- Enemy health scaling does not show as a number anywhere: a hostile person simply takes more hits.
- A person counts as a story mission's by ownership in a partner's game, so the host's street gang members near a
  mission use the mission's values there.
- `scaled` mode applies to a lone player on a server too.
- With `split`, amounts are rounded down per player, so up to `n − 1` dollars of a reward vanish.
