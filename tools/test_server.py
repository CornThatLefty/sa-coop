#!/usr/bin/env python3
"""
Server tests without the game: starts coop-server on this computer and joins it with coop-test-client, the headless
player (tools/test-client).

    python tools/build.py server testclient
    python tools/test_server.py            every test
    python tools/test_server.py balance    the tests whose name has "balance" in it
    python tools/test_server.py -v         with every line the players and the server print

Each test gets a server of its own in a fresh folder, listening on 127.0.0.1 only and with internet = 0: nothing leaves
this computer.
"""
import argparse
import os
import queue
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ".exe" if os.name == "nt" else ""
VERBOSE = False


def find_bin(name):
    for cfg in ("release", "debug"):
        p = ROOT / "build" / cfg / "bin" / (name + EXE)
        if p.exists():
            return p
    for p in (ROOT / "build" / "linux" / name, ROOT / "build" / "linux" / "bin" / name):
        if p.exists():
            return p
    sys.exit(f"{name}{EXE} is not built: python tools/build.py server testclient")


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Failure(Exception):
    pass


class Lines:
    """The output of a process as a queue of lines, with a wait for one that matches."""

    def __init__(self, proc, tag):
        self.proc, self.tag = proc, tag
        self.q = queue.Queue()
        self.seen = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for raw in self.proc.stdout:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if VERBOSE:
                print(f"    [{self.tag}] {line}")
            self.q.put(line)

    def expect(self, pattern, timeout=5.0):
        """The first line from now on that matches; lines before it are dropped."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        while True:
            try:
                line = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                raise Failure(f"{self.tag}: no line matching /{pattern}/ in {timeout:.0f} s; last lines: {self.seen[-6:]}")
            self.seen.append(line)
            m = rx.search(line)
            if m:
                return m
            if time.time() > end:
                raise Failure(f"{self.tag}: no line matching /{pattern}/ in {timeout:.0f} s; last lines: {self.seen[-6:]}")

    def refuse(self, pattern, wait=0.7):
        """No line matching arrives within wait."""
        rx = re.compile(pattern)
        end = time.time() + wait
        while time.time() < end:
            try:
                line = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty:
                return
            self.seen.append(line)
            if rx.search(line):
                raise Failure(f"{self.tag}: unexpected line {line!r}")


class Server:
    def __init__(self, ini="", files=None, args=()):
        self.dir = Path(tempfile.mkdtemp(prefix="coop-test-"))
        self.port = free_port()
        self.ini = self.dir / "server.ini"
        self.data = self.dir / "data"
        self.args = list(args)
        self.proc = None
        self.write_ini(ini)
        for name, text in (files or {}).items():
            (self.dir / name).write_text(text, encoding="utf-8")

    def write_ini(self, body):
        # backup_minutes = 0: every replaced world is a backup, as the tests of the backups count them
        self.ini.write_text("[server]\nname = test\ninternet = 0\nbind = 127.0.0.1\nmax_players = 4\nbackup_minutes = 0\n" + body,
                            encoding="utf-8")

    def start(self, stdin=subprocess.DEVNULL):
        cmd = [str(find_bin("coop-server")), "--config", str(self.ini), "--data", str(self.data), "--port", str(self.port)] + self.args
        self.proc = subprocess.Popen(cmd, stdin=stdin, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=self.dir)
        self.wait_log(r"порту|port", 8)
        return self

    def log(self):
        p = self.data / "server.log"
        return p.read_text(encoding="utf-8", errors="replace") if p.exists() else ""

    def wait_log(self, pattern, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            m = re.search(pattern, self.log())
            if m:
                return m
            if self.proc.poll() is not None:
                raise Failure(f"the server exited with {self.proc.returncode}: {self.log()[-600:]}")
            time.sleep(0.05)
        raise Failure(f"server.log has no /{pattern}/ after {timeout:.0f} s: {self.log()[-600:]}")

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None

    def restart(self):
        self.stop()
        return self.start()

    def close(self):
        self.stop()
        if VERBOSE:
            print("    [server.log]\n      " + self.log().replace("\n", "\n      "))
        shutil.rmtree(self.dir, ignore_errors=True)

    def join(self, name, **kw):
        return Player(self, name, **kw)


class Player:
    def __init__(self, server, name, password="", wants_host=False, protocol=None, expect_welcome=True):
        cmd = [str(find_bin("coop-test-client")), "--port", str(server.port), "--name", name]
        if password:
            cmd += ["--password", password]
        if wants_host:
            cmd += ["--wants-host"]
        if protocol is not None:
            cmd += ["--protocol", str(protocol)]
        self.name = name
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.out = Lines(self.proc, name)
        self.id = self.host = None
        if expect_welcome:
            m = self.expect(r"EVT WELCOME ok=1 id=(\d+) host=(\d+) world=(\d)")
            self.id, self.host, self.world = int(m[1]), int(m[2]), int(m[3])

    def send(self, line):
        self.proc.stdin.write((line + "\n").encode("utf-8"))
        self.proc.stdin.flush()

    def expect(self, pattern, timeout=5.0):
        return self.out.expect(pattern, timeout)

    def refuse(self, pattern, wait=0.7):
        return self.out.refuse(pattern, wait)

    def balance(self, timeout=5.0):
        """The next S_BALANCE as a dict of its numbers, with "models", "missions" and "overrides" as lists."""
        m = self.expect(r"EVT BALANCE ok=1 (.*) models=\[(.*?)\] missions=\[(.*?)\] overrides=\[(.*?)\]", timeout)
        b = {k: float(v) for k, v in (kv.split("=") for kv in m[1].split())}
        b["models"] = [int(x) for x in m[2].split()]
        b["missions"] = m[3].split()
        b["overrides"] = m[4].split()
        return b

    def settle(self):
        """Returns once the server has handled what this player sent so far (its answer to /players comes after)."""
        self.send("chat /players")
        self.expect(rf"EVT CHAT from=255 \[\d+\] {self.name}[ ,]")

    def quit(self):
        self._end("quit")

    def drop(self):
        self._end("drop")

    def _end(self, how):
        if self.proc.poll() is None:
            try:
                self.send(how)
                self.proc.wait(3)
            except (OSError, subprocess.TimeoutExpired):
                self.proc.kill()


def check(cond, what):
    if not cond:
        raise Failure(what)


def near(a, b):
    return abs(a - b) < 1e-4


# ------------------------------------------------------------------------------------------------ tests
TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


@test
def balance_presets_are_written_and_sent(make):
    """A server.ini with no [balance] gets the section with its presets, and a player gets them on join."""
    s = make()
    text = s.ini.read_text(encoding="utf-8")
    check("[balance]" in text and "payout_mode = split" in text, "server.ini did not get its [balance] section")
    check((s.dir / "balance_exclude.ini").exists(), "balance_exclude.ini was not written")
    a = s.join("Alice")
    b = a.balance()
    check(near(b["health_per_player"], 0.5) and near(b["damage_per_player"], 0.15) and near(b["accuracy_per_player"], 0.1), f"combat presets: {b}")
    check(near(b["gang_density_per_player"], 0.25) and near(b["mission_health_per_player"], 0.5) and near(b["mission_scale_radius_m"], 300), f"{b}")
    check(near(b["price_weapons"], 2) and near(b["price_ammo"], 2) and near(b["price_armor"], 3) and near(b["price_clothes"], 1.5), f"prices: {b}")
    check(near(b["price_food"], 1) and near(b["price_barber_tattoo"], 1) and near(b["price_carmods"], 1.5), f"prices: {b}")
    check(near(b["payout_mode"], 1) and near(b["payout_share"], 0.5), f"payout: {b}")
    check(b["models"] == [] and b["missions"] == [] and b["overrides"] == [], f"exclusions: {b}")
    a.quit()


@test
def balance_values_from_server_ini(make):
    """Keys of [balance] reach the players; a value out of range is clamped, an unknown key and a bad value are skipped."""
    s = make("[balance]\nhealth_per_player = 0.75\npayout_mode = scaled\npayout_share = 0.4\nhealth_base = 100\n"
             "price_food = abc\nno_such_key = 3\nprice_armor = 1\n")
    b = s.join("Alice").balance()
    check(near(b["health_per_player"], 0.75), f"health_per_player: {b}")
    check(near(b["payout_mode"], 2) and near(b["payout_share"], 0.4), f"payout: {b}")
    check(near(b["health_base"], 20), f"health_base is not clamped to 20: {b}")
    check(near(b["price_food"], 1), f"a bad value changed price_food: {b}")
    check(near(b["price_armor"], 1), f"price_armor: {b}")
    check(near(b["damage_per_player"], 0.15), f"a key the file does not set lost its preset: {b}")
    s.wait_log(r"unknown key \"no_such_key\"")


@test
def balance_exclusions_and_mission_overrides(make):
    """balance_exclude.ini: models, missions and [mission:<name>] sections reach the players."""
    s = make(files={"balance_exclude.ini": "[exclude]\nmodels = 280, 281; 7\nmissions = Sweet1, intro\n\n"
                                           "[mission:RYDER1]\ndamage_per_player = 0\nmission_health_per_player = 0.25\n"
                                           "[mission:cesar1]\nhealth_base = 2\nbogus = 1\n"})
    b = s.join("Alice").balance()
    check(b["models"] == [280, 281, 7], f"models: {b['models']}")
    check(b["missions"] == ["sweet1", "intro"], f"missions: {b['missions']}")
    check(sorted(b["overrides"]) == ["cesar1.health_base=2", "ryder1.damage_per_player=0", "ryder1.mission_health_per_player=0.25"],
          f"overrides: {b['overrides']}")


@test
def balance_reload_is_the_hosts(make):
    """/reloadbalance reads the files again and sends them to everyone; a partner may not use it."""
    s = make("[balance]\nprice_weapons = 2\n")
    a = s.join("Alice")
    check(near(a.balance()["price_weapons"], 2), "the first balance")
    b = s.join("Bob")
    b.balance()
    check(a.id == a.host and b.host == a.id, "Alice is not the host")
    s.write_ini("[balance]\nprice_weapons = 5\npayout_mode = full\n")
    b.send("chat /reloadbalance")
    b.expect(r"EVT CHAT from=255 Only the host")
    a.refuse(r"EVT BALANCE")
    a.send("chat /reloadbalance")
    for p in (a, b):
        got = p.balance()
        check(near(got["price_weapons"], 5) and near(got["payout_mode"], 0), f"{p.name} after the reload: {got}")
    a.expect(r"EVT CHAT from=255 Balance reloaded")
    a.send("chat /balance")  # the game's own command: the server has nothing to say to it
    a.refuse(r"EVT CHAT from=255 There is no such command")


@test
def protocol_mismatch_is_rejected(make):
    """A player of another build is told so."""
    s = make()
    p = s.join("Old", protocol=63, expect_welcome=False)
    p.expect(r"EVT REJECT your version of the mod differs")


# ---- the world, the roles, the profiles
IN_GAME = "status 2"


def make_world(server, name, seed, size=4096):
    """A file to upload as a world: its path and its crc as the players print it."""
    data = bytes((seed * 31 + i * 7 + (i >> 8)) & 0xFF for i in range(size))
    path = server.dir / name
    path.write_bytes(data)
    return path, f"{zlib.crc32(data) & 0xFFFFFFFF:08X}"


def got_world(player, crc, label=None, timeout=5.0):
    m = player.expect(r"EVT WORLD_BEGIN size=\d+ crc=([0-9A-F]{8}) label=(\S*) version=(\d+)", timeout)
    check(m[1] == crc, f"{player.name} got the world {m[1]}, not {crc}")
    if label is not None:
        check(m[2] == label, f"{player.name} got the world labelled {m[2]!r}, not {label!r}")
    player.expect(r"EVT WORLD_END .* ok=1")
    return int(m[3])


DEDICATED = "dedicated = 1\nsnapshot_minutes = 0\n"


@test
def host_role_goes_to_the_first_and_on_when_it_leaves(make):
    s = make()
    a = s.join("Alice")
    b = s.join("Bob")
    check(a.host == a.id and b.host == a.id, "the first player is not the host")
    a.quit()
    b.expect(rf"EVT LEAVE id={a.id} reason=0")
    b.expect(rf"EVT HOST id={b.id}")


@test
def world_goes_from_the_host_to_a_partner(make):
    s = make()
    a = s.join("Alice")
    a.send(IN_GAME)
    path, crc = make_world(s, "a.b", 1)
    b = s.join("Bob")
    b.send("request")
    a.expect(r"EVT WORLD_WANTED")  # the host's game is asked for a fresh one
    a.send(f"upload {path} host_snapshot")
    got_world(b, crc, "host_snapshot")
    check((s.data / "world" / "world.b").read_bytes() == path.read_bytes(), "world.b is not the uploaded world")


@test
def profiles_are_kept_per_game(make):
    s = make()
    a = s.join("Alice")
    b = s.join("Bob")
    b.expect(r"EVT PROFILE size=0 game=00000000")
    a.send("gameid 1234ABCD")
    b.expect(r"EVT PROFILE size=0 game=1234ABCD")
    b.send("profile 00000000 stale")      # sent before the switch reached it: another game's
    b.send("profile 1234ABCD hello")
    s.wait_log(r"Bob от другой игры")
    time.sleep(0.5)                        # the writer's thread
    b.quit()
    b2 = s.join("Bob")
    b2.expect(r"EVT PROFILE size=5 game=1234ABCD text=hello")
    check((s.data / "profiles" / "1234ABCD" / "bob.bin").read_bytes() == b"hello", "the profile's file")


@test
def game_id_survives_a_restart(make):
    """The stored world's game is known from the first join on after a restart, not when a host says it again."""
    s = make()
    a = s.join("Alice")
    a.send(IN_GAME)
    path, crc = make_world(s, "a.b", 2)
    a.send(f"upload {path} host_snapshot")
    a.send("gameid 0BADF00D")
    a.settle()  # the game is named before Bob's profile comes: one for another game is not written (seen on Linux)
    b = s.join("Bob")
    b.send("profile 0BADF00D kept")
    b.settle()
    time.sleep(0.6)
    a.quit(), b.quit()
    s.restart()
    b2 = s.join("Bob", expect_welcome=False)
    b2.expect(r"EVT WELCOME ok=1 .* game=0BADF00D version=1")
    b2.expect(r"EVT PROFILE size=4 game=0BADF00D text=kept")


