#!/usr/bin/env python3
"""FairAI training worker for a second PC (Linux, e.g. Ubuntu under WSL2).

Loop:
  1. fetch branch `fairai` of the fork; if it changed, check it out and rebuild (incremental)
  2. look for job files in arena/jobs/*.json that have no result yet
  3. run the next job with a frozen copy of the build, uploading progress every few minutes
  4. publish results (small files only, no logs) to branch `fairai-results` of the same fork

The worker only ever runs code from the fork's `fairai` branch. Two exceptions, each allowed by the
owner of the PC and pinned in the fork so that nothing else can arrive: mods (mods.json: Horn of the
Abyss, the MMAI models) and the ONNX Runtime library MMAI needs (tools.json). They are fetched without
the stored token, checked against the pinned content and kept inside the worker's own folder.

Job file (arena/jobs/<id>.json):
  {"script": "run_tournament.py", "args": ["play", "--client", "{client}", ..., "--out", "{out}", "--jobs", "{jobs}"],
   "jobs": 6, "note": "what this is for"}
Placeholders: {client} vcmiclient of the frozen build, {out} output directory, {jobs} parallel games.

Setup: arena/worker/setup-wsl.sh. Start: python3 ~/fairai/vcmi/arena/worker/worker.py
"""
import datetime
import hashlib
import json
import os
import shutil
import signal
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
SELF = Path(__file__).resolve()
SELF_CODE = SELF.read_bytes()


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


# git must never wait for a password on the terminal (an expired token would block the worker)
GIT_ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_ASKPASS="true")


def git(args, cwd, check=True):
    try:
        return subprocess.run(["git", *args], cwd=cwd, check=check, text=True, capture_output=True,
                              env=GIT_ENV, stdin=subprocess.DEVNULL, timeout=300)
    except subprocess.TimeoutExpired:
        if check:
            raise
        return subprocess.CompletedProcess(["git", *args], 1, "", "timeout")


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


def frozen_build(commit: str, needs=()) -> Path:
    snap = SNAPS / commit[:10]
    if (snap / "vcmiclient").exists() and all((snap / n).exists() for n in needs):
        return snap
    log("building ...")
    run(["cmake", "--build", BUILD, f"-j{os.cpu_count() or 2}"])
    SNAPS.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(snap, ignore_errors=True)  # a copy of this commit made before the build could do what the job needs
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
        pushed = git(["push", "-q", "origin", f"HEAD:{RESULTS_BRANCH}"], RESULTS, check=False)
        if pushed.returncode == 0:
            return
        git(["pull", "-q", "--rebase", "origin", RESULTS_BRANCH], RESULTS, check=False)
    log("WARNING: could not upload results - token expired or deleted? Run: bash ~/fairai/vcmi/arena/worker/set-token.sh")
    log("         git said: " + (pushed.stderr or "").strip().splitlines()[-1:][0] if (pushed.stderr or "").strip() else "")


def copy_small_files(src: Path, dst: Path) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    if not src.exists():
        return
    for path in list(src.rglob("*")):
        try:
            if path.is_file() and path.suffix in KEEP_SUFFIXES and "logs_" not in str(path.relative_to(src)) \
                    and path.stat().st_size < 5_000_000:
                target = dst / path.relative_to(src)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
        except OSError:
            pass  # file written or removed while we copy: next sync


# ----------------------------------------------------------------------------- jobs

def pending_jobs():
    """Jobs without a result yet. A job this host left 'running' (worker stopped, PC restarted) runs again:
    this worker runs one job at a time, so nothing of this host can be running while we look."""
    jobs_dir = SRC / "arena" / "jobs"
    if not jobs_dir.exists():
        return []
    result = []
    for path in sorted(jobs_dir.glob("*.json")):
        status_file = RESULTS / path.stem / "status.json"
        if status_file.exists():
            try:
                status = json.loads(status_file.read_text())
            except (OSError, ValueError):
                continue
            if not (status.get("state") == "running" and status.get("host") == socket.gethostname()):
                continue
            log(f"job {path.stem} was interrupted - running it again")
        result.append(path)
    return result


