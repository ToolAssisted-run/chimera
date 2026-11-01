"""Writes a copy of the synthetic core package as "Synth Slots": a package of
two machines (boards) with declared file slots, for the New Project tests.
Board A takes the synthetic game as a cartridge; board B takes disks (a cue
sheet or an image named like a disk) and needs a firmware file. At least
one of the two kinds of file is needed. It has an analog control, a dial.
With no package to copy it writes an empty file, and the tests that need it
skip."""
import json
import sys
import zipfile

src, out = sys.argv[1:3]
try:
    zin = zipfile.ZipFile(src)
except OSError:
    open(out, "wb").close()
    sys.exit(0)
slots = {
    "slots": [
        {"id": "rom", "title": "Cartridge", "min": 0, "max": 1, "formats": ["testrom"],
         "help": "The synthetic game.", "exposedWhen": {"setting": "board", "is": "a"}},
        {"id": "disk", "title": "Disks", "min": 0, "max": -1, "formats": ["cue", "img", "dsk"],
         "help": "Disks, in the order they are swapped.", "namePattern": "^disk[0-9]*\\.(cue|img|dsk)$",
         "nameHelp": "A disk is called disk, a number and .cue, .img or .dsk."},
    ],
    "atLeastOneOf": [["rom", "disk"]],
}
with zin, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
    for info in zin.infolist():
        # the base package's own slots (one game) give way to these
        if info.filename == "file_slots.json":
            continue
        data = zin.read(info)
        if info.filename == "waterbox.config":
            cfg = json.loads(data)
            cfg["coreName"] = "Synth Slots"
            cfg["machineSetting"] = "board"
            cfg["settings"].append({"name": "board", "display": "Board", "type": "enum",
                                    "options": ["a", "b"], "default": "a", "sync": True,
                                    "description": "Which board the project is."})
            cfg["settings"].append({"name": "diskSpeed", "display": "Disk Speed", "type": "int",
                                    "min": 1, "max": 4, "default": 1, "sync": True, "when": ["b"],
                                    "description": "Board B's alone."})
            cfg["settings"].append({"name": "renderer", "display": "Renderer", "type": "enum",
                                    "options": ["software", "opengl-hw"], "default": "software", "sync": True,
                                    "description": "Where the picture is drawn."})
            cfg["settings"].append({"name": "port", "display": "Port", "type": "enum",
                                    "options": ["pad", "mouse", "none"], "default": "pad", "sync": True,
                                    "description": "What is plugged in; each board narrows it."})
            # each board claims kinds of file of its own: the disk slot takes
            # board A's .dsk on board A and board B's .cue and .img on board B
            cfg["machines"] = [
                {"id": "Synth", "label": "Board A", "when": ["a"],
                 "extensions": {".testrom": "Synth", ".dsk": "Synth"},
                 "settingOverrides": {"port": {"default": "mouse"}}},
                {"id": "SynthB", "label": "Board B", "when": ["b"],
                 "extensions": {".cue": "SynthB", ".img": "SynthB"},
                 "settingOverrides": {"port": {"options": ["pad", "none"], "default": "none"}}},
            ]
            # two entries share an id, a setting picking which (issue 390):
            # the fastest disks want a BIOS of their own
            cfg["firmware"] = [{"id": "bios", "display": "Board B BIOS", "name": "boardb.bin",
                                "requiredWhen": {"all": [{"setting": "board", "is": "b"},
                                                         {"not": {"setting": "diskSpeed", "is": 4}}]}},
                               {"id": "bios", "display": "Board B BIOS", "label": "fast disks", "name": "boardbfast.bin",
                                "sha1": "7CDBA0191F422D615674D790C6E1047F65B17F76",
                                "requiredWhen": {"all": [{"setting": "board", "is": "b"},
                                                         {"setting": "diskSpeed", "is": 4}]}}]
            # presets: one for every board (with a value for no setting, and the
            # board, which is not a preset's to move), one for board B alone
            cfg["presets"] = [
                {"id": "full", "label": "Full fill", "description": "fills RAM with 255",
                 "values": {"initFillByte": 255, "board": "b", "nope": 1}},
                {"id": "fast", "label": "Fast disks", "when": ["b"], "values": {"diskSpeed": 4}},
            ]
            # the synthetic core suggests settings for a game (SuggestSettings),
            # and its games have settings and firmware of their own
            cfg["suggestSettings"] = True
            cfg["gameFirmware"] = True
            # a tool of the synthetic core's own (its Emulator menu): the parts
            # picked, joined in their order into one file, each backwards when
            # the option is ticked
            cfg["tools"] = [{
                "id": "join",
                "menu": "Join Synth Parts...",
                "description": "Joins the parts into one file, in the order they are listed.",
                "inputs": [{"name": "part", "label": "Parts", "extensions": ["part"], "multiple": True, "required": True}],
                "options": [{"name": "backwards", "display": "Backwards", "description": "Each part's bytes in the opposite order."}],
                "outputs": [{"name": "joined", "label": "Joined file", "extension": "bin", "nameFrom": "part"}],
            }]
            # a disc recipe (the Media Maker hands it to the engine for a folder it is for)
            cfg["media"] = [{"id": "disc", "label": "synthetic disc", "format": "iso9660",
                             "when": {"rootFile": "SYNTH.BIN"}, "systemArea": [{"at": 0, "u32be": 7}]}]
            # an analog control, for the piano roll's analog columns
            cfg["input"]["axes"] = [{"name": "P1 Dial", "min": -100, "max": 100, "neutral": 0, "header": "Dl"}]
            data = json.dumps(cfg, indent=2).encode()
        zout.writestr(info, data)
    zout.writestr("file_slots.json", json.dumps(slots, indent=2))