@test
def waiters_get_the_stored_world_when_the_host_leaves(make):
    s = make()
    a = s.join("Alice")
    a.send(IN_GAME)
    path, crc = make_world(s, "a.b", 3)
    a.send(f"upload {path} host_snapshot")
    s.wait_log(r"обновил сохранение мира")
    b = s.join("Bob")
    b.send("request")
    a.expect(r"EVT WORLD_WANTED")
    a.quit()  # it never answers
    b.expect(rf"EVT HOST id={b.id}")
    got_world(b, crc)


@test
def a_repeated_world_request_is_answered(make):
    """Who stands in the waiters' list and asks again gets an answer again (it was dropped)."""
    s = make()
    a = s.join("Alice")  # the host stays in its menu
    b = s.join("Bob")
    b.send("request")
    b.expect(r"EVT WORLD_NONE")
    b.send("request")
    b.expect(r"EVT WORLD_NONE")
    path, crc = make_world(s, "a.b", 4)
    a.send(IN_GAME)
    a.expect(r"EVT WORLD_WANTED")
    a.send(f"upload {path} host_snapshot")
    got_world(b, crc)
    b.refuse(r"EVT WORLD_BEGIN")  # once: it stood in the list once


@test
def password_cooldown_after_five_wrong_ones(make):
    s = make("password = right\n")
    for i in range(5):
        s.join(f"Guess{i}", password="wrong", expect_welcome=False).expect(r"EVT REJECT wrong password")
    s.join("Guess5", password="wrong", expect_welcome=False).expect(r"EVT REJECT too many wrong passwords.*in (\d+) s")
    s.join("Owner", password="right", expect_welcome=False).expect(r"EVT REJECT too many wrong passwords")
    s.wait_log(r"5 wrong passwords from 127\.0\.0\.1: it waits 60 s")