def vcmi_mods_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "vcmi" / "Mods"


def fetch(url: str, dest: Path, limit: int = 400_000_000) -> None:
    """Plain HTTPS download: no token, no credentials of any kind. Only URLs pinned in the fork get here."""
    import urllib.request
    if not url.startswith("https://github.com/"):
        raise RuntimeError(f"refusing to download from {url}")
    request = urllib.request.Request(url, headers={"User-Agent": "fairai-worker"})
    total = 0
    with urllib.request.urlopen(request, timeout=120) as response, open(dest, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            total += len(chunk)
            if total > limit:
                raise RuntimeError(f"{url}: larger than {limit} bytes")
            out.write(chunk)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative(name: str) -> Path:
    """A path from an archive, to be written below a folder of ours: nothing absolute, nothing that climbs out."""
    parts = [part for part in name.replace("\\", "/").split("/") if part not in ("", ".")]
    if not parts or name.startswith("/") or any(part == ".." or ":" in part for part in parts):
        raise RuntimeError(f"archive holds an unsafe path: {name!r}")
    return Path(*parts)


def install_mod_archive(name: str, spec: dict, target: Path) -> None:
    """A mod published as one archive (what the VCMI launcher downloads), pinned by its SHA-256."""
    import zipfile
    tmp = vcmi_mods_dir() / f".{name}-download"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    try:
        archive = tmp / "archive.zip"
        fetch(spec["archive"], archive)
        got = sha256_file(archive)
        if got != spec["sha256"]:
            raise RuntimeError(f"mod {name}: archive has SHA-256 {got}, pinned is {spec['sha256']}")
        prefix = spec["folder"].strip("/") + "/"
        out = tmp / "mod"
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                if info.is_dir() or not info.filename.startswith(prefix):
                    continue
                dest = out / safe_relative(info.filename[len(prefix):])
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(z.read(info))
        if not (out / "mod.json").exists():
            raise RuntimeError(f"mod {name}: no {prefix}mod.json in the archive")
        (out / ".fairai-commit").write_text(spec["sha256"] + "\n")
        shutil.rmtree(target, ignore_errors=True)
        out.rename(target)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def ensure_mods(names) -> None:
    """Install the mods a job needs, exactly as pinned in mods.json next to this file (public
    repository and commit, or archive and checksum). The stored token is never offered to those places."""
    if not names:
        return
    pinned = json.loads((SRC / "arena" / "worker" / "mods.json").read_text())
    for name in names:
        spec = pinned[name]  # a job cannot ask for anything that is not pinned in the fork
        target = vcmi_mods_dir() / name
        marker = target / ".fairai-commit"
        if marker.exists() and marker.read_text().strip() == spec.get("commit", spec.get("sha256")):
            continue
        if "archive" in spec:
            log(f"installing mod {name} ({spec['archive']}, SHA-256 {spec['sha256'][:12]})")
            vcmi_mods_dir().mkdir(parents=True, exist_ok=True)
            install_mod_archive(name, spec, target)
            log(f"mod {name} installed in {target}")
            continue
        log(f"installing mod {name} ({spec['url']} at {spec['commit'][:10]}), this may take a while")
        tmp = vcmi_mods_dir() / f".{name}-download"
        shutil.rmtree(tmp, ignore_errors=True)
        tmp.mkdir(parents=True)
        no_token = ["-c", "credential.helper="]
        steps = [["git", "init", "-q"],
                 ["git", *no_token, "fetch", "-q", "--depth", "1", spec["url"], spec["commit"]],
                 ["git", "checkout", "-q", "FETCH_HEAD"]]
        for step in steps:
            res = subprocess.run(step, cwd=tmp, text=True, capture_output=True, env=GIT_ENV, stdin=subprocess.DEVNULL, timeout=3600)
            if res.returncode != 0:
                shutil.rmtree(tmp, ignore_errors=True)
                raise RuntimeError(f"mod {name}: {' '.join(step[:4])} failed: {res.stderr.strip()[-300:]}")
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=tmp, text=True, capture_output=True).stdout.strip()
        if head != spec["commit"]:
            shutil.rmtree(tmp, ignore_errors=True)
            raise RuntimeError(f"mod {name}: got {head}, expected {spec['commit']}")
        shutil.rmtree(tmp / ".git", ignore_errors=True)  # half the disk space; the marker records the version
        (tmp / ".fairai-commit").write_text(spec["commit"] + "\n")
        shutil.rmtree(target, ignore_errors=True)
        tmp.rename(target)
        log(f"mod {name} installed in {target}")


