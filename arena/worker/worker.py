#!/usr/bin/env python3
"""FairAI training worker for a second PC (Linux, e.g. Ubuntu under WSL2).

Loop:
  1. fetch branch `fairai` of the fork; if it changed, check it out and rebuild (incremental)
  2. look for job files in arena/jobs/*.json that have no result yet
  3. run the next job with a frozen copy of the build, uploading progress every few minutes
  4. publish results (small files only, no logs) to branch `fairai-results` of the same fork

The worker only ever runs code from the fork's `fairai` branch.

Job file (arena/jobs/<id>.json):
  {"script": "run_tournament.py", "args": ["play", "--client", "{client}", ..., "--out", "{out}", "--jobs", "{jobs}"],
   "jobs": 6, "note": "what this is for"}
Placeholders: {client} vcmiclient of the frozen build, {out} output directory, {jobs} parallel games.

Setup: arena/worker/setup-wsl.sh. Start: python3 ~/fairai/vcmi/arena/worker/worker.py
"""
import datetime
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(os.environ.get("FAIRAI_ROOT", Path.home() / "fairai"))
SRC = ROOT / "vcmi"
BUILD = ROOT / "build"
SNAPS = ROOT / "snap"
RUNS = ROOT / "runs"
RESULTS = ROOT / "results"
BRANCH = "fairai"
RESULTS_BRANCH = "fairai-results"
POLL_SECONDS = 120
SYNC_SECONDS = 600
DEFAULT_JOBS = max(1, (os.cpu_count() or 2) // 2)
KEEP_SUFFIXES = {".json", ".jsonl", ".md", ".txt"}


def log(msg: str) -> None:
    line = f"[{datetime.datetime.now():%Y-%m-%d %H:%M:%S}] {msg}"
    print(line, flush=True)
    with open(ROOT / "worker.log", "a") as f:
        f.write(line + "\n")


def run(cmd, cwd=None, check=True, quiet=False):
    if not quiet:
        log("$ " + " ".join(str(c) for c in cmd))
    return subprocess.run([str(c) for c in cmd], cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE if quiet else None, stderr=subprocess.STDOUT if quiet else None)


def git(args, cwd, check=True):
    return subprocess.run(["git", *args], cwd=cwd, check=check, text=True, capture_output=True)


# ----------------------------------------------------------------------------- source and build

def update_source() -> str:
    git(["fetch", "--depth", "50", "origin", BRANCH], SRC)
    target = git(["rev-parse", "FETCH_HEAD"], SRC).stdout.strip()
    current = git(["rev-parse", "HEAD"], SRC).stdout.strip()
    if target != current:
        log(f"new code {target[:10]} (was {current[:10]})")
        git(["checkout", "--force", "--detach", target], SRC)
        git(["submodule", "update", "--init", "--depth", "1", "AI/FuzzyLite"], SRC, check=False)
    return target


def frozen_build(commit: str) -> Path:
    snap = SNAPS / commit[:10]
    if (snap / "vcmiclient").exists():
        return snap
    log("building ...")
    run(["cmake", "--build", BUILD, f"-j{os.cpu_count() or 2}"])
    SNAPS.mkdir(parents=True, exist_ok=True)
    # hard-linked copy: later rebuilds replace files and leave this copy intact
    run(["cp", "-al", BUILD / "bin", snap])
    for old in sorted(SNAPS.iterdir(), key=lambda p: p.stat().st_mtime)[:-3]:
        shutil.rmtree(old, ignore_errors=True)
    return snap


# ----------------------------------------------------------------------------- results branch

def prepare_results() -> None:
    if (RESULTS / ".git").exists():
        git(["fetch", "--depth", "1", "origin", RESULTS_BRANCH], RESULTS, check=False)
        git(["reset", "--hard", f"origin/{RESULTS_BRANCH}"], RESULTS, check=False)
        return
    url = git(["remote", "get-url", "origin"], SRC).stdout.strip()
    RESULTS.mkdir(parents=True, exist_ok=True)
    exists = git(["ls-remote", "--heads", url, RESULTS_BRANCH], SRC).stdout.strip()
    if exists:
        run(["git", "clone", "--depth", "1", "--branch", RESULTS_BRANCH, "--single-branch", url, RESULTS])
    else:
        git(["init", "-q"], RESULTS)
        git(["checkout", "-q", "--orphan", RESULTS_BRANCH], RESULTS)
        git(["remote", "add", "origin", url], RESULTS)
        (RESULTS / "README.md").write_text("# FairAI training results\n\nWritten by arena/worker/worker.py.\n")
        git(["add", "README.md"], RESULTS)
        git(["commit", "-q", "-m", "results branch"], RESULTS)
        git(["push", "-q", "-u", "origin", RESULTS_BRANCH], RESULTS)


def publish(message: str) -> None:
    git(["add", "-A"], RESULTS)
    if not git(["status", "--porcelain"], RESULTS).stdout.strip():
        return
    git(["commit", "-q", "-m", message], RESULTS)
    for _ in range(3):
        if git(["push", "-q", "origin", f"HEAD:{RESULTS_BRANCH}"], RESULTS, check=False).returncode == 0:
            return
        git(["pull", "-q", "--rebase", "origin", RESULTS_BRANCH], RESULTS, check=False)
    log("WARNING: could not upload results (network or token?) - will retry with the next update")


def copy_small_files(src: Path, dst: Path) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    if not src.exists():
        return
    for path in src.rglob("*"):
        if path.is_file() and path.suffix in KEEP_SUFFIXES and "logs_" not in str(path.relative_to(src)) \
                and path.stat().st_size < 5_000_000:
            target = dst / path.relative_to(src)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


# ----------------------------------------------------------------------------- jobs

def pending_jobs():
    jobs_dir = SRC / "arena" / "jobs"
    if not jobs_dir.exists():
        return []
    result = []
    for path in sorted(jobs_dir.glob("*.json")):
        status = RESULTS / path.stem / "status.json"
        if status.exists():
            continue
        result.append(path)
    return result


def run_job(path: Path, commit: str) -> None:
    job_id = path.stem
    job = json.loads(path.read_text())
    snap = frozen_build(commit)
    out = RUNS / job_id
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    jobs = str(job.get("jobs", DEFAULT_JOBS))
    args = [a.replace("{client}", str(snap / "vcmiclient")).replace("{out}", str(out)).replace("{jobs}", jobs)
            for a in job.get("args", [])]
    script = SRC / "arena" / job["script"]
    status = {"job": job_id, "state": "running", "commit": commit, "host": socket.gethostname(),
              "cpus": os.cpu_count(), "started": datetime.datetime.now().isoformat(timespec="seconds"),
              "note": job.get("note", "")}
    result_dir = RESULTS / job_id
    result_dir.mkdir(parents=True, exist_ok=True)
    (result_dir / "status.json").write_text(json.dumps(status, indent=1))
    publish(f"{job_id}: started")

    log(f"job {job_id}: {script.name} {' '.join(args)}")
    started = time.time()
    last_sync = started
    with open(out / "job-output.txt", "w") as output:
        proc = subprocess.Popen([sys.executable, str(script), *args], cwd=out, stdout=output, stderr=subprocess.STDOUT)
        while proc.poll() is None:
            time.sleep(30)
            if time.time() - last_sync > SYNC_SECONDS:
                copy_small_files(out, result_dir)
                publish(f"{job_id}: progress")
                last_sync = time.time()
    status["state"] = "done" if proc.returncode == 0 else "failed"
    status["returncode"] = proc.returncode
    status["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    status["minutes"] = round((time.time() - started) / 60, 1)
    copy_small_files(out, result_dir)
    (result_dir / "status.json").write_text(json.dumps(status, indent=1))
    publish(f"{job_id}: {status['state']}")
    log(f"job {job_id}: {status['state']} after {status['minutes']} min")


def main() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    log(f"FairAI worker on {socket.gethostname()} ({os.cpu_count()} CPUs), default {DEFAULT_JOBS} parallel games")
    prepare_results()
    while True:
        try:
            commit = update_source()
            prepare_results()
            jobs = pending_jobs()
            if jobs:
                run_job(jobs[0], commit)
                continue
        except Exception as e:  # keep the worker alive (network hiccups, build errors)
            log(f"ERROR: {e}")
        time.sleep(POLL_SECONDS)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        log("stopped")
        sys.exit(0)
