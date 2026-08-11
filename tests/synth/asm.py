#!/usr/bin/env python3
"""Assembler for Synth machine .testrom files (see SPEC.md).

Source format, by example:

    .entry main          ; entry label (default: offset 0)
    .code
    main:
        MOVI R0, 0x10
        INPUT R1
        BEQ R0, R1, main
        RECT R2, R3, R4, 8, 8    ; w,h assembled into imm
        HALT
    .palette             ; exactly 16 lines RRGGBB (hex)
    000000
    ...
    .tile                ; 8 lines of 8 hex digits (palette indices); repeatable
    01111110
    ...
    .jingle              ; starts a new jingle; note lines: freq vol frames
    440 128 10
    .data                ; hex byte lines (whitespace-separated)
    00 01 02

Comments start with ';'. Numbers are decimal or 0x-hex. Branch/JMP immediates
may be labels.
"""
import re
import struct
import sys

# name -> (opcode, operands) where operands is a string of:
#   a/b/c = register into that field, i = imm, w = "w, h" pair packed into imm
INSNS = {
    "HALT":  (0x00, ""),
    "MOVI":  (0x01, "ai"),
    "MOV":   (0x02, "ab"),
    "ADD":   (0x03, "abc"),
    "SUB":   (0x04, "abc"),
    "MUL":   (0x05, "abc"),
    "DIV":   (0x06, "abc"),
    "AND":   (0x07, "abc"),
    "OR":    (0x08, "abc"),
    "XOR":   (0x09, "abc"),
    "SHL":   (0x0A, "abc"),
    "SHR":   (0x0B, "abc"),
    "ADDI":  (0x0C, "abi"),
    "LDB":   (0x10, "ab"),
    "STB":   (0x11, "ab"),
    "LDW":   (0x12, "ab"),
    "STW":   (0x13, "ab"),
    "LDD":   (0x14, "ab"),
    "JMP":   (0x20, "i"),
    "BEQ":   (0x21, "abi"),
    "BNE":   (0x22, "abi"),
    "BLT":   (0x23, "abi"),
    "BGE":   (0x24, "abi"),
    "INPUT": (0x30, "a"),
    "FRAME": (0x31, "a"),
    "CLEAR": (0x40, "a"),
    "PIXEL": (0x41, "abc"),
    "RECT":  (0x42, "abcw"),
    "TILE":  (0x43, "abc"),
    "TONE":  (0x50, "ab"),
    "STOP":  (0x51, ""),
    "PLAY":  (0x52, "a"),
}


def parse_num(tok, labels=None):
    if labels is not None and tok in labels:
        return labels[tok]
    return int(tok, 0)


