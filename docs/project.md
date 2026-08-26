# The Chimera project: TAS-only refocus

Decided with the user 2026-08-26. Chimera is a TAS tool, not a frontend for
casual gaming. There is exactly one use case: open a project, work in
TAStudio. Everything below follows from that.

## The entry point

Chimera's entry point is not "load core" nor "load rom": it is the project.
The GUI opens on a start screen - New Chimera Project, Open Project, recent
projects - and nothing else. Creating or opening a project leads directly
into TAStudio; TAStudio is no longer a tool among others, it is the
application. The casual paths are fully removed: no Load ROM menu, no
runtime Emulator > Core picker, no live sync-settings dialogs. Core
development and debugging needs are covered headlessly by chimera-run,
which learns to take a project file.

## What a project is

A single JSON file (extension `.chimeraProject`) that contains literally
everything required to reproduce the work, except the data bytes
themselves, which are named by their SHA1:

- **Core identity**: package name, version, hash - pinned at creation.
- **File manifest**: names + SHA1 + the core-defined slot id each file
  fills (cdrom, floppy, hdd, config, ...), order within a slot = swap
  order, cue-referenced bins auto-added as support, cue closure enforced.
  The generic image/support/savedata role triple retires; opening
  validates the manifest against the core's slot declaration. The
  standalone `.chimeraMultiFile` format does not survive as a GUI-facing
  concept; the engine's `ce_multifile_*` code evolves into this section,
  and headless descriptors remain available during the transition.
- **Firmware**: resolved at creation, pinned by hash.
- **Sync settings**: defined once, in the creation wizard.
- **The TAS work itself**: input log, markers, branches. The project IS
  the movie; publishing a finished TAS means handing over the project
  file, and anyone holding files matching the SHA1s can reproduce
  everything.

Not in the project:

- **Paths.** Never stored. Only names and SHA1s.
- **Greenzone / cached states.** A sibling cache file next to the project;
  if present it is loaded, otherwise the session starts from a clean
  state-memory slate. Losing it costs recomputation, never work.
- **Per-user preferences.** Keybinds, window layout, hotkeys stay in user
  config; the project holds only what affects sync and reproduction.

## Creation

One wizard, everything at once. Step one asks project title, emulation
core, and project description. Step two asks for the files - and it is
NOT a blind "add any files" dialog: it is a fully core-informed form.
The core declares categories (CD-ROMs, floppy disks, hard disk,
configuration files, save states, ...), each rendered as empty slots with
a cardinality (exactly 1, 0-N, 1+), the accepted formats, and a small
circled question-mark tooltip explaining what is expected there and in
what format. The form's content is fully decided, populated and formatted
by each core; Chimera only provides the interface and the execution. Files
are hashed as they are added, and order within a category is the swap
order. Then firmware and sync settings, and on create, TAStudio opens at
frame 0.

The declaration lives in a static file generated at core build time and
shipped in the core package - the same pattern as `waterbox.config` for
settings and `default_keybinds.json` for binds. The wizard reads it with
no guest boot, and the declaration is versioned with the core.

### The slot declaration: file_slots.json

One file per core package, beside `waterbox.config`. Read by the wizard
to build the form, and by `ce_project_*` to validate a manifest:

```json
{
  "slots": [
    {
      "id": "floppy",
      "title": "Floppy disks",
      "min": 0,
      "max": -1,
      "formats": ["img", "ima", "xdf", "fdi", "hdm", "nfd", "d88"],
      "help": "what belongs here and in which format (the tooltip text)"
    }
  ],
  "atLeastOneOf": [["floppy", "cdrom", "hdd", "conf"]]
}
```

- `id`: bare lowercase token, unique within the core. This is what a
  manifest entry records as its slot, and what mount naming derives
  from.
- `title`: the category heading the form shows.
- `min` / `max`: cardinality; `max` -1 means unbounded. Exactly one =
  1/1, zero or more = 0/-1, one or more = 1/-1.
- `formats`: accepted file extensions, lowercase without the dot; the
  wizard filters file pickers by them and validation checks membership.
  An extension may legitimately appear in several slots (`.img` is a
  floppy or a hard disk - the user's slot choice resolves what
  sniffing had to guess).
- `help`: the circled-question-mark tooltip text.
- `atLeastOneOf` (optional): groups of slot ids; across each group at
  least one file must be present, for requirements no single slot's
  `min` can express ("some bootable medium").
- `support` is a reserved id: files referenced by a listed cue are
  auto-added to the manifest with slot `support`; a core never declares
  it.

## Editing

Anything in a project can be changed after it is opened, with one
exception: the core. The core selection is fixed at creation - a
different core means a new project. A **structural** change - one that
affects sync: sync settings, the file manifest - first asks: "this will
erase the greenzone and possibly desync your current inputs, are you
sure?". On yes: the greenzone is cleared, the core restarts, and the
frame selector returns to frame 0. The input log is kept (it may no
longer sync; that is the user's informed call). Non-structural changes
(title, description, markers, ...) apply freely.

## Opening

Opening a project first shows a file-resolution dialog: the user provides
the location of every required file for this session (locations are
per-session, wherever the files happen to live). As each file is provided
its SHA1 is checked and divergence is warned about immediately, in the
dialog. A differing file NAME on disk is fine - the project's recorded
name is the canonical label and mount name, the hash is the identity.

Core pin mismatch (installed package differs from the pinned
version+hash): refuse by default with a clear pinned-vs-installed message,
with a knowing override - and the project then records what actually ran,
mirroring the file-hash posture.

## Architecture decisions

- **Serializer in the engine**: `ce_project_*` in libchimera reads,
  writes and validates the project (manifest, core pin, settings, input
  log, markers, branches), evolving `ce_multifile_*` and the
  engine-resident input log. chimera-run and every gate consume projects
  for free; C# only renders UI over it.
- **Movie code is absorbed**: the engine's input-log/movie code becomes
  the project's input-log section and the standalone movie format retires
  from the GUI. Headless chimera-run and the gates keep old movie files
  working during the transition, then migrate to projects.

## Status

- Decided: everything above.
- DONE: file_slots.json in all four core packages; ce_project_* in the
  engine (open/save/resolve/validate/slots map) with test_project;
  chimera-run --project (resolution, core pin, declaration validation,
  transitional rom mounts); cores consume the "slots" mount through the
  guest kit's waterbox_slots.h - DOSBox-X mounts mixed media (floppies +
  CDs + hard disk + conf, gated: the slots leg, 12/12), PPSSPP boots the
  disc slot, both NES cores read the rom slot.
- TODO: the frontend (start screen, creation wizard rendering
  file_slots.json, resolution dialog, TAStudio-as-application, removal
  of the casual paths); PPSSPP memstick-zip import and NES SRAM import
  as future savedata-consuming slots; retiring the transitional
  rom/rom.name/rom2..N mounts once nothing legacy needs them.
