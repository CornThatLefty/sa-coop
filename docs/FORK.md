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
- [Player options in the F2 window](#player-options-in-the-f2-window)
- [Server settings window (F8)](#server-settings-window-f8)
- [Host of a standing server plays its world](#host-of-a-standing-server-plays-its-world)
- [Ownership of people and cars](#ownership-of-people-and-cars)
- [Wider streets](#wider-streets)
- [Server additions](#server-additions)
- [Dedicated mode](#dedicated-mode)
- [Linux server](#linux-server)
- [Language of the text outside the game](#language-of-the-text-outside-the-game)
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

`/balance` in the chat lists the values in effect: player counts, enemy multipliers, the streets' range and numbers,
price multipliers, payout mode.

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
| `sync_driver_leads` | 1 | 0 | 0 or 1 | In a car two players share, the driver's game makes the people and traffic. See [Ownership](#ownership-of-people-and-cars) |
| `sync_instant_hits` | 1 | 0 | 0 or 1 | A player's hit on another game's person shows in its own game at once |
| `spawn_scale` | 1.0 | 1.0 | 1–2 | How many times farther out people and traffic are made and removed. See [Wider streets](#wider-streets) |
| `ped_density` | 1.0 | 1.0 | 0–4 | Number of people on the streets |
| `car_density` | 1.0 | 1.0 | 0–4 | Number of cars in traffic |
| `sync_handover` | 1 | 1 | 0 or 1 | A passer-by or car far from its game's player goes to the game of the player nearer it. See [Ownership](#ownership-of-people-and-cars) |

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

## Player options in the F2 window

The F2 window (the player list) holds each player's own options under its rows. They act at once, are kept in
`[game]` of `coop\coop.ini`, and are not sent to anyone.

| Option | Key | Effect |
| --- | --- | --- |
| `Name tags over players` | `name_tags` | The names, health and armour bars over the other players. Also `/names`, `/names on`, `/names off` in the chat |
| `Chat on screen` | `chat` | The chat's lines. Off, the chat is drawn only while it is typed into (T), so notices of the mod and the server are hidden with it |

Test build 4 had the name tags' checkbox in the F9 window; test build 6 moved it here. Source:
`Players::SetNameTags`, `Players::SetChatShown` in `src/client/entities/player`, the window in
`src/client/pages/players-menu`.

## Server settings window (F8)

F8 in the game opens the server's settings: the `[server]` and `[balance]` sections of its `server.ini` on four tabs
(Server, Combat, Economy, Streets). The host of a game started from the F9 window and a server's admins change them there;
other players see the values without the passwords and the admin list.

A change is in effect at once, is written to `server.ini`, and is told to every player in the chat
(`Server setting: friendly_fire = 1 (Alice).`).

| Kind | Control | Settings |
| --- | --- | --- |
| On or off | Checkbox | `friendly_fire`, `partner_teleport`, `shared_life`, `partner_cheats` |
| Number | Arrows, one step a click | `max_players`, `snapshot_minutes`, `backup_minutes`, every `[balance]` multiplier and `mission_scale_radius_m` |
| Choice | Arrows | `payout_mode` |
| Text | Text box, Enter applies | `name`, `host`, `motd`, `admins` |
| Password | Text box, Enter applies; shown as set or none, never as text | `password`, `admin_password` |
| From the server's next start | Checkbox, marked so | `dedicated`, `internet` |

Not in the window: `port`, `bind` and `turn` (they cannot change under a running server, and a hosting panel sets the
port), and `balance_exclude.ini` (edited as a file, `/reloadbalance` reads it again).

`max_players` can be raised while the server runs: the server opens its connection slots for the game's maximum, not
for the value it started with. A game hosted from the F9 window starts its server with that window's choices, so the
host's game writes the four rules back to `coop.ini` when they change here.

**Wire** (protocol 65): `C_SETTINGS_GET`, `C_SETTINGS_SET` (section, key, value), `S_SETTINGS` (the list with each
setting's kind, limits and flags: the window draws what it is sent, so a setting a later server adds shows under its
key), `S_RULES` (the server's name and the four rules, to everyone when one changes).
`src/common/api/protocol/Settings.h`, `src/server/app/server/Settings.cpp`, `src/client/pages/server-menu`.

## Host of a standing server plays its world

A player who joins a server that stands on its own (a Linux or panel-hosted one, not the server its own game starts)
and gets the host role was told "start a new game or load a save" on every join, and the save it picked replaced the
server's world. Since test build 6 such a host asks the server for its stored world and loads it, as every partner
does. With no world on the server the choice is asked as before.

- Another story on that server: an admin's `/newgame` clears the stored world (kept as backup 1); the host's next
  game becomes the world.
- The host names the game to the server by the id the server's welcome carried, not by the local copy's (which read
  as game 0 and moved every partner to the profiles of game 0).

**Guest host (test build 7).** Such a host is a guest in a world that is everyone's, so its character is its own, by
the rules a partner's already follows:

| | Behaviour |
| --- | --- |
| Character | The host's profile on the server (money, weapons, skills, look, shop purchases) goes over the loaded world. A first visit starts from the world's CJ as it was saved and is the player's own from then on |
| Profile saving | Every 30 s when changed, 3 s after a change of look, and on leaving, as a partner's |
| World while playing | The host's game uploads a snapshot every 5 minutes when it can be saved (on foot, no mission) and after every passed mission |
| World on leaving | Leaving through the F9 window or `/disconnect` uploads a snapshot first, then the connection closes once the server has it (4 s at most). In a vehicle or on a mission the game cannot be saved: the server keeps the last snapshot |
| Game closed without leaving | The server keeps the last snapshot: up to 5 minutes of world changes are lost. The profile is at most 30 s old |
| Wanted level | Cleared on loading: the stars in the save are the last host's |
| Next host | The next player on an empty server gets the host role, loads that world and plays it as its own character |

Limits, all part of the anyone-can-host work still to do:

- A host role that moves while others are in the game (the host leaves and a partner is promoted) is handled as
  upstream handles it. No module reacts to `OnHostChanged`, and the promoted partner's game does not take over the
  story. Sequential play (one leaves, the next joins) is what this build covers.
- A host that brings its own save or starts a new game on an empty server is an ordinary host: the save's CJ is its
  character, and it gets a profile from its next visit on.
- The world's own CJ data (position, the last host's money in the save) stays in the save. A first-time player
  starts from it.

Source: `OnOnline`, `OnWorldReceived`, `Leave`, `Update` in `src/client/entities/session/Flow.cpp`; `ProfilePlayer` in
`src/client/features/progress/Progress.cpp`.

## Ownership of people and cars

Every game simulates the people and cars it owns and shows the others' as copies. A copy answers a hit one ping
after it lands, because its owner's game decides the harm. Upstream's rules, as read from the code:

- The game that created a person or a car owns it.
- Within 60 m of the host only the host's game makes passers-by, traffic and parked cars (90 m to switch back), unless
  the partner is wanted. A partner near the host sees copies only.
- The player at the wheel owns the car from the first frame. A passenger never does.
- A person changes owner only when its owner gives it away: an off-screen passer-by much nearer the other player,
  people through a door, police to a wanted player. Nothing changes owner on a hit.
- A story mission's people and cars stay the host's.

The fork changes this in steps. Step 1 is test build 8; both parts are switches in `[balance]` of `server.ini` (and
checkboxes on the Combat tab of F8), sent to every game so that all follow one rule.

**`sync_driver_leads`: the driver's game makes the streets.** In a car two players share, the game of the player at
the wheel makes the passers-by, traffic and parked cars, and the other games near it make none. The driver is the
one who meets them: with the host as a passenger everything the driver ran into was the host's copy. Not during the
host's story mission (its streets are its script's) and not while a joint side job leads (its leader's, as before).
People and cars made before the ride keep their owner until they are left behind. Every game in the car needs this
build: a game without it keeps making its own, and the street holds two games' people while the ride lasts.
`SharedCarDriver`, `UpdateSuppression` in `src/client/features/population/Population.cpp`.

**`sync_instant_hits`: a hit on a copy shows at once.** The owner's game still decides health, death, the kill's
credit and the crime. In the hitting player's game the copy now also takes the hit as the game applies it:

| Copy | Shot, blow or blast | Car |
| --- | --- | --- |
| Any, on foot | The game's own reaction to the hit; health put back after it | Knocked down (as before) |
| A passer-by (no script's person) with health under 250 | Also dies when the hit exceeds its health by 3, counting the least damage its owner can make of it | Also dies when the hit exceeds its health by 10 |
| In a vehicle | As before: shows when its owner answers | As before |

A copy killed this way is a guess that its owner's game confirms a ping later. Its body stays where it fell in the
hitter's game (it is not moved to where its owner's lies), its loot falls there, and the kill is credited once, by
the owner's `KILLED_BY_YOU`. If the owner's person lives on after 1.5 s of the guess, the copy is made again. A
script's person (`X_SCRIPT` in the ped state's `extra` byte, set by its owner) is never guessed dead: a mission's
people have proofs and health the copy does not know. `HitCopyHere`, `HookKillPedWithCar`, `HookRegisterKill` in
`src/client/features/damage/Damage.cpp`; `predictedMs` in `src/client/entities/ped/Peds.{h,cpp}`.

Not run in a game by the author: whether a copy plays the game's flinch for a non-lethal shot depends on the
engine's damage response for a person with the copies' empty decision maker, which was not checked.

**`sync_handover`: people and cars pass to the nearer player's game.** A passer-by more than 60 m from its game's
player and 30 m nearer another player goes to that player's game; a car likewise (other player within 140 m, 40 m
nearer). The receiving game makes it its own: a new person or car in the same place, with the game's own behaviour
for its kind. What it was doing is not carried over. On (1) is the behaviour of every earlier build. Off (0): nothing
passes by distance; a person or car stays its game's while another player is next to it or sees it, and goes when
the engine removes it. A street then thins around a player whose partner walked away or entered a building. A Grove
member asked to join and a hooker by a stopped car still pass.

A person busy with a threat never passes (test build 10): fighting, shooting at or fleeing from someone, and a cop
arresting, pursuing or after a criminal (tasks 1000–1002, 909, 911, 927, 1101, 1103, 1105). Earlier builds passed
such a person once it was 100 m from its game's player, where that game may have no ground loaded; with
`spawn_scale` above 1.6 every hand-over is past 100 m, so every fight was cut short and a cop after a player lost
its target. `Peds::QueueGiveAway` in `src/client/entities/ped/Peds.cpp`, `Handover` in `Population.cpp`.

Planned next: a person changing owner in place (the same person and number, only the owner changes) with a claim by
the last player to hit it, then a claim by the nearest player as the weakest rule, with a margin and a hold so that
people do not flip between two players standing together.

## Wider streets

Three `[balance]` keys, on the Streets tab of F8, change how far out the game makes its people and traffic and how
many it makes. A change is in effect at once in every game. At the presets (1.0 each) the game is unchanged.

| Key | Effect |
| --- | --- |
| `spawn_scale` | Multiplies every distance at which the engine makes and removes people, traffic and parked cars, and the distance people and vehicles are drawn to |
| `ped_density` | Multiplies the number of people the engine keeps on the streets |
| `car_density` | Multiplies the number of cars the engine keeps in traffic |

**Distances in the game itself and at `spawn_scale = 2`**

| What | Game | At 2 |
| --- | --- | --- |
| People made (in view) | 42.5–50.5 m | 85–101 m |
| People removed | past 54.5 m | past 109 m |
| People drawn | to about 100 m | to about 200 m |
| Traffic made (in view) | 120–160 m | 240–320 m |
| Traffic removed | past 170 m | past 340 m |
| Parked cars made | within 160 m | within 320 m |
| Vehicles drawn | to 212 m | to 424 m |

The rings behind the camera (people 15–25 m, cars 38 m) are fixed distances in the engine and stay as they are.

**Numbers.** A range of 2 covers four times the ground, so the engine's numbers are multiplied by
`density × spawn_scale²`: a street looks as full as before, out to the new distance. The engine's own limits are 25
people and 12 moving cars. Each game stops at a part of its own pools: 45% of the ped pool for street people, 40% of
the vehicle pool for traffic. The rest is for mission people and the other games' copies.

| Pools | People at range 2, density 1 | Traffic at range 2, density 1 |
| --- | --- | --- |
| The game's own (140 people, 110 vehicles) | 63 | 44 |
| A limit adjuster's 1000 and 1000 | 100 | 48 |

Random traffic stops at 127 cars whatever the pools: the engine's own check of that number (45 in the game) holds one
signed byte.

`coop.log` has `Pools: N people, N vehicles, N objects` at every game start and a `Streets:` line whenever the range
or the numbers change. The mod reads the pool sizes as they are and changes none: Open Limit Adjuster or fastman92
Limit Adjuster gives the bigger pools.

A script's own density (`SET_PED_DENSITY_MULTIPLIER`, `SET_CAR_DENSITY_MULTIPLIER`) still applies on top: a mission
that empties a street still empties it.

**The mod's own distances** that follow from the engine's are multiplied by `spawn_scale` in every game alike:

| Distance | Game's range | Where |
| --- | --- | --- |
| A person is announced to another player within / dropped past | 120 / 150 m (170 / 220 m in a vehicle) | `SyncIn`, `SyncOut`, `FootIn`, `FootOut` in `Peds.cpp` |
| A vehicle is announced within / dropped past | 170 / 220 m | `TrafficIn`, `TrafficOut` in `Vehicles.cpp` |
| Two players this close share one game's people and traffic | 60 m (90 m to part) | `kShareRadius`, `kShareRadiusOff` in `Population.cpp` |
| A person goes to the nearer player's game | past 60 m, 30 m nearer the other | `HookManagePed` |
| A car goes to the nearer player's game | other player within 140 m, 40 m nearer | `HookPossiblyRemoveVehicle` |
| A spot in another player's view makes no new person or car | 42.5 m, 90 m | `SeenByOther` |
| A leaving game's people and cars go to a player this near | 250 m | `WeInherit` in `Players.cpp` |

Not multiplied: the 100 m within which a game keeps a person or car because it has ground under it. The game loads
collision about 120 m around its player whatever the range.

`kShareRadius` was to stay fixed in the implementation spec. It is scaled: at range 2 a game makes people up to
101 m out, and two games 70 m apart would each fill the same street.

**Limits**

- A collision file is loaded while the player is inside its box widened by 120 m, so ground can end 120 m from a
  player. What a person does past it was not checked in a game. Traffic drives on its road nodes and is not affected.
- The engine has 3000 nodes for noting who points at an entity, and with none free a deleted entity's pointers stay
  (see [Fixes](#fixes-to-inherited-behaviour), test build 10). The mod adds 27000. `coop.log` has
  `Entity references: N free` as the number falls.
- Road nodes are loaded 350 m around a player. That is why `spawn_scale` stops at 2.
- The engine finds a new car's spot by walking road links out from the player: at most 230 m of links (multiplied by
  `spawn_scale` by the mod) and at most 30 nodes (not changed). Where nodes stand close together, a range near 2
  makes fewer cars than the numbers allow.
- More people and cars mean more to draw and to send. A range of 1.5 with the game's own pools is a moderate start.
- Every game needs this build. A game on an older build keeps the game's own range, and people and cars pass between
  the two games at different distances.

Not in this build: the mod raising the pools itself, and each game's pool sizes shown to the other players.

`src/client/features/balance/Streets.cpp`; none of it was run in a game by the author.

## Server additions

These apply to every server, a game hosted from the F9 window included.

| Addition | Use |
| --- | --- |
| `[balance]` in `server.ini`, `balance_exclude.ini` | See above |
| `/reloadbalance` (host, admin or console) | Re-reads both files and sends them to every player |
| `bind =` in `[server]`, `--bind <address>` | Address to listen on; empty listens on every address |
| World backups | The world a new upload replaces is kept: `world\world.b.1` (latest) to `world.b.10`. Since test build 7 not every upload makes one: the first replaced world after a server start, every upload after a passed mission, and otherwise one when the last backup is `backup_minutes` old (preset 30; 0: every replaced world). Snapshots come every few minutes, and a backup each left ten backups less than an hour deep |
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

**Wire additions** (since protocol 64, appended fields that an older fork build skips): `S_WELCOME` carries `dedicated`, the
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

## Linux server

`coop-server` builds and runs on x86-64 Linux. It is the same program as `coop-server.exe`: one `Server.cpp`, one
protocol, the same `server.ini`, console commands and data files.

```
python tools/build.py              # build/release/bin/coop-server (g++ 12 or later; CC and CXX name another compiler)
python tools/build.py testclient   # the headless player of tools/test_server.py
python tools/build.py dist         # dist/coop-server-linux-x86_64.tar.gz
```

The `linux` job of `.github/workflows/build.yml` does all three on Ubuntu 22.04 with g++ 12, runs
`tools/test_server.py` against the Linux binary, and keeps the archive as an artifact. A tagged release carries it as
a fourth file. The C++ library is linked into the program, so it needs only glibc 2.35 or later (Ubuntu 22.04,
Debian 12 and newer).

The archive holds `coop-server`, `server.ini` with every key at its preset (`deploy/linux/server.ini` plus the
`[balance]` section the server writes itself), `balance_exclude.ini`, the systemd unit `sa-coop.service` and
`README.md` with the install steps, the port to open (7788/UDP) and the data folder's layout, and
`egg-sa-coop-server.json`, an egg for a Pelican panel that installs a release from GitHub. Source: `deploy/linux`.
The server logs `Server ready on UDP port N` once it listens: the line the egg waits for.

**How the code is shared.** Each Windows-only piece has a Linux half behind `#ifdef _WIN32` in the same file:

| Piece | File | Windows | Linux |
| --- | --- | --- | --- |
| UTF-8 and wide text | `common/lib/util/Util.cpp` | `MultiByteToWideChar` | A UTF-8 codec; `wchar_t` holds a whole code point |
| Clock | same | `QueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` |
| Files | same | `CreateFileW`, `MoveFileExW`, `FlushFileBuffers` | `open`, `rename`, `fsync` |
| Lowercase | same | `CharLowerBuffW` | Latin, Latin-1 and Cyrillic by table |
| Log file and time | `common/lib/log/Log.cpp` | `_wfopen`, `GetLocalTime`, UTF-8 BOM | `fopen`, `localtime_r`, no BOM |
| Entry point | `server/app/entry/main.cpp` | `wmain`, console control handler, `ReadConsoleW`, `LOCALAPPDATA` fallback | `main`, `SIGINT` and `SIGTERM`, lines of stdin, exit with a message when the data folder cannot be written |
| Sockets outside ENet | `common/api/nat-traversal/NatTraversal.cpp` | Winsock | BSD sockets |
| ENet | `third_party/enet` | `win32.c` | `unix.c` |

New in `Util.h` for both systems: `RemoveFile`, `RenameFile`, `CopyFileOver`, `SleepMs`, `NativePath`. `Server.cpp`
calls these instead of `DeleteFileW`, `Sleep` and `std::filesystem`.

**Paths (deviation from the spec).** The spec names `std::filesystem::path` for the server's paths. They stay
`std::wstring` joined with backslashes (`world\world.b` after the data folder), and `NativePath` turns the
backslashes into slashes at the system calls on Linux. `Server.cpp`'s path code is then one text for both systems
and the Windows build's behaviour does not change. A file name cannot hold a backslash of its own: nicknames are
sanitized to letters, digits and `_-.[]`.

**Left out of the Linux build:** UPnP, the signalling channel, NAT hole punching and the TURN relay (WinHTTP and
BCrypt code). They get a home computer through its router. `turn =` is ignored with a warning. **Kept:** STUN through
the game socket and the invite code, written to `server-data/invite.txt`.

**Stopping.** `quit` typed into the console, `SIGINT` or `SIGTERM` stop the server in order: players are told, the
last profiles are written. CI checks it in the image a Pelican or Pterodactyl panel runs servers in
(`ghcr.io/pelican-eggs/yolks:debian`), with the console left open after `quit` as a panel leaves it. Before test
build 8 the server logged its stop there and never ended: the process's exit waited for the thread reading the
console (glibc 2.41 takes every stream's lock at exit). The console is read with `read()` now, and the process ends
with `_exit` once the server has stopped.

**State.** Compiled and tested only in CI: the 22 server tests pass against the Linux binary there. No game has
connected to it, and the egg has not been imported into a panel. `dedicated = 1` still waits for the client side (see [Dedicated mode](#dedicated-mode)); with
`dedicated = 0` the first player in the game is the host, as on any server started outside the F9 window.

## Language of the text outside the game

From test build 11 everything read outside the game's own windows is in English: the server's console and
`server.log`, every line of `coop.log`, the comments the programs write into a new `server.ini` and `coop.ini`,
`install.bat`, the archive's guide (`package/Instructions.txt`, in place of `Инструкция.txt`) and the issue template.
The code's comments are in English as well.

Unchanged: the interface in the game (`TR("ru", "en")` pairs, chosen by `language` in `coop.ini`), the texts the
server sends in both languages, and `docs/ARCHITECTURE.md`.

An ini file kept from an earlier build keeps its Russian comments: the comments are written only into a new file.

`tools/test_server.py` waits for the English log lines (`started on port`, `updated the world save`, `Server stopped`).

## Tests without the game

```
python tools/build.py server testclient
python tools/test_server.py
```

The same two commands work on Linux, and CI runs them on both systems.

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
| `0x52B730` | `CCamera::Process()`, thiscall | Starts `sub esp, 0xA0`; four callers; stores `[esi+0xF4]` at `0x52C9F1` |
| `CCamera + 0xF4` (`0xB6F11C`) | `m_fGenerationDistMultiplier` (float) | Set to 70 / FOV at `0x52C9D9`–`0x52C9F1`; 16 readers: `PossiblyRemoveVehicle` `0x4250EA`, `GenerateOneRandomCar` `0x430407` `0x431A06` `0x431A1E` `0x431A76`, `ManagePed` `0x6120F9` `0x612122`, `CPopulation::Update` `0x616778`…, car generators `0x6F3004` `0x6F30B7`, `SetRenderWareCamera` `0x732906` `0x732920` `0x73293A` |
| `0x7328C0` | `CVisibilityPlugins::SetRenderWareCamera(RwCamera*)`, cdecl | Writes `ms_pedLodDist` at `0x732968`; five callers |
| `0xC88034` | `CVisibilityPlugins::ms_pedLodDist` (float, squared) | Two references: the write above and `RenderPedCB`'s compare at `0x7335EF` |
| `0x8D2538` | `CPopulation::MaxNumberOfPedsInUse` (u32, 25) | Read by `AddToPopulation` at `0x6148D1`; written 25 at `0x56D166` and `0x610E78` only |
| `0x8A5B24` | `CCarCtrl::MaxNumberOfCarsInUse` (u32, 12) | Read by `GenerateOneRandomCar` at `0x43011F`; written 12 at `0x56D170` only |
| `0xC0BC40`, `0xC0BC44`, `0xC0E978` | `CPopCycle::m_NumOther_Peds`, `m_NumCops_Peds`, `m_NumDealers_Peds` (float) | Summed with `m_NumGangs_Peds` by `AddToPopulation` at `0x6148E0`–`0x6148F2` |
| `0xC0BC30`–`0xC0BC3C` | `CPopCycle::m_NumOther_Cars`, `m_NumCops_Cars`, `m_NumGangs_Cars`, `m_NumDealers_Cars` (float) | Summed by `GenerateOneRandomCar` at `0x43013D`–`0x43014F` |
| `0x434224` | The 45 of `cmp eax, 45` in `CCarCtrl::GenerateRandomCars` (random cars at most) | Bytes `83 F8 2D` at `0x434222`, after `mov eax, [NumRandomCars]` |
| `0x4244F6` | Operand of `fcomp [0x858F80]` (230.0) in `CCarCtrl::GenerateCarCreationCoors2`: the road walk's length | Bytes `D8 1D 80 8F 85 00` at `0x4244F4`; the walked length is added at `0x424B26` |
| `0x693C40` | `CTaskSimpleHoldEntity::ProcessPed(CPed*)`, thiscall; `m_pEntityToHold` at `+0x08` | Reads `[esi+8]`, tests its type for 4 (object) and calls its `SetIsStatic` at `0x693D52`–`0x693D72`: the crash's return address `0x693D75` |
| `0x863C40` | `CPlaceable`'s vtable (one function, `0x54F5D0`) | The dword at `+0x10` is the text `PedA` (`0x41646550`); the crash stopped 13 bytes past that address |
| `0x5719B0` | `CReferences::Init()`, cdecl | Sets `pEmptyList` (`0xBA1768`) to `aRefs` (`0xB9B9A8`) and links 3000 nodes of 8 bytes; the array's only two references are in it |
| `0xBA1768` | `CReferences::pEmptyList` | 12 references, all in `Init`, `RegisterReference` and the lists' clean-up (`0x571A26`–`0x571C9B`) |

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

**A person holding a destroyed object (test build 10).** A player's game crashed at `0x4164655D` (test build 9,
17.11). The return addresses give `CTaskSimpleHoldEntity::ProcessPed` (`0x693D72`): a passer-by's hold task called
`SetIsStatic` on what it held, and that was an object already destroyed, with the table its destructors leave
(`CPlaceable`'s, one function). The call read the text behind that table as an address (`0x41646550`) and the game
stopped 13 bytes on. Three changes:

- The hold task's step looks at what it holds first: a destroyed object is let go and the task ends as it does with
  nothing held. `coop.log` gets `A ped (model N) held a destroyed object`.
- `IsObjectValid` no longer counts a destroyed object left in its pool slot as an object, so the mod hangs none on a
  person's copy.
- The engine notes who points at an entity in 3000 list nodes and clears those pointers when the entity is deleted.
  With no node free it notes nothing, and the pointer stays. The game's own pools stay within 3000; bigger pools with
  the wider streets of test build 9 hold several times as many people and cars. The mod adds 27000 nodes to the
  free list after `CReferences::Init`.

Which of the two left the pointer (a destroyed object kept in its slot, or the reference list run out) is not known:
only the crash log was at hand. `HookHoldStep`, `LinkExtraRefs` in `src/client/app/patches/Patches.cpp`,
`CheckEntityRefs` in `Diag.cpp`.

## Release files

Upstream ships one archive of about 515 MB. `python tools/build.py dist` in the fork writes three files on Windows.
A tagged release carries all three, and the Linux server's archive (`coop-server-linux-x86_64.tar.gz`,
see [Linux server](#linux-server)) as a fourth:

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
- `spawn_scale` above about 1.5: people past the loaded ground stand still until a player comes nearer, and traffic
  thins where road nodes stand close. See [Wider streets](#wider-streets).
- A person counts as a story mission's by ownership in a partner's game, so the host's street gang members near a
  mission use the mission's values there.
- `scaled` mode applies to a lone player on a server too.
- With `split`, amounts are rounded down per player, so up to `n − 1` dollars of a reward vanish.