@test
def right_password_joins(make):
    s = make("password = right\n")
    for i in range(3):
        s.join(f"Guess{i}", password="wrong", expect_welcome=False).expect(r"EVT REJECT wrong password")
    s.join("Owner", password="right")


# ---- dedicated mode
@test
def dedicated_world_is_kept_from_a_game_that_does_not_play_it(make):
    """The world lock: a host that joined from a game of its own does not replace the stored world."""
    s = make(DEDICATED, start=False)
    seed, seed_crc = make_world(s, "seed.b", 10)
    s.args += ["--world", str(seed)]
    s.start()
    a = s.join("Alice", expect_welcome=False)
    a.expect(r"EVT WELCOME ok=1 .* world=1 .* dedicated=1 game=00000000 version=1")
    a.send(IN_GAME)
    own, own_crc = make_world(s, "own.b", 11)
    a.send(f"upload {own} host_snapshot")
    a.expect(r"EVT CHAT from=255 The server kept its world")
    a.send(f"upload {own} progress")  # a mission passed in its own game: its own game still
    a.expect(r"EVT CHAT from=255 The server kept its world")
    b = s.join("Bob")
    b.send("request")
    a.expect(r"EVT WORLD_WANTED")
    a.send("keep")
    check(got_world(b, seed_crc) == 1, "the stored world's version")
    # the host loads the stored world: from now on its game is that world, and its progress replaces it
    a.send("request")
    got_world(a, seed_crc)
    b.send(IN_GAME)
    nxt, nxt_crc = make_world(s, "next.b", 12)
    a.send(f"upload {nxt} progress")
    check(got_world(b, nxt_crc, "progress") == 2, "the new world's version")
    check((s.data / "world" / "world.b.1").read_bytes() == seed.read_bytes(), "the replaced world is not backup 1")


