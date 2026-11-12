#!/usr/bin/env python3
"""Plays the part of an AI agent's program for the MCP server's test
(docs/mcp-server.md): starts chimera-mcp, speaks MCP to it on its standard
input and output, and checks what the tools say about the synthetic core
against files the other tests already trust.

Usage: mcp-agent.py <chimera-mcp> <reference ram> <reference vram> <movie.txt> <last frame .tga> [mode]

A running Chimera must be serving. tests/synth/run-witness.sh starts one of
two: chimera-run --mcp-serve, which has no window, or the real window with
--mcp-server and a project ("window" as the last argument). The window is
asked the same things and a few more that only it can answer; it writes no
picture file to compare with, so "-" is given for the .tga and the picture is
only checked to be one.

With "runner-changes" or "window-changes" as the mode the server was started
with changes allowed, and after everything above the tools that change things
are tried: memory and named values are written and read back, and in the
window the input is edited and undone, frames are inserted and deleted, a
marker and branches are made, the playback position is moved - and at the end
the machine, played again to the movie's last frame, must hold the reference
memory, which it only can if every edit was undone exactly.

With "local" as the mode there is no Chimera at all: chimera-mcp is started
with the arguments that follow the mode (--cores, --work, --files) and opens
a machine of its own. The script lists the cores, starts a project from the
game named in MCP_TEST_ROM, writes the movie's input with set_input and
plays it - after which everything above is asked of that machine, changes
included, and the picture is compared again. It then saves the project into
the work folder (MCP_TEST_WORK), exports the movie, closes, opens the saved
project and plays it to the reference memory, and checks that paths outside
the folders it was given are refused.

Prints one line for each thing that is wrong and exits 1, or prints nothing
and exits 0.
"""
import base64
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
import project_file

mcp, ram_path, vram_path, movie_path, tga_path = sys.argv[1:6]
mode = sys.argv[6] if len(sys.argv) > 6 else "runner"
window = mode.startswith("window")
local = mode == "local"
changes = mode.endswith("-changes") or local
holder = window or local  # something holds a movie that can be edited: TAStudio, or chimera-mcp itself
wrong = []


def check(what, ok):
    if not ok:
        wrong.append(what)


