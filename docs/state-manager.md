# The state history

How Chimera remembers where a machine has been: rewind, the greenzone, branch
states, and what survives closing a project.

Status: phase 1 BUILT (miniBox 0c972de, with its measurements below); the rest
is design, decided with the user 2026-09-08.
It replaces the surviving implementations with one, and it is the remaining half of
step 5 of the engine migration (docs/engine-migration.md), which always said
TAStudio's state history would move onto the session's.

## Why this is being redone

There are two answers in the tree to one question - what did this machine look
like at frame N:

- the engine's greenzone (`ce_session_greenzone_*`): budget-bounded, anchored,
  with seek and invalidate. Witnessed by the gates. Only `chimera-run` uses it.
- `PagedStateManager` (C#): what TAStudio actually uses, with the older
  `ZwinderStateManager` still selectable beside it and `ZwinderBuffer`
  underneath that one as its storage.

There is deliberately no third one for rewind, and it is worth being exact
about why, because the name suggests otherwise. `MainForm.Rewind` does nothing
unless a tool claims `WantsToControlRewind`; TAStudio is the only tool that
does, and in a TAS-only frontend TAStudio is always there. Its `Rewind()` is
`WheelSeek` - a seek backwards through the greenzone - and its `CaptureRewind`
is a no-op with a comment saying TAStudio handles this just fine. There is no
rewind buffer and no rewind setting left in config. Rewind is a gesture over
the state history, not a mechanism of its own.

The two that remain are BizHawk-lineage designs, written when a savestate was
kilobytes and a big one was a few megabytes. Chimera's cores are whole sandboxed
machines. Measured on the user's own Prince of Persia run under xemu, a state
is 75 MB at frame 150, 102 MB at 600 and 210 MB at 1200: it grows as the game
touches memory, and it is the whole machine every time.

Everything downstream of that number is now wrong:

- **Saving loses the work silently.** The greenzone is serialized through
  managed `byte[]` buffers - the history into one array, each lump into
  another, the whole archive into a third - and a .NET array is indexed by
  `int`, so it stops near 2 GB. A 3711-frame run is about 5.5 GB of states.
  The write throws, a `catch` swallows it, and the project is saved with no
  states at all and nothing said. Reopening then replays from frame 0, which
  is what the user reported and what started this.
- **Stepping back costs a seek.** Holding rewind asks for the nearest state and
  replays forward to the frame before the one you were on. On a core that
  emulates at 52 ms a frame that is as expensive as jumping anywhere else in
  the run, which is why going back one frame does not feel like the inverse of
  going forward one frame.
- **The right implementation is the unused one.** The engine's greenzone is in
  the right place - linkable by jaffarPlus, testable without Mono or a display
  - and the application never calls it.

A patch to the 2 GB ceiling would leave three implementations, a rewind that
does not work, and a state costing what the machine IS rather than what it DID.

## The decisions

Taken by the user, 2026-09-08:

1. **The cache lives in a per-user directory**, keyed by project identity, not
   beside the project. BUILT (`fbff1a4`): the project gained an id, minted at
   creation and carried in the file, and `ProjectCache` resolves a directory
   from it. The remembered file locations moved there first - a `.chimeraLocal`
   beside an older project is read once, moved, and taken out of the folder -
   so a project's folder now holds only the project. Projects live in synced folders - the user's are in
   Google Drive - and a multi-gigabyte sidecar there is uploaded on every save,
   with the sync client holding files open mid-write. It follows the core store
   (docs/core-manager.md): `%LOCALAPPDATA%\Chimera` on Windows, the XDG data
   directory elsewhere, `CHIMERA_DATA_HOME` overriding both.
2. **One implementation.** `PagedStateManager`, `ZwinderStateManager`, the
   settings chooser and the type-dispatching converter all go. Settings naming
   a deleted type fall back to defaults, which is safe precisely because the
   cache is regenerable.
3. **Rewind is not a separate thing**, and already is not one. It stays a
   gesture over the history, and the history is what has to make it cheap.
4. **The policy is locality, not a time budget.** SUPERSEDED and retaken,
   2026-09-08. It was "a seek should cost about a second at worst", and that
   promise cannot be kept: churn is not stationary, so a chain sized to hit a
   second on the mean frame overshoots on a busy one - which is exactly the
   scene somebody is scrubbing through when they notice. The history is instead
   dense where the work is and coarse where it is not, and the time a seek takes
   follows from that shape rather than being promised in advance. See "The
   policy: dense near the work" below. Measurement does not go away; it sets the
   defaults per core instead of enforcing a guarantee.
5. **A branch keeps a whole state of its own.** Taken 2026-09-08, settling the
   one question phase 3 could not start without. A branch is somebody's
   alternative route, and restoring one must never walk a delta chain or depend
   on the history it was made in: the history is a cache that coarsens and
   evicts around it, and a branch that became unreachable because of that would
   have lost work. So a branch stores a whole state, as it already does
   (`TasBranch.CoreData`, a `CloneSavestate`), and phase 3 keeps it that way
   rather than making a branch an anchor in the session's history.

   WHERE that whole state is changed on 2026-09-17 (user-decided; issue #84).
   It was a byte array in the frontend, and an array cannot be more than 2 GiB:
   a PS3's state is 4.3 GiB even compressed, and creating a branch threw an
   overflow. The state is now a FILE beside the project's cache
   (`Branches/<id>.state`), written and read by the engine
   (`ce_session_state_save_file` / `_load_file`), which streams the machine
   through zstd to the file and back - the sandbox saves and loads through
   callbacks, so the state never exists whole anywhere and has no size it
   cannot be. The branch holds the file's NAME (`TasBranch.StateFile`). The
   decision itself is untouched: a branch still owns a whole state, outside the
   history, and one without it replays to its frame.

   Jumping to a branch may still hand its state to the history to hold as an
   anchor, which is what happens today - but that copy is the history's, and
   losing it changes nothing about the branch. What stays true either way is
   the split the rest of this rests on: a branch's frame, name, input log and
   markers are WORK and live in the project; its state is cache, and losing it
   costs replaying to that frame.
6. **miniBox grows delta states**, and the design goes straight for them rather
   than shipping content-addressed dedup first.

## What a state costs, and the idea that changes it

Today every stored state is a full snapshot of the machine relative to the
sealed baseline. Two states a few hundred frames apart are mostly the same
bytes, and both are stored whole.

miniBox already knows better than that. It maps writable pages read-only,
faults on first write, snapshots the page's previous contents and marks it
dirty - that is how a savestate carries only what changed since seal. What it
does not do is track change since any LATER moment.

So it grows one: an **epoch**. Marking an epoch re-protects the pages that are
currently writable, so the next write to each faults again and records two
things - that the page changed in this epoch, and what it held before. Both
fall out of machinery that already exists; the guest cannot observe any of it,
so the frozen machine spec is untouched and no movie is affected.

That gives the history two new kinds of thing to store:

- a **forward delta**: the pages changed during an epoch, as they ended. Apply
  it to the machine at the epoch's start and you have the machine at its end.
- a **reverse delta**: the same pages as they *began*, which the fault handler
  captured anyway. Apply it and you go back one epoch. (Removed later - see
  "What rewinding cost, and why it is gone". Every frame is reached forwards.)

The consequences are what make this worth doing:

- A state costs the frame's churn instead of the machine's size, so capturing
  every frame becomes affordable where capturing every fourth frame is not.
- With a delta per frame the greenzone is **complete**: every frame has a
  state, and seeking stops re-emulating anything. A seek becomes "load the
  nearest anchor, apply deltas forward", and the latency budget sets how far
  apart anchors sit, measured in delta applications rather than in emulated
  frames.
- ~~**Rewind stops being a seek.**~~ One frame back was to be one reverse
  delta, costing that frame's churn, instead of restoring the nearest state and
  replaying to get there. This is the one part of the design that did not
  survive contact with the measurement: see "What rewinding cost, and why it is
  gone".

Content-addressed storage still earns its place underneath, because deltas
repeat: a page written with the same bytes every frame is one chunk. Dedup
becomes a property of the store rather than a strategy of its own.

## The shape

**One history, owned by the engine.** Capture, storage, eviction, seek,
invalidate and persistence live in `libchimera`, next to the session that
already owns the movie and the frame position - the same rule as everything
else there: the frontend cannot desync what it cannot reach. TAStudio becomes a
view over it, asking where states exist and asking to be moved.

**Storage separated from policy.** The store knows about chunks, anchors,
deltas, a memory tier and a disk tier, and nothing about TAS. The policy knows
about anchors per second, pins, and budgets, and nothing about bytes on disk.
The current `PagedStateManager` reasons well about density and reservation;
that thinking survives, its storage does not.

**The store is the on-disk format.** Not a zip of lumps assembled in memory:
an append-only chunk file with an index, written as it goes. Saving a project
flushes and writes the index rather than rewriting gigabytes, so saving at
frame 3711 costs what changed since the last save. No archive is ever
materialized whole, in managed memory or otherwise, which is the bug class this
started with.

**The greenzone sidecar retires.** DONE. `.chimeraGreenZone` beside the project
is now kept in the per-user cache, keyed by the project's id, so a project's
folder holds the project and nothing else.

One sentence here said an old cache could simply be ignored, since that costs
recomputation and never work - and while that is true of the RULE, it is a poor
thing to do on the release that moves the file. It would throw away somebody's
hours of greenzone silently AND leave the gigabyte sitting in the synced folder
that was the whole complaint. So a greenzone found beside a project is moved
into the cache the first time it is opened, and read where it lies if the move
will not happen.

## The ABI, and not breaking what works

`chimera-run` and every gate drive `ce_session_greenzone_*` today, and the
engine header is additive-only by rule. So the existing entry points stay and
are reimplemented over the new history with their current meanings, and the new
capabilities arrive as new calls. The gates keep passing unchanged, which is
the point: a redesign of the state history must not be witnessed by a rewritten
witness.

## Proving it

The synthetic core cannot exercise any of this - its machine has 4096 bytes of
RAM - which is exactly why none of the existing gates caught a 2 GB ceiling.
**The synth machine gains a settable RAM size**, so a multi-gigabyte history is
something Chimera's own CI can produce in seconds without a real core, a rom or
firmware.

The legs that have to exist:

- **The history changes nothing.** A run with capture on and a run with capture
  off must be byte-identical. This is the one that matters; everything else is
  performance.
- **A delta is a state.** Anchor plus applied deltas must equal the full state
  taken at that frame, byte for byte, at every frame of a run.
- **A reverse delta is the previous frame.** Step back and forward across a run
  and land on the same machine each time.
- **Persistence round-trips across processes.** Write a history, exit, reopen,
  seek into it, and reach the machine a straight replay reaches - the check the
  user's crash and the empty greenzone would both have failed.
- **Eviction keeps every frame reachable.** Under budget pressure the history
  coarsens; no frame may become unreachable, and the anchor never goes.
- **An edit replayed is an edit typed.** Play to X, seek back to X-n, put
  DIFFERENT input in from there, and carry on past X to X+Y: the machine at
  X+Y must be the machine a straight run of the edited movie reaches. This is
  the promise a tool-assisted run actually rests on, and it is the one leg
  `--seek` cannot stand in for - a seek replays the SAME inputs, so it passes
  with the edit path broken.

  Two things it needs to be worth running. The comparison must be the
  MACHINE - the buses the core publishes, and the picture - and not the
  sandbox's arena state: the arena carries the guest heap as well, and two
  identical runs already differ there, so comparing it proves nothing in either
  direction. And the edit has to matter: an edit whose input the game ignores
  makes the whole comparison vacuous, so the input is calibrated per game
  (press what the game notices, measured, not assumed) and the run is rejected
  when the edited movie and the plain one reach the same machine anyway.

  `chimera-run --play <n> --seek <f> --edit-from <movie> --final-buses <file>`
  is the shape.

## What the first phase measured

Phase 1 is built (miniBox `0c972de`), and it turned the design's two open
questions into numbers. Measured on a real xemu boot into Prince of Persia,
1300 frames, null renderer:

| | |
|---|---|
| churn | mean 506 pages a frame (2.0 MB), max 7143 (28.6 MB) |
| a delta against a full state | 2.0 MB against 75 MB at frame 150, 210 MB by frame 1200 |
| cost of an epoch per frame | 49.2s -> 66.8s over the run, +14 ms a frame, 37% |

The saving is about a hundredfold, which is the number this design was betting
on, and it holds. The cost is not free and does not go away: an epoch per frame
means a fault per page written, so it scales with churn - the right shape, and
still 37% on this core. Two consequences for the phases below:

- **The epoch cadence is a policy knob, not a constant.** Per-frame epochs buy
  a complete history and a rewind that costs one frame; every few frames buys
  most of the saving for a fraction of the overhead. The engine picks it the
  same way it picks anchor spacing - from what it measures - and the dense
  cadence belongs near the playhead where somebody is working, not across a
  long unattended seek.
- **A complete per-frame history is still gigabytes.** 2 MB a frame is 2.6 GB
  for 1300 frames. Deltas make density affordable where it was impossible, but
  they do not remove the need to coarsen with distance, so the tiered policy
  stays exactly as important as it was.

One negative result worth keeping: restricting the post-mark protection refresh
to the runs that actually change, instead of walking the arena, was worth about
1% here. The fault path dominates, not the walk. It is kept because a big arena
with little churn - a 2 GB DOS machine writing fifty pages a frame - is the
case it exists for, but it is not where the time goes on a console.

## What a seek costs

Capture was the first half and storage was the second; getting a frame BACK is
the half the one-second decision actually rests on, and it was measured last.

`tests/perf/seekbench.cpp` runs a real xemu boot into Prince of Persia, captures
every frame of a stretch, and then times seeks to frames it has stored. Every
target is a stored frame, so the seek's own replay is zero frames and what the
clock sees is the restore alone. `CHIMERA_NO_DELTAS=1` makes every capture an
anchor, which is the same build, the same run and the same budget against whole
states - the A to this B.

300 captured frames, a 4 GB budget, null renderer, and a bare frame of 29 ms:

| | whole states | deltas |
|---|---|---|
| frames the budget held | 56 | 301 |
| a stored frame | 72.8 MB | 1.66 MB |
| a captured frame | 91.3 ms (+62) | 40.8 ms (+12) |
| worst seek | 19.7 s | 1.19 s |
| mean seek | 8.8 s | 0.61 s |

The old shape is worse at both ends at once, which is the part worth saying
plainly. It is not that whole states buy speed with memory: they cost five times
the capture overhead AND they hold a fortieth of the run, and because they hold
so little of it every seek lands far from a stored frame and turns into a replay
of a hundred-odd frames at 90 ms each. The 19.7 s worst case is 225 frames of
replay, and it grows with the run.

The delta side is linear and legible. From the traced 1200-frame run:

| | |
|---|---|
| anchor load | 30-45 ms, flat from a 68 MB state to a 201 MB one |
| a delta applied | 4.7 ms, steady across chains of 24 to 450 |
| so a restore | about 35 ms + 4.7 ms per link |

Two things follow.

**A delta is six times cheaper than re-running the frame.** 4.7 ms against 29
ms. That is the ratio that justifies deltas over the obvious alternative of
sparse anchors and replay, and it is a ratio to re-measure per core rather than
assume: a core whose frame is cheap and whose churn is heavy could invert it,
and for that core the right chain is short.

**The chain limit is the latency target, written in the wrong units.** 512 links
is 2.4 s, and the run's worst seek measured 2.07 s - so the constant's own
comment, which claimed it sat inside a second, was wrong. It is now 200, which
makes the claim true on this core at a cost of roughly a sixth more memory. It
should not be a constant at all: the number that belongs there is the target
divided by the per-delta cost this core is showing, and that is the next piece
of phase 2.

What is still unmeasured, and should not be guessed at: the cost with the GPU
renderer running rather than the null one, and the same numbers on rpcs3, whose
state and churn are both larger.

### What a restore costs now (2026-09-10)

The 4.7 ms per link above was not the delta's cost; it was the arena's.
Applying a delta ended by re-protecting every page in the block, and loading an
anchor walked every page to find the ones that differed. `tests/perf/restorebench.c`
isolates the two on the same synthetic arena as epochbench - an anchor of 8 MB,
32 deltas, best of five - and the ares arena is the 2 GB row:

| arena | written/delta | anchor load | a delta applied | the seek |
|---|---|---|---|---|
| 64 MB | 256 | 3.9 -> 4.2 ms | 0.25 -> 0.14 ms | 12.0 -> 8.7 ms |
| 2 GB | 16 | 4.9 -> 1.4 ms | 2.29 -> 0.011 ms | 78.0 -> 1.7 ms |
| 2 GB | 256 | 9.3 -> 5.2 ms | 2.72 -> 0.15 ms | 96.2 -> 10.0 ms |
| 2 GB | 1024 | - -> 18.8 ms | - -> 0.65 ms | - -> 39.4 ms |

Read the two 2 GB rows against each other: a sixteen-page delta cost 2.3 ms and
a 256-page one 2.7, so 2.2 of it was the same whatever the delta held. It is
proportional to the delta now. The anchor load is proportional to what the load
changes, which is why it grows with the pages written per delta - those are the
pages it has to put back.

What changed: `mb_block_delta_apply` refreshes only the pages in its two lists
whose protection actually moved (the same before/after comparison
`mb_block_load_state` was already making), and the load compares the machine's
status and dirty maps against the state's a word at a time - the two facts are
kept packed, one byte a page, beside the forty-byte page array, so eight
untouched pages cost two loads instead of eight struct reads.

On the cores, traced with `CHIMERA_HISTORY_TRACE=1`, a restore of 34 links went
from 59 ms to 3 on the N64 and from 54 ms to 2 on the Game Boy. That the two
were the same number before, with deltas four times apart in size, was the
whole diagnosis.

## The policy: everything until the budget is full, then a doubling shape (user-decided, 2026-09-15)

Editing a movie is local, and rewinds are not: somebody working near the end
of a long run also jumps back a thousand frames to check something, and what
that costs is the replay from the nearest stored frame. So the greenzone keeps
every frame it captures until its memory budget is full, and only then gives
frames up - toward a shape that stays dense where the work is and gets
exponentially sparser behind it.

Until 2026-09-15 the history thinned by DISTANCE whatever the budget: every
frame for 120 frames behind the newest, one in 3 for 1800 behind that, one in
1200 beyond. Reported on New Star Soccer: a 16 GB budget sat at 3 GB used,
while a jump 2000 frames back replayed up to 1200 frames. The user's rule: "only
discard snapshots on full memory budget use. Then keep a near 4-max separation
snapshot band, then exponentially grow it 8, 16, 32, 64, 128, 256... so that mid
and long term regressions are not that expensive."

**The bands** are measured back from the frontier - the newest stored frame -
and double in length (greenzone_shape.h). With the near spacing s0 (the
`GreenzoneMaxNearStride` setting, 4 by default) and a goal of G snapshots a band
(32, `ce_session_greenzone_band_goal`), band k covers the frames between
s0*G*(2^k - 1) and s0*G*(2^(k+1) - 1) behind the frontier:

| band | frames behind the frontier | 32 snapshots are |
|---|---|---|
| 0 | 0 - 128 | 4 apart |
| 1 | 128 - 384 | 8 apart |
| 2 | 384 - 896 | 16 apart |
| 3 | 896 - 1920 | 32 apart |
| 4 | 1920 - 3968 | 64 apart |
| 5 | 3968 - 8064 | 128 apart |
| ... | as many as the run is long | doubling |

**G is a goal, not a quota.** A heavy core may not have the budget for 32
snapshots in all, so over the budget each frame given up comes from the band
holding the MOST snapshots, ties to the farthest. A band that aged with more than
its share (frames captured 4 apart that are now 2000 back) sheds it first; once
bands are level every band shrinks in turn, so the budget is spread round robin
across them. **A band's last snapshot is never given up**, and neither is frame
0, a pinned frame, or the frontier. Frame 0 and the frontier are kept on their own
account and do not count as a band's last: counted, the frontier was always the
near band's "last", the frame before it always went, and under a starved budget
nothing ever aged into a farther band - the history collapsed to frame 0 and the
frontier. A band can still be empty for a moment, when its one snapshot ages into
the next before another arrives, but the tail stays exponential: between any two
kept frames the gap is at most twice the width of the older one's band. If
nothing may go, the budget is missed rather than a band emptied - the promise
pins already made. Inside the chosen
band the frame that goes is the one whose going leaves the smallest gap: a link
in the middle of a stretch is composed into the next, a stretch's last link is
dropped, and an anchor only goes once its stretch holds nothing else.

**When and how much.** Only when `m_bytes > m_budget`, and then down to 95% of
it so a full greenzone is not thinned again on every frame; that extra is at
most 16 frames a call, and composition 32 MB a call (the first merge of a call
always happens, however big, so a heavy core can always make progress). Getting
back under the budget itself is not bounded - the budget is the promise.

**The merge caps are not thinning's.** composePair refuses a merge over 8 MB or
bigger than the anchor; those caps were there for coarsening by distance, a
little every frame whatever the budget. A greenzone over its budget cannot drop
a frame in the middle of a stretch - only compose it - and a merge always gives
memory back, so thinning ignores them and bounds its own spend. Measured before
that was understood: a single stretch stayed at 1478 bytes against a budget of
821.

**Spilling** (a headless tool's option; the frontend spills nowhere) still puts
the oldest stretch on disk first, keeps the stretch being written whole in
memory, and settles what is on disk to the far stride once near_frames +
mid_frames have passed it; `ce_session_greenzone_bands`' four band values now
mean only that.

Only NEW frames are captured. Going back and playing the same input forward is a
replay, and a replay changes nothing (user-decided, 2026-09-13): a capture of a
frame the history already reaches past stores nothing and drops nothing, so what
is ahead is still there to jump back to. What changes the timeline - an edit,
recording over an entry, input that is not the movie's, a different log - calls
`invalidateAfter` itself before the frame is played. Until 2026-09-13 every
capture dropped everything after it, and on a Ruffle project returning 3000
frames to the end after a look back was 99 s of emulation (docs/design-principles.md,
"A replay changes nothing").

### A delta continues only the machine that was stored (issue #68, 2026-09-15)

A delta is what the machine changed since its epoch was marked, so it means
something only on top of the exact machine the epoch was marked on. Two ways of
breaking that were found by a TAStudio crash on Ruffle - a guest heap musl
aborted on, a few ops after an edit behind the playhead - and both built
machines that never existed:

- **Across a gap.** The epoch is marked where the machine stands before a
  frame. An edit behind the playhead cut the stretch back to the edited frame,
  a frame ran before the seek back, and its delta was pushed straight after the
  edit: what one frame did at 268 applied to the machine at 230.
- **After a replay.** A replay that reached the stretch's last frame and went
  on measured its next delta on the machine it had emulated to, and pushed it
  onto the machine that was STORED there. Those are not the same bytes on a GPU
  core - the picture is read back into guest memory, and one replayed Ruffle
  frame measured 74 to 85 bytes apart - so the restore was half of each pass.

So the history knows where the machine stands and whether it is the stored copy
of that frame (just captured, or just restored). An epoch is marked only on the
stored copy of the stretch's last frame, and a delta is taken only from such an
epoch; after a replay the next frame is an anchor. Frames played after an edit
made behind the machine are not stored at all - they are the timeline the edit
replaced - until a restore or a load puts the machine back at or before the
edit. A direct load (a branch) lets the epoch go, and an edit that arrives while
a load has left the machine's frame unknown is decided by the next capture.

`CHIMERA_HISTORY_VERIFY=1` checks this on a real core: every stored frame's
machine is hashed, every restore is hashed again, and a restore that differs is
named with its chain. The comparison is of the machine rather than of the bytes
of its state, because a state also carries bookkeeping - which pages are dirty -
that two copies of one machine can disagree about.

The budget then decides only what happens to the far band. The engine can send
it to disk, oldest first, into a directory its caller names (the spill described
below, which `chimera-run --spill` still uses). **Chimera does not** (user-decided,
2026-09-15): a greenzone over its budget gives frames up toward its doubling
bands (see "The policy"), and nothing is written to disk until the project is saved.

Every boundary in that table is a knob with a per-core default, in frames rather
than seconds, because the engine does not know a core's frame rate and the
frontend does.

### One budget, in memory, and it is not a reservation

A history holds what fits in its memory budget - four gigabytes by default - and
nothing more. Every frame is kept until it is full; past it frames are given up
toward the doubling bands of "The policy", which costs replaying to reach a frame
that used to be stored.

It used to hold more than that on disk: memory's overflow was spilled into the
project's cache, under a second, disk budget of ten gigabytes (user-asked,
2026-09-10). That extended a greenzone past what the machine could hold, and did
it by writing gigabytes to the drive continuously while somebody worked - a cost
paid by the SSD, in wear, for a convenience. Chimera no longer turns spilling on,
and the disk budget is gone (user-decided, 2026-09-15; docs/design-principles.md,
"A greenzone lives in memory, and the disk is for saving"). The history file is
still written when a project is saved, which is the one moment somebody asked for
the disk. Spill files an earlier build left in a project's cache are deleted when
the project is next opened, because nothing else would ever remove them.

The budget is set in Tools > Cache Manager, and can be set **per project**; a
project's own number lives in its cache directory rather than in the
`.chimeraProject`: a budget is a fact about the machine the work is being done
on, and the project file is the one thing that gets handed to somebody else. A
run edited on a workstation must not arrive at a laptop insisting on thirty-two
gigabytes.

**It is not a reservation.** A short session never comes near it.

### Running out of memory halves the budget

The history is the biggest thing Chimera holds that it does not need, so it is
where the end of memory is met first - and it is the one place that can answer.
A capture that throws `std::bad_alloc` halves the budget, gives back what that
frees, and tries again; repeatedly, because one halving of a budget the machine
cannot afford is unlikely to be enough. Below sixteen megabytes it stops and the
frame is simply not stored, which costs replaying to reach it.

A greenzone that has quietly become half as deep is a run that continues. The
alternative is a throw out of a capture and a session lost.

The halved figure is **not written back to the settings**. What a machine can
spare this afternoon is not a decision somebody made, and it must not silently
become one.

### What is on disk is settled into the far band (2026-09-10)

The disk budget drops the oldest stretches in the file, which is the rule the
memory side has always had. For a while it did not thin them: `coarsen()`
skipped a spilled stretch, so what landed on disk kept whatever density it had
when it went. Six thousand Game Boy frames under a 64MB memory budget put
1567MB in the file with no disk limit, close to every frame's delta, because a
stretch spilled out of the near band was never coarsened afterwards.

Now a spilled stretch is SETTLED once the far boundary has passed it: read back
from the file, its landings composed down to the far grid - the same merge
tidy() makes, under the same caps, a few merges per frame so that it is a
little work every frame rather than a stall - and the result appended to the
file. The old body becomes dead room, which the compaction takes back when the
dead half is the bigger half, as it always did. A stretch that was already past
the far boundary when it was spilled is marked settled then and never read
back; one bigger than the memory budget is left as it is, because the working
copy would not fit. An edit that truncates a stretch while it is being settled
is honoured: the rewritten body keeps only what the stretch still answers for.

The spill order is unchanged - the oldest stretch goes first, whatever band it
is in - because under a budget smaller than the near and mid bands that IS the
right stretch to move, and density on disk is now a temporary condition, not a
permanent one.

The cache manager bounds the directory the file lives in, but it will never take
what is open (docs/cache-manager.md) - correctly, because the history is reading
it - which is why the limit has to be here.

### When the far band cannot reach the disk

A full disk, near enough always. The history carries on: `evict()` falls back to
thinning in memory - dropping trailing deltas, then whole stretches - which is
the correct fallback and costs frames rather than the session.

It is also completely silent, and from a piano roll a greenzone that stops
growing looks like the history going sparse for no reason anybody can see. So
the history remembers that it happened (`spillFailed()`, cleared when a new
spill directory is set) and `ce_session_greenzone_spill_failed` hands the fact
over. The engine knows; **saying it is the frontend's** - TAStudio asks about
once a second until it is true, then says it once and stops asking.

A message rather than an error because nothing has gone wrong that the history
cannot handle. What has gone wrong is the disk, and that is worth knowing before
the next thing on it fails less gracefully.

### Coarsening is composition, never deletion

This is the part that constrains the implementation, and it is easy to get
wrong. `deltas[i]` walks frame `anchor+i` to `anchor+i+1`. Dropping a link in
the middle of a chain does not thin it - it orphans every frame after it, since
there is then nothing to walk through. That is why the eviction this replaces
could only ever truncate a chain from its end.

Thinning is therefore COMPOSITION: two adjacent deltas are merged into one
delta that spans two frames. On miniBox's format that is an exact byte-level
merge - take the later program break and thread set, union the two page lists
with the later page winning - which is precisely what applying both in order
does, and it needs no sandbox and no running machine. Composed sizes grow
sublinearly, because a frame's churn lands mostly on the pages the frame before
it touched.

Two consequences fall out. A segment's links stop being one frame each, so a
link carries the frame it lands on and the invariant becomes "the spans tile the
segment without gaps" - the same contiguity, stated in the units it actually
holds in. And over a long enough span a composed delta approaches the machine's
whole working set, at which point the far band stops being deltas at all and is
simply anchors; that is a simplification rather than a special case.

### What the bands cost on a heavy core

Cheap cores make any policy look good. xemu, at 2 MB a frame and a 200 MB
anchor, is where the defaults have to be honest. Ten minutes of it:

| band | if it held | cost |
|---|---|---|
| near | 2 s, every frame | 240 MB |
| mid | everything else, one in two | ~54 GB |
| far | anchors every 20 s | 6 GB |

So the mid band's EXTENT, not its spacing, is what the budget actually buys, and
a mid band defined as "everything that is not near or far" is not affordable on
this core. It needs an extent of its own.

The far band's spacing has a latency of its own, too. Landing between two
preserved points 20 s apart on xemu costs either a 1200 frame replay, about 35 s
at 29 ms a frame, or a walk across the mid band's composed deltas if it reaches
that far, about 4 s. Neither is a second. That is the honest cost of giving up
the promise, and it is why the defaults have to come from measurement even
though the guarantee does not.

## What the bands measured

The same xemu run as above, 1200 captured frames, with the defaults the engine
ships (every frame for 2 s, one in three for 30 s, an anchor every 10 s):

| | chain of 200, no bands | the bands | the bands, 512 MB budget, spilling |
|---|---|---|---|
| budget | 4 GB | 4 GB | 512 MB |
| frames it holds | 301 of 300 | 482 of 1300 | 778 of 1300 |
| what it cost | 499 MB | 1557 MB | 2446 MB, mostly on disk |
| a captured frame | 40.8 ms | 47.6 ms | 47.8 ms |
| worst seek | 1.19 s | 0.99 s | 1.38 s |
| mean seek | 0.61 s | 0.54 s | 0.54 s |

Three things worth reading off it.

**The worst seek came in under a second without being promised one.** It
follows from the anchor spacing - a restore walks one stretch and no further -
which is a knob that trades memory for latency directly, rather than a target
the code has to keep hitting as churn moves around.

**Spilling holds MORE of the run on an eighth of the memory.** 778 frames
against 482, because the budget no longer has to destroy an old stretch to make
room; it moves it. Seeking into one of those costs about what seeking into
memory costs - the anchor load was always the small half - so the far band being
on disk is close to free until the disk is slow.

**Capture got slightly dearer, not cheaper.** 40.8 ms to 47.6, because
coarsening is real work done every frame. That is the trade the design makes on
purpose: a little per frame, always, instead of a stall when the budget fills.

## What coarsening costs

Thinning a band merges a landing into its neighbour, and the neighbour keeps
the span - so the neighbour ACCUMULATES, and every later merge re-reads all of
it. Collapsing four hundred landings that way is quadratic in the bytes, and
that stayed invisible for as long as the only core measured had frames that
overlapped almost perfectly.

`tests/perf/coarsenbench.c` is that loop with real deltas and no core. The
number that decides everything is how much of a frame's churn lands on the same
pages as the frame before it:

| frames overlap | uncapped | capped at 8 MB |
|---|---|---|
| 99% | 0.16 ms/frame, band 4.1 MB | 0.17 ms/frame, band 4.1 MB |
| 90% | **11.0 ms/frame**, band 40.1 MB | 0.42 ms/frame, band 45.6 MB |
| 70% | **33.6 ms/frame**, band 119.9 MB | 0.63 ms/frame, band 132.6 MB |

Thirty three milliseconds a frame, spent reclaiming a few per cent, on a
machine whose frames overlap seventy per cent. Two fixes, and the second is the
one that matters:

**Compose two deltas where they lie.** The streaming `wbx_compose_delta` reads
both into buffers of its own before it can merge them, because a count is
written before its list and a write callback cannot be seeked back to. That is
the right shape for a delta coming off a disk and the wrong one for the caller
that does this every frame, which holds both contiguously already - it was
copying megabytes to look at megabytes. `wbx_compose_delta_mem` walks them in
place: nothing allocated, each input byte read once, each output byte written
once. Nine times faster on a two megabyte merge. It is bound optionally, and a
host without it falls back to the streaming one for the same answer.

**Cap how large a merge may get** (`kMergeCap`, 8 MB - about a millisecond of
memory bandwidth). This is what turns the quadratic into a bounded per-frame
cost, and it is nearly free: the merges it refuses are the handful of biggest
ones, which are exactly where the union is closest to the sum and least is
reclaimed. Six refusals out of four hundred buy back a twenty-six-fold
speed-up for fourteen per cent more memory in the band. A landing left in place
only makes the band denser than asked, which is safe; the budget answers for
the memory.

The anchor guard stays as the outer bound on the same thought: a composed link
that already costs what a whole machine costs is better served by the anchor it
is walking from.

## What rewinding cost, and why it is gone

Reverse deltas were removed. Every frame is now reached the one way: **load an
anchor and apply the deltas since it.**

They were measured, on the same xemu run, 1200 frames, 4 GB budget:

| | kept | not kept |
|---|---|---|
| a captured frame | 56.9 ms | 48.6 ms |
| one frame back | 5.2 ms | a seek, up to 1.0 s |
| the history on disk | 1557 MB | 1557 MB |

Eight milliseconds on every captured frame, against a gesture that dropped from
a second to five - and that was the honest trade as long as capture was rare.
It stopped being rare. The greenzone captures every frame now, so the eight
milliseconds is paid by everybody all the time, and it bought a gesture that a
faster seek serves well enough.

The saving is larger than the table says, because the price was not only the
second delta. Keeping backwards possible meant the fault handler copied every
page the first time a frame wrote it - a four kilobyte memcpy per page per
frame, inside a signal handler, out of a fixed pool that could be exhausted -
and it meant carrying a pre-image pointer and kind on every page struct, which
made every remaining walk of the page array wider. All of that went with it.

`mb_block_delta_save` refuses the backward direction outright rather than
answering it with zeros: a delta of zeros would wipe memory the machine still
needs, and nothing else would ever say so.

## What a captured frame costs, and what it is proportional to

Capturing every frame only works if a capture costs the frame. It did not.

Measured with `tests/perf/epochbench.c`, which is the epoch machinery alone -
no core, no game - on an arena of a chosen size with a chosen number of pages
written per frame. Milliseconds per frame, before and after:

| arena | written/frame | open | faults | delta | total |
|---|---|---|---|---|---|
| 64 MB | 256 | 0.41 -> 0.39 | 1.20 -> 0.88 | 0.06 -> 0.00 | **1.67 -> 1.27** |
| 256 MB | 256 | 0.70 -> 0.40 | 1.32 -> 0.88 | 0.24 -> 0.00 | **2.26 -> 1.28** |
| 1 GB | 256 | 1.56 -> 0.43 | 1.38 -> 0.89 | 1.11 -> 0.01 | **4.04 -> 1.33** |
| 2 GB | 256 | 3.51 -> 0.44 | 1.70 -> 1.05 | 4.43 -> 0.02 | **9.64 -> 1.51** |
| 2 GB | 16 | 2.78 -> 0.09 | 0.14 -> 0.06 | 3.24 -> 0.01 | **6.15 -> 0.16** |
| 2 GB | 1024 | 5.20 -> 1.66 | 5.86 -> 3.84 | 5.47 -> 0.02 | **16.53 -> 5.51** |

Read the 16-page and 1024-page rows together and the fault is plain: writing
sixteen pages cost 6.1 ms and writing a thousand cost 16.5 - **the work was
proportional to how big the machine could be, not to what it did.** Sixty four
kilobytes of change cost more than a third of a frame at sixty a second, on
every core with a big arena, which is exactly the "everything got slower" that
prompted this.

Every per-frame path walked the page array end to end, several times over:
opening an epoch cleared per-page state and set a flag on every tracked page,
then looked for the runs to re-protect; saving a delta counted the dirty pages,
compared the whole allocation map against a copy of itself taken when the epoch
opened, and then walked the array again for the data. A page struct is forty
bytes, so a two gigabyte arena is twenty one megabytes a pass.

The fix is not cleverness, it is bookkeeping. The sets a frame cares about are
kept as **bitmaps**, one bit a page, sixty four kilobytes for that same arena:

- `epoch_bits` - written during this epoch. Set by the fault that lets the
  write through, which is two words of work inside the handler.
- `stat_bits` + `epoch_status` - pages whose allocation changed, and what it
  was. Recorded as the change happens, which replaced both the half-megabyte
  copy taken per epoch and the comparison that read it back.
- `unheld_bits` - pages mapped writable right now. This is the one that matters
  most: opening an epoch re-protects exactly these, and they are exactly the
  pages written since the last epoch opened, because everything else is still
  protected from that one. It used to re-protect every page the machine had
  ever written, every frame.

Iteration is ascending, which the delta format needs: both its lists are in
page order so that composing two deltas is a merge of sorted runs.

A clean page needs no hold and gets none. It is already read-only for the
baseline's sake, its first write already faults, and that fault records the
epoch's page too - which is why the flag can be set on so few pages without
losing any.

What is left is proportional to the frame: one `mprotect` per run of pages
written last frame, the guest's own write faults, and the delta's own bytes.
The bench scatters its writes as widely as it can, so its runs are one page
long and its numbers are the worst case; a real machine writes in clusters.

The `faults` column fell too, by a quarter to a third, and that is the removal
of reverse deltas showing up: the handler no longer copies a page every time a
frame first writes it.

### Hot pages: a write that happens every frame is not watched, it is read (2026-09-10)

What was left after the bookkeeping was the faults, and on a real machine most
of them are the same pages every frame: a framebuffer, an audio ring, the CPU's
own registers. Each paid a fault, and a re-protection at the next epoch so that
it could fault again, to report what was already known. On the N64 that was two
thirds of a captured frame.

A page written a few frames in a row (`HOT_AFTER`, three) goes hot: it stays
writable, and what it did is found at delta time by comparing it with a copy
taken when the epoch opened. A hot page unchanged for a few frames
(`COLD_AFTER`, eight) cools and is held again like any other. The comparison is
exact where a fault is not: a page written with the bytes it already held is
left out of the delta. The bench's fourth argument is pages written every frame
at fixed places:

| arena | scattered/frame | every frame | open | faults | delta | total |
|---|---|---|---|---|---|---|
| 2 GB | 300 | 0 | 0.50 | 1.21 | 0.03 | **1.75 ms** |
| 2 GB | 100 | 200 | 0.22 | 0.36 | 0.04 | **0.63 ms** |
| 2 GB | 0 | 300 | 0.04 | 0.00 | 0.02 | **0.06 ms** |
| 256 MB | 20 | 50 | 0.04 | 0.07 | 0.01 | **0.11 ms** |

On the cores, the history's share of a run: N64 9% -> 5%, Game Boy 14% -> 11%.
The Game Boy's deltas came out a third smaller (287 KB -> 185 KB a frame, 700
frames 110 MB -> 85 MB) from the exactness alone; the N64's barely moved, its
writes are real. What remains is the pages a frame writes for the first time in
a while, which is what a fault is for.

The invariant, and the reason it is safe: a hot page's shadow is the page AS THE
EPOCH OPENED. Opening an epoch copies every hot page - the sandbox cannot know
whether the guest ran since the last delta was saved, and it does run, for the
frame before an anchor - and a state load or a delta applied refreshes the copy
or cools the page as it rewrites it. A page whose allocation changes cools; so
does one a load makes clean, because clean pages are held for the baseline's
sake. Windows stacks are never hot: they already have a shadow of their own,
kept against the sealed image rather than the epoch. Shadows come from the same
mapped pool as baseline snapshots, capped at 32768 pages (`HOT_MOST`).

## What the client adds to a frame, and why a seek is served on a clock (2026-09-10)

Everything above is the engine. A TAStudio seek runs the frontend's main loop
once per emulated frame, and with `CHIMERA_LOOP_TRACE=1` the loop says where
each frame's wall time goes, per phase, every 300 frames. A Game Boy seek of
1200 frames with the piano roll open, headless under Xvfb on the dev box:

| | advance | tools after | render | messages | other | per frame |
|---|---|---|---|---|---|---|
| before | 3.0 | 1.15 | 0.65 | 0.60 | 0.5 | **5.9 ms** |
| after | 3.0 | 0.50 | 0.53 | 0.40 | 0.4 | **4.85 ms** |

`advance` is the machine plus its capture - the same 3.0 ms `chimera-run`
shows - and everything else was the client: the piano roll refreshed, the
picture presented, the message queue pumped, per frame, for frames nobody
could look at. Headless mode already served the host on a wall-clock cadence
instead of per frame, for the same reason; a seek does the same now. Sixty
times a second the window is live, shows where the seek has got to and takes a
click to stop it; the frames between get the tools' fast update and nothing
else, and are not drawn - which on a console with a 3D chip is most of the
frame. The destination frame is always drawn and shown. What remains in the
`after` row is those sixty services, each a real present and a real refresh on
a software GL.

Two things to know. The frames a seek passes through get the fast update
whether or not it is a turbo seek, so a Lua script that counts frames during a
seek wants "Run Lua during turbo", as it already did for a turbo one. And on
the Xvfb box a TURBO seek measured slower than a plain one - the present and
the message pump cost several milliseconds each with no frame drawn. That is
the software GL, not the client: the same seek on the Windows box with the real
GPU (`Chimera.exe --headless` driven through interop, the same project and Lua
kicker) is 3.5 ms a frame plain and 3.1 turbo against 2.7 for the machine, and
the trace's two later phases say where the rest goes there - `movie` 0.22 ms is
the greenzone capture itself, and `messages` 0.35 ms is the piano roll
repainting at the sixty services a second, which is what the person is
watching.

## What a bug hunt found, and what now holds it (user-asked, 2026-09-10)

The round above made the history fast. This one went looking for what it, and
everything around it, could get WRONG - by writing two randomized differential
tests and then re-introducing each bug they found, to check the test really
catches it. Both are in the gate.

`tests/unit/test_fuzz.c` in miniBox drives the page tracker with random writes,
maps, unmaps, protections and zeroings, and after every frame asks the only
question that matters: does loading an anchor and applying the deltas since it
reproduce the machine byte for byte, and the allocation map with it? It seeks
backwards and continues from where it lands, as a rerecord does. Half a million
checks a run.

`source/engine/tests/test_state_history.cpp` does the same to the history:
random frames, restores, edits, pins, budget changes, spills, and save/load
round trips against a model of what every frame held, on sixteen seeds with
small budgets and tight bands so that every path runs.

What they found, all fixed:

- **Two histories in one process shared a spill file.** The name was
  `history-spill.bin` in the project's cache directory, so the same project open
  twice - or reopened before the session that had it was gone - had the second
  truncate the first's file, and every frame the first had on disk came back as
  garbage. The name carries the process and the instance now, and a history
  sweeps stale ones a dead session left behind, because they are gigabytes each
  and nothing else would ever remove them.
- **A saved history carried frames an edit had removed.** A spilled stretch is
  copied into the saved file byte for byte, and the copy took the whole body -
  including the links after the edit's cut, which are still lying there. A
  reopened project then offered frames the movie no longer had. It is copied
  link by link now, only as far as the stretch still answers for.
- **A delta could be chained onto a spilled stretch.** An edit landing on the
  last frame of one made the next capture a delta held in memory while every
  restore reads the file, where the old timeline's links still are. A fresh
  anchor starts a stretch of its own instead.
- **Turning the greenzone on again kept the old spill file.** The disk budget
  was then held against stretches that no longer existed, and the room never
  came back.
- **A stretch being settled was matched by anchor frame alone.** After an edit a
  new stretch can be spilled with the same anchor frame, and the old body's
  offsets would have been read out of the new one. It is matched by where it
  lies in the file as well, and a compaction that moves it mid-settle simply
  ends that attempt.
- **A state load that failed part way left pages writable while calling them
  clean.** The load puts a page back to its sealed content and holds it
  read-only so the next write faults; returning early skipped applying that to
  the pages it had already done. Until the next epoch - and the caller is under
  no obligation to open one, since a refused restore leaves the session running
  - writes to those pages were invisible: absent from the next delta and absent
  from the next state. Both `mb_block_load_state` and `mb_block_delta_apply`
  have one exit now, and a page a delta half wrote is marked dirty, because it
  no longer holds what the baseline holds.
- **A hot page could be left hot while clean.** `madvise` zeroes a page whose
  sealed image was zero and calls it clean; a clean page is held for the
  baseline's sake, and a hot page never faults, so it would have been written
  without ever becoming dirty again and every anchor after would have omitted
  it. Cooling is enforced wherever the dirty bit is written.

### The beginning of the run is never given up, and what survives is spread (user-reported, 2026-09-10)

Reported from use: a rewind to a part of the movie the greenzone no longer
covered simply did not happen, and something crashed along the way.

**The beginning was being dropped.** The disk budget gave up the oldest stretch
in the file whichever one it was, the first one included. Once frame zero was
gone, `nearest()` answered -1 for every early frame, and the piano roll's
`GoToFrame` - finding nothing to load - unpaused and seeked FORWARD from a frame
already past the target, which arrives nowhere. That is the whole symptom: a
rewind that does nothing and says nothing. The encode path had been relying on
the same invariant in as many words ("Frame zero always has a state"), so it
would have thrown instead.

The first stretch is now never a candidate for eviction, on disk as in memory.
Going back to a frame nothing covers means starting from the beginning and
replaying to it, and that is only possible while the beginning is there.

**And what survives is spread over the run, not huddled at the playhead.**
Dropping the oldest every time empties the far past first, so a long session
ends with everything near the present and a return to the middle costs a replay
from zero. The stretch that goes is now the one whose absence widens the gap
between its neighbours least, which applied repeatedly thins the run evenly and
leaves anchors - breadcrumbs - across the whole movie. Measured on a Game Boy
under a 64MB memory budget and a 64MB disk budget, jumping 4000 frames back
repeatedly through 9000 frames: every jump landed within a few hundred frames of
its target, and the run survived.

The piano roll also refuses a backward seek it cannot serve rather than starting
one that cannot end, and says why. With the invariant above that should be
unreachable; a seek that silently never arrives is not a thing to leave possible
on the strength of an invariant somewhere else.

### A restore that cannot be walked no longer leaves a machine that never existed

The worst of them, and the one that is a change in behaviour rather than a fix.
A restore is an anchor and the deltas after it, applied to the live machine.
Fail on the fourth of thirty - a damaged spill file, a truncated history - and
the machine is not the frame asked for, and not the frame it was on before
either: it is a machine that never existed. The old code returned false and left
it exactly there, and the session carried on. Frames are captured from it, a
movie is recorded against it, and the desync surfaces somewhere else entirely.

Now the failure is contained. The anchor is loaded again, so the machine is one
that did exist, at that anchor's frame; the stretch that would not walk is given
up, because whatever is wrong with it will be wrong next time; and the frame it
landed on is reported, so the session and the frontend follow the machine rather
than believing the number they had. If even the anchor will not load, nothing
here can help and it says so: that machine has to be reloaded.

## What the history is allowed to cost (user-reported, 2026-09-10)

Reported from use: Flash under Ruffle runs slowly in Chimera while vanilla
Ruffle does not. Measured on the GPU box with `CHIMERA_LOOP_TRACE=1`, playing
New Star Soccer, milliseconds per frame:

| | before | after |
|---|---|---|
| the machine (`advance`) | 58.8 | 32.9 |
| the greenzone (`movie`) | 52.4 | 14.8 |
| the whole frame | 112.0 | 48.4 |

Two separate causes, and the second is this file's.

**The history was capturing twenty megabytes a frame.** Ruffle rewrites its
working set every frame - `MB_TRACE_DELTA` says only three per cent of those
pages are allocator churn and three quarters are hot, so it is real writing and
no tracking cleverness helps. Storing it took as long again as running the
frame, and six hundred frames came to twelve gigabytes.

The near band used to keep EVERY frame, whatever a frame cost to keep. That is
right on a light machine - a Game Boy's delta is 300KB and taking it is a
fraction of a millisecond against three of emulation - and wrong on a heavy one.
So the history now measures what capture costs against what the run costs and
gives the near band a STRIDE when capture passes 15% of wall time: the frames
between landings are not stored, the epoch stays open across them, and the next
delta describes them all at once. Reaching a frame that was not stored costs
replaying at most a stride of them, which is what the further bands have always
done.

Two guards keep it from thinning where it should not. A capture that is quick in
absolute terms - under two milliseconds - never moves the stride however large
its share, because a machine nobody is waiting for should keep the near band's
promise. And the stride is re-examined only every thirty captures and moved one
step at a time, because the thing measured is noisy by nature: one frame loads a
level, the next draws a menu.

**And the delta was being copied twice.** The sink appended each page as the
sandbox handed it over, so a vector growing by doubling copied everything it
already held, repeatedly, inside every capture. The sandbox knows how many pages
the frame touched before any of them are read (`wbx_get_epoch_page_count`), so
the room is asked for once; an anchor asks for what the last anchor took.

## Phasing

Each phase is separately gated and separately landable.

1. **miniBox: epochs and deltas. DONE** (miniBox `0c972de`.) Epoch marking, forward and reverse deltas,
   and the page introspection they need. Gated in miniBox's own suite, plus a
   differential check that a delta chain equals the full state.
2. **The engine: store and history. IN MEMORY, DONE** (`7252029`.) Segments -
   an anchor and the contiguous deltas that walk forward from it - with a byte
   budget and eviction, and the `ce_session_greenzone_*` entry points
   reimplemented over them, so chimera-run and every gate drive it without
   knowing it changed. A host without epochs falls back to anchors, since an
   older libminiboxhost beside a newer libchimera is ordinary. Proven on xemu
   with 200 MB states: a 200-frame run seeking back to 100 through the chain
   dumps System RAM byte-identical to a run that never stopped; the witness is
   40/40 and CHIMERA_HISTORY_TRACE shows its seek legs really do walk deltas.
   Persistence followed (`64a4e16`): the history writes to a file and reads one
   back, streamed a segment at a time so that nothing is ever assembled in
   memory, and the witness gained the leg that could not exist before - one
   process keeps a history, a second starts from it, seeks into states it never
   made, and lands on the goldens. The engine carries a machine id rather than
   deciding what makes two machines the same, since the caller already knows
   about cores, settings and files.
   The band policy followed, in four gated steps, all DONE:
   - **Composition in miniBox** (`40fcdb1`). `mb_delta_compose` merges two
     adjacent forward deltas into one that spans both, taking no host and no
     block, so a history can thin states it is merely storing.
   - **Variable spans in the engine** (`83bd31e`). A link carries the frame it
     lands on instead of an implied stride of one; the file format became
     ChimeraHistory2 and names its predecessor as superseded rather than
     refusing it.
   - **The band policy** (`03e9799`). Distance-driven coarsening on a per-band
     grid, and the anchor spacing in place of the chain limit.
   - **Spill** (`7c9502c`). The oldest stretches to the caller's directory once
     the budget is full, restored by reading only as far along one as the target
     needs.
   The frontend's greenzone moved to the per-user cache (`c595db6`), so a
   project's folder holds the project and nothing else. Phase 2 is closed.
3. **The frontend: one history. DONE.** `PagedStateManager`,
   `ZwinderStateManager`, `ZwinderBuffer`, `IStateManager`, the settings chooser
   and the settings page are deleted, and TasMovie drives the engine's history
   through `IStateHistory`: 867 lines added against 4773 removed.

   What was load-bearing C# is now a remote control. The history holds no copy
   of itself up here - not which frames exist, not the lag flags, which ride
   along as the note - because a second copy is a second thing to keep true, and
   it would go wrong quietly on the long runs where nobody could reproduce it.

   Two things this settled. A branch does not go through the history at all: it
   keeps a whole state of its own (decision 5), so reaching one never depends on
   a history that coarsens and evicts around it. And a marker that wants instant
   navigation pins its frame instead, which is cheap to name, impossible for the
   engine to guess, and far cheaper than a whole state each.

   The rewind gesture followed. Going back a frame was a seek: restore the
   nearest stored frame and replay to the target, which costs an anchor load and
   every link taken since it in order to undo one frame's work. It is now a
   reverse delta, which undoes exactly that frame.

   Reverse deltas are free of faults - the pre-images are captured already, by
   the same fault that serves the forward one - so they cost only the write, and
   they are kept only near the playhead, which is where stepping backwards
   happens. `LoadStateAt` tries the walk first and falls through to the restore,
   and the walk never goes further than its window, so a long jump does not pay
   a hundred applications to discover it should have seeked.

## Sharp edges to expect

- **Re-protecting an arena is not free.** A 2 GB layout is half a million
  pages. Only pages that are currently dirty and writable need re-protecting,
  and the runs must be coalesced. This has to be measured on xemu and rpcs3
  before committing to an epoch per frame; the fallback is an epoch every few
  frames, which costs seek precision and nothing else.
- **Fault storms.** Per-epoch first-write faults are bounded by the churn, which
  is the thing being paid for anyway - but a game that touches a lot of memory
  every frame is the case to measure, not to assume.
- **A state load rewrites the machine.** Every piece of host-side bookkeeping
  keyed to guest memory has to be re-established afterwards. The session already
  learned this twice, for the wide-input latches and for the trace flag; epoch
  tracking is the third.
- **A GPU core's objects are still its own.** Delta states do not change that a
  renderer's GL object names belong to the session that made them
  (docs/gpu-bridge.md); the rebuild on a moved context stays exactly as it is.
- **Anchors still cost the machine.** Deltas make the frames between anchors
  cheap; an anchor is a full state. The budget is spent mostly on how often
  anchors are taken, which is what the latency target decides.

## What a rewind was blamed for, and was not

A PS2 project crashed on rewind, and a Flycast one crashed on a piano-roll
edit. Both are the state history's paths, so the state history was where the
search started. It was not there, and the way that was established is worth
keeping, because the next report of this shape will look identical.

The measurements, all on the reporter's own machine and their own game:

| what was asked | answer |
| --- | --- |
| does a rewind land where the forward pass was | guest RAM identical at every depth, 1 to 29 steps |
| does a rewound machine still follow the same trajectory | rewind 20, replay 180: identical every frame |
| does `restore` agree with a run that never stopped | identical at every frame |
| is a PS2 savestate even stable to compare | stable, and deterministic across a reload |
| plain playback, 1200 frames, real GPU | survives |
| with the greenzone capturing and spilling | survives |
| with every memory domain swept by raw pointer each frame | survives |
| with the piano roll open, 1500 frames (tests/soak) | survives |

Two wrong turns, kept because each cost real time:

**A savestate is not a machine.** The first comparison digested whole
savestates and reported a mismatch from the fourth rewind step back. It was
bookkeeping: a page restored to the value it already held is DIRTY in one path
and clean in the other, so the state carries it in one and not the other while
the machine is identical. Compare guest memory - `ce_session_domain_read`, or
replay and compare trajectories - and the difference disappears. A savestate
parser that ignores which pages are invisible will also mis-assign every
payload after the first invisible dirty page, which is how the same run
produced a confident, wrong page number.

**The loudest fault is not always the first.** The reported fault was a host
address reading a no-access page, which reads like host code running off the
end of a buffer. It was the second fault of two in the same second: the guest
faulted first on a null pointer, and Windows then dispatched that exception on
the guest's own 1 MiB stack until it reached the guard at the bottom. The
guard-page fault was the symptom of reporting the real one. miniBox now names
the region an address lands in, so that reads as "the guard at the bottom of
the alt stack - this stack overflowed" rather than as a page number.

### What it was

A delta being wrong after all, and in the one place nothing was watching.

`munmap` zeroes the page it takes away, because that is what the guest's next
`mmap` of it is entitled to find - musl hands fresh anonymous memory to malloc
on exactly that promise. None of that goes through the fault handler, so the
epoch never heard about it: the delta recorded the page moving to free and back
and not a byte of content, and a frame rebuilt from it came back holding what
the page held BEFORE it was freed. The guest then handed that out as fresh
memory. A heap does this all day, which is why the damage was neither subtle nor
local, and why it always landed somewhere unrelated - a `std::map` insert, the
microVU dispatch - seventy frames after the seek that caused it.

Finding it needed the crash on demand, and that needed the piano roll, because
the trigger is a seek BACKWARDS and nothing else:

| variant, 1500 frames | result |
| --- | --- |
| plain play, TAStudio open | survives |
| record mode, no jumps | survives |
| jumps back, with or without record | dies within ~70 frames of the first |

Fixed in miniBox a710035: an epoch is told when a page's allocation changes,
not only when one is written. Both crashing variants now survive.

Two things worth keeping from the search. `test_delta` calls its cases from
`run_all` by hand, so adding a function is not adding a test - the three tests
that pin this passed against the broken code the first time they were written,
which is a green run that means nothing. And the 64 GiB `mmap` in the log, which
looked like the cause for most of a day, was a corrupted size read out of a heap
that had already been handed a stale page: a symptom of this, several steps
downstream. miniBox now prints the guest return
addresses on a refusal that large, and a core package ships `core.wbx`
unstripped, so `addr2line` names the caller.

## What a second thread could take, and what it could never (user-asked, 2026-09-12)

Nothing is built yet. This is the design and the arithmetic behind it, written
before the code so that the case can be argued with rather than discovered
afterwards.

The machine runs on one core on purpose: a movie that replays is a machine
whose every step is the same step, and parallelism inside emulation is how that
promise is lost. The greenzone is not the machine. It is a cache of where the
machine has been, and the history already treats it as one - `tuneStride`
changes what is kept from what the clock says, so what a greenzone holds has
been a function of wall time since the day it was tuned. Nothing a movie
depends on is in here. That is what makes the question askable at all.

### The measurement that decides it

The question is not "can this be moved" but "how much of it is worth moving",
and that is a ratio: how much of a capture is miniBox walking its page tables -
which happens where the machine is and can happen nowhere else - and how much
is memcpy into a buffer, which cares about no order and no lock.

`tests/perf/storebench.c` times both halves over the same work, once with a
sink that only counts and once with the sink StateHistory has. Best of three,
this workstation, milliseconds:

| arena / dirty | what is taken | walk | walk+store | the copy |
|---|---|---|---|---|
| 64 MB / 8 MB | anchor, 8.0 MB | 0.02 | 0.66 | 0.64 (97%) |
| 256 MB / 32 MB | anchor, 32.1 MB | 0.14 | 4.48 | 4.34 (97%) |
| 2 GB / 256 MB | anchor, 257 MB | 2.07 | 41.4 | **39.4 (95%)** |
| 2 GB / 1 GB | anchor, 1025 MB | 2.44 | 153.5 | **151.0 (98%)** |
| 8 GB / 1 GB | anchor, 1028 MB | 8.23 | 162.6 | **154.3 (95%)** |
| 2 GB, 64 pages/frame | delta, 256 KB | 0.00 | 0.07 | 0.06 |
| 2 GB, 256 pages/frame | delta, 1.0 MB | 0.04 | 0.36 | 0.32 |
| 2 GB, 1024 pages/frame | delta, 4.0 MB | 0.07 | 1.43 | 1.36 |

The first pass over a cold arena costs three to four times the warm figure -
the 257 MB anchor was 118 to 182 ms before the caches and the page tables were
warm - so an anchor in a real session is somewhere between the two, and the
honest range for a machine of that size is **40 to 180 ms**.

Set that beside what a frame costs to capture at all (`epochbench`, same box,
2 GB arena):

| pages written/frame | open (mprotect) | guest faults | delta walk | delta copy |
|---|---|---|---|---|
| 64 | 0.15 | 0.26 | 0.03 | 0.06 |
| 256 | 0.45 | 1.05 | 0.04 | 0.32 |
| 1024 | 1.94 | 4.55 | 0.07 | 1.36 |

**Only the last column can move.** `epoch_begin` must finish before the frame
runs, because an epoch that opens late does not describe the frame; and the
faults are not work done ON the emulation thread, they ARE the emulation thread
- the guest's own stores, trapped. At 256 pages a frame that is 0.32 ms of
1.86, about a sixth, and the design log's real-run figures - the history is 5%
of an N64 run and 11% of a Game Boy one - put the offloadable share of a whole
session at **one to three per cent**.

So the first finding is a negative one, and it is the important one: threading
the steady-state per-frame capture is not worth a thread. Anybody proposing
this for throughput has the wrong reason.

### The prize is the stalls

What a person notices is not a percentage, it is a freeze. There are three, and
they are all on the capture path:

- **The anchor.** 40 to 180 ms on a 257 MB machine, 150 to 700 ms on a
  gigabyte one, every `m_anchorSpacing` frames - that is once every ten
  seconds. On a PS2, Xbox or PS3 core it is several frames to half a second of
  nothing, on a clock.
- **The spill.** `evict()` is `while (m_bytes > m_budget)`, and every turn of
  it can be an `fwrite` plus `fflush` of a whole stretch, with `evictDisk()`
  and possibly `compactSpill()` - which rewrites the file's live suffix -
  behind it. A 10 MB stretch measured 7.2 ms to ext4 and **56.9 ms to NTFS**.
  Once the budget is full, which is the steady state of any long session, that
  lands on arbitrary frames, several times over on some of them.
- **Coarsening.** 0.16 to 0.72 ms a frame with the 8 MB merge cap, and 11 to 34
  without it. The cap is there precisely because this is on the critical path,
  and it costs density: at 70% overlap it leaves 18 merges of 400 undone.

All three are bytes moving between buffers and files. None of them touches the
machine.

### Anchors thin the greenzone as well as stalling it

Found while measuring, and true today regardless of any thread.

`tuneStride` feeds every capture into one exponential mean and moves the near
band's stride when capture passes `kCostShare` of wall time. Anchors go into
that mean with everything else. Work it through with a machine at 2 ms a
capture on a 10 ms frame, and one 121 ms anchor: the capture mean goes to about
7.9 ms and the wall mean to about 16, so the share reads 0.49 against a ceiling
of 0.15, and `want = stride * (share / kCostShare)` asks for three times the
stride. The near band drops from every frame to one in three, and recovers one
step per thirty captures - about ninety frames - by which time the next anchor
is a sixth of the way closer.

So on a heavy core a significant fraction of all frames are captured at a
degraded stride because of a cost that has nothing to do with the frames being
captured. The fix is cheap and independent of everything else here: measure
anchors separately from deltas, and tune the stride on the deltas, which are
the thing the stride actually controls.

### Why this is less exotic than it sounds

Two pieces of the mechanism already exist, in the fault path, on both operating
systems:

- **The mirror.** miniBox keeps an always-RW second mapping of the whole arena
  (`mirror_addr`). Host-side reads go through it so that they never trip dirty
  detection. A helper thread reading guest memory through the mirror is doing
  exactly what every host-side read already does.
- **Copy-on-write pages.** `mb_page_maybe_snapshot()` copies a page into a slot
  from a page-sized pool (`snap_alloc`) inside the handler, before a write is
  let through. It exists to hold the sealed baseline; the machinery is the
  machinery.

A copy-on-write ANCHOR is those two facts put together:

1. At anchor time, walk the dirty set and record it - 2 to 8 ms, the `walk`
   column above - and mark those pages read-only. `epoch_begin` already pays
   for a re-protection of the same shape every frame.
2. Hand the page list to the helper, which reads through the mirror and fills
   the anchor buffer at its own pace.
3. When the guest writes one of those pages, the fault handler copies that page
   into the anchor's slot before letting the write through, and marks it done.
   The handler already does a page copy on this path for the baseline.
4. The anchor is complete when the helper and the handler between them have
   covered the list.

The emulation thread's share becomes the walk - 2 to 8 ms instead of 40 to 700
- plus one extra 4 KB copy for each page the guest happens to write while the
drain runs. At 256 pages a frame and a drain of a few frames that is under a
megabyte of extra copying, against a stall measured in hundreds of
milliseconds.

### The contract

Whatever is built, these must hold, and each is a thing a test can be written
against:

1. **The helper never writes guest memory.** It reads through the mirror. Every
   mutation of the machine - `load_state`, `delta_apply` - stays on the
   emulation thread.
2. **Every reader of the history drains first.** `restore`, `invalidateAfter`,
   `saveTo`, `configure` and `clear` block until pending work is finished or
   cancelled. A seek that raced a pending spill would read a file that is not
   written yet.
3. **The queue is bounded instead.** Bytes handed to the writer are released
   from the budget at once (rule 6), so the bound on what the history holds
   while a write is in flight is the QUEUE: half the memory budget, never less
   than four megabytes nor more than sixty-four. Unbounded, a run measured 29
   MB of writes in flight - memory the budget could not see, and a file full of
   ranges that were dead before they were written.
4. **A frame is never lost to a busy helper.** If the queue is full or the
   helper has failed, the work happens in line, exactly as it does today. The
   thread is an optimisation and must be droppable.
5. **Tests can make it synchronous.** The differential fuzzers
   (`test_state_history.cpp`, miniBox's `test_fuzz.c`) are the reason the
   history is trusted, and they stay deterministic only if a drain-now mode
   makes the helper a function call. That mode is not a test-only path - it is
   what a headless run and a gate use as well.
6. **Nothing about the movie depends on when the helper runs.** The greenzone is
   a cache; it is allowed to hold different frames on different machines, and
   already does.
7. **Work that nobody is waiting for may be killed where it stands.** Anything
   speculative - the prefetch of phase 6, the compression of phase 5 - is
   cancelled by an edit, a seek or memory pressure, and never holds a lock that
   owed work needs. The two classes and their rules are below.

These six say what the helpers may do. What happens when somebody steps back,
edits, switches a branch and does it again immediately - which is not an edge
case but the work itself - is the subject of "Concurrency by construction"
below, and the short version is that the loop owns everything, the helpers own
only the buffer in their hands, and interference turns work in flight into
garbage rather than into a hazard.

### The order, and what each is worth

| phase | what moves | worth | risk |
|---|---|---|---|
| 0 **BUILT** | writing the history at project save and autosave | a save of a 122 MB history: 51 ms in line, 0.0 ms to return | low: a queue and a barrier |
| 1 **BUILT** | spill and settle I/O (compaction stayed on the loop) | 1.7x to 12x the 99th percentile of a captured frame, 1.0x to 7.2x the worst | low: no guest memory, no miniBox change |
| 2 | coarsening and composition | 0.16 to 0.72 ms/frame, and the 8 MB cap can go - a denser history for the same memory | low inside StateHistory, but it costs the step-for-step identity of phases 0 and 1: a merge lands a frame late, so the history holds different frames threaded than in line. Left undone for that reason, not for difficulty |
| 3 **BUILT** | copy-on-write anchors | 6x to 15.7x less on the emulation thread: 521 ms becomes 33 on a gigabyte machine | this is where miniBox's single-thread assumption was faced |
| 4 **MEASURED, NOT BUILT** | deferred delta store | measured on a PS2: the delta costs this thread 3.6 ms mean, 6.9 at the 90th percentile - but the pages it copies are the pages the guest writes again next frame, so holding them for a helper turns a memcpy into a fault plus the same memcpy (see "The frame after an anchor") | same mechanism as 3, paid every frame, on exactly the pages it cannot win on |
| 5 **BUILT** | zstd what reaches the disk, on the writer; the disk budget counts compressed bytes (user-decided, 2026-09-13) | speed, space and depth: a PS2 history 2.5 GB on disk becomes 79.9 MB, a cold restore of a spilled anchor 103 ms becomes 59, and a 1024 MB budget that kept two raw anchors keeps everything | low; disk evictions now follow the writer's reports, so threaded and in line may drop from disk at different moments |
| 6 | a rolling composed prefix behind the playhead, on a thread of its own | the backwards step: ~280 links walked becomes one composed apply plus a short tail | low, and uniquely so - it is the one phase nothing waits for |

Phases 0 to 2 are worth doing whether or not 3 ever is, and they are where the
felt improvement per unit of risk is highest - phase 0 most of all, since it is
the only one whose freeze is measured in seconds. Phase 3 is the large prize
inside the capture path, and the real decision. Phase 6 is deliberately last:
it is the only speculative one, and phase 3 may leave it with nothing to do -
see its section below.

What was actually built, and what each phase measured once it existed, is under
"What was built, and what it measured" further down. Phase 2 is the one that
was reconsidered rather than deferred for time: it cannot keep the property
that makes the others verifiable.

These are phases of work, not threads. Which roles run where, how many of each,
and the two classes they fall into is its own question, answered in "How many
threads, and which".

### What no thread can take

The `open` and `faults` columns - 1.5 ms of the 1.86 at 256 pages a frame,
about eighty per cent - are not offloadable in any design. They are one
`mprotect` per run of pages written last frame, and the guest's own stores
trapping. The lever that reaches them is not concurrency but GRANULARITY: a 2
MB huge page faults once where 512 pages fault 512 times, at the cost of deltas
512 times coarser and a history that costs what the machine could be again.
That is a different project with a different trade, and it should not be
confused with this one.

### How it gets proved

`run-storebench.sh` and `run-epochbench.sh` before and after, on the same box,
for the numbers; `CHIMERA_HISTORY_TRACE=1` and `CHIMERA_LOOP_TRACE=1` on a real
core, where the claim is about a hitch rather than a mean - so a per-frame
MAXIMUM, not an average, because the worst frame is the whole point. And the
two fuzz harnesses run in drain-now mode AND threaded against the same model: a
helper that produces a different history from the synchronous path is a bug,
and that is the property worth fuzzing hardest.

### Phase 6 in detail: the rolling prefix, which is allowed to be wrong (user-asked, 2026-09-12)

Stepping BACKWARDS is the gesture a TAS is made of, and it is the expensive
one. A restore walks from its segment's anchor, `m_anchorSpacing` is 600, and
inside a segment the near band keeps every frame and the mid band one in three
- so a step back late in a segment walks about 280 links. Measured on a real
core, 34 links cost 3 ms (N64, after the 2026-09-10 round), so 280 is 20 to 30
ms there and more on a heavy machine. For one frame backwards.

The proposal was a thread that rebuilds the states within about 30 frames of
the playhead, newest first, cached and entirely discardable. The instinct is
right and the framing - discardable, never load-bearing - is the correct one.
The shape wants changing in one respect: **a state is a whole machine**. Thirty
of them on a mid-weight core is 7.7 GB of cache to save 25 ms, and on a Game
Boy it is effort spent on a machine whose entire state is smaller than this
paragraph's table would be.

The cheap shape with the same payoff is a **rolling composed prefix plus the
raw tail**. The helper keeps ONE composed delta covering the anchor up to
(playhead - N, default 30), and the newest N frames stay as the history has
them. A restore into that window is then: load the anchor, apply one composed
delta, apply at most N small ones. Tens of megabytes instead of gigabytes, and
the primitive already exists - `composePair` and `mb_block_delta_compose_mem`
are coarsening, pointed backwards.

It obeys three rules, all of which come from the cache being allowed to be
wrong in only one way - by being absent:

1. **Derived, never invented.** The prefix is built from the history's own
   bytes with the history's own primitives. A cache that builds a state a
   different way from the walk is a machine that never existed, which is the
   failure this file fears most and the one that surfaces seventy frames later
   somewhere unrelated.
2. **Dropped by any edit.** `invalidateAfter` throws it away. That is trivially
   correct because it is keyed by frame.
3. **Never waited on.** If it is not ready, the walk happens as it does today.
   Nothing blocks on it, ever.

**But do phase 3 first, and then measure again.** That was the instruction, and
it was followed: phase 3 exists, the measurement was taken on a PlayStation 2,
and the answer is under "What cheap anchors did to the backwards step" below.
The short of it is that denser anchors more than halved the backwards step and
then hit a floor made entirely of loading one whole machine - so a rolling
prefix would be shortening the part that is already cheap. Read that before
building any of this.

### How many threads, and which (user-asked, 2026-09-12)

"A helper thread" was shorthand. The work divides by what it touches and by
whether anything waits for it, and those two questions give different answers
for different phases, so the right shape is a small set of named roles rather
than one queue with everything in it.

| role | what it does | who waits for it | how many |
|---|---|---|---|
| writer | spill, settle, compaction, streaming a project save | a barrier, at save and at close | one - the spill file is an append-ordered queue and a second writer would interleave into it |
| drainer | the copy half of a capture: anchor pages and deferred deltas, read through the mirror | the next capture, at the latest | a small pool: copying 257 MB is memory-bandwidth bound, and one thread gets a fraction of what the bus offers |
| tidier | coarsening, composing, settling metadata | nothing directly; the budget notices | one - it mutates the history's own structure |
| prefetcher | the rolling prefix of phase 6 | NOBODY, by construction | one, independent, cancellable at any moment |
| compressor | zstd of cold deltas (phase 5) | nothing; a delta is readable either way | a pool, bounded by spare cores |

The division that matters is not the count but the two CLASSES:

- **Owed work** - writer, drainer, tidier. The history has already promised it:
  bytes are counted against the budget, a restore must see it, a save must
  contain it. A barrier waits for these, and they may not be abandoned, only
  finished or rolled back.
- **Speculative work** - prefetcher, compressor. Nobody has promised anything.
  Any edit, any seek, any memory pressure cancels them where they stand, and
  the only correct response to "it was not ready" is to do the thing the old
  way.

Four rules keep the two classes from poisoning each other, and each is a thing
a test can be written against:

1. **The emulation thread is never starved.** Helpers are bounded by what the
   machine can spare - the count comes from spare cores, never from a constant
   - and they run at a lower priority. A core that is itself saturating the
   machine (rpcs3 on a four-core laptop) must end up with no helpers at all
   rather than with contention, and that is a measurement, not an assumption.
2. **No speculative thread ever holds a lock that owed work needs.** The
   prefetcher works from a snapshot of what it needs, publishes its result with
   one atomic swap, and is killed rather than waited for.
3. **The fault handler takes no lock.** It runs on the emulation thread, inside
   a signal handler on Linux and a vectored exception handler on Windows;
   claiming a page from the drainer is a per-page atomic and nothing else. A
   mutex there would put the machine behind a helper, which is the exact
   inversion this whole design exists to avoid.
4. **Every role is droppable.** With the helpers disabled - by configuration,
   by a failed thread start, or by a test - everything happens in line, exactly
   as it does today. That is what makes the threaded path verifiable: the
   synchronous path is the reference implementation, and the fuzzers run both
   against the same model.

### Concurrency by construction: what the user can do to it (user-asked, 2026-09-12)

The worry is the right one, and it is the reason this design is written before
the code. Somebody stepping back, jumping forward, scrubbing the piano roll,
editing an input, switching a branch and doing it again ten times a second is
not an edge case - it is what TAS work IS. So the model has to be one where
interference cannot produce an unstable state, rather than one where it does
not happen to.

The answer is not a bigger lock. It is that **only one thread owns anything,
and the helpers never own, they convert**: bytes in, bytes out. Everything
below follows from that.

**There is no user thread.** The run loop calls `Application.DoEvents()`
itself, so a click, a hotkey, a piano-roll drag and a menu action are all
dispatched ON the loop thread, between frames. A user action is therefore
already serialised with captures and restores, and always has been. Nothing in
this design introduces a second thread that can act on the user's behalf. That
is the single most important fact about the whole model: the actions arrive in
order, on the one thread that is allowed to change things.

#### The eleven rules

1. **One owner.** The loop thread alone mutates the machine, `m_segments`,
   `m_bytes`, the pin set and the spill metadata. No helper touches any of
   them. There is consequently no lock on the history at all, and no lock for
   a user action to wait behind.
2. **Helpers convert bytes to bytes.** A helper is handed an immutable buffer
   and a destination, and hands back a buffer or a file range. It cannot walk
   the history, cannot ask what frame is newest, cannot free anything.
3. **Bodies are immutable and reference-counted.** A link's bytes become
   `shared_ptr<const vector<uint8_t>>`. A helper holding one cannot have it
   freed underneath by a coarsening, an eviction or an edit; the structure
   drops its reference and the bytes go when the last holder does. This one
   change removes the entire use-after-free class that an edit-during-work
   would otherwise create.
4. **Completions are applied by the loop, never by the helper.** A finished
   piece of work goes on a completion queue; the loop drains that queue at one
   point per frame and at barriers. So every change to the structure still
   happens in loop order, and "what the user did" and "what a helper finished"
   are ordered against each other by the same thread that orders frames.
5. **Work nobody wants any more is cancelled, not finished.** A stretch
   dropped while its write is still queued clears a flag the job holds, and
   the job writes nothing: those bytes will never be read, and writing them is
   megabytes of I/O into a range that then looks like dead room and brings on
   a compaction that copies every live byte for nothing. (Built as a flag per
   write rather than the generation counter the design first imagined, because
   a write is the only thing that outlives the decision to make it.)
6. **A spill settles its metadata at once; only the write is late.** Built
   the other way round from the sketch above, and for a reason worth keeping:
   if the segment held its memory copy until the write landed, the budget
   would be met later than it is asked to be, and eviction - which is driven
   by that byte count - would make different decisions threaded than in line.
   So the stretch is marked spilled, its range reserved and its memory
   released immediately, exactly as before, and the rule that pays for it is
   that anything READING the file waits for the writer first, and only for the
   range it needs (`m_writtenThrough`). A write that fails is undone when it
   is reported. That is what makes the step-for-step comparison in the
   differential test possible at all. One half of it was later given up on
   purpose: once spilled bodies were compressed and the disk budget was made to
   count what the file really holds (user-decided, 2026-09-13), a stretch
   weighs on the DISK only from its report - so disk evictions follow the
   writer's timing, while everything decided with memory still comes out the
   same. See "Phase 5" below.
7. **The file is append-only while anything can read it.** Settling a spilled
   stretch writes the new version at a NEW offset and flips the metadata on
   completion; the old range is freed afterwards. Nothing is ever rewritten
   under a reader.
8. **Compaction is the one exclusive act, and it copies rather than moves.**
   Moving the live suffix to the front changes every offset, so it is done into
   a second file which the loop flips to when it is complete; readers use the
   old file until then. Where there is no room for both, compaction is skipped
   and the budget is met by dropping - which is what already happens when
   spilling fails.
9. **A barrier is "help finish", not "wait".** When the loop needs work that is
   in flight - a restore before a pending capture drain, a save, a close - it
   joins in using the same per-item claim the helpers use. The worst case is
   therefore one thread doing the work, which is today's behaviour, and the
   worst case for a user action is the speed of the code that exists now.
10. **The fault handler takes no lock.** It runs on the loop thread inside a
    signal handler on Linux and a vectored exception handler on Windows.
    Claiming a page from a drainer is a single compare-and-swap per page:
    whoever wins copies it, and the loser goes on. There is no waiting, no
    ordering requirement, and no path by which the machine can end up behind a
    helper.
11. **Speculation is published atomically or thrown away.** The prefetcher
    works from references it already holds, publishes with one atomic swap of a
    pointer plus its generation, and is cancelled - never joined - by any edit,
    seek or memory pressure. A stale prefix is ignored and freed. Nothing ever
    depends on it existing.

#### What each thing the user can do actually does

| the user does | what happens to work in flight |
|---|---|
| steps back one frame, or seeks anywhere | pending capture drains are helped to completion (bounded, and the loop joins in), the prefetch is cancelled, the machine is restored, the prefetch restarts behind the new playhead |
| edits an input | generation bumps; every completion after that frame is dropped and its bytes freed; queued spill writes for dropped stretches are abandoned and the file truncated back by the writer, in queue order |
| switches a branch | a state load plus a different input log: generation bumps, everything speculative dies, the prefix is rebuilt from the new timeline |
| scrubs the piano roll for ten seconds | a seek per position, each cancelling the last prefetch before it started - which costs nothing, because a cancelled speculation has by definition produced nothing anybody wanted |
| saves the project | the writer takes a metadata snapshot and streams bodies; the loop continues; closing the project is the barrier that must finish |
| runs out of memory | the drainer's pending bytes are counted in `m_bytes`, so the budget sees them; speculation is cancelled first, owed work is finished, and only then does `halveBudget` do what it does today |

#### Why this cannot deadlock

No helper ever acquires a lock the loop holds, because helpers hold no lock on
history state at all - they own their input buffer and their output buffer, and
communicate through single-producer/single-consumer queues and atomics. The
loop acquires nothing from a helper except at a barrier, and a barrier is
bounded by work that is already in progress, which the loop is allowed to do
itself. A deadlock needs a cycle; there is no second edge to make one from.

#### How it gets proved rather than asserted

The differential fuzzers are the reason this part of Chimera is trusted, and
they extend to exactly this question. The same random sequence of actions -
capture, seek, step back, edit, branch, save, budget squeeze - runs twice: once
with the helpers off, where everything happens in line, and once with them on
and with completions delivered at randomised points relative to those actions.
The resulting history must be identical, and so must the machine's bytes after
a restore to every frame the history offers. A helper that produces a different
history from the synchronous path is a bug, and the synchronous path is the
reference implementation by construction.

Two properties get their own tests because they are the ones that would fail
silently: that `m_bytes` returns to exactly what it was after a cancelled
generation (the accounting bug that wrapped the budget past zero is the
precedent), and that the spill file contains no range that no segment claims
after a storm of edits (the leak a dropped completion would cause).

### What else is on the critical path, once you look past the capture

The question was asked of the greenzone, but the same helper answers two more,
and the first of them is the largest stall in the application.

**Saving the project writes the whole history, on the thread that runs the
machine.** `TasMovie.Project.cs` calls `States.Save(...)` inline, and the
default budgets are 4 GB in memory and 10 GB on disk, so a save can stream
FOURTEEN gigabytes before the window moves again. At the rates measured for
spilling - 1.4 GB/s to ext4, 176 MB/s to NTFS - that is about ten seconds on a
good Linux disk and over a minute on the Windows install. And it is not only
asked for: TAStudio's autosave timer fires it every thirty minutes by default,
unprompted, on the UI thread.

Nothing about that write needs the machine. The history's metadata - what
frames exist, what is spilled where - is small and could be snapshotted in
microseconds, leaving the helper to stream bodies while the run continues; the
only true barrier is closing the project, where the save must finish before the
files go. The one thing to get right is what an edit does to a save already in
flight, and the history already has the answer in `invalidateAfter`: a save
that has been overtaken by an edit is abandoned and taken again, because a
history file that describes a timeline that no longer happens is worse than no
file. That is the same rule the spill file lives by.

What that missed (2026-09-13): a background save is one job on the writer, as
big as the history, and anything on the machine's thread that waits for the
writer waits for the save. A spill over the write queue's cap does exactly
that - a 42.4 s frame on a Ruffle project whose history saved as 4.35 GB. So
while a save is pending the history neither spills nor compacts, and keeps its
memory budget by thinning (docs/design-principles.md, "A save in the background
still stopped the run").

This is worth doing FIRST, before anything in the capture path: the mechanism
is a queue and a barrier, it touches no guest memory, it needs nothing from
miniBox, and the freeze it removes is measured in seconds rather than
milliseconds.

**The GPU is a stall, and a thread is the wrong tool for it.** On a core with a
hardware renderer, the guest blocks waiting on the GPU - the bridge is already
instrumented for exactly this (`CHIMERA_GL_PROFILE`, `CHIMERA_GL_WHY` for what
the guest was doing when it blocked, `CHIMERA_GL_GPUTIME` for whether the GPU
is genuinely busy or the pipeline is being drained for nothing). It is
tempting to answer that with a render thread, and that would be a mistake to
reach for first: a GL context belongs to a thread, the guest is waiting for a
RESULT rather than for the work, and a machine whose picture is on the GPU has
a savestate problem already (docs/gpu-bridge.md). The fixes that reach this are
pipelining - a readback that lands a frame later through a pixel buffer, fewer
fences - and they are a different piece of work. Measure with the three
switches above before anything is built.

**And what turned out not to be there.** The render path has no CPU pixel work
to move: scaling and filtering are the GPU's. Video encoding is already off the
loop - `AviWriter` has had a worker thread and a queue since BizHawk, and
`FFmpegWriter` writes down a pipe to another process. Branch states are stored
uncompressed on purpose (`zstdCompress: false`), so no interactive path is
paying for zstd. The `movie` phase in the loop trace is 0.22 ms and is mostly
the greenzone capture itself, which is where this design came in. The piano
roll cannot leave the UI thread at all, and is already served on a clock rather
than per frame.

### What was built, and what it measured (2026-09-12)

Phases 0, 1 and 3 exist. Phase 2 was deliberately left, and phases 4 to 6 are
untouched. Everything below was measured on this workstation, and every number
is reproducible with the benches named beside it.

#### Phase 1: the writer

`spill()` settles every piece of metadata the moment it is asked for - the
stretch is marked spilled, its range in the file is reserved, its bytes are
handed to the writer - so the budget's arithmetic and every decision that
follows from it are what they always were. Only the write is late.

The rule that costs is the other side of that: **anything that READS the file
waits for the writer first**, and only for the range it is about to read
(`m_writtenThrough`). Restores, saves, settling and compaction all do; a
capture does not, and a capture is what a frame has to be quick for. A write
that fails is undone when it is reported - the stretch gets its bytes back and
stops being spilled - which leaves the history exactly where a synchronous
spill returning false left it.

`tests/perf/run-spillbench.sh`, best of two interleaved passes:

| machine / frames / budget | mean | 99th | worst frame |
|---|---|---|---|
| 8 MB, 512 KB/frame, 128 MB, 2000 frames | 1.1x | 3.7x | 1.2x |
| 32 MB, 1 MB/frame, 256 MB, 1500 frames | 1.2x | 1.7x | 1.0x |
| 32 MB, 2 MB/frame, 8 MB | 1.4x | 2.1x | 4.9x |
| 16 MB, 4 MB/frame, 6 MB | 1.5x | 5.2x | 7.2x |
| 8 MB, 512 KB/frame, 4 MB (budget < one state) | 1.1x | 12.0x | 1.6x |
| 64 MB, 256 KB/frame, 16 MB (anchor-bound) | 1.3x | 2.1x | 1.4x |

#### Phase 0: the project save

`saveToLater` copies the metadata now - so the file describes the history as it
stood when the save was asked for - and streams the bodies on the writer. The
bodies are shared and immutable, so what the run does next cannot touch them;
spilled stretches are read from the spill file by the same thread that writes
it, in queue order.

A save of a 122 MB history measured **51 ms in line against 0.0 ms to return**,
and the metadata snapshot stays under a millisecond at four thousand frames,
because it is proportional to the number of links rather than to the bytes. The
barrier is `WaterboxCore.Dispose`, not the movie's - see the defects below.

#### Phase 3: the copy-on-write anchor

miniBox grew `mb_block_state_plan`: it writes everything except the page data
into the caller's buffer, holds the pages the data will come from, and hands
back a list. `mb_block_plan_fill` copies a range of them on any thread;
`mb_block_plan_capture` is the fault handler's half, copying a page the guest
is about to write before the write lands; `mb_block_plan_finish` copies
whatever is left and lifts the holds. Whoever gets to a page first wins it with
one atomic exchange - there is no lock in this at all, which matters because
one of the two racers is a signal handler.

What made it worth building is that holding pages is cheap where copying them
is not (`tests/perf/protbench.c`, measured): 256 MB held read-only costs
**0.76 ms in one call** and 1.08 ms in megabyte runs, against 40 to 180 ms to
copy it - and `refresh_range` already coalesces runs.

`tests/perf/run-planbench.sh`, the cost ON the emulation thread:

| machine / dirty / pages written while it filled | old | new | |
|---|---|---|---|
| 256 MB / 128 MB / none | 84.7 ms | 9.6 ms | **8.9x** |
| 64 MB / 32 MB / 500 | 21.3 ms | 3.6 ms | **6.0x** |
| 256 MB / 128 MB / 1000 | 120.8 ms | 9.3 ms | **12.9x** |
| 1 GB / 512 MB / 2000 | 521.1 ms | 33.2 ms | **15.7x** |

and on a real PlayStation 2 (232 MB state, `chimera-run`, anchor every twenty
frames) **113 to 129 ms became 12 to 17** - but only after the buffer it writes
into stopped being zeroed first; see below.

and in every case the planned state is byte for byte the state the old path
would have written, which the bench checks and returns non-zero if it is not.

What the guest pays for it: one fault per page it writes during the drain that
the copier has not reached yet, measured at about 17 microseconds each while
the copier is saturating memory bandwidth. A thousand such pages over a drain
of ninety milliseconds is a few milliseconds a frame, against a sixty
millisecond freeze that is gone.

**One page cannot be held: a guest stack on Windows.** A fault delivered on the
stack's own page has nowhere to build its exception frame and the machine dies
where it stands rather than faulting (miniBox 9f1c533, and the ares crash that
found it). Those pages are copied at plan time instead - a few hundred against
tens of thousands - which is why `plan_can_hold` exists.

#### And then on a real console, where it did nothing at all

planbench said a 256 MB anchor costs 9.6 ms on the emulation thread instead of
84.7. A PlayStation 2 booting Gran Turismo 4 through `chimera-run`, with its
232 MB state and an anchor every twenty frames, said something else:

	ANCHOR 232683218 bytes planned, 56337 pages to fill, 140.9 ms here
	ANCHOR 232785618 bytes planned, 56362 pages to fill, 117.1 ms here

against 113 to 129 ms for the same anchors written in line. The whole design,
measured on a real machine, was worth nothing.

The trace was then split in two, which took two minutes and answered it:

	140.9 ms here (buffer 129.5, plan 11.3)

**The plan was doing exactly what the bench promised - eleven milliseconds for a
232 MB machine - and `std::vector::resize` was spending a hundred and thirty
zeroing the buffer it was about to be written over.** A vector value-initialises
what it grows into; at this scale that is a memset of a quarter of a gigabyte
plus the first-touch faults for every page of it, all on the thread that runs
the emulator, to prepare memory whose every byte is about to be overwritten.

So a body is now allocated by an allocator whose `construct` does nothing
(`StateHistory::Bytes`). Whoever fills the bytes pays for touching them, and for
a planned anchor that is the drainer. The same PlayStation 2 anchor:

	ANCHOR 232933074 bytes planned, 56398 pages to fill, 12.2 ms here (buffer 2.5, plan 9.6)

**113 to 129 ms became 12 to 17**, which is the number phase 3 was built for,
and the EE RAM after a three hundred frame run with a seek back through the
greenzone is byte-identical whether the helpers are on or off.

**That number was only half true, and the other half was found the next day
(2026-09-13).** It measured the frame the anchor was taken on. Every capture
began by finishing any plan still being filled - "one plan at a time" - so the
FRAME AFTER each anchor waited for the drainer, and the drainer took 141 to 153
ms to fill 220 MB. Traced once the wait was given a line of its own:

	plan of frame 31: filled in 152.5 ms on the drainer; this thread WAITED 102.56 ms
	plan of frame 106: filled in 143.4 ms on the drainer; this thread WAITED 124.65 ms
	plan of frame 215: filled in 145.2 ms on the drainer; this thread WAITED 145.26 ms

So most of what planning took off the emulation thread came back one frame
later. Nothing needed it: a delta does not read the anchor, composing reads only
the anchor's length (fixed when it is planned), and the sandbox serves an epoch
and a plan on the same pages by design (`test_an_epoch_across_a_plan`). The wait
now happens only where the bytes are really needed - the next anchor, a restore,
a spill or a drop of that stretch, a save, a clear - and dropping the stretch
being filled waits too, because the drainer is writing into its buffer. Same
run: every wait 0.00 ms, anchors 13 to 14 ms, 300 frames in 11.5 s instead of
12.8 (10.0 s with no history at all), the EE RAM identical at three seeks, and
six cores byte-identical plain, seeking and seeking in line. The fill itself is
slow for a memcpy because the buffer is new: the kernel zero-fills each of its
fifty-six thousand pages on first touch, which is the cost the no-init
allocator moved OFF this thread rather than removed.

#### The frame after an anchor, and what a second drainer and phase 4 would buy (2026-09-13)

With the wait gone, the two ideas left on the list for this path - more threads
filling a plan, and deferring a delta's copy the way an anchor's is deferred
(phase 4) - were measured on the same PlayStation 2 run before either was kept.

**A drainer pool was built, measured, and taken out again.** The sandbox claims
every page of a plan with an atomic exchange, so several threads filling
disjoint slices of one plan cannot copy a page twice, and splitting the fill was
twenty lines. It did make the fill faster - the drainer's slice went from about
145 ms alone to 78 with two threads and 31 to 39 with four, the kernel's
first-touch faults scaling across threads - and the EE RAM stayed identical at
three seeks. And it bought nothing anyone could feel: 300 frames took **11.5 s
with one, two and four threads alike**, anchors stayed 12 to 17 ms, and nothing
waited on any of them. Once the loop stopped waiting for the fill, the fill's
length is a window in which a guest write to an uncopied page is copied by the
fault handler - and that window was not where the frame's time was. WorkThread
says it is not a thread pool, deliberately; this is the measurement that says
the deliberate choice costs nothing.

**Phase 4 is not built, and the measurement says why.** What a captured delta
costs this thread, per delta past boot, split three ways:

| | mean | median | 90th | worst |
|---|---|---|---|---|
| the sandbox writing the delta out | 3.6 ms | 2.9 | 6.9 | 10.7 |
| coarsening | 0.04 ms | 0.00 | 0.01 | 4.1 |
| the budget | 0.00 ms | 0.00 | 0.00 | 0.00 |

So the delta write is the whole of it, and phase 4 would move exactly that to a
helper by holding the pages copy-on-write. But the pages a delta copies are the
pages the guest wrote THIS frame, which are overwhelmingly the pages it writes
again NEXT frame - that is what made them hot - so holding them turns a memcpy on
this thread into a fault on this thread followed by the same memcpy. An anchor
wins by deferring because most of a machine is not written in the next hundred
milliseconds; a delta is, by construction, the part that is.

Two things are worth keeping from that. A synthetic bench measured the
mechanism correctly and the SYSTEM not at all, because the allocation it did
once in a loop of its own was the whole cost in the real path. And the tool that
found it was one extra number in a trace line - the same lesson as the gate that
swallowed its stderr, arriving from the other end.

#### What cheap anchors did to the backwards step, and what they did not

The design said phase 3 might delete phase 6: an anchor that costs 12 ms
instead of 120 can be taken every sixty frames instead of every six hundred, and
then every backwards seek walks a short chain by construction - no speculation,
no cache, no second code path that can disagree with the first. Measured on the
PlayStation 2, seeking back two hundred frames:

| anchor every | budget | what the seek cost |
|---|---|---|
| 600 frames (the default) | 512 MB | **91 ms** - anchor 0 (1.8 MB) plus **75 deltas, 62 ms of it** |
| 120 frames | 512 MB | 81 ms - the anchor read back from disk, plus 20 deltas |
| 60 frames | 2 GB | **39 ms** - anchor 183 (221.2 MB) **34 ms**, plus 6 deltas, 5 ms |
| 20 frames | 2 GB | 40 ms - anchor 190 (221.2 MB) 35 ms, plus 5 deltas, 5 ms |

So the prediction holds, and it has a floor. Taking anchors five times more
often more than halves the backwards step, 91 ms to 39 - and then stops dead,
because what is left is not the walk at all. **It is loading one whole machine:
34 of the 39 ms.** Going from sixty frames to twenty buys one millisecond.

That changes the answer to phase 6 rather than confirming it. A rolling composed
prefix shortens the DELTA WALK, and on a machine of this size the delta walk was
already five milliseconds of thirty-nine. The speculation would be buying the
part that is already cheap. What it cannot touch is the anchor load, and nothing
that starts from an anchor can.

Two things follow. `m_anchorSpacing` at 600 is now a number chosen when an
anchor cost what an anchor used to cost, and on a heavy core it should come down
a long way - at the price the bands have always charged, which is that an anchor
is the biggest thing the budget holds, so denser anchors mean fewer frames kept.
That trade is per-core arithmetic (a PlayStation 2's anchor is 221 MB, a Game
Boy's is under a megabyte) and it wants measuring per core rather than a new
constant guessed here.

And the only thing that could reach below that floor is not a cache: it is
going BACKWARDS from where the machine already is, which is what reverse deltas
were, and they were removed for costing a page copy in the fault handler on
every frame. That handler now copies pages for a planned anchor already. Whether
the same machinery makes reverse deltas cheap enough to bring back for the near
band alone is a real question, and a different one - it belongs to whoever picks
this up next, with a measurement rather than an opinion.

**It was picked up, and measured (2026-09-13): not bringing them back.** A
reverse delta needs every page a frame writes as it was BEFORE the write. A hot
page has that already - its shadow is the page as the epoch opened - so the
question was how much of a frame's writing lands on hot pages. On the
PlayStation 2 (`MB_TRACE_DELTA=1`, 300 frames of Gran Turismo 4, 121 deltas):

| | pages a delta | of them hot | pre-images the fault handler would copy |
|---|---|---|---|
| every delta | 1,283 | 22% | median 454 pages, 90th percentile 1,305, worst 43,122 (168 MB) |
| the last 60, past boot | 852 | 35% | 556 pages, **2.2 MB a frame** |

So two thirds of what a frame writes lands on pages that are held, not hot, and
every one of those would be copied inside the fault handler again - the exact
cost the removal took away, a little under half of it, with a burst of 168 MB
in one frame that a fixed pool would have to survive. On top of it the near band
carries a second delta for every frame it keeps, so the same budget keeps about
half as many frames there. And what it would buy is smaller than it looks,
because this machine's near band is not keeping every frame to begin with:
capture is already 38% of the run, the stride tuner has the near band keeping
one frame in three, and a step back to a skipped frame replays whatever
reverse deltas saved. Adding cost to capture makes the tuner thin the band
further, which is more replay. The planned anchor already took the floor that
mattered (an anchor on this thread 113 ms to 17), anchors every sixty frames
took the walk to five milliseconds, and the rest is one anchor load. Reverse
deltas stay out; the number that would change the answer is a core where most
written pages are hot, and none measured so far is one.

#### The spacing picks itself (2026-09-13)

`m_anchorSpacing` was a constant, 600, chosen when an anchor cost what an
anchor used to cost, and the section above says it should come down a long way
on a heavy core and not at all on a light one - which is per-core arithmetic,
and a table of cores is exactly the kind of thing that is wrong the day a core
changes. So the history does the arithmetic itself, from what it is holding.

A restore loads one anchor and walks the links after it, and both cost roughly
what their bytes cost to put back. So a stretch is closed once walking it would
cost as much as loading its anchor: its links weigh what the anchor weighs. A
seek then costs at most about two anchor loads, on any machine, and nobody had
to measure that machine first. `hasRoom` decides it, on the emulation thread and
from logical bytes, so threaded and in line close stretches on the same frames.

Weight alone was wrong about light machines, and the first run said so: a NES,
an Atari 2600, a Genesis and a SNES all closed a stretch every thirty-one frames,
because their anchors are small enough that thirty frames of four-kilobyte pages
outweigh them - and a seek there was already a millisecond, so that bought
nothing and spent the budget on anchors that cannot be coarsened. So there are
two floors. The links must also weigh `m_anchorWalkFloor`, 64 MB - about twenty
milliseconds of walking on the PlayStation 2 - before their weight counts; and a
stretch spans at least thirty frames, so a machine whose every frame rewrites
most of it does not become a history of anchors. With the floors, those four
cores went back to one anchor for the whole run (the NES, the 2600 and DOS took
one or two more, at the four-megabyte budget the oracle squeezes them into),
and all six were byte-identical plain, seeking, and seeking in line.

The PlayStation 2, 300 frames, 4 GB in memory and 10 GB on disk:

| seek back to | fixed, every 600 | weighed (anchors landed at 31, 106, 215) |
|---|---|---|
| 100 | 99 ms - anchor 0 + 60 deltas | **63 ms** - anchor 31 + 33 deltas |
| 200 | 103 ms - anchor 0 + 86 deltas | **44 ms** - anchor 173 + 8 deltas |
| 290 | 135 ms - anchor 0 + 155 deltas | **53 ms** - anchor 215 + 37 deltas |
| held in memory at the end | 603 - 861 MB | 1,025 - 1,520 MB |

The EE RAM is identical across all nine runs. The price is the one the bands
have always charged, said plainly: the anchors are the biggest things the
budget holds, so the same budget keeps fewer frames in memory - here about 650
MB more for three anchors. That is why the floor is a weight rather than a
frame count: a machine only pays for denser anchors when its seeks were slow
enough to be worth it, and the far band's anchors go to disk compressed, where
one is about ten megabytes.

The caller keeps both choices. `bands(..., anchorSpacing)` with a positive
spacing is that spacing exactly, as before; a negative one is a ceiling with the
weighing on, which is also the default with a ceiling of 600. The tests pin
both: a fixed spacing yields one stretch, a ceiling of 50 yields stretches of
51, the weighed rule on a tiny machine closes stretches at the frame floor, a
ceiling below the frame floor still wins, the real walk floor leaves a light
machine one stretch, and every frame the weighed stretches offer restores
exact. The two fuzzers now draw a weighed spacing for half their seeds.

#### Phase 5: what reaches the disk is compressed

Measured before it was built, on real machine states at zstd level 1:

| state | raw | compressed | ratio | compress | decompress |
|---|---|---|---|---|---|
| PlayStation 2, Gran Turismo 4 | 232.9 MB | 9.95 MB | 23.4x | 3.2 GB/s | 6.1 GB/s |
| DOSBox-X | 9.3 MB | 253 KB | 36.7x | 4.3 GB/s | 9.5 GB/s |
| ares, NES | 2.1 MB | 17.7 KB | 117.6x | 8.8 GB/s | 12.0 GB/s |
| Genesis Plus GX | 822 KB | 37.9 KB | 21.7x | 3.3 GB/s | 6.6 GB/s |
| Snes9x | 500 KB | 64.1 KB | 7.8x | 2.5 GB/s | 3.6 GB/s |

A machine is mostly memory nobody has written, and what has been written is
mostly repetitive; seven to a hundred and eighteen times smaller, at speeds a
disk cannot match. (`tests/perf/spillbench` prints a ratio too, and it is
meaningless - its fake machine writes single-byte slices, which compress to
nothing. The ratios above are the real ones.)

**How it is built, and the one thing it had to keep.** The writer compresses
each body as it serialises it - never holding a second copy of a whole machine
- and appends the frame wherever the file really ends. Everything the history
DECIDES with stays logical: a spill still reserves the uncompressed length at
once, and what is live, what is dead, when to compact and what the disk budget
drops are all computed from those numbers, exactly as before. A size nobody
knows until the compressor has run could not have come out the same threaded as
in line, and that sameness is what the differential test exists to hold. Where
the bytes really landed - `physAt`, `physLength`, `packed` - arrives with the
writer's report, and only readers look at it. Appending rather than writing at
the reserved offset matters on Windows: a compressed body written in place would
leave a hole the size of the difference after every stretch, and NTFS fills
holes with zeros. Every reader (a restore, a save, settling) goes through
`SpillBodyReader`, which hands back the raw layout from either a zstd frame or a
raw extent without holding the decompressed body whole; a saved history is
compressed on its own terms (below), whatever the spill file holds. Settling now writes its
result on the writer too, where it used to write a whole machine on the
emulation thread once per settled stretch.

**What it measured.** A PlayStation 2 seek back to frame 198, landing on a
stretch whose 221 MB anchor has been spilled (anchor 183 plus 5 deltas):

| bodies | page cache | the restore | the spill file on disk |
|---|---|---|---|
| raw | warm | 66 ms | 2,747.5 MB |
| compressed | warm | 54 ms | 79.8 MB |
| raw | cold | **103 ms** | 2,509.2 MB |
| compressed | cold | **59 ms** | 79.9 MB |

The EE RAM after the run is byte-identical in all four. The file is 31 times
smaller for the same logical history, and a cold restore is 1.75 times faster on
this disk, which reads at 1.4 GB/s. On the Windows install's NTFS, measured at
176 MB/s, a cold read of that anchor raw would be about 1.3 s against about 57
ms of compressed read plus 36 of decompression - an estimate from the measured
throughput, not a measurement of the restore.

**The measurement had two traps in it, and both would have flattered the old
path.** The first attempt seeked back to frame 60 and landed on anchor 0, the
1.8 MB boot state: it measured a delta walk, not a machine read off a disk. And
a stretch spilled a moment ago sits in the page cache, so a "disk" read of it is
a memcpy, and compression can only ADD its decompression time there - there is
no dropping the cache system-wide without root. `CHIMERA_SPILL_COLD=1` makes the
writer flush each body and evict its own file's pages (`posix_fadvise`, which
needs no privilege), and that is what the cold rows are. `CHIMERA_SPILL_RAW=1`
writes bodies uncompressed, for the A against the B.

**And then it bought depth, because the owner of the budget chose it
(user-decided, 2026-09-13).** As first built, the disk budget still counted
logical bytes, so the same setting kept the same stretches in a thirty-first of
the space. The choice put to the user was to count what the file really weighs,
at the price of the disk half of the history's decisions following the writer's
timing; the answer was to count compressed bytes.

So a stretch now costs the disk nothing when it is reserved and costs what the
writer really wrote when the writer reports it; forgetting or settling a
stretch gives back what it really weighed; compaction judges the file by its
real length (`m_fileBytes`). The budget is held once a frame, after that frame's
reports have been applied, and again whenever a flush lands more - and between
frames the file may run ahead of the budget by what is still being written,
which the writer's queue bounds. `diskBytes()` now reports what is really on
the disk, which is also what the cache manager is shown.

The same PlayStation 2, a 1024 MB disk budget (512 MB live), a seek back to
frame 130:

| bodies | what is live on disk at the end | the seek back |
|---|---|---|
| raw | 528.2 MB, the budget full, stretches dropped all run | not in the history any more: replayed |
| compressed | 79.7 MB, nothing ever dropped | **from disk: anchor 124 plus 2 deltas, 49 ms** |

with the EE RAM identical, and a writer that was handed 4.8 GB of bodies and
wrote 135 MB of them. That is the depth: the raw run's budget was spent on two
anchors at a time and threw the rest away, and the compressed run never came
near it.

**What it cost, said plainly.** The step-for-step sameness of threaded and in
line now covers what the history DECIDES with memory - which frames are kept,
what is held, what is coarsened, what is spilled - but no longer what the disk
budget drops, because a stretch starts to weigh on the disk when its report
lands and that moment is the writer's. Both modes drop correctly; they can drop
at different moments. The differential test says exactly that: it runs without
a disk budget, compares the history step by step, and compares what the disk
holds once, after a flush, where the same stretches compress to the same bytes
either way. The disk budget's own correctness - every frame still offered comes
back exact, the first stretch is never dropped, the file is held to the number
given - stays the fuzz's and the disk-budget test's to prove, and both pass.

It was tested the way everything else here was: `test_state_history` with
bodies compressed, raw and in line, and twenty repeats compressed; the six real
cores through `chimera-run`, every seek back restoring from compressed bodies,
byte-identical; the synthetic witness, 48 ok; fifteen more repeats each with
the helpers off and with bodies raw; the UI suite, none failed; the three
frontend soaks, clean. And on real Windows, where the writer loads a different
libzstd from beside a different library: NES, Atari 2600 and Genesis through a
cross-built `chimera-run.exe`, plain against seek against seek in line,
identical - with the trace checked rather than assumed, because a missing DLL
would have written every body raw and passed anyway. It reported 37 planned
anchors, a restore read back from disk, and a writer handed 29.5 MB that wrote
342 KB.

#### A saved history is compressed too

A project's saved history is the greenzone written out, so it is made of the
same mostly-unwritten machines the spill file is, and it had stayed raw.
`ChimeraHistory4` is the magic, written as it is, followed by exactly
`ChimeraHistory3`'s layout as one zstd stream at level 1. Nothing else about
the format changed, and that is deliberate: the writer streams the same records
through the compressor that it used to stream to the file, a spilled stretch is
copied out of the spill file through `SpillBodyReader` into the same stream, and
the loader reads the records through a `SpillBodyReader` over the rest of the
file, raw for 3 and compressed for 4. So nothing of a history is held twice to
save or load it, which is the promise the persistence code exists to keep, and
a length the file could not hold is still refused as damage before anything is
allocated for it (bounded, when compressed, by the densest block zstd can
write). 3 is still read, and still written when there is no libzstd or
`CHIMERA_HISTORY_RAW=1` asks for it; 1 and 2 stay superseded.

The PlayStation 2 again: 300 frames, 512 MB in memory and the rest spilled, so
the save copies stretches out of the spill file as well as serialising the ones
still in memory.

| saved history | on disk | the save | the load |
|---|---|---|---|
| raw (`ChimeraHistory3`) | 1,845.5 MB | 881 ms | 954 ms |
| compressed (`ChimeraHistory4`) | **60.0 MB** | **685 ms** | 1,202 ms |

Thirty-one times smaller, and the save is FASTER, because level 1 compresses
faster than this disk writes. The load is a quarter slower here, and that is the
flattering case for raw: the file had just been written and was read back out
of the page cache. A cold read of 1.8 GB off the Windows install's NTFS, at the
176 MB/s measured there, is ten seconds before a byte is decompressed. Both
reopened histories served a seek back to frame 144 from the loaded stretches,
and the EE RAM after all four runs - saved raw, saved compressed, reopened from
each - is byte-identical. `test_state_history` gained a round trip that
restores every offered frame from a compressed file and checks every note,
checks which magic was written, and refuses a compressed file cut in half; it
passes compressed, raw and with the helpers off.

#### A closed stretch is packed in memory (user-asked, 2026-09-18)

The disk was compressed and memory was not, and memory is where the greenzone
lives now. The user asked for compression to be measured on PS3 states once
the disc was no longer carried twice (rpcs3 76b992c): the remaining 1.23 GB of
an Oblivion state packed 7.7x at zstd level 1 in 1.1 s. So the history packs
its bodies in memory, and the question was where and when, because "phase 2"
had already been left undone for exactly the reason that threatened here: a
helper that lands its result whenever it finishes gives a history that holds
different frames threaded than in line, and the differential fuzz is the
reason the history is trusted.

**What is packed.** A stretch that has CLOSED - the next anchor has been taken.
From then on it is only read (a restore, a save) or shortened (a merge, a pop,
a drop), never written to, so its anchor and its links are each held as one
zstd frame (level 1, the writer's level, for the writer's reason). The newest
stretch stays raw: it is the one still being appended to and the one a
backwards step lands in most. A `Body` now says what it IS (`size()`, the raw
length, which every decision is made from - the merge caps, when a stretch
closes, what a spill reserves) apart from what it COSTS (`held()`, which is
what the budget counts). A body that does not shrink stays raw.

**When the budget learns of it.** The packing is on a helper of its own (a
gigabyte is a third of a second at best), and its result is taken on the
emulation thread at ONE moment: when the next stretch closes. The job was
posted a whole stretch ago, so the wait is normally nothing, and it is paid at
all only so that the moment `bytes()` changes is the same threaded as in line -
the differential fuzz's signature includes `bytes()` step by step, and it
passes unchanged. A body is matched back by identity, so a stretch shortened
meanwhile keeps what it still has and a stretch dropped meanwhile takes
nothing. `CHIMERA_HISTORY_TRACE=1` prints each stretch as it is packed and any
wait for the packer; `CHIMERA_HISTORY_PACK=0` keeps everything raw, for the A
against the B.

**What reads a packed body.** A restore streams it into the sandbox through a
`MemBodyReader` - the in-memory twin of `SpillBodyReader`, a megabyte at a
time, never the body decoded whole. A save writes it as it is (see "A saved
history carries its bodies as they are held", below; the first version decoded
and re-encoded it into the file's stream). A merge decodes its two inputs
whole - they are capped at megabytes - and packs the result again in line, so
a packed stretch stays packed as it thins. A load packs every stretch but the
newest IN LINE, before the budget looks at it: a history saved from twenty
packed PS3 stretches would otherwise be thinned to three the moment it came
back.

**Measured, Oblivion (PS3) on the GTX 1060**: 2400 frames, an 8 GB budget, a
rewind to frame 1200 three times, everything else the same.

| | raw (`CHIMERA_HISTORY_PACK=0`) | packed |
|---|---|---|
| a stretch (anchor 1.2 GB + its deltas) | 2.45 GB | 0.26 to 0.44 GB (5.6x to 9.2x) |
| frames held at the end | 312 | **817** |
| the rewind to 1200 landed on | anchor 434, then 766 frames replayed | **frame 1198**: anchor 910 + 72 deltas |
| that restore | 0.19 to 0.35 s (then the replay) | 0.99 to 1.20 s |
| packing a stretch, on the helper | - | 2.1 to 2.6 s |
| waits for the packer on the emulation thread | - | none |

The deltas pack worse than the anchor (a PS3 frame's churn is not zeros), so a
stretch packs six to nine times where a lone state packs eight. The restore is
slower per byte - 1.2 GB decoded at about 3 GB/s is 0.4 s the raw path did not
pay - and faster per rewind, because the raw budget had thinned frame 1200's
neighbourhood away and the packed one had not: a second of decoding against
766 frames of PlayStation 3 emulation. That is the trade the whole history
makes, restated: memory is depth, and depth is what a rewind costs.

`test_state_history` gained two blocks under a padded fake machine (its 64-byte
states do not shrink): every frame of every packed stretch restores exactly,
threaded and in line; a saved history comes back no heavier; and a budget that
forces merges inside packed stretches (35 of them, through the decode path)
still answers every offered frame exactly.

#### A saved history carries its bodies as they are held (user-asked, 2026-09-18)

`ChimeraHistory4` was 3's layout as one zstd stream, and with the stretches
already packed in memory a save had to decode every frame and encode it again
into that stream, and a load had to decode the stream and pack every stretch
again in line. `ChimeraHistory5` writes each body as it is held: a record is
`raw length, held length, bytes`, and held < raw says the bytes are a zstd
frame (a packed body is only ever kept when it shrank, so the two lengths are
the flag and there is no other). A body still raw at the save - the newest
stretch, one that would not shrink - is packed on the way out, so the file is
no bigger than 4 was. The file itself is not compressed; its bodies are. A
load takes a frame as it is, without decoding it: one that will not decode is
found by the restore that needs it, which drops that stretch and says so, as
any chain that will not walk is treated. 3 and 4 are still read (a 4 is
hand-built in the test from a 3 through libzstd), 1 and 2 stay superseded,
and `CHIMERA_HISTORY_RAW=1` still writes 3. A spilled stretch is copied in one
body at a time - read raw, packed, written - so an anchor is the most that is
held whole meanwhile; the frontend spills nothing.

**Measured, Oblivion (PS3) on the GTX 1060**, 2400 frames under an 8 GB
budget, the same history written by both writers, then loaded by a fresh
process that seeks back to 1500 through it:

| | `ChimeraHistory4` | `ChimeraHistory5` |
|---|---|---|
| the file | 2.567 GB | 2.568 GB |
| the save | 18.5 s | **4.7 s** |
| the load | 23.1 s | **1.0 s** |
| the restore of 1497 from the loaded stretch | 0.80 s | 0.82 s |

What is left of the save is the newest stretch being packed on the way out and
2.5 GB going to the disk; what is left of the load is reading 2.5 GB. The
restore is the same because the stretch arrives as it was held either way.

Found on the way, and run down the same day (user-asked): `--greenzone-check`
on this PS3 run reported the machine restored at 1500 as 543 to 552 pages
larger than the straight pass's state - in the same process with no history
file, and from a 4, a 5 and a raw file alike, so not the history's. Two
things, once the two states were aligned page for page:

- **A miniBox defect, fixed (chimera-common-minibox b5092e5).** A page the
  guest gives back inside an epoch (munmap, MADV_DONTNEED) is zeroed and made
  CLEAN on the live machine, its baseline being zero - but the epoch still
  listed it, so the delta carried its zeros and `delta_apply` marked it dirty.
  A machine rebuilt from deltas therefore held pages a machine that ran the
  frames did not: identical bytes, a bigger state. On Oblivion, 100 pages
  after a 55-delta restore, all zero. A data entry's index now carries a
  "clean" flag in its top bit, the composers keep the later entry's flag, and
  apply leaves such a page clean. With the fix, and the GL rebuild off, the
  restored state is the same size and the same page set as the straight one.
- **What is left is the GPU's, not the machine's.** 44 KB in 106 pages, sizes
  equal: about ninety pages of RGBA pixel rows (the RSX's readback into guest
  memory - the redrawn picture, which after a restore is drawn by GL objects in
  another state), two counters one apart, and a few read-only pages holding
  host addresses. With the rebuild on (the default, issue #43) the rebuild's
  own allocations add some 400 pages on top. MainRAM is identical throughout.

So the byte-exact oracle cannot pass on a GPU-bridged core whose readback
lands in guest memory, and that is not a greenzone defect; the oracle for
such a core is the memory domain (`--dump MainRAM`), which is what every
comparison today used.

#### The stride tuner stopped listening to anchors

Found while measuring rather than while looking, and true before any of this
was built. `tuneStride` fed every capture into one exponential mean, anchors
included - and an anchor costs what the MACHINE is where a delta costs what the
frame did. One 121 ms anchor in a stream of 2 ms captures dragged the mean far
enough past `kCostShare` to treble the near band's stride, which then recovered
one step per thirty captures. A significant share of every heavy run was being
stored sparsely because of a cost that had nothing to do with the frames being
stored - and thinning the near band cannot make an anchor cheaper anyway,
because anchors happen on `anchorSpacing` rather than on the stride.

So an anchor now only resets the clock the next delta measures against. On the
PlayStation 2 run above the near band thins once, for deltas that really are
expensive on that machine, and then comes back to keeping every frame.

#### How close the near band stays (user-decided, 2026-09-15)

The tuner above thins the near band when storing every frame costs more than
`kCostShare` of the run. Nothing capped it but 32, and on a heavy core it got
there and stayed: a user editing a Ruffle project reported that "as I progress,
the greenzone snapshots are starting to get more and more scattered. They
should be always closer to the last input". Mapped with `chimera-run
--greenzone-map` after 2500 frames of that project, the history kept one frame
in 32 right behind the playhead and one in two a thousand frames back - the
older ones had been captured before the stride rose, so the frames a person
rewinds to were the sparsest it held.

What each stride cost, measured on the GTX 1060 with the stride pinned
(`CHIMERA_NEAR_STRIDE`), 2500 frames, memory budget 2 GB:

| near band | wall time | frames kept in the last 120 |
|---|---|---|
| no history | 12-18 s | - |
| every frame | 71-74 s | 120 |
| one in 2 | 52 s | 62 |
| one in 4 | 41 s | 33 |
| one in 8 | 32 s | 19 |
| one in 16 | 24 s | 10 |
| one in 32 | 22 s | 5 |

A sparser band is genuinely faster, and more than the tuner's own number says:
the share it measures is the time inside a capture, while a stored frame also
runs slower - miniBox write-tracks the pages it touches - and that lands in the
emulation. The share stayed near a third from stride 1 to 10 while the run got
much faster. A first fix that kept a raise only when the share fell undid raises
that were paying, and ran 2.6 times slower; it was not kept.

So the stride is capped, and the cap is a setting: `GreenzoneMaxNearStride`, 4
by default, beside the memory budget in the Greenzone budgets window, for every
project and per project (`budgets.json` in the project's cache directory, like
the budget - a fact about the machine, not the movie). At 4 a rewind right
behind the playhead replays at most three frames. The engine takes it as
`ce_session_greenzone_max_near_stride`; `chimera-run --greenzone-max-stride`
does the same. The decision itself is `stride_tuner.h`, checked without a
machine by `test_stride_tuner`.

#### The four defects the testing found

None of these would have been caught by a green unit suite, and each is the
reason the bar was set where it was.

1. **A reader and the writer shared one FILE, and a FILE is one position.**
   Even with glibc locking every call, and even reading a range nobody was
   writing, the loop seeking to offset 0 and the writer seeking to the end
   interleaved - and the loop's read came back from the end. It surfaced as
   **restores returning somebody else's frames, seven runs in twenty**. The
   file is now open twice: the writer owns one handle, the loop owns the other,
   and separate handles are separate positions.
2. **The save barrier called into a freed session.** A four-thousand-frame soak
   survived every seek and died on the way out: a movie is disposed when a
   project closes, and by then the emulator it borrowed may be gone. The
   barrier belongs to the session (`WaterboxCore.Dispose`), which is the thing
   that knows it is alive.
3. **A barrier taken before deciding not to act.** `compactSpill` drained and
   then decided not to compact, so every spill waited for its own write within
   a frame or two of queuing it: a 1.8x win became a 2.4x LOSS the moment the
   file had a budget. The cheap question comes first now, and the wait only for
   an answer of yes.
4. **Compaction judging a file that had not been written yet.** With writes in
   flight the counts describe a file that does not exist - ranges reserved and
   not written, and stretches dropped before their write landed - and it
   compacted at every opportunity instead of every third one, copying every
   live byte each time. A busy writer now defers the question to a quieter
   frame, and past three times the live set compacts anyway, because the
   promise about the file's size is a promise.

Two more things came out of the same work: a queued write whose stretch is
dropped before it lands is **cancelled** rather than written, and the writer is
not allowed to fall further behind than half the memory budget (four megabytes
to sixty-four), because an unbounded queue is memory the budget cannot see.

#### What it was tested against

- `test_state_history` **40 runs threaded and 25 in line, no failures** - the
  repetition is the point, since the FILE-position bug failed seven in twenty.
- A differential block in that suite runs the same 400-operation sequence
  threaded and in line and compares what a caller can see after every step -
  count, bytes held, bytes on disk, and the exact set of frames offered - and
  they are identical, which is the claim phase 1 makes.
- miniBox's own suite, 8 of 8, including a new `test_plan`: a planned state
  equals a written one; writes during a plan do not change it; a THREAD filling
  while the guest writes the same pages, eight rounds, is identical; a planned
  state loads; and an epoch across a plan still describes what the frame did.
- The synthetic witness, **48 ok, 0 failed**, which is the frontend and the
  engine both driving a real core through a real greenzone.
- Six real cores through `chimera-run`, each played to the end, then played
  again with a seek back through a greenzone small enough to spill, then again
  with the helpers off: **NES (two games), Atari 2600, Genesis, SNES and a
  disk-backed DOS machine all come back byte-identical in all three**. ares and
  Ruffle expose no memory domains to compare, and their savestates are not
  byte-reproducible between two identical plain runs, so they were checked for
  liveness only - they run clean under a spilling greenzone either way.
- Three frontend soaks: six thousand frames of play with a jump back every
  thirty-five, six thousand in RECORD mode with a jump back every twenty-five,
  and four thousand with the greenzone thrown away every eight hundred. All
  exit clean.
- The UI suite, 727 tests, none failed.
- **And all of it again on real Windows**, which is where the fault handler is a
  vectored exception handler and where a guest stack may not be held at all.
  miniBox's suite cross-built and run through interop, `test_plan` included:
  106 checks, all passed. Then three real cores - NES, Atari 2600 and Genesis -
  driven by a cross-built `chimera-run.exe` from a staged directory of its own:
  plain, seek back through a spilling greenzone, and seek with the helpers off,
  byte-identical in all three. The trace was checked rather than assumed: 37
  planned anchors and none written in line, so the path under test is the path
  that ran.

### What must be proved before any of it lands (user-decided, 2026-09-12)

None of this is committed or pushed on a green unit suite. The history is the
one part of Chimera whose failures are silent, arrive late and land somewhere
else - the `munmap` delta bug above surfaced seventy frames after the seek that
caused it, in a `std::map` insert - and a helper thread is exactly the kind of
change that turns a rare wrong byte into an unreproducible one. So the bar is
the user's, and it is a bar of evidence rather than of green ticks:

- **Stress.** Long runs, not the few hundred frames a unit test affords: the
  budget full, the spill file compacting, stretches settling, all at once and
  for long enough that the steady state is the thing being tested rather than
  the warm-up.
- **Backwards.** Seeking back, repeatedly and at every distance - inside the
  near band, into the mid band, into a spilled stretch on disk, and to frame
  zero - because a restore that races pending work is the failure this design
  most invites.
- **Back and forth with changing inputs.** Re-recording: seek back, change an
  input, run forward, seek back further, change another. That is the sequence
  that found the last three history bugs, and it is the one that makes
  `invalidateAfter` race everything the helper is holding.
- **Cores with disk-backed files.** HDD dumps and save data, where the machine
  is not only its arena.
- **Every core.** All of them, not the two that are easy to drive, because the
  cost profile that decides whether a thread helps at all is per-core: a Game
  Boy's whole state is smaller than one page of this document's tables.
- **Different situations.** Playback, recording, turbo, a seek in progress, a
  project being saved, an encode running, the budget being hit mid-seek.

And the performance claim must be MEASURED, before and after, on the same box:
`run-storebench.sh` and `run-epochbench.sh` for the machinery, and a real core
under `CHIMERA_LOOP_TRACE=1` for the frame, reported as a per-frame maximum
rather than a mean. A change that improves the average and keeps the hitch has
not done the thing it was built to do.

Only then does any of it become a commit.
