# Multi-file games: the .chimeraMultiFile descriptor

A game made of several files - multiple floppies or discs, a cue and its
bin, previously exported save data - is named by one descriptor file, and
every byte it brings to the machine is hashed. BizHawk's multi-disk bundler
(an XML of paths) is the ancestor; this design trades its flexibility for
reproducibility.

## The descriptor

A JSON manifest, not a container:

```json
{
  "files": [
    { "name": "game_disc1.cue", "sha1": "AB12...", "role": "image" },
    { "name": "game_disc1.bin", "sha1": "CD34...", "role": "support" },
    { "name": "game_disc2.cue", "sha1": "EF56...", "role": "image" },
    { "name": "game_disc2.bin", "sha1": "9A78...", "role": "support" },
    { "name": "my_progress.hdd", "sha1": "1B2C...", "role": "savedata" }
  ]
}
```

- **Names are bare.** Every file lives in the descriptor's own folder; a
  name with a path separator is structurally invalid. Names are recorded
  exactly (case included - Linux would fail a case mismatch, so every
  platform must).
- **Order matters.** The images' order IS the swap order: the first image
  mounts as `rom` (its name mounted as `rom.name`), the rest as
  `rom2..romN` - the disk-swap inputs cycle them in exactly this order.
- **Roles.** `image`: an ordered, mountable, swappable item. `support`:
  present and hashed, but only referenced by another file (a cue's bin),
  mounted under its own name. `savedata` (at most one): a previously
  exported save, mounted under the fixed name `savedata` for the core to
  consume by its own nature (DOSBox-X seeds the hard disk from it, PPSSPP
  unpacks a memstick zip, a NES core fills battery RAM). Cores that consume
  it advertise so; the frontend warns when a descriptor carries savedata
  for a core that cannot take it.
- **No system field, no format/version field.** The core is chosen before
  the game in chimera, and the hashes - not the schema - carry the
  reproducibility weight.

## The rules (engine-enforced, ce_multifile_*)

Structural, rejected outright: invalid JSON, no `files`, empty list, a
non-bare or duplicated name, an unknown role, a malformed sha1, no image,
more than one savedata, and **cue closure** - every file a listed cue
references must itself be listed, or unhashed bytes would reach the
machine.

Per-file, reported not fatal: a missing file, a hash mismatch. The caller
(the frontend) refuses by default and may knowingly proceed - safe, because
the movie records what was actually loaded, never what the descriptor
claimed. Creation (`ce_multifile_save`) is strict where loading is lenient:
every file must be present and closed over, or the save is refused - an
incomplete set is never intentional.

## Movies

A movie recorded from a multi-file game carries one header line:

```
GameFiles game_disc1.cue=AB12... game_disc1.bin=CD34...:support game_disc2.cue=EF56... ...
```

Entries in descriptor order (order is meaning - the firmware line sorts,
this one must not), names percent-encoded ('%', '=', ':' and anything
outside 0x21..0x7E), images untagged, other roles tagged after the hash,
and the hashes are those of the bytes ACTUALLY loaded. The movie is fully
self-describing: playback needs no descriptor, only files whose names and
hashes match. A movie that began from restored save data names the exact
bytes it began from; a movie from power-on simply has no savedata entry.

## Loading

`chimera-run` (and the frontend) route a `.chimeraMultiFile` rom through
`ce_multifile_open`, then mount: the first image as `rom` plus its real
name as `rom.name`, further images as `rom2..romN`, support files under
their own names, savedata as `savedata` - all through `ce_session_open`'s
extra-files channel. Within a descriptor, names are contract (they are
recorded in the movie); the bare single-rom path stays nameless, as
decided for PPSSPP.

## Status

- Engine: DONE (ce_multifile_* in engine.h, multifile.cpp, test_multifile,
  chimera-run descriptor loading; proven against the DOSBox-X core with a
  two-floppy descriptor, including the tamper-refusal path).
- Frontend: TODO - open path + hash-mismatch dialog, GameFiles in movie
  headers and playback verification, the creation dialog
  (File > Create Multi-File Game...), Export Save Data's "attach to
  descriptor" flow, and the disc-swap OSD (mirror the guest's pending
  selection, show names from the descriptor).
- Cores: TODO - consume the `savedata` mount (DOSBox-X hdd seed precedence:
  savedata > .hdd rom > formattedHardDisk; PPSSPP memstick zip; NES SRAM),
  and a gate leg per core proving descriptor load + swap order + movie
  round-trip.
