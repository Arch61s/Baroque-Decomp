#!/usr/bin/env python3
"""Floor sweep driver for Baroque-Native (read-only validation).

Runs `Baroque-Native.exe --floor N --seed S --shot <png> T` for every floor id, strictly one process at a time,
hidden window (--shot), per-run timeout. Appends one JSON line per run to the results file as it goes, so an
interrupted sweep keeps everything done so far (re-running skips (floor, seed) pairs already in the file).

usage: python tools/sweep/sweep.py [--exe PATH] [--out results.jsonl] [--ticks 90] [--seeds 1,2]
                                   [--floors all|100-140,301,...] [--png-dir DIR] [--timeout 60]
"""
import argparse, json, os, re, subprocess, sys, threading, time, queue
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from pngstat import png_stats  # noqa: E402


def all_floor_ids():
    """Every floor id that has an FDT (DATA/MAP/FLOOR/Fnnn.FDT in the root tree or a pack)."""
    ids = set()
    assets = ROOT / "work" / "assets"
    for p in assets.rglob("F*.FDT"):
        m = re.fullmatch(r"F(\d{3})\.FDT", p.name, re.I)
        if m:
            ids.add(int(m.group(1)))
    return sorted(ids)


def parse_floors(spec):
    if spec == "all":
        return all_floor_ids()
    out = []
    for part in spec.split(","):
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a, 0), int(b, 0) + 1))
        else:
            out.append(int(part, 0))
    have = set(all_floor_ids())
    return [f for f in out if f in have]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(ROOT / "build/agent_sweep/Release/Baroque-Native.exe"))
    ap.add_argument("--out", default=str(ROOT / "tools/sweep/results/sweep.jsonl"))
    ap.add_argument("--png-dir", default=os.environ.get("SWEEP_PNG_DIR", str(ROOT / "tools/sweep/results/png")))
    ap.add_argument("--ticks", type=int, default=90)
    ap.add_argument("--seeds", default="1")
    ap.add_argument("--floors", default="all")
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--extra", default="", help="extra args appended to every run (space separated)")
    ap.add_argument("--tag", default="", help="label stored in each record (e.g. 'walk')")
    ap.add_argument("--env", action="append", default=[], help="NAME=VALUE for the game process (e.g. BAROQUE_DEBUG_AI=1)")
    a = ap.parse_args()

    out = Path(a.out); out.parent.mkdir(parents=True, exist_ok=True)
    pngdir = Path(a.png_dir); pngdir.mkdir(parents=True, exist_ok=True)
    done = set()
    if out.exists():
        for line in out.read_text(encoding="utf-8", errors="replace").splitlines():
            try:
                r = json.loads(line); done.add((r["floor"], r["seed"], r.get("ticks"), r.get("tag", "")))
            except Exception:
                pass
    seeds = [int(s, 0) for s in a.seeds.split(",")]
    floors = parse_floors(a.floors)
    jobs = [(f, s) for s in seeds for f in floors if (f, s, a.ticks, a.tag) not in done]
    print(f"sweep: {len(jobs)} runs ({len(floors)} floors x seeds {seeds}), {len(done)} already done", flush=True)

    # PNG statistics run on a worker thread while the next game process runs (still one game process at a time).
    q = queue.Queue(); lock = threading.Lock()

    def worker():
        while True:
            item = q.get()
            if item is None:
                return
            rec = item
            try:
                if rec["png"] and Path(rec["png"]).exists():
                    rec["png_stats"] = png_stats(rec["png"])
            except Exception as e:  # pragma: no cover
                rec["png_stats"] = {"error": str(e)}
            with lock, out.open("a", encoding="utf-8") as fh:
                fh.write(json.dumps(rec) + "\n")
            q.task_done()

    th = threading.Thread(target=worker, daemon=True); th.start()
    env = dict(os.environ)
    for kv in a.env:
        k, _, v = kv.partition("="); env[k] = v
    for i, (floor, seed) in enumerate(jobs):
        png = pngdir / f"f{floor:03d}_s{seed}_t{a.ticks}{('_' + a.tag) if a.tag else ''}.png"
        if png.exists():
            png.unlink()
        cmd = [a.exe, "--floor", str(floor), "--seed", str(seed), "--shot", str(png), str(a.ticks)]
        if a.extra:
            cmd += a.extra.split()
        t0 = time.time(); status = "ok"; rc = None
        try:
            p = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, timeout=a.timeout, env=env,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            rc = p.returncode; so, se = p.stdout, p.stderr
            if rc != 0:
                status = "crash"
        except subprocess.TimeoutExpired as e:
            status = "timeout"; so, se = e.stdout or b"", e.stderr or b""   # subprocess.run already killed our child
        dt = time.time() - t0
        if status == "ok" and not png.exists():
            status = "no_png"
        se_text = se.decode("utf-8", "replace")
        if a.env:   # BAROQUE_DEBUG_AI trace: keep PLAYER lines and characters below y -1000 (fell out of the map)
            kept = []; ai_lines = 0
            for ln in se_text.splitlines():
                if ln.startswith("ai t=") and " PLAYER " not in ln:
                    ai_lines += 1
                    m = re.search(r"pos=\((-?\d+),(-?\d+),(-?\d+)\)", ln)
                    if m and int(m.group(2)) < -1000:
                        kept.append("ai-fell " + ln)
                    continue
                kept.append(ln)
            se_text = "\n".join(kept)
        rec = {"floor": floor, "seed": seed, "ticks": a.ticks, "tag": a.tag, "status": status, "rc": rc,
               "secs": round(dt, 2), "cmd": " ".join(cmd),
               "stderr": se_text, "stdout": so.decode("utf-8", "replace"),
               "png": str(png) if png.exists() else None}
        q.put(rec)
        print(f"[{i + 1}/{len(jobs)}] floor {floor} seed {seed}: {status} rc={rc} {dt:.1f}s "
              f"stderr {len(rec['stderr'].splitlines())} lines", flush=True)
    q.put(None); th.join()
    print("sweep: done", flush=True)


if __name__ == "__main__":
    main()
