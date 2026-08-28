# Save data: the core keeps it, the user takes it out

Status: SPEC + first tenant (PPSSPP memstick). User-decided 2026-08-25.
This is the third design for "what a machine keeps" (see design-principles.md,
"Storing progress: cleared to the ground" for the two removed ones).

## The design, in one paragraph

A core that has save data (memory card files, a hard disk image, SRAM the
machine writes) keeps it **inside the guest machine**, in ordinary guest
memory. The frontend never touches it, never writes it to disc on its own,
and never learns what it means. When the user wants their progress out, they
pick `Emulator > Export Save Data...`, which is enabled whenever the loaded
core exposes the capability - no change detection, no automation, no timer:
the user knows when their progress is worth exporting. The export is the
core's own enumeration of `(relative path, bytes)`, written by the frontend
as a zip (or, for a single file, as that file). Taking the exported files
back in happens through game inputs - mounted, hash-bound files the movie
header cites - so a movie's starting save data is reproducible by hash,
exactly like its rom and firmware.

## Why in-guest storage costs nothing (the BizHawk/DOSBox lesson)

The proven recipe is BizHawk's DOSBox-X core (`waterbox/dosbox/bizhawk.cpp`):
the 2 GB hard-disk image enters as a read-only file, and guest code copies it
into a memory-backed file **before seal**. That looks wasteful until you
remember what seal means: the whole copy becomes part of the sealed baseline
snapshot, and savestates are page-granular dirty tracking against that
baseline. A savestate therefore carries only the pages the game actually
wrote since boot - dirtied sectors of the disk image, not the 2 GB - and
rewind/loadstate/TAS re-runs are correct by construction, because the save
data rolls back with the rest of the machine. miniBox has the identical
machinery (`source/host/memblock.c`, `MB_SNAP_ZERO` for untouched zero
pages), so no sandbox changes are needed: **a core that wants writable
files builds them in guest memory and declares a heap big enough**.

Consequences a core author must respect:

- Seed writable data from read-only mounts **during Init, before seal**, so
  the copy lands in the baseline, not in every savestate.
- Never keep save data host-side or in `ECL_INVISIBLE` memory: anything the
  machine can change must be captured by whole-machine savestates, or rewind
  silently corrupts it.
- Size the memory layout for the largest image the core accepts. The budget
  is declared, visible, and bounded - unbounded host-disk growth is the thing
  this design kills.

Cores whose persistent state is plain machine memory (NES SRAM and the like)
need none of this MACHINERY: their save data is already machine state, already
rewinds, and is already reachable through savestates. They still export the
group, though - user-decided 2026-08-28, revising the original "they simply do
not". Reproduction was never the question: without an export there is no way to
take a saved game off a cart, and without the import slot below no project can
start from one. What such a core exports is the memory itself (a NES cart's
battery WRAM), and what it refuses is a save for a machine that keeps none.

## The guest ABI group: savedata export

The sixth optional guest export group, probed like the other five (a missing
symbol comes back from `wbx_get_proc_addr` as address 0). All four exports
present = the capability exists; any absent = no capability.

```c
/* Snapshots the exportable file list and returns its length. The list is
 * DYNAMIC (a game creates files while it runs), so this is called at export
 * time, never cached from Init. Index results below refer to the snapshot
 * taken by the most recent call. */
ECL_EXPORT int32_t GetSaveDataFileCount(void);

/* The file's path, relative, '/'-separated, no leading '/', no "..".
 * This is the zip entry name the user sees, and the name a future game
 * input uses to put the file back. Borrowed; valid until the next
 * GetSaveDataFileCount call or guest re-entry. */
ECL_EXPORT const char *GetSaveDataFileName(int32_t index);

ECL_EXPORT int64_t GetSaveDataFileSize(int32_t index);

/* Guest pointer to the file's bytes, contiguous, size bytes long. The host
 * reads it directly (frame boundary, guest halted) - bulk data never crosses
 * as a call per byte. */
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t index);
```

Rules:

- Everything happens at a frame boundary, like everything in chimera. The
  host only calls between `FrameAdvance`s, so the guest never snapshots a
  torn mid-frame state.
