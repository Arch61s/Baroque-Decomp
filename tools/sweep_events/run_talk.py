#!/usr/bin/env python3
"""Hub talk sweep: Baroque-Native --floor 91 --seed 1 --talk SPEAKER, frames at t60 / t110 / t180 (hidden window,
one process at a time, 90 s timeout). Appends JSON lines to tools/sweep_events/talk_results.jsonl.

usage: python tools/sweep_events/run_talk.py [--speakers C04A,C08A,...]
"""
import argparse, json, os, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "sweep"))
from pngstat import png_stats  # noqa: E402

EXE = ROOT / "build/agent_evsweep/Release/Baroque-Native.exe"
SHOTS = ROOT / "work/native_captures/events_sweep/talk"
LOGS = Path(__file__).resolve().parent / "logs"
DEFAULT = "C04A,C08A,C10A,C11A,C12A,C13A,C14A,C15A,C23A,C24A"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--speakers", default=DEFAULT)
    ap.add_argument("--ticks", default="60,110,180")
    ap.add_argument("--floor", default="91")
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent / "talk_results.jsonl"))
    a = ap.parse_args()
    SHOTS.mkdir(parents=True, exist_ok=True); LOGS.mkdir(parents=True, exist_ok=True)
    ticks = [int(t) for t in a.ticks.split(",")]
    for spk in a.speakers.split(","):
        pngs = {t: SHOTS / f"talk_{spk}_t{t:04d}.png" for t in ticks}
        for p in pngs.values():
            if p.exists():
                p.unlink()
        cmd = [str(EXE), "--floor", a.floor, "--seed", "1", "--talk", spk]
        for t in ticks[:-1]:
            cmd += ["--shot-also", str(pngs[t]), str(t)]
        cmd += ["--shot", str(pngs[ticks[-1]]), str(ticks[-1])]
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
        stderr = se.decode("utf-8", "replace")
        (LOGS / f"talk_{spk}.err").write_text(stderr, encoding="utf-8")
        shots = {}
        for t, pth in pngs.items():
            shots[str(t)] = (dict(png=str(pth.relative_to(ROOT)), **png_stats(str(pth), step=8)) if pth.exists() else None)
        rec = {"speaker": spk, "floor": a.floor, "status": status, "rc": rc, "secs": round(dt, 1),
               "cmd": " ".join(f'"{c}"' if " " in c else c for c in cmd).replace(str(ROOT) + os.sep, ""),
               "stderr": stderr, "shots": shots}
        with open(a.out, "a", encoding="utf-8") as fh:
            fh.write(json.dumps(rec) + "\n")
        print(f"{spk}: {status} rc={rc} {dt:.1f}s stderr {len(stderr.splitlines())} lines", flush=True)


if __name__ == "__main__":
    main()
