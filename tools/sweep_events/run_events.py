#!/usr/bin/env python3
"""Event sweep driver for Baroque-Native (read-only validation, 2026-10-10).

For every DATA/EVENT_EN/E??.EVT: one Baroque-Native process (strictly one at a time, hidden window via --shot, per-run
timeout), floor chosen per event (PLAN below), 3-4 captures (early / middle / late [/ after end]) with --shot-also, a
--logic-hash file (per tick event frame) and stderr. One JSON line per run is appended to the results file as it goes;
re-running skips events already in the file.

usage: python tools/sweep_events/run_events.py [--events 01,03,...] [--timeout 90] [--cap 2100] [--talk]
"""
import argparse, json, os, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "sweep"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pngstat import png_stats  # noqa: E402
from evt_len import evt_info  # noqa: E402

EXE = ROOT / "build/agent_evsweep/Release/Baroque-Native.exe"
SHOTS = ROOT / "work/native_captures/events_sweep"
LOGS = Path(__file__).resolve().parent / "logs"

# event -> (floor, extra args). Floors: trigger sites (FUN_001229d0: 0x5f E01/51/52/53, 0x5c E06/E62, 0x60 E71/E72),
# ACT 'Event n' owners (C04A E03 hub, C04E E91/92 hub, C03A E04 MR01_30 / E14 MR03_40|MR08_40, C02A E05/E08/E82
# MR04|05_31, C06A E12 MR02_31, C22A E13 MR02_30, C25..C29 E21..E49 MR00_00), death / clear events E10/E11/E15/E83
# at the tower bottom (F140). Map id of the file selects the room (rooms 30/31/40 are the first room of those FDTs).
# 2026-10-10 evfix: --flag / --clear-flag / --ctl-flag now apply at the end of the Game init (after the new-game wipe
# FUN_00178ad0), so the real trigger floors of FUN_001229d0 are used: 0x5f E51 = flags 1+2, E52 = flag 1, E53 = flag 1 +
# control bit 0x100000 (killed by C10A); 0x5c / 0x60 E62 / E72 = flag 0x58 clear. Room placements gated by flags:
# MR02_31 (E12) J024 / C04C / C28A need flag 15; MR02_30 (E13) C22A / C22B need flag 201; MR99_00 (floor 99, E10 / E11
# / E15 / E83 scene set J025 at y 5000 + J026) needs flag 5.
PLAN = {1: (95, []), 51: (95, ["--flag", "1", "--flag", "2"]), 52: (95, ["--flag", "1"]),
        53: (95, ["--flag", "1", "--ctl-flag", "0x100000"]),
        6: (92, []), 62: (92, ["--clear-flag", "0x58"]), 71: (96, []), 72: (96, ["--clear-flag", "0x58"]),
        3: (91, ["--event", "3"]), 91: (91, ["--event", "91"]), 92: (91, ["--event", "92"]), 93: (91, ["--event", "93"]),
        4: (104, ["--event", "4"]), 5: (115, ["--event", "5"]), 8: (115, ["--event", "8"]), 82: (115, ["--event", "82"]),
        9: (115, ["--event", "9"]), 12: (112, ["--flag", "15", "--event", "12"]), 13: (120, ["--flag", "201", "--event", "13"]),
        14: (128, ["--event", "14"]), 50: (100, ["--event", "50"]),
        10: (99, ["--flag", "5", "--event", "10"]), 11: (99, ["--flag", "5", "--event", "11"]),
        15: (99, ["--flag", "5", "--event", "15"]), 83: (99, ["--flag", "5", "--event", "83"])}
for n in range(21, 50):
    PLAN[n] = (101, ["--event", str(n)])


