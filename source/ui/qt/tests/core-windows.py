#!/usr/bin/env python3
"""A core's own windows (issue 610): their QML runs in Chimera's process,
outside the sandbox, so it runs locked down (owner, 2026-11-17): a file of
the computer reaches the core only as the person picks it in Chimera's
dialog, and nothing goes over the network. This writes a core's QML that
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
import pathlib
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
    # a file of the computer only through Chimera's dialog: no call reads or
    # writes one by its path, a picture from outside is not shown, and what is
    # asked of the dialog carries a name, never a place
    "nopath": 'Component.onCompleted: probe.report("nopath", [typeof files.read, typeof files.write, typeof files.readBytes,'
              ' typeof files.list].join(","))',
    "picture": 'Image { source: "PICTURE"; onStatusChanged: probe.report("picture", "status " + status) }',
    "choose": 'Component.onCompleted: files.choose("dump", "write", "Save the dump", ["Dumps (*.bin)"], "OUTSIDE/../../dump.bin")',
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
    for d in (folder, outside):
        os.makedirs(d)
    with open(os.path.join(outside, "dot.png"), "wb") as f:
        f.write(DOT)
    # the picture's address as a file address of this system (file:///C:/... on
    # Windows), and the folder with forward slashes: a backslash in a QML
    # string starts an escape
    picture = pathlib.Path(outside, "dot.png").as_uri()
    outside_qml = outside.replace("\\", "/")
    for name, body in CASES.items():
        with open(os.path.join(folder, name + ".qml"), "w") as f:
            f.write("import QtQuick\nimport QtQuick.Controls\nimport QtQuick.Layouts\nItem {\n"
                    + body.replace("BASE", base).replace("PICTURE", picture).replace("OUTSIDE", outside_qml) + "\n}\n")
    with open(os.path.join(folder, "worker.mjs"), "w") as f:
        f.write(WORKER.replace("BASE", base))
    run = subprocess.run([program, "cases", folder] + list(CASES), capture_output=True, text=True, timeout=120)
    out = run.stdout
    print(out)
    print(run.stderr[-3000:])
    reports = {}
    refused = {}
    for line in out.splitlines():
        if line.startswith("REFUSED "):
            what, _, address = line[len("REFUSED "):].partition(" ")
            refused.setdefault(what, []).append(address)
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
    # pictures, components and fonts from anywhere but the core's files are
    # not loaded. A Qt with a network module ends them in an error (the
    # request is refused); one without (Chimera's releases, the Windows
    # builds) never fetches an address that is not a file, so they stay
    # Loading, and are never Ready
    network = "QML_NETWORK 1" in out
    check(network or "QML_NETWORK 0" in out, "the cases said whether QML has a network module")
    def not_loaded(name):
        got = reports.get(name, [])
        return got[-1:] == ["status 3"] if network else "status 1" not in got
    for name in ("image", "loader", "outside", "font"):
        check(not_loaded(name), "%s is not loaded: %s" % (name, reports.get(name)))
    for name, address in (("image", base + "/image.png"), ("loader", base + "/remote.qml"), ("font", base + "/font.ttf")):
        check(address in refused.get(name, []), "%s's address is among the refused: %s" % (name, refused.get(name)))
    # the system's browser is given nothing
    check(reports.get("browser") == ["returned false"], "no browser opened: %s" % reports.get("browser"))
    # modules that read or write files, or are not Qt Quick's, are refused
    for name in ("dialogs", "storage", "labs", "qtcore"):
        check(reports.get(name) == ["refused"], "%s refused: %s" % (name, reports.get(name)))
    check(reports.get("remoteimport", ["refused"]) == ["refused"] or "CREATED" not in str(reports.get("remoteimport")),
          "a module from the network is refused: %s" % reports.get("remoteimport"))
    check(f"REFUSED browser {base}/browser" in out, "the browser's address is among the refused")
    # no file by its path, and a picture from outside the core's files is not shown
    check(reports.get("nopath") == ["undefined,undefined,undefined,undefined"], "no call takes a path: %s" % reports.get("nopath"))
    check(not_loaded("picture") and picture in refused.get("picture", []), "a picture from outside is refused: %s %s" % (reports.get("picture"), refused.get("picture")))
    # what Chimera's dialog is asked for: the purpose, write, the title, the filters, and a name only
    check("WANTED choose :: dump|write|Save the dump|Dumps (*.bin)|dump.bin" in out, "the dialog is asked for a name, never a place")

window = subprocess.run([program, "window", monitor], capture_output=True, text=True, timeout=60)
print(window.stdout)
print(window.stderr[-2000:])
check(window.returncode == 0, "the synthetic core's window over its view model (exit %d)" % window.returncode)
# a package's window files unpacked where the window is opened from (issue
# 663): windows/kit/SaveData.qml must come out as <folder>/kit/SaveData.qml
import zipfile
with tempfile.TemporaryDirectory() as tmp:
    package = os.path.join(tmp, "core.chimeraCore")
    with zipfile.ZipFile(package, "w") as z:
        z.writestr("core.wbx", "x")
        z.writestr("waterbox.config", "{}")
        z.writestr("windows/Monitor.qml", "import QtQuick\nItem {}\n")
        z.writestr("windows/kit/SaveData.qml", "import QtQuick\nItem {}\n")
    unpacked = subprocess.run([program, "unpack", package, os.path.join(tmp, "out")], capture_output=True, text=True, timeout=60)
    print(unpacked.stdout)
    check(unpacked.returncode == 0, "a package's window files are unpacked where the window opens from (exit %d)" % unpacked.returncode)
server.shutdown()
print("%d failed" % failures)
sys.exit(1 if failures else 0)
