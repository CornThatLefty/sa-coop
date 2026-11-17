# SA:COOP dedicated server for Linux

`coop-server` is the same server the game starts on Windows when a player hosts, built for x86-64 Linux. It needs
glibc 2.35 or later (Ubuntu 22.04, Debian 12 and newer) and nothing else.

| File | Purpose |
| --- | --- |
| `coop-server` | The server |
| `server.ini` | Its settings, every key at its preset, `[balance]` included |
| `balance_exclude.ini` | Balance exclusions and per-mission overrides |
| `sa-coop.service` | systemd unit |

## Install

```sh
sudo useradd --system --home /opt/sa-coop --shell /usr/sbin/nologin sacoop
sudo mkdir -p /opt/sa-coop
sudo tar -xzf coop-server-linux-x86_64.tar.gz -C /opt/sa-coop --strip-components=1
sudo chown -R sacoop:sacoop /opt/sa-coop
sudo cp /opt/sa-coop/sa-coop.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now sa-coop
```

The settings are in `/opt/sa-coop/server.ini`; `sudo systemctl restart sa-coop` applies an edit. The `[balance]`
section alone is re-read without a restart by `/reloadbalance` from an admin in the game.

## Port

The server listens on one UDP port, 7788 unless `port` says otherwise. It has to be open in the machine's firewall
and in the hosting provider's rules:

```sh
sudo ufw allow 7788/udp
```

Players connect from the F9 window of the game with the server's address and port, or with the invite code the server
writes to `server-data/invite.txt` once it knows its public address.

## Data

Everything the server keeps is in `/opt/sa-coop/server-data`:

| Path | Contents |
| --- | --- |
| `server.log` | The log; the previous run's is `server.old.log` |
| `world/world.b` | The shared world save |
| `world/world.b.1` to `world.b.10` | The worlds it replaced, newest first |
| `world/game.id` | The id of the game the world belongs to |
| `profiles/` | Each player's profile |
| `invite.txt` | The invite code |

A backup is a copy of that folder. `journalctl -u sa-coop -f` follows the log as well.

## Without systemd

```sh
./coop-server --config server.ini --data server-data
```

Typed into its terminal: `players`, `kick <name or number> [reason]`, `say <text>`, `reloadbalance`, `quit`.
Ctrl+C stops it as `quit` does.

## Not in the Linux build

UPnP, NAT hole punching and the TURN relay: they get a home computer through its router, and a server with an address
of its own has no use for them. `turn =` in `server.ini` is ignored with a warning in the log.