- The guest decides what "save data" means: the whole memory stick, one disk
  image, a directory tree. The host applies no filter and no interpretation.
- Names are identity: export the same path a seeded input file arrived under,
  so export-then-reimport round-trips.
- Zero files is a valid answer (a memory card with nothing on it yet). The
  capability - and the menu item - exist regardless.

## The engine ABI

```c
/* nonzero when the core exports the savedata group */
int32_t ce_session_savedata_available(const ce_session *s);
/* snapshots the guest's list; names and sizes below refer to this snapshot */
int32_t ce_session_savedata_count(ce_session *s);
const char *ce_session_savedata_name(const ce_session *s, int32_t index);
int64_t ce_session_savedata_size(const ce_session *s, int32_t index);
/* copies out [offset, offset+len) of file index; returns bytes copied */
int64_t ce_session_savedata_read(ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len);
```

The engine copies names and sizes out of the guest at snapshot time, and
validates every name (relative, no "..", no backslashes; an offending entry
is dropped with a warning on stderr rather than handed to a zip writer). The
ranged read is deliberate: a 2 GB disk image streams out in chunks, never
materialized whole on either side of the boundary.

`chimera-run` grows `--export-savedata <dir>` (writes the tree under dir),
which is how gates compare an export against goldens and against the native
reference build.

## The frontend

One menu item, `Emulator > Export Save Data...`, added by
`DisplayDefaultCoreMenu` when the running core's service provider still has
`ICoreSaveData` (the service is implemented unconditionally on WaterboxCore
and unregistered when the group is absent - the same gating as every other
core-managed service). On click:

- zero files: a plain message, nothing written;
- one file: a save dialog for that file under its own name;
- several: a save dialog for a `.zip`, entries written streamed.

The frontend never parses a byte of it. There is no import UI here and none
is planned: files go back in as game inputs.

## Taking it back in (BUILT, 2026-08-28)

The descriptor this section anticipated is the project's slot declaration
(docs/project.md), so the input side is one more slot. Every core that keeps
save data declares:

```json
{ "id": "savedata", "title": "Save data", "min": 0, "max": 1,
  "formats": ["sav"], "help": "..." }
```

and seeds from it **during Init, before seal**, so the contents land in the
sealed baseline and a savestate carries only what the game wrote since. Each
file is mounted read-only and hash-bound like any input and the movie header
cites it, so a movie's starting save data is reproducible the way its rom and
firmware are.

Three rules, learned building the first six:

- **Names are identity.** A project mounts a file under its recorded name, so
  a core seeds by opening the name it EXPORTS. Export and import are the same
  list read in two directions, and a round trip is the test.
- **Refuse what will not be read.** A core that is handed save data it does
  not recognize - a name it never opens, a cart with no battery, a file the
  wrong size - fails the load and says why. A project carrying someone's
  progress that is silently ignored is worse than one that will not start.
- **Several files, or a tree, become a zip**, because that is what the export
  writes. PPSSPP unpacks one onto its memory stick; libzip reads it from
  MEMORY, since its file source wants fcntl and a sandbox does not answer.

Built for: PCSX2 (memcard1.ps2, memcard2.ps2, bios.nvm), Flycast (vmu_A1.bin,
vmu_A2.bin), Opera (NVRAM.ram), PPSSPP (the stick, as a zip), quickerNES and
QuickerNesHawk (battery.sav, and disk.sav on an FDS).

## Tenants

- **PPSSPP** (first, done): the RAM memstick (`ram-filesystem.cpp`) is
  already exactly the in-guest storage this spec requires; the group walks
  its node tree and exports every file (standard empty directories carry no
  entry). `run-wbx.c` grows `--savedata-out <dir>` and the gate compares
  native, sandbox and rerecord exports byte for byte.
- **DOSBox** (future): the BizHawk recipe ported - image seeded from its
  read-only mount pre-seal, one exported file. The 2 GB case is why
  `ce_session_savedata_read` is ranged.
- **NES cores**: not tenants. SRAM is machine state; savestates already
  carry it.