@test
def dedicated_world_is_sent_while_the_host_sits_in_the_menu(make):
    s = make(DEDICATED, start=False)
    seed, seed_crc = make_world(s, "seed.b", 13)
    s.args += ["--world", str(seed)]
    s.start()
    s.join("Alice")  # the host, in its menu
    b = s.join("Bob")
    b.send("request")
    got_world(b, seed_crc)


@test
def dedicated_seed_does_not_replace_a_stored_world(make):
    s = make(DEDICATED, start=False)
    seed, seed_crc = make_world(s, "seed.b", 14)
    s.args += ["--world", str(seed)]
    s.start()
    s.stop()
    other, other_crc = make_world(s, "other.b", 15)
    s.args[-1] = str(other)
    s.start()
    a = s.join("Alice")
    a.send("request")
    got_world(a, seed_crc)
    s.wait_log(r"--world and --new-game are ignored")


@test
def admins_and_acceptworld(make):
    """A named admin with the password lets the host's own game replace the world; nobody else can."""
    s = make(DEDICATED + "admins = Alice\nadmin_password = secret\n", start=False)
    seed, seed_crc = make_world(s, "seed.b", 20)
    s.args += ["--world", str(seed)]
    s.start()
    bob = s.join("Bob")  # the host: no admin by it
    bob.send(IN_GAME)
    alice = s.join("Alice")
    own, own_crc = make_world(s, "own.b", 21)
    bob.send("chat /acceptworld")
    bob.expect(r"EVT CHAT from=255 Only an admin can replace")
    bob.send("chat /admin secret")  # the password without the name
    bob.expect(r"EVT CHAT from=255 Wrong admin password")
    alice.send("chat /acceptworld")  # the name without the password
    alice.expect(r"EVT CHAT from=255 Only an admin can replace")
    alice.send("chat /admin nope")
    alice.expect(r"EVT CHAT from=255 Wrong admin password")
    alice.send("chat /admin secret")
    alice.expect(r"EVT CHAT from=255 Admin rights granted")
    alice.send("chat /acceptworld")
    bob.expect(r"EVT WORLD_WANTED")
    alice.expect(r"EVT CHAT from=255 The next save of Bob's game replaces")
    bob.send("gameid 00C0FFEE")
    bob.send(f"upload {own} host_snapshot")
    s.wait_log(r"версия 2")
    alice.send("request")
    bob.expect(r"EVT WORLD_WANTED")  # the host is in the game: a fresh one is asked for first
    bob.send("keep")
    check(got_world(alice, own_crc) == 2, "the accepted world's version")
    # the world's game came with it, and the next upload of that game needs no admin
    carol = s.join("Carol", expect_welcome=False)
    carol.expect(r"EVT WELCOME ok=1 .* game=00C0FFEE version=2")
    nxt, nxt_crc = make_world(s, "next.b", 22)
    bob.send(f"upload {nxt} progress")
    s.wait_log(r"версия 3")
    # an earlier world comes back
    alice.send("chat /restoreworld")
    alice.expect(r"EVT CHAT from=255 Earlier worlds: 1, 2 ")
    alice.send(IN_GAME)
    alice.send("chat /restoreworld 2")
    check(got_world(alice, seed_crc, "backup_2") == 4, "the restored world's version")
    got_world(bob, seed_crc, "backup_2")
    bob.send("chat /restoreworld 1")
    bob.expect(r"EVT CHAT from=255 Only an admin can restore")


