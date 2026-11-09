#!/usr/bin/env python3
"""Run nine baseline-v2 VM measurements; never creates Git commits/tags.

python3 experiments/priority/run-nine-tests.py --check  # local checks, no VM calls
python3 experiments/priority/run-nine-tests.py          # starts the full batch
"""
import collections
import csv
import datetime as dt
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import signal
import statistics
import subprocess
import sys
import tarfile
import time

PROJECT = Path(__file__).resolve().parents[2]
RESULTS = PROJECT / "experiments/priority/results"
REMOTE = "/home/vagrant/os-challenge-common"
BINARIES = {"baseline_v2": "bin/server-baseline",
            "priorityQueue_cache_v2": "bin/priority/server-priorityQueue",
            "priorityChunked_cache_v2": "bin/priority/server-priorityChunked"}
PLAN = [(v, seed) for seed, order in (
    (1, ("baseline_v2", "priorityQueue_cache_v2", "priorityChunked_cache_v2")),
    (2, ("priorityQueue_cache_v2", "priorityChunked_cache_v2", "baseline_v2")),
    (3, ("priorityChunked_cache_v2", "baseline_v2", "priorityQueue_cache_v2"))) for v in order]
SETTINGS = dict(TOTAL=500, START=0, DIFFICULTY=30000000,
                REP_PROB_PERCENT=20, DELAY_US=600000, PRIO_LAMBDA=1.5)


def now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def stem(variant, seed):
    return f"{variant}_t500_d600000_s{seed}_r1"


def validate_result(path):
    text = path.read_text()
    rows = [tuple(map(int, m)) for m in re.findall(
        r"^\[([01]) (\d+) (\d+) (\d+)\] (\d+)$", text, re.M)]
    result = re.search(r"^Results: ([\d.]+) (\d+)$", text, re.M)
    if len(rows) != 500 or not result:
        raise RuntimeError(f"Incomplete measurement: {path.name}")
    if float(result[1]) != 100 or any(r[0] != 1 or r[1] != r[2] for r in rows):
        raise RuntimeError(f"Incorrect answers: {path.name}")
    score = int(result[2])
    if abs(sum(r[3] * r[4] for r in rows) // 500 - score) > 1:
        raise RuntimeError(f"Score does not match raw answers: {path.name}")
    return score, collections.Counter((r[1], r[3]) for r in rows)


def write_batch_summary(batch, measurements):
    with (batch / "batch-summary.csv").open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["variant", "runs", "min_reliability", "median_score",
                         "min_score", "max_score", "median_elapsed_seconds"])
        for variant in BINARIES:
            rows = [m for m in measurements if m["variant"] == variant]
            if rows:
                scores = [m["score"] for m in rows]
                writer.writerow([variant, len(rows), min(m["reliability_percent"] for m in rows),
                                 int(statistics.median(scores)), min(scores), max(scores),
                                 statistics.median(m["elapsed_seconds"] for m in rows)])


def ssh(machine, command, timeout=60):
    env = dict(os.environ, VAGRANT_CHECKPOINT_DISABLE="1")
    p = subprocess.run(["vagrant", "ssh", machine, "-c", command],
                       cwd=PROJECT / "arm64", env=env, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                       timeout=timeout)
    if p.returncode:
        raise RuntimeError(f"{machine}: {p.stderr.strip()} {p.stdout.strip()}")
    return p.stdout.strip()


def remote_python(machine, code, data=None):
    command = "python3 -c " + shlex.quote(code)
    if data is not None:
        command += " " + shlex.quote(json.dumps(data))
    return ssh(machine, command)


def launch(machine, argv, log, cwd, env=None):
    return int(remote_python(machine, """
import json,os,subprocess,sys
d=json.loads(sys.argv[1])
with open(d['log'],'ab',buffering=0) as f:
 p=subprocess.Popen(d['argv'],cwd=d['cwd'],env=dict(os.environ,**d['env']),
   stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
print(p.pid)
""", dict(argv=argv, log=log, cwd=cwd, env=env or {})))