def ensure_onnxruntime() -> Path:
    """The library MMAI computes its models with, from the release pinned in tools.json. Only the files
    named there are taken from the archive, each only if its content is the pinned one."""
    import tarfile
    spec = json.loads((SRC / "arena" / "worker" / "tools.json").read_text())["onnxruntime"]
    wanted = dict(spec["files"])
    pin = hashlib.sha256(json.dumps([spec["archive"], spec["files"], spec.get("links", {})], sort_keys=True).encode()).hexdigest()
    target = ROOT / "onnxruntime"
    marker = target / ".fairai-pin"
    if marker.exists() and marker.read_text().strip() == pin:
        return target
    log(f"installing ONNX Runtime ({spec['archive']})")
    tmp = ROOT / ".onnxruntime-download"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)
    try:
        archive = tmp / "archive.tgz"
        fetch(spec["archive"], archive)
        out = tmp / "root"
        with tarfile.open(archive) as tar:
            for member in tar:
                if not member.isfile():
                    continue
                rel = member.name.split("/", 1)[1] if "/" in member.name else member.name  # below the archive's top folder
                if rel not in wanted:
                    continue
                data = tar.extractfile(member).read()
                got = hashlib.sha256(data).hexdigest()
                if got != wanted[rel]:
                    raise RuntimeError(f"ONNX Runtime: {rel} has SHA-256 {got}, pinned is {wanted[rel]}")
                dest = out / safe_relative(rel)
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(data)
                if rel.startswith("lib/"):
                    dest.chmod(0o755)
                del wanted[rel]
        if wanted:
            raise RuntimeError("ONNX Runtime: the archive lacks " + ", ".join(sorted(wanted)))
        for link, to in spec.get("links", {}).items():
            os.symlink(to, out / safe_relative(link))
        (out / ".fairai-pin").write_text(pin + "\n")
        shutil.rmtree(target, ignore_errors=True)
        out.rename(target)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    log(f"ONNX Runtime installed in {target}")
    return target


def ensure_mmai_build(onnxruntime: Path) -> None:
    """Add the MMAI battle AI to the build as a plug-in. ENABLE_MMAI_DEFINE=OFF keeps the compiler flags of
    everything else as they are: only MMAI itself is compiled."""
    cache = BUILD / "CMakeCache.txt"
    text = cache.read_text(errors="replace") if cache.exists() else ""
    if "ENABLE_MMAI:BOOL=ON" in text and f"ONNXRUNTIME_ROOT:PATH={onnxruntime}\n" in text:
        return
    log("adding MMAI to the build")
    run(["cmake", "-S", SRC, "-B", BUILD, "-DENABLE_MMAI=ON", "-DENABLE_MMAI_DEFINE=OFF", f"-DONNXRUNTIME_ROOT={onnxruntime}",
         f"-DONNXRUNTIME_INCLUDE_DIR={onnxruntime / 'include'}", f"-DONNXRUNTIME_LIBRARY={onnxruntime / 'lib' / 'libonnxruntime.so'}"])