@test
def admin_without_password_is_by_name(make):
    s = make(DEDICATED + "admins = alice, Dave\n")
    bob = s.join("Bob")
    alice = s.join("Alice")
    alice.send("chat /host Alice")
    for p in (bob, alice):
        p.expect(rf"EVT HOST id={alice.id}")
    bob.send("chat /host Bob")
    bob.expect(r"EVT CHAT from=255 Only an admin can pass")
    bob.send(f"chat /kick Alice")
    bob.expect(r"EVT CHAT from=255 Only the host can kick")  # the host's role gives no rights on a dedicated server
    alice.send("chat /kick Bob away")
    bob.expect(r"EVT KICKED away")


@test
def newgame_clears_the_world_and_keeps_a_backup(make):
    s = make(DEDICATED + "admins = Alice\n", start=False)
    seed, seed_crc = make_world(s, "seed.b", 30)
    s.args += ["--world", str(seed)]
    s.start()
    a = s.join("Alice")
    a.send(IN_GAME)
    a.send("newgame")  # its game started a new story by itself: the world stays
    a.expect(r"EVT CHAT from=255 The server keeps the shared world")
    a.send("chat /newgame")
    a.expect(r"EVT CHAT from=255 An admin started a new story")
    check(not (s.data / "world" / "world.b").exists(), "world.b is still there")
    check((s.data / "world" / "world.b.1").read_bytes() == seed.read_bytes(), "the cleared world is not backup 1")
    b = s.join("Bob", expect_welcome=False)
    b.expect(r"EVT WELCOME ok=1 .* world=0 .* version=0")
    fresh, fresh_crc = make_world(s, "fresh.b", 31)
    a.send("gameid 0000BEEF")
    a.send(f"upload {fresh} host_snapshot")  # the first world of an empty server is taken
    b.send("request")
    a.expect(r"EVT WORLD_WANTED")
    a.send("keep")
    got_world(b, fresh_crc)
    s.join("Carol", expect_welcome=False).expect(r"EVT WELCOME ok=1 .* game=0000BEEF")


