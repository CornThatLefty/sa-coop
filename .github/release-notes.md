Fork of SA:COOP for GTA San Andreas 1.0 US. Test build 10: a crash of test build 9 guarded, people in a fight keep
their fight, and a switch for people and cars passing between games.

It connects to test builds 6 to 9 (protocol 65) and not to builds 1 to 5 or to upstream 0.3.4. Every player and
the server need this build for the new settings.

## Fixed since test build 9

- **Crash at `4164655D`.** A passer-by's task for a held thing (a phone, a bag, a tray) used an object that was
  already destroyed. The task now lets go of such an object. The game's list for clearing pointers to deleted people,
  cars and objects holds 3000 entries and runs out with big pools and wider streets; the mod adds 27000.
  To check: a long session with the range raised. `coop.log` has `Entity references: N free`, and
  `A ped (model N) held a destroyed object` each time the guard acts. Both lines are worth a report.
- **A person in a fight keeps its fight.** A passer-by fighting, shooting or fleeing, and a cop arresting or
  pursuing, no longer passes to the other player's game. Before, it passed once 100 m from its own game's player and
  became a calm stroller; with the range above 1.6 that was every hand-over.
- **Switch: `People and cars pass to the nearer player's game`** (`sync_handover`, F8, Combat tab). Off, nothing
  passes by distance: a person or car stays with its game while another player is next to it or sees it. A street
  then thins around a player whose partner walked away or entered a building.

Not run in a game. Which of two causes left the crash's pointer is not known from the crash log alone.

## New in test build 9

- **Wider streets.** Three settings on the new Streets tab of F8 (`[balance]` of `server.ini`), in effect at once in
  every game:
  - `People and traffic range` (`spawn_scale`, 1 to 2): how many times farther out the game makes and removes
    people, traffic and parked cars. People and vehicles are drawn that much farther too.
  - `People density` (`ped_density`) and `Traffic density` (`car_density`), 0 to 4.

  The numbers grow with the range, so a street stays as full as before out to the new distance. Each game stops at
  a part of its own pools: 63 street people and 44 cars with the game's own pools, more with Open Limit Adjuster or
  fastman92 Limit Adjuster. At 1.0 each the game is unchanged.
  To check: the host sets the range to 1.5 in F8. People and cars show farther down a long street within a few
  seconds, in every player's game. `/balance` prints `Streets: range x1.5, people x2.25, traffic x2.25` (lower
  numbers where a pool is small). `coop.log` has `Pools:` and `Streets:` lines.
  To watch for at a range near 2: people standing still far away until a player comes nearer, and thin traffic in
  dense city blocks. Both come from limits of the game (`docs/FORK.md`, "Wider streets").

Not run in a game. Not in this build: the mod raising the pools itself, and each game's pool sizes shown to the
other players.

## New in test build 8

- **The driver's game makes the streets.** In a car two players share, the passers-by and traffic are made by the
  game of the player at the wheel, so what the driver runs into is its own and answers at once. Not during the
  host's story mission. Switch: `sync_driver_leads` (F8, Combat tab).
  To check: a partner drives with the host as a passenger for a block or two, then runs people over: they fall and
  die with no delay in the partner's game. `coop.log` of both says `A car shared with another player`.
- **Hits on other games' people show at once.** A shot, a blow or a car hit on a person the other game owns is
  applied in the hitting player's game right away. A passer-by also dies there at once when the hit kills it
  clearly; a mission's people still die when their owner's game says so. Switch: `sync_instant_hits`.
  To check: a partner near the host shoots a passer-by the host's game made: it reacts and dies with no delay, its
  money falls in the partner's game, and the partner's kill count rises by one. A wounded passer-by that survives
  in the host's game after dying in the partner's stands up again there after about two seconds: worth a report
  with both `coop.log` files.

- **Linux server stops when asked.** On a Pelican panel the server logged its stop and never ended, and had to be
  killed. It ends within a second of the panel's Stop now (the fix is in the server: Reinstall the panel's server
  with this release).

The first two were not run in a game. Either switch set to 0 in F8 gives the earlier behaviour.

## New in test build 7

- **The host's own character on a standing server.** A player who joins an empty Linux or panel server gets the
  host role, loads the server's world, and plays it with its own money, weapons, skills and look, saved on the
  server as a partner's are. A first visit starts from the world's CJ as saved.
  To check: player A joins alone, earns money, leaves with the F9 window's Leave. Player B joins alone: the world
  is as A left it (missions, properties), and B has B's own money and weapons. A joins again later: A's money and
  weapons are back.
- **The world goes back to the server.** The host's game uploads the world every 5 minutes when it can be saved
  (on foot, no mission), after every passed mission, and on leaving through F9 or `/disconnect`. A game closed
  without leaving loses up to 5 minutes of world changes; the character is at most 30 seconds old.
