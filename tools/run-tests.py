#!/usr/bin/env python3
"""Runs the tests tools/export-tests.py packed, on the system they were built
for (the Windows CI jobs, x64 and arm64): each test's program, with its
arguments, folder and environment, the folder holding the build's libraries
first on PATH, and its timeout. Says PASS, FAIL or TIMEOUT for each, as
meson test does, and ends with the counts; exits 1 if any did not pass.

    python tools/run-tests.py <exported folder> [--skip <name>]... [--only <name>]...

--skip leaves a test out by its name, and the summary says it was left out
(a test that needs what the machine has not got, such as a graphics card).
The test's output is printed when it does not pass."""
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
    return value.replace("@ROOT@", folder) if isinstance(value, str) else value


build = real(plan["build"])
env_base = dict(os.environ)
env_base["PATH"] = build + os.pathsep + env_base.get("PATH", "")
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
        r = subprocess.run(cmd, cwd=real(t["workdir"]), env=env, capture_output=True, text=True,
                           errors="replace", timeout=t["timeout"] or None)
        status = "PASS" if r.returncode == 0 else f"FAIL (exit {r.returncode:#x})" if r.returncode < 0 or r.returncode > 255 else f"FAIL (exit {r.returncode})"
        output = (r.stdout or "") + (r.stderr or "")
    except subprocess.TimeoutExpired as e:
        status = "TIMEOUT"
        output = (e.stdout or b"").decode(errors="replace") + (e.stderr or b"").decode(errors="replace") \
            if isinstance(e.stdout, bytes) else (e.stdout or "") + (e.stderr or "")
    took = time.monotonic() - start
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