def stop_owned(machine, pid, token):
    if pid is None:
        return
    remote_python(machine, """
import json,os,signal,sys,time
d=json.loads(sys.argv[1]); pid=d['pid']
try:
 if open(f'/proc/{pid}/stat').read().split()[2]=='Z': sys.exit(0)
 cmd=open(f'/proc/{pid}/cmdline','rb').read().replace(b'\\0',b' ').decode()
except FileNotFoundError:
 sys.exit(0)
if d['token'] not in cmd or os.getpgid(pid)!=pid:
 raise RuntimeError('Refusing to stop an unrelated process')
os.killpg(pid,signal.SIGTERM)
for _ in range(30):
 time.sleep(0.1)
 try:
  if open(f'/proc/{pid}/stat').read().split()[2]=='Z': break
 except FileNotFoundError: break
else:
 os.killpg(pid,signal.SIGKILL)
""", dict(pid=pid, token=token))


def preflight():
    # Abort rather than interrupt a user test already in progress.
    clients = ssh("client", "pgrep -a -x client || true")
    if clients:
        raise RuntimeError("A client test is already running: " + clients)
    cpus = int(ssh("server", "getconf _NPROCESSORS_ONLN"))
    if cpus != 2:
        raise RuntimeError(f"Expected the original 2-CPU setup; found {cpus}")
    remote_python("server", """
import json,os,re,signal,subprocess,sys,time
d=json.loads(sys.argv[1])
active=subprocess.check_output(['ss','-Htn','state','established','sport = :5003'],text=True)
if active.strip(): raise RuntimeError('Server still has active client connections')
listeners=subprocess.check_output(['ss','-H','-ltnp','sport = :5003'],text=True)
pids=set(map(int,re.findall(r'pid=(\\d+)',listeners)))
if listeners.strip() and len(pids)!=1: raise RuntimeError('Unknown listener on port 5003')
for pid in pids:
 argv=open(f'/proc/{pid}/cmdline','rb').read().split(b'\\0')[0].decode()
 cwd=os.readlink(f'/proc/{pid}/cwd')
 exe=os.path.realpath(os.path.join(cwd,argv))
 allowed={os.path.realpath(os.path.join(d['project'],p)) for p in d['allowed']}
 if exe not in allowed: raise RuntimeError('Refusing to stop an unrelated server: '+exe)
 os.kill(pid,signal.SIGINT)
 for _ in range(50):
  if not subprocess.check_output(['ss','-H','-ltn','sport = :5003'],text=True).strip(): break
  time.sleep(0.1)
 else: raise RuntimeError('Existing server did not release port 5003')
""", dict(project=REMOTE, allowed=["server", *BINARIES.values()]))
    return cpus