def job_withdrawn(job_id: str) -> bool:
    """A running job is stopped when its file disappears from the branch (checked at every sync).
    Only a clear answer counts: network trouble never cancels a job."""
    if git(["fetch", "--depth", "50", "origin", BRANCH], SRC, check=False).returncode != 0:
        return False
    res = git(["cat-file", "-e", f"FETCH_HEAD:arena/jobs/{job_id}.json"], SRC, check=False)
    if res.returncode == 0:
        return False
    # the branch itself must be readable, else the missing file means nothing
    return git(["cat-file", "-e", "FETCH_HEAD:arena/worker/worker.py"], SRC, check=False).returncode == 0


def run_job(path: Path, commit: str) -> None:
    job_id = path.stem
    job = json.loads(path.read_text())
    mods = job.get("mods", [])
    needs = []
    try:
        ensure_mods(mods)
        if "mmai" in mods:
            ensure_mmai_build(ensure_onnxruntime())
            needs.append("AI/libMMAI.so")
    except Exception as e:  # a download that does not match its pin, no network: this job cannot run - the next one can
        log(f"job {job_id}: cannot prepare what it needs: {e}")
        result_dir = RESULTS / job_id
        result_dir.mkdir(parents=True, exist_ok=True)
        (result_dir / "status.json").write_text(json.dumps({
            "job": job_id, "state": "failed", "commit": commit, "host": socket.gethostname(),
            "finished": datetime.datetime.now().isoformat(timespec="seconds"),
            "error": f"preparation failed: {e}"[:500], "note": job.get("note", "")}, indent=1))
        publish(f"{job_id}: failed (preparation)")
        return
    snap = frozen_build(commit, needs)
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
    cancelled = False
    with open(out / "job-output.txt", "w") as output:
        # own process group: a cancelled job takes its games down with it
        proc = subprocess.Popen([sys.executable, str(script), *args], cwd=out, stdout=output, stderr=subprocess.STDOUT,
                                start_new_session=True)
        while proc.poll() is None:
            time.sleep(30)
            if time.time() - last_sync > SYNC_SECONDS:
                copy_small_files(out, result_dir)
                publish(f"{job_id}: progress")
                last_sync = time.time()
                if job_withdrawn(job_id):
                    log(f"job {job_id}: its file was removed from the branch - stopping it")
                    cancelled = True
                    try:
                        os.killpg(proc.pid, signal.SIGTERM)
                        proc.wait(timeout=60)
                    except (OSError, subprocess.TimeoutExpired):
                        try:
                            os.killpg(proc.pid, signal.SIGKILL)
                        except OSError:
                            pass
                        proc.wait()
    status["state"] = "cancelled" if cancelled else ("done" if proc.returncode == 0 else "failed")
    status["returncode"] = proc.returncode
    status["finished"] = datetime.datetime.now().isoformat(timespec="seconds")
    status["minutes"] = round((time.time() - started) / 60, 1)
    copy_small_files(out, result_dir)
    (result_dir / "status.json").write_text(json.dumps(status, indent=1))
    publish(f"{job_id}: {status['state']}")
    log(f"job {job_id}: {status['state']} after {status['minutes']} min")


def restart_if_updated() -> None:
    """Between jobs: if the fetched code changed this script, continue with the new version."""
    try:
        if SELF.read_bytes() == SELF_CODE:
            return
    except OSError:
        return
    log("worker.py changed - restarting with the new version")
    os.execv(sys.executable, [sys.executable, str(SELF), *sys.argv[1:]])


def main() -> int:
    ROOT.mkdir(parents=True, exist_ok=True)
    log(f"FairAI worker on {socket.gethostname()} ({os.cpu_count()} CPUs), default {DEFAULT_JOBS} parallel games")
    prepare_results()
    while True:
        try:
            commit = update_source()
            restart_if_updated()
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