- **Backups.** `backup_minutes` (preset 30, also in F8): a replaced world becomes a backup when the last backup is
  that old, and at every passed mission, so ten backups reach back hours and not minutes.

Not in this build: the host role moving while other players are in the game. When the host leaves with partners
still playing, the game behaves as before.

## New in test build 6

- **Server settings window, F8.** The server's settings on three tabs (Server, Combat, Economy): rules, player
  count, passwords, admins, every balance value. The host of a game started from F9 and a server's admins change
  them there; a change is in effect at once, is written to `server.ini` and is told in the chat. Other players see
  the values.
  To check: the host turns `Friendly fire` on in F8: every player gets the chat line and can be hurt at once; a
  partner's F8 shows the values with no controls; `Weapon price` changed in F8 shows in `/balance` for everyone.
- **Player options in the F2 window.** `Name tags over players` (moved here from F9) and `Chat on screen`. Each
  player's own, kept in `coop\coop.ini`. With the chat off its lines are hidden until T is pressed.
- **Host of a standing server.** A player who joins a Linux or panel server and gets the host role loads the world
  that server keeps, instead of being asked for a save. With no world there the choice is asked as before. An
  admin's `/newgame` clears the server's world for another story.
  To check: the host leaves and joins again: its game loads the server's world with no question, and partners keep
  their profiles (money, weapons).
- **Linux server.** Logs `Server ready on UDP port N`; `egg-sa-coop-server.json` for a Pelican panel is in the
  archive, and its `Release` variable takes `latest`.

## New since test build 4

- **World backups.** Every world a new save replaces is kept on the host: `coop\server-data\world\world.b.1` (the
  latest) to `world.b.10`. `/restoreworld` from the host lists them, `/restoreworld 2` brings one back for everyone.
  To check: pass two missions, `/restoreworld`, then `/restoreworld 1`: every player reloads into the earlier world.
- **Host leaves.** The host role goes to a player who is in the game with the current world, not to whoever connected
  first, and a player waiting for the world is answered instead of waiting for a host that left.
  To check: with three players, the host quits while one partner is still loading: that partner gets into the game.
- **Profiles after a restart.** The server remembers which game its world belongs to (`world\game.id`), so partners
  get that game's profiles on the first join after a server restart.
  To check: restart the host's game and server, load the same save: partners have their money, weapons and stats.
- **Password cooldown.** Five wrong passwords from an address make it wait 60 seconds, doubling up to an hour.
- **Admins.** `admins =` and `admin_password =` in `[server]` of `coop\server.ini`, `/admin <password>`,
  `/host <player>`. The host of a game started from the F9 window is an admin as before.
- **Server for Linux.** `coop-server-linux-x86_64.tar.gz` in this release: the same server for a Linux machine, with
  its settings, a systemd unit and install steps in its `README.md`. It passes the server's tests on Linux; no game
  has connected to it yet. `dedicated = 1` is for server tests until the game's side of it is built.

## New since test build 3

- **Name tags switch.** The names, health and armour bars over the other players turn off and on in the game: the
  `Name tags over players` checkbox of the F9 window (shown while connected), or `/names`, `/names on`, `/names off`
  in the chat. The setting is each player's own and is kept in `coop\coop.ini` (`name_tags`).
  To check: `/names` hides the tags at once in that player's game alone; the F9 checkbox shows the same state; the
  setting holds after a restart of the game.

## Fixed since test build 2

- **Picked-up cash was shared as a reward.** The reward of a mission or a joint side job was everything the earner's
  wallet gained from its start to its end, so cash picked up from the dead went to every player as a `Mission reward`
  (`$21`, `$141` and `$58` in a test session, after three missions that pay nothing). The reward is now only the money
  the mission's or the job's own script pays. Picked-up cash stays with the player who picked it up.
  To check: the host picks up cash during a mission that pays no money (`drugs3`, `drugs1`, `music3` by the
  `Mission started` line of `coop.log`): no `Mission reward` line, no wallet but the host's changes. A mission that
  pays (`sweet1` and `sweet3` pay $200) still gives one line with the script's amount, split.

## Fixed since test build 1

- **Crash after a paramedic revives a ped.** In another player's game the revived ped's copy stayed dead: it slid about
  lying down, took no hits, and the game crashed soon after (`gta_sa.exe` at `005E5815`). The copy now comes back to
  life with its owner's ped, and a ped left with no animation no longer crashes the game.
  To check: kill a passer-by near another player, wait for the ambulance, attack the revived ped in both games.

## Files

