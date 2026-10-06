#!/usr/bin/env python3
"""LAN soak test: one host and up to 7 clients of the real game, headless, on
this machine. Every ship is driven by the --bot autopilot; the host starts the
races by itself (--autostart) and goes back to the lobby after each one.

    make headless
    python tests/lan_soak.py --data /path/to/wipeout --players 8 --races 2
    python tests/lan_soak.py --data /path/to/wipeout --players 8 --races 1 --loss 0.05

The game data folder is linked (not copied) into a temporary directory.
Checks: no crash, everybody joins, every race starts and ends for everybody,
the remote ships really drive (laps completed), snapshot stream health.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def link_dir(target, link):
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "mklink", "/J", link, target], check=True, stdout=subprocess.DEVNULL)
    else:
        os.symlink(target, link)


def unlink_dir(link):
    # Remove only the link, never what it points to
    if os.name == "nt":
        subprocess.run(["cmd", "/c", "rmdir", link], check=False)
    else:
        os.unlink(link)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="the wipeout/ game data folder")
    ap.add_argument("--exe", default=os.path.join(ROOT, "wipegame-headless.exe" if os.name == "nt" else "wipegame-headless"))
    ap.add_argument("--players", type=int, default=8)
    ap.add_argument("--races", type=int, default=1)
    ap.add_argument("--circuit", type=int, default=0)
    ap.add_argument("--loss", type=float, default=0.0, help="simulated packet loss, both ways")
    ap.add_argument("--dup", type=float, default=0.0, help="simulated duplicated packets")
    ap.add_argument("--port", type=int, default=47850)
    ap.add_argument("--timeout", type=float, default=420.0, help="seconds per race")
    ap.add_argument("--keep", action="store_true", help="keep the logs directory")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(args.data, "track01")):
        sys.exit("no game data in %s" % args.data)
    if not os.path.exists(args.exe):
        sys.exit("build the headless binary first: make headless")

    work = tempfile.mkdtemp(prefix="wipeout-lan-soak-")
    data_link = os.path.join(work, "wipeout")
    link_dir(os.path.abspath(args.data), data_link)
    exe = os.path.join(work, os.path.basename(args.exe))
    shutil.copy(args.exe, exe)

    common = ["--headless-loop", "--bot", "--stats", "--port", str(args.port), "--circuit", str(args.circuit)]
    if args.loss:
        common += ["--net-loss", str(args.loss)]
    if args.dup:
        common += ["--net-dup", str(args.dup)]

    procs = []
    logs = []

    def launch(name, extra):
        log_path = os.path.join(work, name + ".log")
        log = open(log_path, "w")
        p = subprocess.Popen([exe] + common + ["--name", name] + extra, cwd=work, stdout=log, stderr=subprocess.STDOUT)
        procs.append((name, p))
        logs.append((name, log_path, log))

    print("work dir: %s" % work)
    started = time.time()
    launch("HOST", ["--host", "--autostart", str(args.players), "--races", str(args.races), "--pilot", "0"])
    time.sleep(1.0)
    for i in range(1, args.players):
        launch("C%d" % i, ["--join", "127.0.0.1:%d" % args.port, "--races", str(args.races), "--pilot", str(i)])
        time.sleep(0.2)

    deadline = started + 20 + args.timeout * args.races
    host = procs[0][1]
    while time.time() < deadline and host.poll() is None:
        time.sleep(1.0)
    host_timed_out = host.poll() is None

    # The clients leave by themselves when the host ends the game
    grace = time.time() + 20
    while time.time() < grace and any(p.poll() is None for _, p in procs):
        time.sleep(0.5)
    killed = []
    for name, p in procs:
        if p.poll() is None:
            p.kill()
            killed.append(name)
        p.wait()
    for _, _, log in logs:
        log.close()

    # --- Evaluate ---------------------------------------------------------------
    failures = []
    if host_timed_out:
        failures.append("host did not finish %d race(s) in time" % args.races)
    if killed:
        failures.append("had to kill: %s" % ", ".join(killed))

    print("\n%-5s %5s %6s %7s %7s %7s %6s %8s %6s" % ("who", "exit", "races", "snaps", "missed", "late", "gap ms", "maxlap", "malf"))
    for (name, p), (_, log_path, _) in zip(procs, logs):
        text = open(log_path, errors="replace").read()
        if "Abort at" in text:
            failures.append("%s aborted: %s" % (name, re.search(r"Abort at.*", text).group(0)))
        if p.returncode not in (0, None) and name not in killed:
            failures.append("%s exit code %s" % (name, p.returncode))

        race_stats = re.findall(r"net stats \(race\) \w+: .*?malformed (\d+), snapshots (\d+), late (\d+), missed (\d+), max gap (\d+) ms", text)
        laps = [int(x) for x in re.findall(r"net progress \w+: phase \d+ lap (-?\d+)", text)]
        max_lap = max(laps) if laps else -1
        snaps = sum(int(r[1]) for r in race_stats)
        missed = sum(int(r[3]) for r in race_stats)
        late = sum(int(r[2]) for r in race_stats)
        malformed = sum(int(r[0]) for r in race_stats)
        gap = max([int(r[4]) for r in race_stats] or [0])
        races = len(race_stats)
        print("%-5s %5s %6d %7d %7d %7d %6d %8d %6d" % (name, p.returncode, races, snaps, missed, late, gap, max_lap, malformed))

        if races != args.races:
            failures.append("%s saw %d of %d races end" % (name, races, args.races))
        if max_lap < 1:
            failures.append("%s never completed a lap (the remote ship didn't drive)" % name)
        if name != "HOST":
            if malformed:
                failures.append("%s received %d malformed packets" % (name, malformed))
            if snaps and missed > snaps * (args.loss * 1.6 + 0.02):
                failures.append("%s missed %d of %d snapshots" % (name, missed, snaps))
            limit = 400 if args.loss else 250
            if gap > limit:
                failures.append("%s had a %d ms gap without snapshots" % (name, gap))
            if "CONNECTION TO THE HOST LOST" in text:
                failures.append("%s lost the connection" % name)
        else:
            if "dropped out" in text:
                failures.append("the host dropped a player: " + ", ".join(re.findall(r"net: (\S+) dropped out", text)))
            if text.count("everybody is ready, go") != args.races:
                failures.append("the host started %d races" % text.count("everybody is ready, go"))

    print("\ntook %.0f s" % (time.time() - started))
    unlink_dir(data_link)
    if failures:
        print("FAILED:")
        for f in failures:
            print("  - " + f)
        print("logs in %s" % work)
        sys.exit(1)
    print("OK")
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    else:
        print("logs in %s" % work)


if __name__ == "__main__":
    main()
