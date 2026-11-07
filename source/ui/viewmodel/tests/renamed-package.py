"""Writes a copy of a core package under another core name: a second
installed core that takes the same kind of file, for the test of choosing
between cores. With no package to copy it writes an empty file, and the test
that needs it skips."""
import json
import sys
import zipfile

src, name, out = sys.argv[1:4]
# optionally the core's kind ("game"), and firmware entries as JSON
kind = sys.argv[4] if len(sys.argv) > 4 else ""
firmware = json.loads(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[5] else None
# and the date the package says it was made
version_date = sys.argv[6] if len(sys.argv) > 6 else ""
# and buttons more than the synthetic core's, as JSON (a second player,
# keyboard keys: for the piano roll's Columns menu)
more_buttons = json.loads(sys.argv[7]) if len(sys.argv) > 7 and sys.argv[7] else []
# and a core that compiles a game's code before a project is made ("precompile")
precompile = len(sys.argv) > 8 and sys.argv[8] == "precompile"
# and a core that declares no virtual size, with room for a wider picture than
# it draws ("square": its pixels are square, as Ruffle's)
square = len(sys.argv) > 9 and sys.argv[9] == "square"
# and the rate the core says it mixes its sound at (audio.rate)
rate = int(sys.argv[10]) if len(sys.argv) > 10 and sys.argv[10] else 0
# and players the package declares (input.players), as JSON: each control's
# player by the "P<n> " in its name, then these over it ({"Power": 2})
players_over = json.loads(sys.argv[11]) if len(sys.argv) > 11 and sys.argv[11] else None
# and the controls TAStudio shows by default (input.tastudioColumns), as JSON
shown = json.loads(sys.argv[12]) if len(sys.argv) > 12 and sys.argv[12] else None
try:
    zin = zipfile.ZipFile(src)
except OSError:
    open(out, "wb").close()
    sys.exit(0)
with zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for info in zin.infolist():
        data = zin.read(info)
        if info.filename == "waterbox.config":
            cfg = json.loads(data)
            cfg["coreName"] = name
            if kind:
                cfg["kind"] = kind
            if firmware is not None:
                cfg["firmware"] = firmware
            if version_date:
                cfg["versionDate"] = version_date
            if more_buttons:
                cfg["input"]["buttons"] = cfg["input"]["buttons"] + more_buttons
            if precompile:
                cfg["precompile"] = True
            if square:
                cfg["video"].pop("virtualWidth", None)
                cfg["video"].pop("virtualHeight", None)
                cfg["video"]["width"] = 320
            if rate:
                cfg["audio"]["rate"] = rate
            if shown is not None:
                cfg["input"]["tastudioColumns"] = shown
            if players_over is not None:
                controls = cfg["input"]["buttons"] + [a["name"] for a in cfg["input"].get("axes", [])]
                def player(n):
                    if n in players_over:
                        return players_over[n]
                    head = n.split(" ", 1)[0]
                    return int(head[1:]) if " " in n and head[:1] == "P" and head[1:].isdigit() else 0
                most = max([player(n) for n in controls] + [0])
                cfg["input"]["players"] = [[n for n in controls if player(n) == p] for p in range(1, most + 1)]
            data = json.dumps(cfg, indent=2).encode()
        zout.writestr(info, data)