@test
def dedicated_game_id_is_not_overwritten_by_a_new_host(make):
    """A promoted partner's game knows no game number (0) or another one: the server keeps the world's."""
    s = make(DEDICATED)
    a = s.join("Alice")
    a.send(IN_GAME)
    first, crc = make_world(s, "first.b", 40)
    a.send("gameid AAAA0001")
    a.send(f"upload {first} host_snapshot")
    s.wait_log(r"версия 1")
    b = s.join("Bob")
    b.expect(r"EVT PROFILE size=0 game=AAAA0001")
    a.quit()
    b.expect(rf"EVT HOST id={b.id} game=AAAA0001")
    b.send("gameid 0")
    b.send("gameid BBBB0002")
    b.refuse(r"EVT PROFILE")
    s.join("Carol", expect_welcome=False).expect(r"EVT WELCOME ok=1 .* game=AAAA0001")
    s.restart()
    s.join("Dave", expect_welcome=False).expect(r"EVT WELCOME ok=1 .* game=AAAA0001")


@test
def host_role_goes_to_who_plays_the_newest_world(make):
    s = make(DEDICATED, start=False)
    seed, seed_crc = make_world(s, "seed.b", 50)
    s.args += ["--world", str(seed)]
    s.start()
    a = s.join("Alice")
    c = s.join("Carol")  # here longer than Bob, in the game, but with no world of the server's
    b = s.join("Bob")
    c.send(IN_GAME)
    b.send(IN_GAME)
    b.send("request")
    got_world(b, seed_crc)
    c.settle()
    a.quit()
    for p in (b, c):
        p.expect(rf"EVT HOST id={b.id}")


@test
def new_host_behind_the_world_gets_it(make):
    s = make(DEDICATED, start=False)
    seed, seed_crc = make_world(s, "seed.b", 51)
    s.args += ["--world", str(seed)]
    s.start()
    a = s.join("Alice")
    b = s.join("Bob")
    b.send(IN_GAME)
    b.settle()
    a.quit()
    b.expect(rf"EVT HOST id={b.id}")
    got_world(b, seed_crc)


@test
def dedicated_host_is_asked_for_snapshots(make):
    s = make("dedicated = 1\nsnapshot_minutes = 0.02\n", start=False)  # every 1.2 s
    seed, seed_crc = make_world(s, "seed.b", 60)
    s.args += ["--world", str(seed)]
    s.start()
    a = s.join("Alice")
    a.send(IN_GAME)
    a.refuse(r"EVT WORLD_WANTED", 2.0)  # its game does not play the stored world: its snapshot would be refused
    a.send("request")
    got_world(a, seed_crc)
    a.expect(r"EVT WORLD_WANTED", 4.0)
    snap, snap_crc = make_world(s, "snap.b", 61)
    a.send(f"upload {snap} host_snapshot")
    s.wait_log(r"версия 2")
    a.expect(r"EVT WORLD_WANTED", 4.0)  # and again


@test
def host_of_a_standing_server_gets_its_stored_world(make):
    """Outside dedicated mode too: the first player in is the host, the welcome says a world is stored, and the host's
    request for it is answered with it (its game loads that world instead of asking for a save)."""
    s = make(start=False)
    seed, seed_crc = make_world(s, "seed.b", 70)
    s.args += ["--world", str(seed)]
    s.start()
    a = s.join("Alice")
    check(a.host == a.id and a.world == 1, "the first player is the host of a server with a world")
    a.send("request")
    got_world(a, seed_crc)


