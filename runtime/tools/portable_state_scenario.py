#!/usr/bin/env python3
"""Game test of portable save states (runtime/src/portable_state.h), headless.

usage: portable_state_scenario.py WWHD_BINARY GAME_DIR SAVE_DIR WORK_DIR [--skip-full]

SAVE_DIR is a save folder (with user/cking.sav whose Quest Log 1 is in play, e.g. on Outset); only a
copy of cking.sav and cking_playlog.sav is used. Three runs, one game at a time:

  make:  boot, file select (A presses), gameplay; for the "house" case a stage change to Link's
         house (LinkRM, the stage-change request at 104741F0 poked), for the "outset" case none
         (same stage as the cold boot, another place); walk a little, save a portable state to slot 1
  load:  cold boot of the same save, gameplay on Outset, change the rupees (so the load has to
         restore them) and walk elsewhere, load slot 1; when Link has arrived save slot 2.
         Checks: stage, room and Link's position (within 30 units) and angle match, the save data
         (inventory, flags, progress: the cking.sav block) and the HD sections are equal apart from
         the time of day (it runs on)
  full:  full save states (WWHD_STATE_SAVE_AT / WWHD_STATE_LOAD_AT) still save and load

A game is only started when no performance benchmark (run_bench.py) is running; the script starts
at most one game and stops it itself (TERM, then KILL).
"""
import os
import re
import shutil
import signal
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools", "savegame"))


def wait_for_quiet_machine():
    while True:
        out = subprocess.run(["ps", "-axo", "command"], capture_output=True, text=True).stdout
        # a Python process running the benchmark (not shells that merely mention its name)
        if not re.search(r"^\S*python[\d.]*\s+(\S*/)?run_bench\.py", out, re.M):
            return
        print("  a benchmark is running; waiting", flush=True)
        time.sleep(30)


def run_game(binary, game, run_dir, env_extra, until, timeout):
    """starts the game in run_dir, stops it when `until(log)` is true or after `timeout` s"""
    wait_for_quiet_machine()
    env = dict(os.environ)
    env.update({"WWHD_HIDDEN_WINDOWS": "1", "WWHD_NO_AUDIO": "1", "WWHD_NO_HOST_INPUT": "1", "WWHD_NO_GAMEPAD": "1",
                "WWHD_RENDERER_RUNTIME": "metal", "WWHD_STATE_DIR": "states", "WWHD_SHADER_CACHE": "../shader_cache.bin"})
    env.update(env_extra)
    log_path = os.path.join(run_dir, "log")
    with open(log_path, "w") as log:
        p = subprocess.Popen([binary, "--game", game, "--save", "save"], cwd=run_dir, env=env, stdout=log,
                             stderr=subprocess.STDOUT)
    t0 = time.time()
    try:
        while p.poll() is None and time.time() - t0 < timeout:
            time.sleep(1)
            if until(open(log_path, errors="replace").read()):
                time.sleep(1)
                break
    finally:
        if p.poll() is None:
            p.send_signal(signal.SIGTERM)
            try:
                p.wait(5)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait(5)
    assert p.poll() is not None, "game still running"
    return open(log_path, errors="replace").read()


def prepare(work, tag, save_dir, states=None):
    d = os.path.join(work, tag)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(os.path.join(d, "save", "user"))
    for f in ("cking.sav", "cking_playlog.sav"):
        src = os.path.join(save_dir, "user", f)
        if os.path.exists(src):
            shutil.copy(src, os.path.join(d, "save", "user", f))
    os.makedirs(os.path.join(d, "states"))
    for f in states or []:
        shutil.copy(f, os.path.join(d, "states"))
    return d


def presses():  # A every 60 frames through the title and file select into gameplay
    return ",".join("%d-%d:8000" % (f, f + 8) for f in range(1200, 3200, 60))


def parse_state(path):
    kv = {}
    for line in open(path):
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            kv[k.strip()] = v.strip()
    return kv