def end_frame(info):
    e = info["end"]
    if e is None or e <= 10 or e >= 99999 or info.get("realMax", 0) > e:
        e = info.get("realMax") or 600
    return e


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--events", default="all")
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument("--cap", type=int, default=2100, help="max ticks per run (the port runs ~27 ticks/s)")
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent / "results.jsonl"))
    ap.add_argument("--seed", default="1")
    a = ap.parse_args()
    SHOTS.mkdir(parents=True, exist_ok=True); LOGS.mkdir(parents=True, exist_ok=True)
    out = Path(a.out)
    done = set()
    if out.exists():
        for line in out.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                done.add(json.loads(line)["event"])
            except Exception:
                pass
    evs = sorted(int(p.name[1:3]) for p in (ROOT / "work/assets/DATA/EVENT_EN").glob("E*.EVT"))
    if a.events != "all":
        want = {int(x) for x in a.events.split(",")}
        evs = [e for e in evs if e in want]
    for ev in evs:
        if ev in done:
            continue
        info = evt_info(ROOT / f"work/assets/DATA/EVENT_EN/E{ev:02d}.EVT")
        floor, extra = PLAN.get(ev, (91, ["--event", str(ev)]))
        end = end_frame(info)
        start = 10   # PORT: event starts on the first Game ticks
        early = start + min(90, max(30, end // 6))
        mid = start + end // 2
        late = start + max(end - 40, end * 9 // 10)
        ticks = [early, mid, late]
        if start + end + 90 <= a.cap:
            ticks.append(start + end + 90)   # after the end: gameplay camera / HUD back
        if start + end > a.cap:   # truncated: spread over the cap
            ticks = [early, a.cap // 2, a.cap]
        ticks = sorted({min(t, a.cap) for t in ticks})
        truncated = start + end > a.cap
        last = ticks[-1]
        pngs = {t: SHOTS / f"e{ev:02d}_f{floor}_t{t:04d}.png" for t in ticks}
        for p in pngs.values():
            if p.exists():
                p.unlink()
        hashp = LOGS / f"e{ev:02d}.hash"
        cmd = [str(EXE), "--floor", str(floor), "--seed", a.seed] + extra + ["--logic-hash", str(hashp)]
        for t in ticks[:-1]:
            cmd += ["--shot-also", str(pngs[t]), str(t)]
        cmd += ["--shot", str(pngs[last]), str(last)]
        t0 = time.time(); status = "ok"; rc = None
        try:
            p = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, timeout=a.timeout,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            rc = p.returncode; so, se = p.stdout, p.stderr
            if rc != 0:
                status = "crash"
        except subprocess.TimeoutExpired as e:
            status = "timeout"; so, se = e.stdout or b"", e.stderr or b""
        dt = time.time() - t0
        stderr = se.decode("utf-8", "replace"); stdout = so.decode("utf-8", "replace")
        (LOGS / f"e{ev:02d}.err").write_text(stderr, encoding="utf-8")
        # event frame progress from the logic hash
        evlog = {"first_tick": None, "last_tick": None, "max_frame": -1, "ended": False, "ticks": 0}
        if hashp.exists():
            prev = -1
            for line in hashp.read_text(errors="replace").splitlines():
                parts = line.split()
                try:
                    tick = int(parts[0]); fr = int([x for x in parts if x.startswith("ev=")][0][3:])
                except Exception:
                    continue
                evlog["ticks"] = tick
                if fr >= 0:
                    if evlog["first_tick"] is None:
                        evlog["first_tick"] = tick
                    evlog["last_tick"] = tick
                    evlog["max_frame"] = max(evlog["max_frame"], fr)
                elif prev >= 0:
                    evlog["ended"] = True
                prev = fr
        shots = {}
        for t, pth in pngs.items():
            if pth.exists():
                try:
                    shots[str(t)] = dict(png=str(pth.relative_to(ROOT)), **png_stats(str(pth), step=8))
                except Exception as e:
                    shots[str(t)] = {"png": str(pth.relative_to(ROOT)), "error": str(e)}
            else:
                shots[str(t)] = None
        if status == "ok" and not all(shots.values()):
            status = "missing_png"
        rec = {"event": ev, "floor": floor, "map": info["map"], "end": end, "cues": info["cues"], "truncated": truncated,
               "status": status, "rc": rc, "secs": round(dt, 1),
               "cmd": " ".join(f'"{c}"' if " " in c else c for c in cmd).replace(str(ROOT) + os.sep, ""),
               "stderr": stderr, "stdout": stdout[-4000:], "evlog": evlog, "shots": shots}
        with out.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(rec) + "\n")
        print(f"E{ev:02d} floor {floor}: {status} rc={rc} {dt:.1f}s ev frames {evlog['first_tick']}..{evlog['last_tick']} "
              f"max {evlog['max_frame']} ended={evlog['ended']} stderr {len(stderr.splitlines())} lines", flush=True)
    print("done", flush=True)


if __name__ == "__main__":
    main()
