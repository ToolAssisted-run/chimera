#!/usr/bin/env python3
"""A core's own windows (issue 610): their QML runs in Chimera's process,
outside the sandbox, so it runs locked down (owner, 2026-11-17): it reads any
file, writes only in its core's output folder, and reaches nothing over the
network. This writes a core's QML that reads and writes through `files`, and
tries every way out it can - a request by address, a picture,
a component or a font from the network, a file outside its own, the
system's browser, a module that reads or writes files - runs each in a core's
engine (test-core-windows cases), and listens on a local port for anything
that gets there. Nothing may. What a core's window needs (Qt Quick, its
Controls, Layouts) still loads, and the synthetic core's own window works
over its view model (test-core-windows window).

These cases live here, not in the C++ or a .qml file, because they name the
network classes tools/check-no-network.sh keeps out of Chimera's sources.

usage: core-windows.py <test-core-windows> <Monitor.qml>
"""
import http.server
import os
import subprocess
import sys
import tempfile
import threading

program, monitor = sys.argv[1], sys.argv[2]
failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


heard = []


class Listener(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        heard.append(self.path)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"leaked")

    do_POST = do_GET

    def log_message(self, *args):
        pass


server = http.server.HTTPServer(("127.0.0.1", 0), Listener)
threading.Thread(target=server.serve_forever, daemon=True).start()
base = "http://127.0.0.1:%d" % server.server_address[1]