def portable_case(binary, game, save_dir, work, name, warp, expect_stage, origin=3300):
    """make a portable state (after an optional stage-change poke and a walk), load it in a cold boot"""
    ok = True
    d = prepare(work, name + "-make", save_dir)
    env = {"WWHD_PRESS": presses(), "WWHD_TEST_ORIGIN": str(origin),
           "WWHD_STICK": "%d-%d:0:1" % (origin + 420, origin + 450), "WWHD_PORTABLE_SAVE_AT": "%d:1" % (origin + 540)}
    if warp:
        env["WWHD_TEST_POKE"] = "1:104741F0:" + warp
    log = run_game(binary, game, d, env, lambda l: "portable state written" in l or "not saved" in l, 300)
    m = re.search(r"slot 1: portable state written \((\d+) bytes; (\S+) room (-?\d+) at", log)
    state1 = os.path.join(d, "states", "slot1.wwstate")
    if not m or not os.path.exists(state1):
        print(name + ": make FAILED (no portable state)\n" + "\n".join(l for l in log.splitlines() if "savestate" in l or "test]" in l)[-3000:])
        return False
    print(name + ": slot 1 written, %s bytes, stage %s room %s" % m.groups())
    s1 = parse_state(state1)
    if s1["stage"] != expect_stage:
        print(name + ": expected stage %s, got %s" % (expect_stage, s1["stage"]))
        ok = False
    size = os.path.getsize(state1)
    if size > 64 * 1024:
        print(name + ": FAILED: file is %d bytes" % size)
        ok = False

    # cold boot, change the rupees and walk elsewhere, load slot 1, save slot 2 once Link has arrived
    d2 = prepare(work, name + "-load", save_dir, [state1])
    env = {"WWHD_PRESS": presses(), "WWHD_TEST_ORIGIN": str(origin),
           "WWHD_TEST_POKE": "1:*101F84DC+24:0063",  # 99 rupees before the load (the state has the save's own count)
           "WWHD_STICK": "%d-%d:1:0" % (origin + 30, origin + 80),
           "WWHD_PORTABLE_LOAD_AT": "%d:1" % (origin + 90), "WWHD_PORTABLE_SAVE_AT": "%d:2" % (origin + 600)}
    log = run_game(binary, game, d2, env, lambda l: "slot 2: portable state written" in l or "slot 2: not saved" in l, 300)
    arrived = re.search(r"portable load: arrived in (\S+) room (-?\d+) at (\S+) (\S+) (\S+) \(distance (\S+)", log)
    print(name + ": " + (arrived.group(0) if arrived else "Link did not arrive"))
    state2 = os.path.join(d2, "states", "slot2.wwstate")
    if not arrived or not os.path.exists(state2):
        print("\n".join(l for l in log.splitlines() if "savestate" in l or "test]" in l)[-3000:])
        return False
    s2 = parse_state(state2)
    for k in ("stage", "room", "file_slot", "player_name"):
        if s1[k] != s2[k]:
            print(name + ": %s differs: %s vs %s" % (k, s1[k], s2[k]))
            ok = False
    p1 = [float(x) for x in s1["link_pos"].split()]
    p2 = [float(x) for x in s2["link_pos"].split()]
    dist = sum((a - b) ** 2 for a, b in zip(p1, p2)) ** 0.5
    da = (int(s1["link_angle_y"]) - int(s2["link_angle_y"])) & 0xFFFF
    da = min(da, 0x10000 - da)
    print(name + ": position %s -> %s (distance %.1f), angle difference %d" % (p1, p2, dist, da))
    if dist > 30 or da > 0x400:
        ok = False
    # save data: equal apart from the time of day (it runs on) and the checksum pair
    a, b = bytes.fromhex(s1["savedata"]), bytes.fromhex(s2["savedata"])
    import wwsave as W
    diff = [n for n, (off, size) in W.HD_FIELDS.items() if a[off:off + size] != b[off:off + size]]
    print(name + ": save data fields that differ: %s (time of day %s -> %s)" % (diff or "none", s1["time_of_day"], s2["time_of_day"]))
    if set(diff) - {"status_b.time"}:
        ok = False
    rupee_off = W.HD_FIELDS["status_a.rupee"][0]
    print(name + ": rupees %d (state) / %d (after the load; 99 were poked before it)" %
          (int.from_bytes(a[rupee_off:rupee_off + 2], "big"), int.from_bytes(b[rupee_off:rupee_off + 2], "big")))
    for k in ("hd_player", "hd_status", "hd_event", "hd_map"):
        if s1[k] != s2[k]:
            print(name + ": %s differs" % k)
            ok = False
    return ok


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    binary, game, save_dir, work = (os.path.abspath(a) for a in sys.argv[1:5])
    skip_full = "--skip-full" in sys.argv
    os.makedirs(work, exist_ok=True)
    ok = True
    origin = 3300  # TV frame: well inside gameplay

    # Link's house (another stage than the cold boot's Outset), and Outset itself (same stage, other place)
    warp = "4C696E6B524D0000" + "0000" + "00" + "FF" + "01" + "00"  # "LinkRM", point 0, room 0, layer -1, enabled, wipe 0
    ok = portable_case(binary, game, save_dir, work, "house", warp, "LinkRM") and ok
    ok = portable_case(binary, game, save_dir, work, "outset", None, "sea") and ok

    # ---- full states keep working
    if not skip_full:
        d3 = prepare(work, "full", save_dir)
        env = {"WWHD_PRESS": presses(), "WWHD_STATE_SAVE_AT": "%d:3" % origin, "WWHD_STATE_LOAD_AT": "%d:3" % (origin + 300)}
        log = run_game(binary, game, d3, env, lambda l: "Loaded slot 3" in l or "slot 3: cannot" in l or "slot 3: not" in l, 300)
        written = re.search(r"slot 3: written \(([\d.]+) MB on disk", log)
        loaded = "Loaded slot 3" in log
        print("full: %s, %s" % ("written (%s MB)" % written.group(1) if written else "NOT written", "loaded" if loaded else "NOT loaded"))
        if not (written and loaded):
            ok = False
        try:
            os.remove(os.path.join(d3, "states", "slot3.bin"))  # this run's own ~300 MB file
        except OSError:
            pass
    print("RESULT: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
