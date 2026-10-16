#!/usr/bin/env python3
"""Conversation sweep, capture pass: one talk per run in the real Game scene with its speaker present
(--talk SPEAKER places the player in front of the NPC on the 30th Game tick, --talk-name NAME plays that talk through
FUN_001a73a0), --talk-trace, hidden-window captures relative to the talk start: the default set is +2 (window opens),
+45 (first sentence typed) and +45 after every sentence trigger of the catalog (multi-sentence talks), optionally a
conversation-camera press (pad 0x20, key-assign slot 0x4e65c0) at +CAM with captures after it.
The talk start tick is found by a short trace-only run first (cached per floor/speaker/flags in starts.json).

usage: python tools/sweep_talks/run_shots.py SPEC [SPEC ...]
   SPEC = SPEAKER:FLOOR:NAME[:flags=a+b][:cam=TICK][:at=t1+t2][:window]
   e.g. C08A:103:C08_V000:cam=60   C24A:91:C24_V073   C11A:91:C11_V010:window
"""
import json, re, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
EXE = ROOT / "build/agent_talksweep/Release/Baroque-Native.exe"
SHOTS = ROOT / "work/native_captures/talk_sweep"
LOGS = HERE / "logs"
NOWIN = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def run(cmd, timeout=240):
    try:
        p = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, timeout=timeout, creationflags=NOWIN)
        return p.returncode, p.stderr.decode("utf-8", "replace")
    except subprocess.TimeoutExpired as e:
        return "timeout", (e.stderr or b"").decode("utf-8", "replace")


def base(spk, floor, name, flags, seed=1):
    cmd = [str(EXE), "--floor", str(floor), "--seed", str(seed), "--keep-hp", "--talk", spk, "--talk-name", name, "--talk-trace", "1"]
    for f in flags:
        cmd += ["--flag", str(f)]
    return cmd


def start_tick(spk, floor, name, flags, seed=1):
    cache_p = HERE / "starts.json"
    cache = json.loads(cache_p.read_text()) if cache_p.exists() else {}
    key = f"{spk}:{floor}:{'+'.join(map(str, flags))}:{seed}"
    if key in cache:
        return cache[key]
    rc, err = run(base(spk, floor, name, flags, seed) + ["--shot", str(LOGS / "probe.png"), "150"])
    m = re.search(r"talk trace t=(\d+) state=5 active=1", err)
    t0 = int(m.group(1)) if m else None
    if "no NPC" in err:
        t0 = None
    cache[key] = t0
    cache_p.write_text(json.dumps(cache, indent=1))
    return t0


def main():
    cat = {c["file"]: c for c in json.loads((HERE / "catalog.json").read_text(encoding="utf-8"))["talks"]}
    SHOTS.mkdir(parents=True, exist_ok=True); LOGS.mkdir(parents=True, exist_ok=True)
    for spec in sys.argv[1:]:
        parts = spec.split(":")
        spk, floor, name = parts[0], int(parts[1]), parts[2]
        opts = dict(p.split("=", 1) if "=" in p else (p, "1") for p in parts[3:])
        flags = [int(x) for x in opts.get("flags", "").split("+") if x]
        seed = int(opts.get("seed", "1"))
        t0 = start_tick(spk, floor, name, flags, seed)
        if t0 is None:
            print(f"{spec}: NO START (speaker not on floor / talk did not start)", flush=True)
            continue
        c = cat[name]
        rel = [2, 45] + [s["frame"] + 45 for s in c["triggers"].get("sentence", [])]
        if "at" in opts:
            rel = [int(x) for x in opts["at"].split("+")]
        cmd = base(spk, floor, name, flags, seed)
        if "cam" in opts:
            ct = t0 + int(opts["cam"])
            cmd += ["--press", "0x20", str(ct)]
            rel += [int(opts["cam"]) + 6, int(opts["cam"]) + 30]
        rel = sorted(set(rel))
        tag = f"{name}_f{floor}" + ("_cam" if "cam" in opts else "") + ("_win" if "window" in opts else "")
        shots = [SHOTS / f"{tag}_p{r:04d}.png" for r in rel]
        for s in shots:
            if s.exists(): s.unlink()
        for r, s in zip(rel[:-1], shots[:-1]):
            cmd += ["--shot-also", str(s), str(t0 + r)]
        cmd += ["--shot-window" if "window" in opts else "--shot", str(shots[-1]), str(t0 + rel[-1])]
        t1 = time.time()
        rc, err = run(cmd)
        (LOGS / f"shot_{tag}.log").write_text(err, encoding="utf-8")
        texts = [l for l in err.splitlines() if l.startswith("talk text") or "talk trace" in l and ("name=" in l or "state=" in l)]
        ok = sum(1 for s in shots if s.exists())
        print(f"{spec}: rc={rc} t0={t0} shots {ok}/{len(shots)} {time.time() - t1:.0f}s", flush=True)
        for l in texts[:12]:
            print("   ", l[:220], flush=True)


if __name__ == "__main__":
    main()