@test
def next_host_gets_the_world_the_last_one_left(make):
    """A host uploads its world as it leaves; the next player in is the host and is given that world, with its own
    profile of that game. Backups: the first replaced world, then one a mission ("progress"), not one a snapshot."""
    s = make("backup_minutes = 30\n")
    a = s.join("Alice")
    a.send(IN_GAME)
    first, first_crc = make_world(s, "first.b", 80)
    a.send(f"upload {first} host_snapshot")
    a.send("gameid 0C0FFEE0")
    a.settle()
    a.send("profile 0C0FFEE0 alice-money")
    second, second_crc = make_world(s, "second.b", 81)
    a.send(f"upload {second} host_snapshot")
    s.wait_log(r"версия 2")
    third, third_crc = make_world(s, "third.b", 82)
    a.send(f"upload {third} host_snapshot")       # a snapshot minutes after a backup: no backup of its own
    s.wait_log(r"версия 3")
    fourth, fourth_crc = make_world(s, "fourth.b", 83)
    a.send(f"upload {fourth} progress")           # a mission passed: backed up whatever the time
    s.wait_log(r"версия 4")
    last, last_crc = make_world(s, "last.b", 84)
    a.send(f"upload {last} host_leaves")
    s.wait_log(r"версия 5")  # the game's own connection sends what is queued before it closes; the test client's does not
    a.quit()
    world = s.data / "world"
    check((world / "world.b.2").read_bytes() == first.read_bytes(), "backup 2: the first replaced world of this run")
    check((world / "world.b.1").read_bytes() == third.read_bytes(), "backup 1: the world a passed mission replaced")
    check(not (world / "world.b.3").exists(), "a snapshot soon after a backup makes none")

    b = s.join("Bob", expect_welcome=False)
    b.expect(r"EVT WELCOME ok=1 id=(\d+) host=\1 world=1 .* game=0C0FFEE0 version=5")  # the host at once, a world waits
    b.expect(r"EVT PROFILE size=0 game=0C0FFEE0")  # its own of that game: none yet
    b.send("request")
    got_world(b, last_crc)
    b.send("profile 0C0FFEE0 bob-money")
    b.settle()
    time.sleep(0.5)
    b.quit()
    a2 = s.join("Alice", expect_welcome=False)
    a2.expect(r"EVT PROFILE size=11 game=0C0FFEE0 text=alice-money")


@test
def linux_server_stops_on_quit_and_on_a_signal(make):
    """A hosting panel stops the server by typing quit into its console, or with SIGTERM, while that console stays
    open: the server has to end by itself (a panel's server hung there and had to be killed, 17.11). Linux alone: the
    Windows server reads a real console, not a pipe."""
    if os.name == "nt":
        return

    def ends(s, how):
        try:
            code = s.proc.wait(6)
        except subprocess.TimeoutExpired:
            raise Failure(f"the server did not end within 6 s of {how}; its log ends: {s.log()[-300:]!r}")
        check(code == 0, f"the server ended with {code} after {how}")
        check("Сервер остановлен" in s.log(), f"no orderly stop in the log after {how}")

    s = make(start=False)
    s.start(stdin=subprocess.PIPE)
    a = s.join("Alice")
    s.proc.stdin.write(b"quit\n")
    s.proc.stdin.flush()
    ends(s, "quit")
    a.expect(r"EVT DISCONNECTED|EVT KICKED|EVT LEAVE|EVT END")

    s2 = make(start=False)
    s2.start(stdin=subprocess.PIPE)
    s2.proc.terminate()  # SIGTERM
    ends(s2, "SIGTERM")


def settings_of(p):
    """The next S_SETTINGS of a player: (may it edit, {"section.key": value})."""
    m = p.expect(r"EVT SETTINGS ok=1 edit=(\d) n=\d+ (.*)")
    items = {}
    for part in re.findall(r"(\S+?\.\S+?)=(.*?);k\d;f\d", m[2]):
        items[part[0]] = part[1].strip()
    return m[1] == "1", items