def run_batch():
    # Check all output names before stopping a server or creating measurements.
    for variant, seed in PLAN:
        for suffix in (".txt", ".time", ".metadata.json"):
            if (RESULTS / (stem(variant, seed) + suffix)).exists():
                raise RuntimeError("Refusing to overwrite " + stem(variant, seed) + suffix)

    batch = RESULTS / ("batch-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%d-%H%M%SZ"))
    batch.mkdir()
    remote_batch = REMOTE + "/" + str(batch.relative_to(PROJECT))
    state = dict(status="preparing", started_at=now(), completed=0, total=9,
                 settings=SETTINGS, workers=2, measurements=[], batch_directory=str(batch))

    def save():
        temp = batch / "status.tmp"
        temp.write_text(json.dumps(state, indent=2) + "\n")
        temp.replace(batch / "status.json")

    def log(message):
        line = f"{now()} {message}"
        print(line, flush=True)
        with (batch / "batch.log").open("a") as f:
            f.write(line + "\n")

    save()
    (batch / "plan.json").write_text(json.dumps([
        dict(number=i, variant=v, seed=s, run=1, binary=BINARIES[v])
        for i, (v, s) in enumerate(PLAN, 1)], indent=2) + "\n")
    server_pid = client_pid = None
    server_token = client_token = ""
    try:
        state["server_online_cpus"] = preflight()
        # Freeze the actual build inputs without relying on untracked files
        # being represented by the Git hash in the existing client logger.
        names = ["Makefile", "server.c", "src/server_helpers.c", "src/cache_c.c",
                 "experiments/priority/cache_c.c",
                 "experiments/priority/server_priorityQueue.c",
                 "experiments/priority/server_priorityChunked.c",
                 "experiments/run-experiment.sh", "get-bin-path.sh"]
        names += [str(p.relative_to(PROJECT)) for p in sorted((PROJECT / "include").glob("*.h"))]
        state["measured_commit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=PROJECT, text=True).strip()
        state["baseline_commit"] = subprocess.check_output(
            ["git", "rev-parse", "baseline-v2^{}"], cwd=PROJECT, text=True).strip()
        hashes = {}
        archive = batch / "sources.tar.gz"
        runner = None
        with tarfile.open(archive, "w:gz") as tar:
            for name in names:
                data = (PROJECT / name).read_bytes()
                hashes[name] = hashlib.sha256(data).hexdigest()
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o755 if name.endswith(".sh") else 0o644
                tar.addfile(info, io.BytesIO(data))
                if name == "experiments/run-experiment.sh":
                    runner = data
        (batch / "run-experiment.sh").write_bytes(runner)
        (batch / "source-hashes.json").write_text(json.dumps(hashes, indent=2) + "\n")
        state["source_archive_sha256"] = hashlib.sha256(archive.read_bytes()).hexdigest()
        state["source_archive"] = str(archive)
        build = ssh("server", "mktemp -d /tmp/os-priority-batch.XXXXXX")
        if not re.fullmatch(r"/tmp/os-priority-batch\.[A-Za-z0-9]+", build):
            raise RuntimeError("Unexpected temporary build directory")
        state["build_directory"] = build
        output = ssh("server", f"tar -xzf {shlex.quote(remote_batch+'/sources.tar.gz')} -C {shlex.quote(build)} && "
                     f"make -C {shlex.quote(build)} experiments EXP=priority DEBUG=", timeout=120)
        (batch / "build.log").write_text(output + "\n")
        sums = ssh("server", "sha256sum " + " ".join(shlex.quote(build+"/"+p) for p in BINARIES.values()))
        state["binary_sha256"] = {line.split()[1]: line.split()[0] for line in sums.splitlines()}
        state["client_sha256"] = ssh("client", "sha256sum " + shlex.quote(REMOTE+"/arm64/bin/linux/client")).split()[0]
        state["compiler"] = ssh("server", "cc --version").splitlines()[0]
        save()
        expected = {}

        for index, (variant, seed) in enumerate(PLAN, 1):
            name = stem(variant, seed)
            raw = RESULTS / (name + ".txt")
            timer = RESULTS / (name + ".time")
            exit_file = batch / (name + ".exit")
            wrapper = batch / (name + ".sh")
            env = dict(SETTINGS, EXP="priority", VARIANT=variant, SEED=seed, RUN=1)
            env_text = " ".join(shlex.quote(f"{k}={v}") for k, v in env.items())
            # The finite wrapper records completion even when the client fails.
            wrapper.write_text("#!/bin/sh\ncd " + shlex.quote(REMOTE) + " || exit 1\n"
                + "/usr/bin/time -p -o " + shlex.quote(REMOTE+"/"+str(timer.relative_to(PROJECT)))
                + " env " + env_text + " sh " + shlex.quote(remote_batch+"/run-experiment.sh")
                + "\nresult=$?\nprintf '%s\\n' \"$result\" > " + shlex.quote(remote_batch+"/"+exit_file.name)
                + "\nexit \"$result\"\n")
            server_token = build + "/" + BINARIES[variant]
            server_log = batch / (name + ".server.log")
            server_pid = launch("server", [server_token, "5003"],
                                remote_batch+"/"+server_log.name, build, {"POOL_THREADS": "2"})
            for _ in range(50):
                if server_log.exists() and "Listening on port 5003" in server_log.read_text():
                    break
                time.sleep(0.1)
            else:
                raise RuntimeError("Server did not start: " + server_log.read_text())
            if "2 workers" not in server_log.read_text():
                raise RuntimeError("Unexpected worker count")
            listener = ssh("server", "ss -H -ltnp 'sport = :5003'")
            if f"pid={server_pid}," not in listener:
                raise RuntimeError("The expected server does not own port 5003")

            client_token = remote_batch + "/" + wrapper.name
            client_pid = launch("client", ["/bin/sh", client_token],
                                remote_batch+"/"+name+".client.log", REMOTE)
            started = time.monotonic()
            state.update(status="running", current=dict(number=index, variant=variant,
                         seed=seed, answers_logged=0, raw_log=str(raw)))
            save()
            log(f"START {index}/9: {variant}, seed={seed}, 2 workers")
            last_report = 0
            while not exit_file.exists():
                elapsed = int(time.monotonic() - started)
                if elapsed > 3600:
                    raise RuntimeError("Measurement exceeded one hour: " + name)
                text = raw.read_text() if raw.exists() else ""
                answers = len(re.findall(r"^\[[01] ", text, re.M))
                state["current"].update(answers_logged=answers, elapsed_seconds=elapsed)
                save()
                if elapsed - last_report >= 60:
                    log(f"RUN {index}/9: {answers}/500 logged, {elapsed}s")
                    last_report = elapsed
                time.sleep(5)
            if exit_file.read_text().strip() != "0":
                raise RuntimeError("Client command failed: " + name)
            score, identities = validate_result(raw)
            if seed in expected and identities != expected[seed]:
                raise RuntimeError("Different requests for the same seed: " + name)
            expected.setdefault(seed, identities)
            match = re.search(r"^real ([\d.]+)$", timer.read_text(), re.M)
            if not match:
                raise RuntimeError("Missing elapsed time: " + name)
            metadata = dict(variant=variant, seed=seed, run=1, settings=SETTINGS,
                reliability_percent=100, score=score, response_count=500,
                elapsed_seconds=float(match[1]), workers=2, server_online_cpus=2,
                source_archive=str(archive), source_archive_sha256=state["source_archive_sha256"],
                server_binary_sha256=state["binary_sha256"][server_token],
                client_binary_sha256=state["client_sha256"], compiler=state["compiler"],
                measured_commit=state["measured_commit"], baseline_commit=state["baseline_commit"],
                note="Actual build inputs are archived; the raw Git header may describe a later working tree.")
            sidecar = RESULTS / (name + ".metadata.json")
            sidecar.write_text(json.dumps(metadata, indent=2) + "\n")
            stop_owned("client", client_pid, client_token)
            client_pid = None
            stop_owned("server", server_pid, server_token)
            server_pid = None
            state["measurements"].append(metadata)
            state["completed"] = index
            write_batch_summary(batch, state["measurements"])
            save()
            summary = subprocess.run([sys.executable, str(PROJECT / "experiments/summarize.py"), "priority"],
                                     cwd=PROJECT, text=True, capture_output=True, check=True)
            (batch / "summary.log").write_text(summary.stdout)
            log(f"DONE {index}/9: {variant}, seed={seed}, score={score}, real={match[1]}s")
        state.update(status="completed", finished_at=now())
        state.pop("current", None)
        save()
        log("All nine measurements completed and validated.")
    except BaseException as exc:
        state.update(status="failed", error=f"{type(exc).__name__}: {exc}", finished_at=now())
        save()
        log("STOPPED: " + state["error"])
        raise
    finally:
        for machine, pid, token in (("client", client_pid, client_token), ("server", server_pid, server_token)):
            try:
                stop_owned(machine, pid, token)
            except Exception as exc:
                log(f"Cleanup requires attention on {machine}: {exc}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--check"]:
        assert len(PLAN) == len(set(PLAN)) == 9
        assert collections.Counter(v for v, s in PLAN) == {v: 3 for v in BINARIES}
        assert collections.Counter(s for v, s in PLAN) == {1: 3, 2: 3, 3: 3}
        for variant in ("fifo", "priorityQueue", "priorityChunked"):
            score, rows = validate_result(RESULTS / (stem(variant, 5041) + ".txt"))
            assert score > 0 and sum(rows.values()) == 500
        print("Batch checks passed: nine unique runs and all three existing logs validate; no VM calls.")
    elif sys.argv[1:]:
        raise SystemExit("Usage: run-nine-tests.py [--check]")
    else:
        def interrupted(signum, frame):
            raise KeyboardInterrupt("Batch interrupted")
        signal.signal(signal.SIGTERM, interrupted)
        # Keep this finite Mac job awake; the assertion ends with this process.
        subprocess.Popen(["/usr/bin/caffeinate", "-i", "-w", str(os.getpid())],
                         stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
        run_batch()
