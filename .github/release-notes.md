Fork of SA:COOP for GTA San Andreas 1.0 US. Test build 3: co-op balance (combat scaling, shop prices, split payouts),
a crash fix, and picked-up cash kept out of shared rewards.

It does not connect to upstream 0.3.4 (protocol 64). It plays with test builds 1 and 2, but each fix works only in the
game of a player who has this build, so every player should install it.

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

Either every player installs the extras or none: a pack's skin is not shown to a player without the pack.

## Install

1. Unpack `SA-COOP.zip` over the game folder, replacing files. A first install also runs `install.bat` from the game
   folder once.
2. `coop\server.ini` and `coop\balance_exclude.ini` in the zip hold every setting at its preset, the `[balance]`
   section included, and can be edited before the first game is hosted. They replace the files of an earlier install:
   to keep an edited `coop\server.ini`, skip those two files when unpacking. `coop\coop.ini` is not in the zip.
3. Optional: unpack `SA-COOP-extras.zip` over the game folder the same way.
4. The F9 window shows `v0.3.4-fork.3` in its corner when the build is in place.
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

Dedicated Linux server, host migration, 10 players, contests for more than two, spawn distance settings, train boarding
in missions, mission reinforcements.

## Engine addresses

Every new address was checked in the 1.0 US executable; none is marked `TODO(verify)`. The list is in `docs/FORK.md`.