@test
def settings_are_read_and_changed_from_the_game(make):
    """The host reads the settings and changes them: in effect at once, told to everyone, written to server.ini."""
    s = make("password = secret\n")
    a = s.join("Alice", password="secret")
    b = s.join("Bob", password="secret")
    a.send("settings")
    edit, items = settings_of(a)
    check(edit, "the host may edit")
    check(items.get("server.friendly_fire") == "0" and items.get("server.max_players") == "4", f"the values: {items}")
    check(items.get("server.password") == "1", "a password that is set shows as set, never as its text")
    check(items.get("balance.payout_mode") == "split" and "balance.price_weapons" in items, f"the balance: {items}")
    b.send("settings")
    edit, items = settings_of(b)
    check(not edit, "a partner may not edit")
    check("server.password" not in items and "server.admins" not in items, "a partner is not shown the admins' settings")

    a.send("set server friendly_fire 1")
    for p in (a, b):
        p.expect(r"EVT RULES ok=1 name=test ff=1 tp=1 life=0 cheats=1")
    check(settings_of(b)[1].get("server.friendly_fire") == "1", "the change reaches a partner's window")
    check(re.search(r"(?m)^friendly_fire = 1", s.ini.read_text(encoding="utf-8")), "server.ini has the change")

    a.send("set balance price_weapons 4.5")
    for p in (a, b):
        check(near(p.balance()["price_weapons"], 4.5), f"{p.name}: the price")
    a.send("set balance payout_mode full")
    check(near(b.balance()["payout_mode"], 0), "the payout mode by its word")
    a.send("settings")
    check(settings_of(a)[1].get("balance.sync_driver_leads") == "1", "a switch of the balance shows as 1 or 0")
    a.send("set balance sync_driver_leads 0")
    check(near(b.balance()["sync_driver_leads"], 0), "the switch reaches every game")
    check(re.search(r"(?m)^sync_driver_leads = 0", s.ini.read_text(encoding="utf-8")), "server.ini has the switch")
    text = s.ini.read_text(encoding="utf-8")
    check(re.search(r"(?m)^price_weapons = 4.5", text) and re.search(r"(?m)^payout_mode = full", text), "server.ini has the balance")

    b.send("set server friendly_fire 0")
    b.expect(r"EVT CHAT from=255 The server's settings are changed by the host or an admin")
    a.send("set server max_players 9")
    a.expect(r"EVT CHAT from=255 Setting max_players: not a value")
    a.send("set balance price_weapons 99999")
    a.expect(r"EVT CHAT from=255 Setting price_weapons: not a value")
    a.refuse(r"EVT BALANCE")


@test
def settings_hold_after_a_restart_and_passwords_change_live(make):
    s = make("max_players = 2\n")
    a = s.join("Alice")
    a.send("set server max_players 3")
    a.send("set server password letmein")
    a.send("set server dedicated 1")
    a.expect(r"EVT CHAT from=255 Server setting: dedicated = 1 \(Alice\), from the server's next start")
    a.settle()
    b = s.join("Bob", expect_welcome=False)
    b.expect(r"EVT REJECT")                      # the new password holds at once
    b2 = s.join("Bob", password="letmein")
    c = s.join("Carol", password="letmein")       # a third player: max_players was raised under a running server
    check(c.id is not None, "the third player is in")
    a.send("settings")
    check(settings_of(a)[1].get("server.dedicated") == "1", "a setting for the next start shows as written")
    b2.send("chat /players")
    b2.expect(r"EVT CHAT from=255 \[\d+\] Bob")
    for p in (a, b2, c):
        p.quit()
    s.restart()
    a2 = s.join("Alice", password="letmein", expect_welcome=False)
    a2.expect(r"EVT WELCOME ok=1 .* dedicated=1 ")
    a2.send("settings")
    edit, items = settings_of(a2)
    check(items.get("server.max_players") == "3" and items.get("server.dedicated") == "1", f"after the restart: {items}")
    check(not edit, "dedicated: the host role gives no rights")


def main():
    global VERBOSE
    ap = argparse.ArgumentParser()
    ap.add_argument("filter", nargs="?", default="")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    VERBOSE = args.verbose
    find_bin("coop-server"), find_bin("coop-test-client")
    failed = 0
    for fn in TESTS:
        if args.filter not in fn.__name__:
            continue
        servers, players = [], []

        def make(ini="", files=None, args=(), start=True):
            s = Server(ini, files, args)
            servers.append(s)
            join = s.join

            def tracked(name, **kw):
                p = join(name, **kw)
                players.append(p)
                return p

            s.join = tracked
            return s.start() if start else s

        try:
            fn(make)
            print(f"ok    {fn.__name__}")
        except Failure as e:
            failed += 1
            print(f"FAIL  {fn.__name__}: {e}")
        finally:
            for p in players:
                p.quit()
            for s in servers:
                s.close()
    print(f"{failed} failed" if failed else "all passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
