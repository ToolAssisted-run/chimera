# Seven ways a gate goes green on a broken thing

Every failure mode below is one that shipped here. None is hypothetical, and
all seven were found on 2026-09-20, in a single day, across three different
repositories. They are written down because they are a class of mistake rather
than seven accidents, and because the cost of each was measured in months.

A gate is a claim. When the claim is wrong, the damage is not that a bug
reached a user - bugs reach users anyway. The damage is that the green tick
said one would not, so nobody looked.

Read this before writing a leg, and when a leg has never been seen to fail.

## A. A leg that never runs, anywhere

It SKIPs in CI, and nobody runs it locally either. The check exists in the
script, reads plausibly, and has never once executed. A permanent SKIP is not a
check; it is a comment.

> quickerNES's `ports:columns` SKIPped because it wants `CHIMERA_ROOT` and
> nothing sets it. When it was finally run by hand it FAILED: an Arkanoid
> paddle records its neutral as `80` where the gate expects `0`. A dormant leg
> had been sitting on a real defect for as long as it had existed.

**What to do.** A SKIP must name what would make it run, and somebody must be
able to answer "when did this last actually execute?". A leg that can only run
on a machine with content CI does not have is legitimate - flycast's arcade
legs are the good case, because the content is somebody's copyrighted rom set -
but then the PLAN.md records what it said on the machine that had the roms, and
the SKIP is visible in the summary rather than silent.

## B. A check with no negative control

Ask of any leg: *would this still pass if the thing it checks were stubbed out,
empty, or absent?* If nobody has ever watched it fail, you do not know that it
can.

> The PCSX2 gate was green for months while PCSX2's per-title game database was
> EMPTY - every PS2 game running with none of its own corrections, because
> `GameDatabase.cpp` had been excluded from the source set and `findGame()`
> stubbed to return nothing. No leg asserted the database contained anything at
> all.

**What to do.** Break the thing and watch the leg go red, then revert. Write in
the commit that you did. This is the single cheapest, highest-value act in
testing and it takes minutes. When a leg is added for a bug that was just
fixed, run it against the build that had the bug; if it does not fail there, it
does not test the bug.

## C. Absent is indistinguishable from failed

An optional probe answers "not supported" when the truth is "it broke". The
caller cannot tell the difference, so a failure wears the costume of a feature
that was never there.

> miniBox's thunk pool held 32 entries and returned 0 for everything past them.
> `mb_host_proc_addr` reports that 0 exactly as it reports a symbol that is not
> in the ELF. Seventeen of quickerNES's forty-nine exports read as "this core
> does not have that feature" while sitting in the binary.

**What to do.** An optional probe needs three answers, not two: present,
absent, and failed. Where the API cannot carry the third, make the failure
loud - a line on stderr naming what ran out is enough, and it is what turned
this one from invisible into obvious. The same rule applies upwards: Lua's
memory callbacks returned an empty GUID for "this core cannot", which reads as
success; they now raise.

## D. Asserting only the default state

The check inspects the thing at rest and never in the state where it breaks.

> A test named "nothing is left holding a light colour under Dark" passed while
> selected rows in every list were unreadable - light text on a light
> selection. It only ever looked at UNSELECTED rows.

**What to do.** Enumerate the states the thing has - selected, focused, empty,
disabled, mid-operation, at a boundary - and say which the leg covers. A leg
that covers one state should say so in its own description, so the next person
does not read it as covering all of them.

## E. A synthetic stand-in that does not behave like the real subject

The gate runs a small program of its own. That program exercises a different
path from the thing users run, so a whole class of bug is structurally
uncatchable - not missed, uncatchable.

> The flycast gate's GPU legs use `triangle.elf`, which draws one untextured
> flat polygon. Its texture cache therefore never holds a section over a render
> target, which is precisely the condition of the state-load crash in issue
> #110. No amount of running that leg could ever have found it. (rpcs3's
> `flip.elf` is the same shape for the same reason: it draws with the CPU.)
>
> The rpcs3 gate's own programs agree native == sandbox exactly. Real games do
> not always (issue #120). The gate is green and the guarantee it stands for is
> unestablished for games.

**What to do.** A synthetic subject is often the only lawful one - a gate
cannot ship somebody's game. That is fine, but write down what it does NOT
stand in for, in PLAN.md, next to the leg. The failure is not using a stand-in;
it is forgetting that it is one.

## F. A timing or ordering assumption that makes the check vacuous

The leg asks its question at a moment when the answer cannot be anything but
the expected one.

> The flycast panel leg pressed Test at frame 450. The Atomiswave BIOS does not
> read the cabinet switches until about frame 2000. Here it produced a FAIL and
> so was noticed; the same shape elsewhere produces a PASS that means nothing.

**What to do.** A leg that presses a button must prove the machine was in a
state to read it - the classic form is idle != pressed, which fails honestly
when the press was too early. Beware the inverse trap: the press must still be
HELD on the frame that is captured.

## G. CI runs something different from what the local gate runs

Whatever CI does not run is not gated, whatever the script says.

> The rpcs3 core's entire gate was unrunnable locally for eight days -
> `waterbox/native.mk` was never updated when new sources arrived, so the
> native reference did not link - and CI never noticed, because CI does not
> build the native reference.

**What to do.** Keep a table, per repo, of what the gate script claims against
what CI actually runs, and treat the gap as a known, named risk rather than an
oversight waiting to be discovered. If CI cannot run a leg, somebody owns
running it, and the PLAN.md says who and when.

---

## How they group

A, C and G are checks that do not execute. B, D and F are checks that execute
but cannot fail. E is a check on the wrong subject.

The two cheapest audits, by a wide margin:

1. **G** - diff what CI runs against what the gate script contains. Reading
   only, and it tells you which legs are load-bearing.
2. **B** - break the thing, watch the leg go red, revert. Minutes per leg, and
   it converts a belief into a fact.

## The rule that covers all seven

**A leg that has never been seen to fail is a leg that has not been tested.**

"Fixed by construction" is not fixed. On 2026-09-19 a paletted-texture fix
shipped on that reasoning, with the commit saying plainly that no disc on hand
used the format so the fix "waits for a game of sprites to prove it". The next
day a game of sprites arrived and the fix was itself the bug - it expanded the
palette a second time, and Street Fighter Zero 3 drew a white gi as magenta.
Two plausible readings of the same code cannot both be right, and nothing but
running it decides which.
