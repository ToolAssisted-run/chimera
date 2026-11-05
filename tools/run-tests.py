#!/usr/bin/env python3
"""Runs the tests tools/export-tests.py packed, on the system they were built
for (the Windows CI jobs, x64 and arm64): each test's program, with its
arguments, folder and environment, the folder holding the build's libraries
first on PATH, and its timeout. Says PASS, FAIL or TIMEOUT for each, as
meson test does, and ends with the counts; exits 1 if any did not pass. A
test that is a Python script is run by the Python that runs this.

    python tools/run-tests.py <exported folder> [--skip <name>]... [--only <name>]...

--skip leaves a test out by its name, and the summary says it was left out
(a test that needs what the machine has not got, such as a graphics card).
The last 40 lines of a test's output are printed when it does not pass; the
whole of every test's output is kept in <exported folder>/logs/<name>.txt.
Its output and errors are one stream, in the order the program wrote them."""
import json
import os
import subprocess
import sys
import time

args = sys.argv[1:]
folder = os.path.abspath(args.pop(0))
skip, only = set(), set()
while args:
    flag, name = args.pop(0), args.pop(0)
    (skip if flag == "--skip" else only).add(name)

with open(os.path.join(folder, "tests.json")) as fh:
    plan = json.load(fh)


def real(value):
    if not isinstance(value, str):
        return value
    return sys.executable if value == "@PYTHON@" else value.replace("@ROOT@", folder)


build = real(plan["build"])
logs = os.path.join(folder, "logs")
os.makedirs(logs, exist_ok=True)
env_base = dict(os.environ)
env_base["PATH"] = os.pathsep.join([build] + [real(p) for p in plan.get("path", [])] + [env_base.get("PATH", "")])
env_base.update({k: real(v) for k, v in plan.get("env", {}).items()})
passed, failed, skipped = [], [], []
for t in plan["tests"]:
    name = t["name"]
    if (only and name not in only) or name in skip:
        skipped.append(name)
        continue
    cmd = [real(a) for a in t["cmd"]]
    env = dict(env_base)
    env.update({k: real(v) for k, v in t["env"].items()})
    start = time.monotonic()
    try:
        r = subprocess.run(cmd, cwd=real(t["workdir"]), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True, errors="replace", timeout=t["timeout"] or None)
        status = "PASS" if r.returncode == 0 else f"FAIL (exit {r.returncode:#x})" if r.returncode < 0 or r.returncode > 255 else f"FAIL (exit {r.returncode})"
        output = r.stdout or ""
    except subprocess.TimeoutExpired as e:
        status = "TIMEOUT"
        output = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
    took = time.monotonic() - start
    with open(os.path.join(logs, name.replace(" ", "_") + ".txt"), "w", encoding="utf-8") as fh:
        fh.write(output)
    print(f"{status:<24} {took:7.2f}s  {name}", flush=True)
    if status == "PASS":
        passed.append(name)
    else:
        failed.append(name)
        print("\n".join("    " + line for line in output.strip().splitlines()[-40:]), flush=True)

print(f"\n{len(passed)} passed, {len(failed)} failed, {len(skipped)} left out")
if failed:
    print("failed:", ", ".join(failed))
if skipped and not only:
    print("left out:", ", ".join(skipped))
sys.exit(1 if failed else 0)
