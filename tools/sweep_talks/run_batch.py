#!/usr/bin/env python3
"""Conversation sweep, log pass: every talk of the catalog played to its end through FUN_001a73a0 (--talk-batch) in the
real Game scene (hidden window, --shot run), with --talk-trace. One process per chunk, one process at a time.
Writes tools/sweep_talks/logs/batch_<chunk>.log and appends one JSON line per talk to tools/sweep_talks/batch.jsonl.

usage: python tools/sweep_talks/run_batch.py [--speakers C24A,..] [--files a,b] [--floor 91] [--chunk 120]
"""
import argparse, json, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
EXE = ROOT / "build/agent_talksweep/Release/Baroque-Native.exe"
LOGS = HERE / "logs"


def parse(lines):
    """talk batch start/end blocks -> records"""
    recs, cur = [], None
    for ln in lines:
        m = re.search(r"talk batch start (\S+) t=(\d+)", ln)
        if m:
            cur = dict(file=m.group(1), t0=int(m.group(2)), texts=[], names=[], voice=[], states=[], log=[])
            continue
        if cur is None:
            continue
        cur["log"].append(ln)
        m = re.search(r"talk batch end (\S+) ticks=(\d+) result=(\S+)", ln)
        if m:
            cur["ticks"] = int(m.group(2)); cur["result"] = m.group(3)
            recs.append(cur); cur = None
            continue
        m = re.search(r"talk text t=(\d+) frame=(-?\d+) (cleared|rect=\(([^)]*)\) style=(\d+) speed=(-?\d+) scale=\(([^)]*)\) prop=(\d+) col=(\w+) lines=(\d+) widest=(-?\d+) reach=(-?\d+) text=(.*))", ln)
        if m:
            if m.group(3) == "cleared":
                cur["texts"].append(dict(t=int(m.group(1)), frame=int(m.group(2)), cleared=True))
            else:
                cur["texts"].append(dict(t=int(m.group(1)), frame=int(m.group(2)), rect=[float(x) for x in m.group(4).split()],
                                         style=int(m.group(5)), speed=int(m.group(6)), scale=m.group(7), prop=int(m.group(8)),
                                         col=m.group(9), lines=int(m.group(10)), widest=int(m.group(11)), reach=int(m.group(12)),
                                         text=m.group(13)))
            continue
        m = re.search(r'talk trace t=(\d+) name=(-?\d+) "(.*)"', ln)
        if m:
            cur["names"].append(dict(t=int(m.group(1)), idx=int(m.group(2)), name=m.group(3))); continue
        m = re.search(r"talk trace t=(\d+) voiceStatus (-?\d+) -> (-?\d+) frame=(-?\d+)", ln)
        if m:
            cur["voice"].append([int(m.group(1)), int(m.group(3)), int(m.group(4))]); continue
        m = re.search(r"talk trace t=(\d+) state=(-?\d+) active=(-?\d+) idx=(-?\d+) file=(\S+) cat=(-?\d+) spk=(.{0,4}) frame=(-?\d+) noVoice=(\d+) voice=(-?\d+) vs=(-?\d+)", ln)
        if m:
            cur["states"].append(dict(t=int(m.group(1)), state=int(m.group(2)), active=int(m.group(3)), file=m.group(5),
                                      frame=int(m.group(8)), noVoice=int(m.group(9)), voice=int(m.group(10)), vs=int(m.group(11))))
    return recs


def run_chunk(names, tag, floor, flags, timeout, seed=1):
    LOGS.mkdir(parents=True, exist_ok=True)
    lst = LOGS / f"batch_{tag}.txt"
    lst.write_text("\n".join(names) + "\n", encoding="utf-8")
    log = LOGS / f"batch_{tag}.log"
    cmd = [str(EXE), "--floor", str(floor), "--seed", str(seed), "--keep-hp"]
    for f in flags:
        cmd += ["--flag", str(f)]
    cmd += ["--talk-batch", str(lst), "--talk-trace", "1", "--shot", str(LOGS / f"batch_{tag}_end.png"), "99999999"]
    t0 = time.time()
    lines = []
    p = subprocess.Popen(cmd, cwd=str(ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                         creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    status = "ok"
    try:
        for raw in p.stderr:
            ln = raw.decode("utf-8", "replace").rstrip()
            lines.append(ln)
            if "talk batch done" in ln:
                break
            if time.time() - t0 > timeout:
                status = "timeout"; break
    finally:
        early = p.poll()             # exited by itself before "talk batch done" (crash / killed from outside)
        p.kill(); p.wait()
    if status == "ok" and not any("talk batch done" in l for l in lines):
        status = f"EXITED EARLY rc={early if early is not None else p.returncode}"
    log.write_text("\n".join(lines), encoding="utf-8")
    return parse(lines), status, time.time() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--speakers", default="")
    ap.add_argument("--files", default="")
    ap.add_argument("--floor", type=int, default=91)
    ap.add_argument("--flags", default="")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--tag", default="")
    ap.add_argument("--chunk", type=int, default=150)
    ap.add_argument("--timeout", type=int, default=3600)
    ap.add_argument("--out", default=str(HERE / "batch.jsonl"))
    a = ap.parse_args()
    cat = json.loads((HERE / "catalog.json").read_text(encoding="utf-8"))["talks"]
    if a.files:
        names = [n for n in a.files.split(",") if n]   # given order (e.g. a talk that kills the player last)
    elif a.speakers:
        want = set(a.speakers.split(","))
        names = [c["file"] for c in cat if c["speaker"] in want]
    else:
        names = [c["file"] for c in cat]
    flags = [int(x) for x in a.flags.split(",") if x]
    for k in range(0, len(names), a.chunk):
        part = names[k:k + a.chunk]
        tag = f"{a.tag}{part[0]}_{len(part)}"
        recs, status, dt = run_chunk(part, tag, a.floor, flags, a.timeout, a.seed)
        with open(a.out, "a", encoding="utf-8") as fh:
            for r in recs:
                r["floor"] = a.floor; r["seed"] = a.seed; r["actors"] = True
                fh.write(json.dumps(r) + "\n")
        print(f"chunk {tag}: {status}, {len(recs)}/{len(part)} talks, {dt:.0f}s", flush=True)


if __name__ == "__main__":
    main()