| File | Size | Contents |
| --- | --- | --- |
| `SA-COOP.zip` | 6 MB | The mod: `coop.asi`, the server with its `server.ini`, the ASI loader, the animation packs. Every player needs it |
| `SA-COOP-extras.zip` | 505 MB | Optional: skin packs for the F4 window and voices for a player in a skin. Unchanged from upstream 0.3.4 |
| `coop.pdb` | 20 MB | Not for playing. Symbols for reading a `crash.log` of this build |
| `coop-server-linux-x86_64.tar.gz` | 2 MB | Not for playing. The server for a Linux machine |

Either every player installs the extras or none: a pack's skin is not shown to a player without the pack.

## Install

1. Unpack `SA-COOP.zip` over the game folder, replacing files. A first install also runs `install.bat` from the game
   folder once.
2. `coop\server.ini` and `coop\balance_exclude.ini` in the zip hold every setting at its preset, the `[balance]`
   section included, and can be edited before the first game is hosted. They replace the files of an earlier install:
   to keep an edited `coop\server.ini`, skip those two files when unpacking. `coop\coop.ini` is not in the zip.
3. Optional: unpack `SA-COOP-extras.zip` over the game folder the same way.
4. The F9 window shows `v0.3.4-fork.10` in its corner when the build is in place.
5. The host starts a game from F9 as before. A `coop\server.ini` kept from an earlier install gets its `[balance]`
   section from the server on the first start.

## Settings

All in `[balance]` of the host's `coop\server.ini`; `docs/FORK.md` lists every key. After an edit, the host types
`/reloadbalance` in the chat and every player gets the new values without a restart. `/balance` lists the values in
effect for any player.

To compare against the unmodified game, set every `*_per_player` key to 0, every `price_*` key to 1 and
`payout_mode = full`, then `/reloadbalance`.

## What to test

Settings

- [ ] `/balance` prints the same values for the host and for a partner.
- [ ] After an edit of `coop\server.ini` and `/reloadbalance`, `/balance` shows the new values for every player.

Payouts (`payout_mode = split`)

- [ ] Story mission passed with 2 or more players: every player gets one chat line, `Mission reward: $N (split K ways).`, and
      every wallet (F2) rises by the same amount.
- [ ] The host's wallet after the mission: its money before the mission plus its part, not the full reward. Cash the
      host picked up during the mission is on top of that and reaches no other player.
- [ ] Cash a partner picks up during a mission or a job changes that partner's wallet alone.
- [ ] Joint taxi job (or vigilante, paramedic, firefighter): each fare is split, and the job's owner sees `Job pay: $A of $B (split 2 ways).`
- [ ] `payout_mode = scaled`, `payout_share = 0.5`: every player gets half of a mission's reward.
- [ ] `payout_mode = full`: every player gets the whole reward, as before.
- [ ] A gift of money through F2 arrives whole.

Shop prices

- [ ] Ammu-Nation: a weapon the player does not have costs 2x; the same weapon bought again costs the `price_ammo` rate;
      armour costs 3x.
- [ ] Clothes shop: prices 1.5x. The price in the menu and the money taken match.
- [ ] Mod shop parts: 1.5x.
- [ ] Food, barber and tattoo prices unchanged at their presets of 1.0.
- [ ] Clothes bought before the update are still worn after a world reload (pass a mission as a partner).

Combat (2 or more players in the game)

- [ ] A story mission's start prints `Co-op scaling: K players, enemy health xA, damage xB, accuracy xC.` for every player.
- [ ] Mission enemies take more body shots than with all `*_per_player` keys at 0. A headshot still kills.
- [ ] Ballas or Vagos on the street and police take more body shots; bystanders and Grove members do not.
- [ ] Enemy gunfire and melee take more of a player's health than with `damage_per_player = 0`.
- [ ] Allies (Sweet, Ryder, recruited Grove members) are not harder to hurt and do not hit harder.
- [ ] Gang members visibly denser in Ballas and Vagos territory with `gang_density_per_player = 1` and 2 players.
- [ ] A partner far from the host (over 300 m) at a mission's start is not counted in the chat line's player number.
- [ ] After leaving the session, a single-player game on the same save behaves as the unmodified game.

## Report with a bug

`coop\coop.log` of the player who saw it and `coop\server-data\server.log` of the host. Lines starting with `Payout:`,
`Mission reward:`, `Mission scaling` and `Balance` show what the build computed.

## Not in this build

Hosting from a dedicated server in the game (the F9 window still starts its own server), host migration without a
reload, 10 players, contests for more than two, train boarding in missions, mission reinforcements.

## Engine addresses

Every new address was checked in the 1.0 US executable; none is marked `TODO(verify)`. The list is in `docs/FORK.md`.