def assemble(srcPath, outPath):
    section = "code"
    code = []          # (lineNo, mnemonic, [operand tokens])
    labels = {}        # name -> instruction index (resolved to byte offset later)
    palette = []       # 16 ints RRGGBB
    tiles = []         # each: 64 palette-index bytes
    jingles = []       # each: list of (freq, vol, frames)
    data = bytearray()
    entry_label = None

    with open(srcPath) as f:
        for lineNo, raw in enumerate(f, 1):
            line = raw.split(";", 1)[0].strip()
            if not line:
                continue
            if line.startswith("."):
                parts = line.split()
                directive = parts[0]
                if directive == ".entry":
                    entry_label = parts[1]
                elif directive == ".code":
                    section = "code"
                elif directive == ".palette":
                    section = "palette"
                elif directive == ".tile":
                    section = "tile"
                    tiles.append(bytearray())
                elif directive == ".jingle":
                    section = "jingle"
                    jingles.append([])
                elif directive == ".data":
                    section = "data"
                else:
                    sys.exit(f"{srcPath}:{lineNo}: unknown directive {directive}")
                continue
            if section == "code":
                m = re.match(r"^(\w+):$", line)
                if m:
                    labels[m.group(1)] = len(code)
                    continue
                parts = line.replace(",", " ").split()
                code.append((lineNo, parts[0].upper(), parts[1:]))
            elif section == "palette":
                palette.append(int(line, 16))
            elif section == "tile":
                row = line.strip()
                if len(row) != 8:
                    sys.exit(f"{srcPath}:{lineNo}: tile rows are 8 hex digits")
                tiles[-1].extend(int(ch, 16) for ch in row)
            elif section == "jingle":
                freq, vol, frames = (int(t, 0) for t in line.split())
                jingles[-1].append((freq, vol, frames))
            elif section == "data":
                data.extend(int(t, 16) for t in line.split())

    # resolve labels to byte offsets
    labels = {name: idx * 8 for name, idx in labels.items()}

    codeBytes = bytearray()
    for lineNo, mnem, ops in code:
        if mnem not in INSNS:
            sys.exit(f"{srcPath}:{lineNo}: unknown instruction {mnem}")
        opcode, pattern = INSNS[mnem]
        a = b = c = imm = 0
        want = len(pattern) + (1 if "w" in pattern else 0)  # w consumes two tokens
        if len(ops) != want:
            sys.exit(f"{srcPath}:{lineNo}: {mnem} takes {want} operands, got {len(ops)}")
        toks = list(ops)
        for field in pattern:
            if field in "abc":
                tok = toks.pop(0)
                m = re.fullmatch(r"[Rr]([0-7])", tok)
                if not m:
                    sys.exit(f"{srcPath}:{lineNo}: expected register R0..R7, got {tok}")
                val = int(m.group(1))
                if field == "a":
                    a = val
                elif field == "b":
                    b = val
                else:
                    c = val
            elif field == "i":
                imm = parse_num(toks.pop(0), labels)
            elif field == "w":
                w = parse_num(toks.pop(0))
                h = parse_num(toks.pop(0))
                if not (0 <= w <= 255 and 0 <= h <= 255):
                    sys.exit(f"{srcPath}:{lineNo}: rect w/h must be 0..255")
                imm = w | (h << 8)
        codeBytes += struct.pack("<BBBBI", opcode, a, b, c, imm & 0xFFFFFFFF)

    gfx = bytearray()
    if palette or tiles:
        if len(palette) != 16:
            sys.exit(f"{srcPath}: .palette must have exactly 16 entries (got {len(palette)})")
        for rgb in palette:
            gfx += bytes(((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF))
        for t in tiles:
            if len(t) != 64:
                sys.exit(f"{srcPath}: each .tile must have 8 rows (64 pixels)")
            gfx += bytes(t)

    snd = bytearray()
    if jingles:
        snd += struct.pack("<H", len(jingles))
        for j in jingles:
            snd += struct.pack("<H", len(j))
            for freq, vol, frames in j:
                snd += struct.pack("<HBB", freq, vol, frames)

    entry = 0
    if entry_label is not None:
        if entry_label not in labels:
            sys.exit(f"{srcPath}: .entry label {entry_label} not defined")
        entry = labels[entry_label]

    header_size = 48
    codeOff = header_size
    gfxOff = codeOff + len(codeBytes)
    sndOff = gfxOff + len(gfx)
    dataOff = sndOff + len(snd)
    rom = bytearray()
    rom += b"SYNTHROM"
    rom += struct.pack("<HH", 1, 0)
    rom += struct.pack("<I", entry)
    rom += struct.pack("<II", codeOff, len(codeBytes))
    rom += struct.pack("<II", gfxOff if gfx else 0, len(gfx))
    rom += struct.pack("<II", sndOff if snd else 0, len(snd))
    rom += struct.pack("<II", dataOff if data else 0, len(data))
    rom += codeBytes + gfx + snd + data
    with open(outPath, "wb") as f:
        f.write(rom)
    print(f"{outPath}: {len(codeBytes)//8} instructions, {len(tiles)} tiles, "
          f"{len(jingles)} jingles, {len(data)} data bytes, {len(rom)} bytes total")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: asm.py <source.sasm> <out.testrom>")
    assemble(sys.argv[1], sys.argv[2])
