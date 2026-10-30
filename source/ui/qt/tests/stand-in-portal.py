#!/usr/bin/env python3
"""A stand-in for the XDG desktop portal's file chooser, for the file chooser
test (file-chooser.py): it owns org.freedesktop.portal.Desktop on the session
bus it is started on and answers OpenFile and SaveFile as the file named by
the first argument says, writing every request it was sent, one JSON line
each, to the second.

  stand-in-portal.py <answer.json> <requests.jsonl> [<version>]

The answer: {"response": 0, "uris": [...], "choices": [["id", "value"]],
"delay_ms": 50}. A missing file answers Cancel."""
import json
import sys

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

SERVICE = "org.freedesktop.portal.Desktop"
PATH = "/org/freedesktop/portal/desktop"
CHOOSER = "org.freedesktop.portal.FileChooser"
REQUEST = "org.freedesktop.portal.Request"

answer_path, log_path = sys.argv[1], sys.argv[2]
version = int(sys.argv[3]) if len(sys.argv) > 3 else 4


def plain(value):
    """D-Bus values as JSON: a path in bytes as text, structs as lists"""
    if isinstance(value, dbus.Array) and value.signature == "y":
        return bytes(value).rstrip(b"\0").decode("utf-8")
    if isinstance(value, (dbus.Array, dbus.Struct, list, tuple)):
        return [plain(v) for v in value]
    if isinstance(value, dbus.Dictionary):
        return {str(k): plain(v) for k, v in value.items()}
    if isinstance(value, dbus.Boolean):
        return bool(value)
    if isinstance(value, (dbus.UInt32, dbus.Int32, dbus.Byte)):
        return int(value)
    return str(value)


class Request(dbus.service.Object):
    @dbus.service.signal(REQUEST, signature="ua{sv}")
    def Response(self, response, results):
        pass


class Chooser(dbus.service.Object):
    def __init__(self, bus):
        super().__init__(bus, PATH)
        self.bus = bus

    def answer(self, method, sender, parent, title, options):
        with open(log_path, "a", encoding="utf-8") as log:
            log.write(json.dumps({"method": method, "parent": str(parent), "title": str(title), "options": plain(options)}) + "\n")
        token = str(options.get("handle_token", "t"))
        path = "/org/freedesktop/portal/desktop/request/%s/%s" % (sender[1:].replace(".", "_"), token)
        request = Request(self.bus, path)
        try:
            with open(answer_path, encoding="utf-8") as f:
                wanted = json.load(f)
        except OSError:
            wanted = {"response": 1}

        def respond():
            results = {}
            if wanted.get("uris"):
                results["uris"] = dbus.Array(wanted["uris"], signature="s")
            if wanted.get("choices"):
                results["choices"] = dbus.Array([dbus.Struct(c, signature="ss") for c in wanted["choices"]], signature="(ss)")
            request.Response(dbus.UInt32(wanted.get("response", 0)), dbus.Dictionary(results, signature="sv"))
            GLib.timeout_add(200, lambda: request.remove_from_connection() and False)
            return False

        GLib.timeout_add(int(wanted.get("delay_ms", 50)), respond)
        return dbus.ObjectPath(path)

    @dbus.service.method(CHOOSER, in_signature="ssa{sv}", out_signature="o", sender_keyword="sender")
    def OpenFile(self, parent, title, options, sender=None):
        return self.answer("OpenFile", sender, parent, title, options)

    @dbus.service.method(CHOOSER, in_signature="ssa{sv}", out_signature="o", sender_keyword="sender")
    def SaveFile(self, parent, title, options, sender=None):
        return self.answer("SaveFile", sender, parent, title, options)

    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="ss", out_signature="v")
    def Get(self, interface, name):
        if interface == CHOOSER and name == "version":
            return dbus.UInt32(version)
        raise dbus.exceptions.DBusException("no such property", name="org.freedesktop.DBus.Error.UnknownProperty")


DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()
chooser = Chooser(bus)
name = dbus.service.BusName(SERVICE, bus)
print("ready", flush=True)
GLib.MainLoop().run()
