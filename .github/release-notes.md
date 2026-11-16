Fork of SA:COOP for GTA San Andreas 1.0 US. Test build 1: co-op balance (combat scaling, shop prices, split payouts).

Every player needs this build: it does not connect to upstream 0.3.4 or to earlier fork builds (protocol 64).

## Files

| File | Size | Contents |
| --- | --- | --- |
| `SA-COOP.zip` | 6 MB | The mod: `coop.asi`, the server with its `server.ini`, the ASI loader, the animation packs. Every player needs it |
| `SA-COOP-extras.zip` | 505 MB | Optional: skin packs for the F4 window and voices for a player in a skin. Unchanged from upstream 0.3.4 |
| `coop.pdb` | 20 MB | Not for playing. Symbols for reading a `crash.log` of this build |

Either every player installs the extras or none: a pack's skin is not shown to a player without the pack.

## Install

1. Unpack `SA-COOP.zip` over the game folder, replacing files. A first install also runs `install.bat` from the game
   folder once.
2. `coop\server.ini` and `coop\balance_exclude.ini` in the zip hold every setting at its preset, the `[balance]`
   section included, and can be edited before the first game is hosted. They replace the files of an earlier install:
   to keep an edited `coop\server.ini`, skip those two files when unpacking. `coop\coop.ini` is not in the zip.
3. Optional: unpack `SA-COOP-extras.zip` over the game folder the same way.
4. The F9 window shows `v0.3.4-fork.1` in its corner when the build is in place.
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
- [ ] The host's wallet after the mission: its money before the mission plus its part, not the full reward.
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

Dedicated Linux server, host migration, 10 players, contests for more than two, spawn distance settings, train boarding
in missions, mission reinforcements.

## Engine addresses

Every new address was checked in the 1.0 US executable; none is marked `TODO(verify)`. The list is in `docs/FORK.md`.