# each case: (name, QML body inside an Item, what must hold of its output)
CASES = {
    "controls": 'Button { id: b; text: "fine" }\n'
                'ColumnLayout { Label { text: "x" } }\n'
                'Component.onCompleted: probe.report("controls", b.text)',
    "xhr": 'Component.onCompleted: { try { var x = new XMLHttpRequest(); x.onreadystatechange = function() {'
           ' if (x.readyState === 4) probe.report("xhr", "status " + x.status + " text " + x.responseText) };'
           ' x.open("GET", "BASE/xhr"); x.send() } catch (e) { probe.report("xhr", "absent") } }',
    "xhrpost": 'Component.onCompleted: { try { var x = new XMLHttpRequest(); x.onreadystatechange = function() {'
               ' if (x.readyState === 4) probe.report("xhrpost", "status " + x.status) };'
               ' x.open("POST", "BASE/post"); x.send("secret") } catch (e) { probe.report("xhrpost", "absent") } }',
    "xhrfile": 'Component.onCompleted: { try { var x = new XMLHttpRequest(); x.onreadystatechange = function() {'
               ' if (x.readyState === 4) probe.report("xhrfile", "length " + String(x.responseText).length) };'
               ' x.open("GET", "file:///etc/hostname"); x.send() } catch (e) { probe.report("xhrfile", "absent") } }',
    "image": 'Image { source: "BASE/image.png"; onStatusChanged: probe.report("image", "status " + status) }',
    "loader": 'Loader { source: "BASE/remote.qml"; onStatusChanged: probe.report("loader", "status " + status) }',
    "outside": 'Loader { source: "file:///etc/outside.qml"; onStatusChanged: probe.report("outside", "status " + status) }',
    "font": 'FontLoader { source: "BASE/font.ttf"; onStatusChanged: probe.report("font", "status " + status) }',
    "browser": 'Component.onCompleted: probe.report("browser", "returned " + Qt.openUrlExternally("BASE/browser"))',
    "dialogs": 'Component.onCompleted: { try { Qt.createQmlObject("import QtQuick; import QtQuick.Dialogs; FileDialog {}", this, "d");'
               ' probe.report("dialogs", "CREATED") } catch (e) { probe.report("dialogs", "refused") } }',
    "storage": 'Component.onCompleted: { try { Qt.createQmlObject("import QtQuick; import QtQuick.LocalStorage; Item {}", this, "s");'
               ' probe.report("storage", "CREATED") } catch (e) { probe.report("storage", "refused") } }',
    "labs": 'Component.onCompleted: { try { Qt.createQmlObject("import QtQuick; import Qt.labs.folderlistmodel; FolderListModel {}", this, "l");'
            ' probe.report("labs", "CREATED") } catch (e) { probe.report("labs", "refused") } }',
    "qtcore": 'Component.onCompleted: { try { Qt.createQmlObject("import QtQuick; import QtCore; Settings {}", this, "c");'
              ' probe.report("qtcore", "CREATED") } catch (e) { probe.report("qtcore", "refused") } }',
    "remoteimport": 'Component.onCompleted: { try { Qt.createQmlObject("import QtQuick; import \\"BASE/lib\\" as R; R.Thing {}", this, "r");'
                    ' probe.report("remoteimport", "CREATED") } catch (e) { probe.report("remoteimport", "refused") } }',
    "worker": 'WorkerScript { source: "worker.mjs"; onMessage: (m) => probe.report("worker", m.r);'
              ' Component.onCompleted: sendMessage({}) }',
    # reading is allowed: a file anywhere, its text and its bytes, a folder's names, a picture
    "read": 'Component.onCompleted: { probe.report("read", files.read("OUTSIDE/data.txt"));'
            ' probe.report("read", "bytes " + files.readBytes("OUTSIDE/data.txt").byteLength);'
            ' probe.report("read", "list " + files.list("OUTSIDE").join(","));'
            ' probe.report("read", "missing [" + files.read("OUTSIDE/none.txt") + "] " + (files.error !== "")) }',
    "picture": 'Image { source: "file://OUTSIDE/dot.png"; onStatusChanged: probe.report("picture", "status " + status) }',
    # writing only inside the core's output folder, its folders made
    "write": 'Component.onCompleted: { probe.report("write", "" + files.write("notes/a.txt", "hi"));'
             ' probe.report("write", "" + files.append("notes/a.txt", "!"));'
             ' probe.report("write", "" + files.writeBytes("b.bin", files.readBytes("OUTSIDE/data.txt")));'
             ' probe.report("write", files.read("notes/a.txt")) }',
    "escape": 'Component.onCompleted: { probe.report("escape", "" + files.write("../escaped.txt", "x"));'
              ' probe.report("escape", "" + files.write("OUTSIDE/absolute.txt", "x"));'
              ' probe.report("escape", "" + files.write("link/through.txt", "x"));'
              ' probe.report("escape", "" + files.write("", "x"));'
              ' probe.report("escape", "said " + (files.error !== "")) }',
}
WORKER = ('WorkerScript.onMessage = function() { try { var x = new XMLHttpRequest();'
          ' x.onreadystatechange = function() { if (x.readyState === 4) WorkerScript.sendMessage({r: "status " + x.status}) };'
          ' x.open("GET", "BASE/worker"); x.send() } catch (e) { WorkerScript.sendMessage({r: "absent"}) } }\n')

# a one-pixel PNG, red
DOT = bytes.fromhex("89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
                    "0000000c4944415408d763f8cfc000000301010018dd8db00000000049454e44ae426082")

