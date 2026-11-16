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
        self.ini.write_text("[server]\nname = test\ninternet = 0\nbind = 127.0.0.1\nmax_players = 4\n" + body, encoding="utf-8")

    def start(self):
        cmd = [str(find_bin("coop-server")), "--config", str(self.ini), "--data", str(self.data), "--port", str(self.port)] + self.args
        self.proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=self.dir)
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