agent = subprocess.Popen([mcp] + sys.argv[7:], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
next_id = 0


def rpc(method, params=None):
    global next_id
    next_id += 1
    agent.stdin.write(json.dumps({"jsonrpc": "2.0", "id": next_id, "method": method, "params": params or {}}) + "\n")
    agent.stdin.flush()
    reply = json.loads(agent.stdout.readline())
    check("the reply to %s carries its id" % method, reply.get("id") == next_id)
    return reply


def call(tool, **arguments):
    """The tool's answer: (is it an error, its texts joined, its content list)."""
    result = rpc("tools/call", {"name": tool, "arguments": arguments})["result"]
    texts = [c["text"] for c in result["content"] if c["type"] == "text"]
    return result["isError"], "\n".join(texts), result["content"]


def facts(tool, **arguments):
    """The tool's answer as the JSON object its first text holds, or {} for an error. A warning
    (memory writes thrown away, another build of chimera-mcp) comes as a text of its own after it."""
    error, text, content = call(tool, **arguments)
    check("%s %s is answered: %s" % (tool, arguments, text[:120]), not error)
    first = next((c["text"] for c in content if c["type"] == "text"), "{}")
    return {} if error else json.loads(first)


def png_pixels(data):
    """(width, height, RGB bytes) of an 8-bit RGB PNG without interlacing."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    at, width, height, packed = 8, 0, 0, b""
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", body[:10])
            assert depth == 8 and colour == 2
        elif kind == b"IDAT":
            packed += body
        at += 12 + length
    raw = zlib.decompress(packed)
    stride = width * 3
    out = bytearray()
    previous = bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - 3] if i >= 3 else 0
            up = previous[i]
            upleft = previous[i - 3] if i >= 3 else 0
            if kind == 1:
                line[i] = (line[i] + left) & 255
            elif kind == 2:
                line[i] = (line[i] + up) & 255
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else upleft)) & 255
        out += line
        previous = line
    return width, height, bytes(out)


def tga_pixels(path):
    """(width, height, RGB bytes, top row first) of an uncompressed 32-bit TGA."""
    data = open(path, "rb").read()
    width, height, depth, descriptor = struct.unpack("<HHBB", data[12:18])
    assert data[2] == 2 and depth == 32
    body = data[18 + data[0]:]
    rows = []
    for y in range(height):
        row = body[y * width * 4:(y + 1) * width * 4]
        rows.append(b"".join(bytes((row[i + 2], row[i + 1], row[i])) for i in range(0, len(row), 4)))
    if not descriptor & 0x20:
        rows.reverse()
    return width, height, b"".join(rows)


# the introduction and the list
hello = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                           "clientInfo": {"name": "the witness", "version": "1"}})["result"]
check("the server names itself", hello["serverInfo"]["name"] == "chimera")
agent.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")
agent.stdin.flush()
tools = {t["name"]: t for t in rpc("tools/list")["result"]["tools"]}
for needed in ("status", "controls", "get_input", "screenshot", "list_memory", "read_memory",
               "registers", "list_properties", "get_property"):
    check("the tool %s is listed" % needed, needed in tools)

movie = [line.rstrip("\r\n") for line in open(movie_path) if line.startswith("|")]
ram = open(ram_path, "rb").read()
vram = open(vram_path, "rb").read()

if local:
    # no window: list what there is, start a project from a game, and write and play the movie
    rom, work = os.environ["MCP_TEST_ROM"], os.environ["MCP_TEST_WORK"]
    for needed in ("list_cores", "describe_core", "open_game", "open_project", "save_project", "export_movie", "close",
                   "server_info", "pause"):
        check("the tool %s is listed where there is no window" % needed, needed in tools)
    for absent in ("resume", "close_tastudio"):
        check("the tool %s, for a window, is not listed where there is none" % absent, absent not in tools)
    info = facts("server_info")
    check("server_info says there is no window and gives the folders it was started with",
          info.get("window") is False and os.path.realpath(info.get("work_folder", "")) == os.path.realpath(work)
          and "commit" in info.get("build", {}))
    check("before anything is opened, status says no machine is open", facts("status").get("machine_open") is False)
    error, text, _ = call("seek", frame=0)
    check("and a tool that needs a project says how to open one", error and "open_game" in text)
    cores = facts("list_cores").get("cores", [])
    check("list_cores names the synthetic core", [c["name"] for c in cores] == ["Synth"] and cores[0]["file"].endswith(".chimeraCore"))
    check("and the kinds of file it opens", cores[0].get("extensions") == ["testrom"])
    described = facts("describe_core", core="Synth")
    check("describe_core gives what the core declares", described.get("name") == "Synth" and isinstance(described.get("settings"), list))
    error, text, _ = call("describe_core", core="No Such Core")
    check("a core that is not there is named", error and "No Such Core" in text)
    error, text, _ = call("open_game", core="Synth", files={"rom": ["/etc/hostname"]})
    check("a game outside the folders given is not read", error and "may read" in text)
    check("and nothing was opened", facts("status").get("machine_open") is False)
    opened = facts("open_game", core="Synth", files={"rom": [rom]}, title="gridWalker.win")
    check("open_game starts a project with an empty movie", opened.get("core") == "Synth" and opened.get("movie_length") == 0)
    check("on frame 0", facts("status").get("frame") == 0)
    facts("set_input", frame=0, entries=movie[:35])
    check("set_input writes the movie's input, lengthening the movie", facts("set_input", frame=35, entries=movie[35:]).get("movie_length") == len(movie))
    check("seek plays it to its end", facts("seek", frame=len(movie)).get("frame") == len(movie))

# status: the machine the run left
status = facts("status")
check("status names the core", status.get("core") == "Synth")
check("status is on the movie's last frame", status.get("frame") == len(movie))
check("status gives the movie's length", status.get("movie_length") == len(movie))
check("status says whether changes are allowed", status.get("changes_allowed") is changes)
check("status says the core has not stopped", status.get("core_stopped") is False)
check("status gives the picture's size", status.get("picture", {}).get("width", 0) > 0)
check("status leaves the controls' names out unless asked (issue 571)", "buttons" not in status and "axes" not in status)
named = facts("status", controls=True)
check("status names the buttons a press takes", named.get("buttons", [])[:4] == ["P1 Up", "P1 Down", "P1 Left", "P1 Right"])
check("and the axes", isinstance(named.get("axes"), list))
if window:
    # what only the window knows: the files the machine runs, the firmware it was given, how fast it runs
    files = status.get("files", [])
    check("status gives the project's files with their paths",
          [f.get("name") for f in files] == ["gridWalker.testrom"] and os.path.isfile(files[0].get("path", "")))
    check("status gives the firmware in use, of which the synthetic core has none", status.get("firmware") == [])
    check("status gives the speed measured", isinstance(status.get("measured_frames_per_second"), (int, float)))
    for needed in ("server_info", "pause", "resume", "save_project", "close_tastudio"):
        check("the tool %s is listed in the window" % needed, needed in tools)
    check("the tool close, for no window, is not listed in one", "close" not in tools)
    # what the cores folder holds, as in the window's own New Project window (issue 355)
    synth = [c for c in facts("list_cores").get("cores", []) if c.get("name") == "Synth"]
    check("list_cores in the window names the synthetic core and the files it opens", len(synth) >= 1 and "testrom" in synth[0].get("extensions", []))
    check("describe_core in the window says what it declares", facts("describe_core", core="Synth").get("name") == "Synth")
    # every Lua function is offered (issue 355), as the Lua Console documents them
    lua = facts("lua_functions", search="framecount")
    check("lua_functions lists the Lua functions by words", "emu.framecount" in [f.get("name") for f in lua.get("functions", [])]
          and "memory" in lua.get("libraries", []))
    info = facts("server_info")
    check("server_info says it is the window and where projects go",
          info.get("window") is True and info.get("folders", {}).get("projects") and info.get("commit"))
    check("pause answers that the machine is paused, on its frame",
          facts("pause") == {"paused": True, "frame": len(movie)})
if holder:
    check("status names the project", status.get("project") == "gridWalker.win")
    check("status says the machine is paused", status.get("paused") is True)
    check("status says whether anything is unsaved", status.get("unsaved_changes") is local)

# controls: the buttons, in the order of the movie's columns
controls = facts("controls")
names = [b["name"] for b in controls.get("buttons", [])]
check("controls lists the buttons in order", names[:4] == ["P1 Up", "P1 Down", "P1 Left", "P1 Right"])

# the input: every line, read in two calls, is the movie file's
got = facts("get_input", count=40)["entries"] + facts("get_input", **{"from": 40, "count": 1000})["entries"]
check("get_input returns the movie's lines", got == movie)
check("get_input past the end returns nothing", facts("get_input", **{"from": len(movie) + 5})["entries"] == [])
error, text, _ = call("get_input", count=5000)
check("get_input refuses more than 1000 frames", error and "1000" in text)

# memory: both domains, byte for byte, are the reference dumps
memory = facts("list_memory")
sizes = {d["name"]: d["size"] for d in memory.get("domains", [])}
check("list_memory gives RAM and VRAM their sizes", sizes.get("RAM") == len(ram) and sizes.get("VRAM") == len(vram))
for name, reference in (("RAM", ram), ("VRAM", vram)):
    read = b""
    for address in range(0, len(reference), 4096):
        count = min(4096, len(reference) - address)
        read += bytes.fromhex(facts("read_memory", domain=name, address=address, count=count).get("bytes_hex", ""))
    check("read_memory of %s is the reference dump" % name, read == reference)
wide = facts("read_memory", domain="RAM", address=2, count=3, type="u16")
check("read_memory reads 16-bit values, low byte first",
      wide.get("values") == [ram[2 + 2 * i] | ram[3 + 2 * i] << 8 for i in range(3)])
wide = facts("read_memory", domain="RAM", address=2, count=2, type="s32", big_endian=True)
check("read_memory reads signed 32-bit values, high byte first",
      wide.get("values") == [struct.unpack(">i", ram[2 + 4 * i:6 + 4 * i])[0] for i in range(2)])
error, text, _ = call("read_memory", domain="NO SUCH", address=0)
check("read_memory names a domain that does not exist", error and "NO SUCH" in text)
error, text, _ = call("read_memory", domain="RAM", address=len(ram) - 2, count=8)
check("read_memory refuses a read past the end", error and "past its end" in text)
error, text, _ = call("read_memory", domain="RAM", address=0, count=5000)
check("read_memory refuses more than 4096 bytes", error and "4096" in text)

# the game's named values: the movie wins the game
properties = facts("list_properties", filter="stat")
check("list_properties filters by name", [p["name"] for p in properties.get("properties", [])] == ["Status"])
check("get_property reads a value as a person does", facts("get_property", name="Status").get("value") == "Won")
error, text, _ = call("get_property", name="No Such Value")
check("get_property names a value that does not exist", error and "No Such Value" in text)

# the picture: the same pixels chimera-run wrote for the last frame
error, text, content = call("screenshot")
images = [c for c in content if c["type"] == "image"]
check("screenshot returns a PNG", not error and len(images) == 1 and images[0]["mimeType"] == "image/png")
if images:
    width, height, pixels = png_pixels(base64.b64decode(images[0]["data"]))
    check("screenshot has the picture's size", (width, height) == (status["picture"]["width"], status["picture"]["height"]))
    check("screenshot is not one flat colour", len(set(pixels[i:i + 3] for i in range(0, len(pixels), 3))) > 1)
    if tga_path != "-":
        check("screenshot is the last frame's picture", (width, height, pixels) == tga_pixels(tga_path))
    half = facts("screenshot", max_width=width // 2)
    check("screenshot scales a picture down to max_width", half.get("width") == width // 2 and half.get("picture_width") == width)
    # a part of the picture, and colours without one (issue 571)
    error, text, content = call("screenshot", region={"x": 3, "y": 5, "width": 10, "height": 7})
    crop = [c for c in content if c["type"] == "image"]
    if crop:
        cw, ch, cpixels = png_pixels(base64.b64decode(crop[0]["data"]))
        expect = b"".join(pixels[((5 + y) * width + 3) * 3:((5 + y) * width + 13) * 3] for y in range(7))
        check("screenshot crops a region, pixel for pixel", (cw, ch) == (10, 7) and cpixels == expect)
    else:
        check("screenshot crops a region: %s" % text[:120], False)
    spots = [[0, 0], [width - 1, height - 1], [7, 9]]
    error, text, content = call("screenshot", pixels=spots, image=False)
    said = json.loads(next((c["text"] for c in content if c["type"] == "text"), "{}"))
    expect = ["#%02X%02X%02X" % tuple(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) for x, y in spots]
    check("screenshot answers the colours of the pixels asked for, with no picture",
          not error and not [c for c in content if c["type"] == "image"] and [p.get("colour") for p in said.get("pixels", [])] == expect)
    error, text, _ = call("screenshot", region={"x": width - 2, "y": 0, "width": 5, "height": 1})
    check("a region past the picture's edge is refused", error and "'region'" in text)
    # whether the picture is blank, by its commonest colour (issue 355)
    colours = {}
    for i in range(0, len(pixels), 3):
        colours[pixels[i:i + 3]] = colours.get(pixels[i:i + 3], 0) + 1
    commonest = max(colours, key=colours.get)
    check("screenshot says the picture is not blank, and its commonest colour",
          half.get("blank") is False and half.get("dominant_colour") == "#%02X%02X%02X" % tuple(commonest)
          and abs(half.get("coverage", -1) - colours[commonest] / (width * height)) < 0.002)

if holder:
    # the tools the movie's holder runs
    markers = facts("list_markers").get("markers")
    if window:
        # TAStudio gives every movie three markers of its own
        check("list_markers lists the project's markers, the first on frame 0",
              isinstance(markers, list) and len(markers) > 0 and markers[0].get("frame") == 0 and "text" in markers[0])
    else:
        check("list_markers lists the project's markers, of which there are none", markers == [])
    check("list_branches lists the project's branches, of which there are none", facts("list_branches").get("branches") == [])
else:
    # a tool that needs the window says so here, where there is none
    error, text, _ = call("list_markers")
    check("a tool the frontend runs is handed to the frontend", error and "window" in text)

# RAM search: looking only, so it works whether or not changes are allowed. What is left
# after a filter must be what the reference dump says is left.
error, text, _ = call("search_results")
check("search_results says that no search is running", error and "search_start" in text)
check("search_start makes every byte a candidate", facts("search_start", domain="RAM").get("candidates") == len(ram))
same = [i for i in range(len(ram)) if ram[i] == ram[0]]
check("search_filter keeps the addresses equal to a value", facts("search_filter", op="equal", value=ram[0]).get("candidates") == len(same))
found = facts("search_results", limit=5)
check("search_results lists them lowest first, with their values",
      found.get("candidates") == len(same) and [r["address"] for r in found.get("results", [])] == same[:5]
      and all(r["value"] == ram[0] and r["previous"] == ram[0] for r in found.get("results", [])))
check("nothing has changed since, so 'not equal to before' leaves none", facts("search_filter", op="not_equal").get("candidates") == 0)
check("search_undo takes that filter back", facts("search_undo").get("candidates") == len(same))
words = [ram[i] | ram[i + 1] << 8 for i in range(0, len(ram), 2)]
check("a search over 16-bit values has half as many candidates", facts("search_start", domain="RAM", size=2).get("candidates") == len(words))
big = [i * 2 for i, w in enumerate(words) if w > 1000]
check("search_filter compares 16-bit values", facts("search_filter", op="greater", value=1000).get("candidates") == len(big))
check("and lists their addresses", [r["address"] for r in facts("search_results", limit=500).get("results", [])] == big[:500])
error, text, _ = call("search_start", domain="Bus")
check("search_start says a bus cannot be searched", error and "bus" in text)
error, text, _ = call("search_filter", op="about")
check("search_filter names the comparisons it knows", error and "not_equal" in text)

# watch: values named once and told with every answer that lands on a frame
steps_now = struct.unpack("<I", ram[4:8])[0]
watched = facts("watch", watches=[{"name": "steps", "property": "Steps"},
                                  {"domain": "RAM", "address": 4, "type": "u32"},
                                  {"domain": "NO SUCH", "address": 0}]).get("watches", [])
check("watch answers with the values now: a property as it reads, memory as a number",
      watched[:2] == [{"name": "steps", "value": str(steps_now)}, {"name": "RAM 0x4 u32", "value": steps_now}])
check("a watch that cannot be read says why and does not spoil the others",
      len(watched) == 3 and watched[2]["name"] == "NO SUCH 0x0 u8" and "NO SUCH" in watched[2].get("error", ""))
check("status carries the watches", facts("status").get("watches") == watched)
error, text, _ = call("watch", watches=[{"address": 4}])
check("a watch with nothing to read is refused", error and "Watch 1" in text)
check("the refused list replaced nothing", facts("status").get("watches") == watched)
check("an empty list ends watching", facts("watch", watches=[]).get("watches") == [] and "watches" not in facts("status"))
# a bus reads what is past its end as zeros; a watch there says it cannot be read (2026-10-18)
buses = {b["name"]: b["size"] for b in facts("list_memory").get("buses", [])}
if buses:
    bus_name, bus_size = next(iter(buses.items()))
    past = facts("watch", watches=[{"domain": bus_name, "address": bus_size}]).get("watches", [])
    check("a watch past a bus's end says it cannot be read", len(past) == 1 and "error" in past[0])
    facts("watch", watches=[])

if not changes:
    # nobody allowed changes: each tool that changes something says how they are allowed
    for tool, arguments in (("add_marker", {"frame": 1, "text": "x"}), ("set_buttons", {"frame": 0, "buttons": {"P1 A": True}}),
                            ("seek", {"frame": 0}), ("write_memory", {"domain": "RAM", "address": 0, "values": [1]}),
                            ("advance_until", {"property": "Steps", "op": "changed"})) + ((("run_lua", {"code": "return 1"}),) if window else ()):
        error, text, _ = call(tool, **arguments)
        check("%s is refused while changes are not allowed" % tool, error and "Allow the agent to make changes" in text)
    check("refused changes changed nothing", facts("get_input", count=1000)["entries"] == movie
          and bytes.fromhex(facts("read_memory", domain="RAM", address=0, count=16)["bytes_hex"]) == ram[:16])


def all_input():
    return facts("get_input", count=1000)["entries"]


def ram_now():
    return bytes.fromhex(facts("read_memory", domain="RAM", address=0, count=4096)["bytes_hex"])


if holder and changes:
    last = len(movie)
    # in the window a branch of the person's work is saved before the first change; here there is no person
    kept = 1 if window else 0
    # input: pressed by name over a range, seen, and taken back by one undo
    answer = facts("set_buttons", frame=5, count=3, buttons={"P1 A": True})
    check("set_buttons answers with the movie's length", answer.get("movie_length") == last)
    edited = all_input()
    check("set_buttons changed frames 5 to 7 and no others",
          [i for i in range(last) if edited[i] != movie[i]] == [5, 6, 7]
          and all(edited[i].replace("|", "") == movie[i].replace("|", "")[:4] + "A" + movie[i].replace("|", "")[5:] for i in (5, 6, 7)))
    # before that first change, the work as it was had been kept as a branch
    branches = facts("list_branches").get("branches", [])
    if window:
        check("a branch was saved before the first change", len(branches) == 1 and branches[0]["note"].startswith("Before the agent's changes"))
    else:
        check("no branch is saved where there is no person's work to keep", branches == [])
    check("status says the project has unsaved changes", facts("status").get("unsaved_changes") is True)
    facts("undo")
    check("undo takes the whole edit back", all_input() == movie)
    # buttons pressed on a schedule (issue 355): one step, the buttons not named kept
    pressed = facts("press_buttons", presses=[{"frame": 5, "buttons": ["P1 A"], "hold": 3},
                                              {"frame": 7, "buttons": ["P1 B", "P1 Start"]}])
    edited = all_input()

    def with_pressed(line, letters):
        flat = list(line.replace("|", ""))
        for column, letter in letters:
            flat[column] = letter
        return flat

    expected = [with_pressed(movie[i], ([(4, "A")] if 5 <= i < 8 else []) + ([(5, "B"), (7, "S")] if 7 <= i < 13 else []))
                for i in range(last)]
    check("press_buttons presses each button on its frames and leaves the rest as it was",
          [list(line.replace("|", "")) for line in edited] == expected)
    check("and answers with the frames it wrote", pressed.get("frame") == 5 and pressed.get("count") == 8)
    facts("undo")
    check("one undo takes all the presses back", all_input() == movie)
    facts("seek", frame=10)
    facts("press_buttons", presses=[{"after": 2, "buttons": ["P1 A"], "hold": 1}])
    check("a press 'after' frames counts from the frame the machine is on",
          [list(line.replace("|", "")) for line in all_input()]
          == [with_pressed(movie[i], [(4, "A")] if i == 12 else []) for i in range(last)])
    facts("undo")
    error, text, _ = call("press_buttons", presses=[{"frame": 1, "after": 1, "buttons": ["P1 A"]}])
    check("a press with both 'frame' and 'after' is refused", error and "one of them" in text)
    error, text, _ = call("press_buttons", presses=[{"frame": 1, "buttons": ["P1 A"]}, {"frame": 2, "buttons": ["No Such Button"]}])
    check("a press of a button that does not exist is refused, and names it", error and "Press 2" in text and "No Such Button" in text)
    check("the refused presses changed nothing", all_input() == movie)
    error, text, _ = call("set_buttons", frame=0, buttons={"No Such Button": True})
    check("set_buttons names a button that does not exist", error and "No Such Button" in text)
    # a list of names where names to values belong once ended the program serving the call
    error, text, _ = call("set_buttons", frame=0, buttons=["P1 A"])
    check("set_buttons with a list of names is refused", error and "objects" in text)
    error, text, _ = call("set_buttons", frame=0, axes=[1])
    check("set_buttons with a list of axis values is refused", error and "objects" in text)
    error, text, _ = call("set_buttons", frame=0)
    check("set_buttons with nothing named is refused", error and "at least one" in text)
    error, text, _ = call("insert_frames", frame=last + 9)
    check("insert_frames past the movie's end is refused", error and str(last) in text)
    # two billion frames past the end were added one empty line at a time,
    # until the process was killed
    error, text, _ = call("set_buttons", frame=2000000000, buttons={"P1 A": True})
    check("set_buttons far past the movie's end is refused", error and "past the movie's end" in text)
    error, text, _ = call("set_input", frame=2000000000, entries=[movie[0]])
    check("so is set_input", error and "past the movie's end" in text)
    check("the refused edits changed nothing", all_input() == movie)
    # input lines written as given, also past the end, and undone in one step
    facts("set_input", frame=last - 1, entries=[movie[1], movie[1], movie[1]])
    check("set_input writes lines and lengthens the movie", all_input() == movie[:-1] + [movie[1]] * 3)
    error, text, _ = call("set_input", frame=0, entries=["|this is not a line|"])
    check("set_input refuses a line that is not one of the movie's", error and "Line 1" in text)
    facts("undo")
    check("undo takes the written lines back", all_input() == movie)
    # frames inserted and deleted
    check("insert_frames lengthens the movie", facts("insert_frames", frame=0, count=2).get("movie_length") == last + 2)
    check("the input moved later", all_input()[2:] == movie)
    check("delete_frames shortens it again", facts("delete_frames", frame=0, count=2).get("movie_length") == last)
    check("the input is the movie's again", all_input() == movie)
    # a marker, and branches
    facts("add_marker", frame=9, text="placed by the witness")
    placed = [m for m in facts("list_markers").get("markers", []) if m.get("placed_by_user")]
    check("add_marker places a marker a person can tell from the movie's own",
          placed == [{"frame": 9, "text": "placed by the witness", "placed_by_user": True}])
    check("save_branch saves a branch", facts("save_branch", note="made by the witness").get("number") == kept + 1)
    notes = [b["note"] for b in facts("list_branches").get("branches", [])]
    check("the new branch has its note", len(notes) == kept + 1 and notes[-1] == "made by the witness")
    error, text, _ = call("load_branch", number=7)
    check("load_branch refuses a branch that does not exist", error and "between 1 and %d" % (kept + 1) in text)
    # watched values come with a seek, and advance_until plays until a value meets a condition
    steps = {"domain": "RAM", "address": 4, "type": "u32"}
    facts("watch", watches=[dict(steps, name="steps")])
    start = facts("seek", frame=0)
    at_start = facts("read_memory", count=1, **steps)["values"][0]
    check("a seek's answer carries the watched value", start.get("watches") == [{"name": "steps", "value": at_start}])
    until = facts("advance_until", op="greater", value=3, **steps)
    check("advance_until stops on the frame where the value first meets the condition",
          until.get("met") is True and until.get("stopped_at") == "condition" and until.get("frame", 0) > 0
          and until.get("frames_played") == until.get("frame") and until.get("watches", [{}])[0].get("value", 0) > 3)
    check("and status agrees on the frame", facts("status").get("frame") == until.get("frame"))
    check("one frame earlier the value did not meet it yet",
          facts("seek", frame=until.get("frame", 1) - 1).get("watches", [{}])[0].get("value", 99) <= 3)
    facts("seek", frame=0)
    changed = facts("advance_until", property="Steps", op="changed")
    check("advance_until with 'changed' stops when a property first differs",
          changed.get("met") is True and changed.get("watches", [{}])[0].get("value") != at_start)
    facts("seek", frame=0)
    limit = facts("advance_until", op="equal", value=999999, max_frames=5, **steps)
    check("advance_until stops after max_frames when the condition is never met",
          limit.get("met") is False and limit.get("stopped_at") == "max_frames" and limit.get("frame") == 5 and limit.get("frames_played") == 5)
    facts("seek", frame=last - 2)
    ended = facts("advance_until", op="equal", value=999999, **steps)
    check("advance_until stops at the movie's end", ended.get("met") is False and ended.get("stopped_at") == "movie_end" and ended.get("frame") == last)
    error, text, _ = call("advance_until", op="equal", value=999999, **steps)
    check("at the movie's end advance_until says there is nothing left to play", error and "movie's end" in text)
    error, text, _ = call("advance_until", domain="NO SUCH", address=0, op="equal", value=1)
    check("advance_until names a value it cannot read", error and "NO SUCH" in text)
    error, text, _ = call("advance_until", property="Test.Name", op="greater", value=1)
    check("advance_until takes a value that is not a number only for 'changed'", error and "'changed'" in text)
    facts("watch", watches=[])
    check("with watching ended, a seek's answer carries no watches", "watches" not in facts("seek", frame=3))

    # pictures taken on a schedule of the machine's own frames (issue 355)
    facts("seek", frame=0)
    error, text, content = call("capture_series", every=10, count=3, first=5)
    answer = json.loads(text) if not error else {}
    shots = answer.get("shots", [])
    check("capture_series takes a picture on each frame of the schedule",
          not error and [shot.get("frame") for shot in shots] == [5, 15, 25] and [shot.get("n") for shot in shots] == [1, 2, 3]
          and len([c for c in content if c["type"] == "image"]) == 3)
    check("each says whether it is blank", all(shot.get("blank") is False and 0 < shot.get("coverage", 0) <= 1 for shot in shots))
    check("and it stops on the last", answer.get("frame") == 25 and facts("status").get("frame") == 25)
    pictures = [c for c in content if c["type"] == "image"]
    _, _, now_content = call("screenshot", max_width=320)
    now_pictures = [c for c in now_content if c["type"] == "image"]
    check("each picture is the frame's own: the last is what screenshot shows on that frame",
          pictures and now_pictures and png_pixels(base64.b64decode(pictures[-1]["data"])) == png_pixels(base64.b64decode(now_pictures[0]["data"])))
    check("frames the movie has are played, not added", all_input() == movie)
    fps = facts("status").get("frames_per_second", 60)
    facts("seek", frame=0)
    half_second = facts("capture_series", every=0.5, unit="seconds", count=1, images=False)
    check("in seconds, the frame nearest is the machine's own time",
          [shot.get("frame") for shot in half_second.get("shots", [])] == [round(0.5 * fps)])
    # without a window, pictures are written only inside the work folder, like everything else
    folder = os.path.join(os.environ["MCP_TEST_WORK"], "shots") if local else tempfile.mkdtemp(prefix="chimera-mcp-shots-")
    os.makedirs(folder, exist_ok=True)
    if local:
        outside = tempfile.mkdtemp(prefix="chimera-mcp-outside-")
        error, text, _ = call("capture_series", every=1, count=1, folder=outside)
        check("without a window, pictures are written nowhere but the work folder", error and "work folder" in text and os.listdir(outside) == [])
        os.rmdir(outside)
    written = facts("capture_series", every=1, count=2, first=0, folder=folder, name="walk-{n}-{frame}.png", images=False)
    files = sorted(os.listdir(folder))
    frame0 = written.get("shots", [{}])[0].get("frame", -1)
    check("with a folder, each picture is written there at its own size",
          files == ["walk-01-%d.png" % frame0, "walk-02-%d.png" % (frame0 + 1)]
          and png_pixels(open(os.path.join(folder, files[0]), "rb").read())[:2] == (status["picture"]["width"], status["picture"]["height"]))
    facts("seek", frame=frame0)
    error, text, _ = call("capture_series", every=1, count=2, first=0, folder=folder, name="walk-{n}-{frame}.png")
    check("a file that is already there is not replaced, and no picture is taken", error and "already" in text)
    shutil.rmtree(folder)
    facts("seek", frame=last)
    beyond = facts("capture_series", every=2, count=2)
    check("past the movie's end, empty frames are added first",
          [shot.get("frame") for shot in beyond.get("shots", [])] == [last + 2, last + 4] and len(all_input()) == last + 4
          and all_input()[:last] == movie)
    facts("undo")
    check("which one undo takes back", all_input() == movie)
    error, text, _ = call("capture_series", every=10, count=51)
    check("capture_series takes at most 50 pictures", error and "50" in text)

    # the playback position: back, then to the end again - and after all of the above the
    # machine at the movie's last frame holds the reference memory
    check("seek goes back to a frame", facts("seek", frame=10).get("frame") == 10 and facts("status").get("frame") == 10)
    check("memory in the middle of the movie is not the end's", ram_now() != ram)
    check("seek goes forward to the movie's end", facts("seek", frame=last).get("frame") == last)
    check("after every edit was undone, the movie plays to the reference memory", ram_now() == ram)
    error, text, _ = call("seek", frame=last + 50)
    check("seek refuses a frame past the movie's end", error and str(last) in text)
    check("load_branch loads the first branch", facts("load_branch", number=1).get("number") == 1)
    check("the input after the load is the movie's", all_input() == movie)

if changes:
    # writing to the machine: read back, and said by status from then on
    check("status says memory has not been written yet", facts("status").get("memory_written_by_agent") is False)
    frame_now = facts("status").get("frame")
    check("write_memory writes bytes", facts("write_memory", domain="RAM", address=0x800, values=[171, 205]).get("bytes_written") == 2)
    check("the bytes written are read back", facts("read_memory", domain="RAM", address=0x800, count=2).get("bytes_hex") == "ABCD")
    facts("write_memory", domain="RAM", address=0x810, values=[0x1234, -2], type="s16", big_endian=True)
    check("write_memory writes wider values in the byte order asked for",
          facts("read_memory", domain="RAM", address=0x810, count=4).get("bytes_hex") == "1234FFFE")
    after = facts("status")
    check("status says memory was written, and on which frame",
          after.get("memory_written_by_agent") is True and after.get("first_memory_write_frame") == frame_now)
    # the search an agent really runs: remember, change something, keep what changed
    facts("search_start", domain="RAM")
    old = facts("read_memory", domain="RAM", address=0x900, count=1)["values"][0]
    facts("write_memory", domain="RAM", address=0x900, values=[(old + 5) & 255])
    check("after one byte was written, 'not equal to before' leaves that byte alone", facts("search_filter", op="not_equal").get("candidates") == 1)
    check("and after the filter its value now is the one remembered",
          [{k: r.get(k) for k in ("address", "address_hex", "value", "previous", "difference")} for r in facts("search_results").get("results", [])]
          == [{"address": 0x900, "address_hex": "0x900", "value": (old + 5) & 255, "previous": (old + 5) & 255, "difference": 0}])
    check("so 'equal to before' keeps it", facts("search_filter", op="equal").get("candidates") == 1)
    error, text, _ = call("write_memory", domain="RAM", address=0, values=[300])
    check("write_memory refuses a value that does not fit", error and "fits in u8" in text)
    error, text, _ = call("write_memory", domain="VRAM", address=0, values=[1])
    check("write_memory refuses memory that cannot be written", error and "cannot be written" in text)
    error, text, _ = call("write_memory", domain="RAM", address=4095, values=[1, 2])
    check("write_memory refuses a write past the end", error and "past its end" in text)
    check("the refused writes wrote nothing", facts("read_memory", domain="RAM", address=4095, count=1).get("values") == [ram[4095]])
    # addresses as hex text, and in hex in the answers (issue 571)
    hexed = facts("read_memory", domain="RAM", address="0x800", count=2)
    check("an address is taken as hex text, and answered in hex too",
          hexed.get("bytes_hex") == "ABCD" and hexed.get("address") == 0x800 and hexed.get("address_hex") == "0x800")
    error, text, _ = call("read_memory", domain="RAM", address="0x80G")
    check("an address that is not a number is refused", error and "'address'" in text)
    # a write lasts until the machine goes back before it, and the agent is told when it went (issue 571)
    if holder:
        facts("seek", frame=5)
        facts("write_memory", domain="RAM", address=0x820, values=[0x5A])
        error, text, _ = call("seek", frame=8)
        check("playing on from a write keeps it, and says nothing of it", not error and "thrown away" not in text)
        error, text, _ = call("seek", frame=3)
        check("going back before a write says it was thrown away, and on which frame",
              not error and "5 were thrown away" in text)
        error, text, _ = call("seek", frame=4)
        check("and it is said once", not error and "thrown away" not in text)
    # freeze: a value held through going back, listed, and let go (issue 571)
    held = facts("freeze", domain="RAM", address="0x830", value=0x77)
    mine = [f for f in held.get("freezes", []) if f.get("address_hex") == "0x830"]
    check("freeze answers the list with the new freeze in it",
          len(mine) == 1 and mine[0].get("value") == 0x77 and mine[0].get("size") == 1 and mine[0].get("on") is True)
    if holder:
        facts("seek", frame=2)
        facts("seek", frame=6)
        check("a frozen byte is held through going back and playing on",
              facts("read_memory", domain="RAM", address=0x830).get("values") == [0x77])
    facts("freeze", domain="RAM", address=0x831, size=2, value=1, when={"op": "greater", "value": 9})
    when = [f.get("when") for f in facts("list_freezes").get("freezes", []) if f.get("address_hex") == "0x831"]
    check("a freeze's condition is listed as it was given", when == [{"op": "greater", "value": 9}])
    error, text, _ = call("freeze", domain="RAM", value=1)
    check("freeze refuses a domain with no address", error and "'address'" in text)
    error, text, _ = call("freeze", domain="RAM", address=1, value=1, when={"op": "sideways", "value": 1})
    check("freeze refuses a condition it does not know", error and "'when'" in text)
    if window:
        # the window keeps its own list (the Cheats window) and puts it back on the machine
        # when it restarts: the agent's freezes must be in it
        facts("run_lua", code="client.reboot_core()")
        kept = [f.get("address_hex") for f in facts("list_freezes").get("freezes", [])]
        check("the window's list has the agent's freezes: a restart keeps them", "0x830" in kept and "0x831" in kept)
    index = [f["index"] for f in facts("list_freezes").get("freezes", []) if f.get("address_hex") == "0x830"]
    left = facts("unfreeze", index=index[0] if index else 99).get("freezes", [])
    check("unfreeze lets go of one by its index", [f.get("address_hex") for f in left] == ["0x831"])
    check("and of all of them", facts("unfreeze", all=True).get("freezes") == [])
    error, text, _ = call("unfreeze", index=0)
    check("unfreeze says when there is nothing there", error and "list_freezes" in text)
    # RAM search: the rest of what the engine does (issue 571)
    check("a 2-byte search takes every other address", facts("search_start", domain="RAM", size=2).get("candidates") == 2048)
    check("and every address but the last when misaligned",
          facts("search_start", domain="RAM", size=2, misaligned=True).get("candidates") == 4095)
    ranged = facts("search_start", domain="RAM", ranges=[["0x100", 16], [0x203, 2]])
    check("ranges keep a search to parts of the domain, each widened to whole 8-byte blocks",
          ranged.get("candidates") == 24 and ranged.get("searched_bytes") == 24)
    error, text, _ = call("search_start", domain="RAM", ranges=[[4090, 16]])
    check("a range past the domain's end is refused", error and "'ranges'" in text)
    for at in (0x400, 0x410, 0x420, 0x430):
        facts("write_memory", domain="RAM", address=at, values=[0x42])
    facts("search_start", domain="RAM")
    facts("search_filter", op="equal", value="0x42")
    shape = facts("search_results")
    check("a table's records show as a stride, and where they lie as a cluster",
          shape.get("candidates") == 4 and shape.get("stride") == 16
          and shape.get("clusters") == [{"from_hex": "0x400", "to_hex": "0x430", "count": 4}])
    facts("search_start", domain="RAM", size=4)
    error, text, _ = call("search_filter", op="equal", value=1.5, display="float")
    check("a float search compares as floats", not error)
    error, text, _ = call("search_start", domain="RAM", size=2)
    error, text, _ = call("search_filter", op="equal", value=1, display="float")
    check("a float search of 2-byte values is refused", error and "float" in text)
    error, text, _ = call("search_filter", op="different_by")
    check("different_by needs 'by'", error and "'by'" in text)
    if holder:
        def ram_at(frame):
            facts("seek", frame=frame)
            return bytes.fromhex(facts("read_memory", domain="RAM", address=0, count=4096)["bytes_hex"])
        r10, r20, r30 = ram_at(10), ram_at(20), ram_at(30)
        facts("seek", frame=10)
        facts("search_start", domain="RAM")
        stepped = facts("search_steps", steps=[{"frame": 20, "op": "not_equal"}, {"frame": 30, "op": "not_equal"}])
        expect = sum(1 for a in range(4096) if r10[a] != r20[a] and r20[a] != r30[a])
        steps = stepped.get("steps", [])
        check("search_steps seeks and filters each step, as by hand",
              [x.get("frame") for x in steps] == [20, 30] and steps[-1].get("candidates") == expect)
        facts("seek", frame=10)
        facts("search_start", domain="RAM")
        facts("seek", frame=20)
        facts("search_filter", op="not_equal", previous="original")
        facts("seek", frame=30)
        left = facts("search_filter", op="not_equal").get("candidates")
        expect = sum(1 for a in range(4096) if r20[a] != r10[a] and r30[a] != r10[a])
        check("previous 'original' compares with the value when the search started", left == expect)
        # the changes counted every frame, against what each frame's memory says
        frames = [ram_at(f) for f in range(0, 13)]
        facts("seek", frame=0)
        facts("search_start", domain="RAM")
        # played, not seeked: a seek forward over kept states loads one instead of running the frames
        facts("advance_until", domain="RAM", address=0xFFF, op="equal", value=999, max_frames=12)
        changed = facts("search_filter", op="greater", compare="changes", value=0).get("candidates")
        expect = sum(1 for a in range(4096) if any(frames[f][a] != frames[f + 1][a] for f in range(12)))
        counts = {r["address"]: r["changes"] for r in facts("search_results", limit=500).get("results", [])}
        expect_counts = {a: sum(1 for f in range(12) if frames[f][a] != frames[f + 1][a]) for a in counts}
        check("each frame's changes are counted: compare 'changes'", changed == expect and expect > 0)
        check("and search_results gives each one's count", counts == expect_counts)
        error, text, _ = call("search_steps", steps=[{"frame": 3, "op": "equal", "value": "nonsense"}])
        check("search_steps says which step could not be done", error and "Step 1" in text and "'value'" in text)
        # what changed in a range between two frames, in one call (issue 571)
        diff = facts("diff_memory", domain="RAM", address=0, length="0x1000", frame_a=10, frame_b=30)
        runs = {}
        for run in diff.get("runs", []):
            before, after = bytes.fromhex(run["before_hex"]), bytes.fromhex(run["after_hex"])
            for k in range(len(before)):
                runs[run["address"] + k] = (before[k], after[k])
        expect = {a: (r10[a], r30[a]) for a in range(4096) if r10[a] != r30[a]}
        check("diff_memory lists the bytes that changed, what they were and became",
              runs == expect and diff.get("changed_bytes") == len(expect) and expect and facts("status").get("frame") == 30)
        # stopping on the first frame any byte of a range changes
        first = next(f for f in range(1, 13) if frames[f] != frames[f - 1])
        facts("seek", frame=0)
        moved = facts("advance_until", domain="RAM", address=0, length=4096, op="changed", max_frames=12)
        check("advance_until stops when any byte of a range changes", moved.get("met") is True and moved.get("frame") == first)
        error, text, _ = call("advance_until", domain="RAM", address=0, length=4, op="equal", value=1)
        check("a range is for 'changed' only", error and "'length'" in text)
    # a game's named value
    check("set_property sets a value from text", facts("set_property", name="Steps", value="7").get("value") == "7")
    check("the value set is read back", facts("get_property", name="Steps").get("value") == "7")
    error, text, _ = call("set_property", name="No Such Value", value="1")
    check("set_property names a value that does not exist", error and "No Such Value" in text)
    error, text, _ = call("set_register", name="PC", value=0)
    check("set_register names a register that does not exist", error and "PC" in text)
    if not holder:
        error, text, _ = call("add_marker", frame=1, text="x")
        check("a change the window makes is handed to the frontend", error and "window" in text)
        error, text, _ = call("advance_until", property="Steps", op="changed")
        check("advance_until needs the window too", error and "window" in text)

if window and changes:
    # the end of an agent's run, with nothing asked (issue 355): save, play, pause, close
    folder = tempfile.mkdtemp(prefix="chimera-mcp-save-")
    path = os.path.join(folder, "agent.chimeraProject")
    saved = facts("save_project", path=path)
    check("save_project saves where it is told, and nothing is unsaved after",
          saved.get("saved") == path and os.path.isfile(path) and saved.get("unsaved_changes") is False
          and facts("status").get("unsaved_changes") is False)
    check("saving it again where it is needs no 'overwrite'", facts("save_project").get("saved") == path)
    other = os.path.join(folder, "someone else's.chimeraProject")
    open(other, "w").write("theirs")
    error, text, _ = call("save_project", path=other)
    check("a file that is there is not replaced", error and "overwrite: true" in text and open(other).read() == "theirs")
    check("unless overwrite is given", facts("save_project", path=other, overwrite=True).get("saved") == other
          and open(other, "rb").read() != b"theirs")
    facts("seek", frame=10)
    check("resume lets the machine run", facts("resume") == {"paused": False, "frame": 10})
    ran = 10
    for _ in range(100):
        ran = facts("status").get("frame", 10)
        if ran > 12:
            break
        time.sleep(0.05)
    check("it plays on by itself", ran > 12)
    stopped = facts("pause")
    check("pause stops it", stopped.get("paused") is True and stopped.get("frame", 0) >= ran)
    # Lua, as the Lua Console's input line runs it (issue 355)
    frame_now = facts("status").get("frame")
    ran = facts("run_lua", code="return emu.framecount(), movie.length()")
    check("run_lua answers what the Lua returned", ran.get("results") == [frame_now, 0] or ran.get("results", [None])[0] == frame_now)
    ran = facts("run_lua", code="print('hello from the agent') return {1, 2, x = nil}, 1 + 1")
    check("and what it printed", ran.get("results") == [[1, 2], 2] and "hello from the agent" in ran.get("output", ""))
    facts("run_lua", code="agent_said = 41")
    check("a statement runs, and what it set is there for the next call", facts("run_lua", code="agent_said + 1").get("results") == [42])
    error, text, _ = call("run_lua", code="error('on purpose')")
    check("an error Lua raises is the answer", error and "on purpose" in text)
    error, text, _ = call("run_lua", code="emu.frameadvance()")
    check("a function that waits for a frame is for scripts", error)
    script = os.path.join(folder, "agent.lua")
    open(script, "w").write("print('the script started')\nwhile true do emu.frameadvance() end\n")
    started = facts("lua_script", path=script)
    facts("status")
    stopped = facts("lua_script", path=script, stop=True)
    check("lua_script starts a script, and stops it",
          started.get("running") is True and stopped.get("running") is False
          and "the script started" in started.get("output", "") + stopped.get("output", ""))
    facts("add_marker", frame=3, text="added after saving")
    error, text, _ = call("close_tastudio")
    check("a project with unsaved changes is not closed", error and "discard_unsaved" in text and facts("status").get("machine_open") is True)
    closed = facts("close_tastudio", discard_unsaved=True)
    check("discard_unsaved closes it without asking", closed.get("closed") is True and closed.get("machine_open") is False
          and facts("status").get("machine_open") is False)
    error, text, _ = call("pause")
    check("with nothing open there is nothing to pause", error)
    check("the project saved before is as it was saved", "added after saving" not in json.dumps(project_file.read(path)))
    # a new project, as the New Project window would make it, opened with nothing asked (issue 355)
    error, text, _ = call("create_project", core="No Such Core", game=status["files"][0]["path"])
    check("create_project names a core that is not there", error and "list_cores" in text)
    made = facts("create_project", core="Synth", game=status["files"][0]["path"])
    check("create_project makes the project and opens it on frame 0",
          made.get("core") == "Synth" and made.get("frame") == 0 and [f.get("name") for f in made.get("files", [])] == ["gridWalker.testrom"])
    now = facts("status")
    check("and it is what is open", now.get("machine_open") is True and now.get("core") == "Synth" and now.get("frame") == 0)
    check("with an empty movie", made.get("movie_length") == 0 and now.get("movie_length") == 0)
    facts("close_tastudio", discard_unsaved=True)

if local:
    # save, export, close, open again: the project made here plays to the reference memory
    saved = facts("save_project", path="made.chimeraProject").get("saved", "")
    check("save_project writes the project into the work folder", saved == os.path.join(os.path.realpath(work), "made.chimeraProject") and os.path.isfile(saved))
    check("after saving nothing is unsaved", facts("status").get("unsaved_changes") is False)
    check("saving it again where it is needs no 'overwrite'", facts("save_project", path="made.chimeraProject").get("saved") == saved)
    open(os.path.join(work, "theirs.chimeraProject"), "w").write("theirs")
    error, text, _ = call("save_project", path="theirs.chimeraProject")
    check("a file that is there is not replaced without 'overwrite'", error and "overwrite: true" in text
          and open(os.path.join(work, "theirs.chimeraProject")).read() == "theirs")
    check("and is with it", facts("save_project", path="theirs.chimeraProject", overwrite=True).get("saved", "").endswith("theirs.chimeraProject"))
    os.remove(os.path.join(work, "theirs.chimeraProject"))
    facts("save_project", path="made.chimeraProject", overwrite=True)
    check("pause says the machine is paused", facts("pause").get("paused") is True)
    error, text, _ = call("save_project", path="/tmp/chimera-mcp-outside.chimeraProject")
    check("save_project writes nowhere but the work folder", error and "work folder" in text and not os.path.exists("/tmp/chimera-mcp-outside.chimeraProject"))
    error, text, _ = call("export_movie", path="../outside.txt")
    check("nor does export_movie, by a path that climbs out of it", error and "work folder" in text)
    facts("export_movie", path="made.txt")
    exported = os.path.join(work, "made.txt")
    check("export_movie writes the movie's input",
          os.path.isfile(exported) and [l.rstrip("\r\n") for l in open(exported) if l.startswith("|")] == movie)
    check("close closes the machine", facts("close").get("closed") is True and facts("status").get("machine_open") is False)
    error, text, _ = call("close")
    check("with nothing open, close says so", error and "open_game" in text)
    error, text, _ = call("open_project", path="/etc/hostname")
    check("open_project reads nothing outside the folders given", error and "may read" in text)
    again = facts("open_project", path="made.chimeraProject")
    check("open_project opens what was saved", again.get("movie_length") == len(movie) and again.get("core") == "Synth")
    # what the agent knew of the machine it closed is not carried into this one,
    # although the sandbox puts its memory at the same address
    check("a machine opened again has had no memory written", facts("status").get("memory_written_by_agent") is False)
    error, text, _ = call("search_results")
    check("and has no search running", error and "search_start" in text)
    check("with its input", all_input() == movie)
    check("its branch", [b["note"] for b in facts("list_branches").get("branches", [])] == ["made by the witness"])
    check("and its marker", facts("list_markers").get("markers") == [{"frame": 9, "text": "placed by the witness", "placed_by_user": True}])
    check("the saved project starts on frame 0", facts("status").get("frame") == 0)
    facts("seek", frame=len(movie))
    check("and plays to the reference memory", ram_now() == ram)
    facts("close")

    # A project whose movie's LogKey names fewer controls than the machine has
    # (made with an older package): a control it does not name joins the key
    # when the agent sets it, and every line gets its column. It used to be
    # left out of the line while the agent was told it was set.
    short = project_file.read(saved)
    short["inputs"] = {"columns": [["P1 Up", "P1 Down"]], "entries": ["|.D|", "|..|", "|.D|"]}
    short["branches"] = []
    short["markers"] = []
    short_path = os.path.join(work, "short.chimeraProject")
    project_file.write(short_path, short)
    facts("open_project", path="short.chimeraProject")
    check("a movie with a short LogKey opens", all_input() == ["|.D|", "|..|", "|.D|"])
    facts("set_buttons", frame=1, buttons={"P1 A": True})
    check("a control its LogKey does not name joins it, and every line has its column",
          all_input() == ["|.D|.|", "|..|A|", "|.D|.|"])
    facts("export_movie", path="short.txt")
    short_text = open(os.path.join(work, "short.txt")).read()
    check("and the movie says so", "LogKey:#P1 Up|P1 Down|#P1 A|" in short_text)
    facts("undo")
    check("undo gives the movie back as it was", all_input() == ["|.D|", "|..|", "|.D|"])
    facts("close")
    os.remove(short_path)
    os.remove(os.path.join(work, "short.txt"))

    # Three refusals that used to cost something (2026-10-18). A line with a
    # newline in it was played as a pressed button and split the saved movie;
    # an export through a link in the work folder was written outside it; an
    # open that failed closed the project that was open, unsaved input and all.
    facts("open_game", core="Synth", files={"rom": [rom]}, title="kept")
    facts("set_input", frame=0, entries=["|.D......|"])
    error, text, _ = call("set_input", frame=1, entries=["|.D\n.....|"])
    check("set_input refuses a line with a newline in it", error and "shape" in text)
    link = os.path.join(work, "link.txt")
    os.symlink("/tmp/chimera-mcp-outside-%d.txt" % os.getpid(), link)
    error, text, _ = call("export_movie", path="link.txt")
    check("export_movie does not write through a link", error and "link" in text
          and not os.path.exists("/tmp/chimera-mcp-outside-%d.txt" % os.getpid()))
    os.remove(link)
    error, text, _ = call("open_project", path="no-such.chimeraProject")
    check("an open that fails keeps the project that was open", error and "open again" in text
          and all_input() == ["|.D......|"] and facts("status").get("unsaved_changes") is True)
    facts("close")

    # The firmware a game is started with is pinned in the project it makes,
    # as the window's projects pin theirs; opening it on another file is said
    # (not refused). The game file stands in for the firmware here.
    facts("open_game", core="Synth", files={"rom": [rom]}, firmware={"bios": rom}, title="pins")
    facts("save_project", path="pins.chimeraProject")
    pins_path = os.path.join(work, "pins.chimeraProject")
    rom_sha1 = hashlib.sha1(open(rom, "rb").read()).hexdigest().upper()
    check("open_game pins the firmware it was given", project_file.read(pins_path).get("firmware") == [{"id": "bios", "sha1": rom_sha1}])
    facts("close")
    other = os.path.join(work, "other.bin")
    open(other, "wb").write(b"not the firmware the project pins")
    reopened = facts("open_project", path="pins.chimeraProject", firmware={"bios": other})
    check("open_project on other firmware says so and opens", reopened.get("core") == "Synth"
          and any("not the one the project pins" in w for w in reopened.get("warnings", [])))
    facts("close")
    check("and on the pinned firmware says nothing", "warnings" not in facts("open_project", path="pins.chimeraProject", firmware={"bios": rom}))
    facts("close")
    os.remove(pins_path)
    os.remove(other)

agent.stdin.close()
agent.wait()
for line in wrong:
    print("WRONG: " + line)
sys.exit(1 if wrong else 0)