with tempfile.TemporaryDirectory() as top:
    folder = os.path.join(top, "windows")
    outside = os.path.join(top, "outside")
    output = os.path.join(top, "output", "Synth")
    for d in (folder, outside, output):
        os.makedirs(d)
    with open(os.path.join(outside, "data.txt"), "w") as f:
        f.write("hello")
    with open(os.path.join(outside, "dot.png"), "wb") as f:
        f.write(DOT)
    # a link inside the output folder that leads out of it
    os.symlink(outside, os.path.join(output, "link"))
    for name, body in CASES.items():
        with open(os.path.join(folder, name + ".qml"), "w") as f:
            f.write("import QtQuick\nimport QtQuick.Controls\nimport QtQuick.Layouts\nItem {\n"
                    + body.replace("BASE", base).replace("OUTSIDE", outside) + "\n}\n")
    with open(os.path.join(folder, "worker.mjs"), "w") as f:
        f.write(WORKER.replace("BASE", base))
    run = subprocess.run([program, "cases", folder, output] + list(CASES), capture_output=True, text=True, timeout=120)
    out = run.stdout
    print(out)
    print(run.stderr[-3000:])
    reports = {}
    for line in out.splitlines():
        if line.startswith("REPORT "):
            what, _, result = line[len("REPORT "):].partition(" :: ")
            reports.setdefault(what, []).append(result)

    check(run.returncode == 0, "the cases ran (exit %d)" % run.returncode)
    # nothing got out, by any road
    check(heard == [], "nothing reached the listener, but it heard %s" % heard)
    # what a core's window needs still loads
    check(reports.get("controls") == ["fine"], "Qt Quick's Controls and Layouts load: %s" % reports.get("controls"))
    check("CASE controls created=1" in out, "the window with Controls was made")
    # requests by address go nowhere
    for name in ("xhr", "xhrpost", "worker"):
        got = reports.get(name, [])
        # a Qt built without its network module has no XMLHttpRequest at all
        check(len(got) == 1 and (got[0].startswith("status 0") or got[0] == "absent") and "leaked" not in got[0],
              "%s ends with nothing: %s" % (name, got))
    check(reports.get("xhrfile", ["length 0"])[0] in ("length 0", "absent"), "a file read by address reads nothing: %s" % reports.get("xhrfile"))
    # pictures, components and fonts from anywhere but the core's files fail
    for name in ("image", "loader", "outside", "font"):
        check(reports.get(name, [""])[-1] == "status 3", "%s ends in an error: %s" % (name, reports.get(name)))
    # the system's browser is given nothing
    check(reports.get("browser") == ["returned false"], "no browser opened: %s" % reports.get("browser"))
    # modules that read or write files, or are not Qt Quick's, are refused
    for name in ("dialogs", "storage", "labs", "qtcore"):
        check(reports.get(name) == ["refused"], "%s refused: %s" % (name, reports.get(name)))
    check(reports.get("remoteimport", ["refused"]) == ["refused"] or "CREATED" not in str(reports.get("remoteimport")),
          "a module from the network is refused: %s" % reports.get("remoteimport"))
    check(f"REFUSED browser {base}/browser" in out, "the browser's address is among the refused")
    # reading: any file, its bytes, a folder's names, a picture; a missing one is said
    check(reports.get("read") == ["hello", "bytes 5", "list data.txt,dot.png", "missing [] true"],
          "a file anywhere is read: %s" % reports.get("read"))
    check(reports.get("picture", [""])[-1] == "status 1", "a picture from a file is shown: %s" % reports.get("picture"))
    # writing: inside the output folder, its folders made, appended to, bytes too
    check(reports.get("write") == ["true", "true", "true", "hi!"], "written in the output folder: %s" % reports.get("write"))
    check(open(os.path.join(output, "notes", "a.txt")).read() == "hi!" and open(os.path.join(output, "b.bin"), "rb").read() == b"hello",
          "the files are there")
    # and nowhere else: up and out, an absolute path, through a link, no name
    check(reports.get("escape") == ["false", "false", "false", "false", "said true"], "nothing written outside: %s" % reports.get("escape"))
    check(not os.path.exists(os.path.join(top, "output", "escaped.txt")) and not os.path.exists(os.path.join(outside, "absolute.txt"))
          and not os.path.exists(os.path.join(outside, "through.txt")), "no file outside the output folder")

window = subprocess.run([program, "window", monitor], capture_output=True, text=True, timeout=60)
print(window.stdout)
print(window.stderr[-2000:])
check(window.returncode == 0, "the synthetic core's window over its view model (exit %d)" % window.returncode)
server.shutdown()
print("%d failed" % failures)
sys.exit(1 if failures else 0)
