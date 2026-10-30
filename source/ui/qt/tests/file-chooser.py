#!/usr/bin/env python3
"""The window's file dialogs on Linux (docs/file-picker.md), run twice by
test-file-chooser: with no session bus, which is Chimera's own chooser, and
with a stand-in portal (stand-in-portal.py) on a private bus of its own,
which is the desktop's chooser asked through the portal. The second run is
skipped, and said to be, where dbus-daemon or Python's dbus and gi modules
are missing.

  file-chooser.py <test-file-chooser>"""
import os
import shutil
import subprocess
import sys
import tempfile

program = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
work = tempfile.mkdtemp(prefix="chimera-file-chooser-")
failed = False


def base_env(folder):
    home = os.path.join(work, "home")
    config = os.path.join(home, ".config")
    os.makedirs(os.path.join(config, "gtk-3.0"), exist_ok=True)
    os.makedirs(os.path.join(home, ".local", "share"), exist_ok=True)
    os.makedirs(os.path.join(folder, "sub"), exist_ok=True)
    # a GTK bookmark the chooser lists among its places
    with open(os.path.join(config, "gtk-3.0", "bookmarks"), "w", encoding="utf-8") as f:
        f.write("file://%s/sub Test games\n" % folder)
    runtime = os.path.join(work, "run")
    os.makedirs(runtime, exist_ok=True)
    os.chmod(runtime, 0o700)
    env = dict(os.environ)
    env.pop("DBUS_SESSION_BUS_ADDRESS", None)
    env.update({
        "HOME": home, "XDG_CONFIG_HOME": config, "XDG_DATA_HOME": os.path.join(home, ".local", "share"),
        "XDG_RUNTIME_DIR": runtime, "CHIMERA_TEST_FOLDER": folder,
        "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software",
    })
    return env


def run(args, env, what):
    global failed
    print("--", what, flush=True)
    code = subprocess.run([program] + args, env=env, timeout=120).returncode
    if code != 0:
        failed = True
        print("FAIL", what, "exit", code, flush=True)


try:
    # no bus at all: Chimera's chooser
    run(["--builtin"], base_env(os.path.join(work, "builtin")), "Chimera's own chooser (no session bus)")

    # a stand-in portal on a bus of the test's own
    missing = [] if shutil.which("dbus-daemon") else ["dbus-daemon"]
    try:
        import dbus  # noqa: F401
        import gi  # noqa: F401
    except ImportError as e:
        missing.append("Python's %s module" % e.name)
    if missing:
        print("SKIP the desktop's chooser through a stand-in portal: no", " or ".join(missing), flush=True)
    else:
        bus_dir = os.path.join(work, "bus")
        os.makedirs(bus_dir)
        config = os.path.join(work, "bus.conf")
        with open(config, "w", encoding="utf-8") as f:
            f.write("""<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:dir=%s</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
""" % bus_dir)
        daemon = subprocess.Popen(["dbus-daemon", "--config-file=" + config, "--nofork", "--print-address=1"], stdout=subprocess.PIPE, text=True)
        portal = None
        try:
            address = daemon.stdout.readline().strip()
            env = base_env(os.path.join(work, "portal"))
            env["DBUS_SESSION_BUS_ADDRESS"] = address
            env["CHIMERA_TEST_ANSWER"] = os.path.join(work, "answer.json")
            env["CHIMERA_TEST_REQUESTS"] = os.path.join(work, "requests.jsonl")
            portal = subprocess.Popen([sys.executable, os.path.join(here, "stand-in-portal.py"), env["CHIMERA_TEST_ANSWER"], env["CHIMERA_TEST_REQUESTS"]],
                                      env=env, stdout=subprocess.PIPE, text=True)
            if portal.stdout.readline().strip() != "ready":
                failed = True
                print("FAIL the stand-in portal did not start", flush=True)
            else:
                run(["--portal"], env, "the desktop's chooser through a stand-in portal")
        finally:
            if portal:
                portal.terminate()
                portal.wait()
            daemon.terminate()
            daemon.wait()
finally:
    shutil.rmtree(work, ignore_errors=True)

sys.exit(1 if failed else 0)
