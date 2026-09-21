# Eight ways a green tick lies to you

Every failure mode below is one that shipped here. None is hypothetical. The
first seven were found on 2026-09-20, in a single day, across three different
repositories; they are ways a GATE goes green on a broken thing, and the cost
of each was measured in months. They are written down because they are a class
of mistake rather than seven accidents.

A gate is a claim. When the claim is wrong, the damage is not that a bug
reached a user - bugs reach users anyway. The damage is that the green tick
said one would not, so nobody looked.

The eighth arrived the next day and is the same disease one step downstream: a
MEASUREMENT that lies while you are debugging what a gate caught. It costs days
rather than months, and it is easier to fall for, because a measurement is not
something anybody thinks to test.

Read this before writing a leg, when a leg has never been seen to fail, and
before believing a number that came out of a probe you wrote this morning.

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

> The theme round-trip test took a window Light - Dark - Light and compared it
> against one that started Light. Both ends of that journey are Light whether
> the middle worked or not, and the middle - a window wearing Light after being
> born Dark - was the only state a user ever saw, because the frontend now
> opens on Dark. Every test built its windows under Light, which is the one
> starting state in which the bug cannot happen.

> And a third time on the same control, which is what makes it a pattern rather
> than three mistakes: after the normal row and the chosen row came the row
> under the POINTER, drawn as a bar with nothing written on it. A control the
> frontend draws itself has no toolkit underneath, so a state nobody enumerated
> is not drawn wrong - it is not drawn.

**What to do.** Enumerate the states the thing has - selected, focused, empty,
disabled, mid-operation, at a boundary - and say which the leg covers. A leg
that covers one state should say so in its own description, so the next person
does not read it as covering all of them. Where a thing has a starting state as
well as an end state, the starting state is one of them: a check that only ever
starts from the default has not been told what the default hides.

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

> Every frontend test runs on Mono under Xvfb. The people who use Chimera run
> .NET Framework WinForms on Windows. That toolkit decides what an assignment
> to `BackColor` does, whether a control repaints, and whether visual styles
> override either - so 889 green tests said nothing at all about whether the
> theme menu worked, which twice it did not.

**What to do.** A synthetic subject is often the only lawful one - a gate
cannot ship somebody's game, and it cannot run Windows on a Linux runner. That
is fine, but write down what it does NOT stand in for, in PLAN.md, next to the
leg. The failure is not using a stand-in; it is forgetting that it is one. Then
ask what the cheapest real-subject check would be: for the theme menu it was
one program compiled against the frontend assemblies and run on the developer's
own Windows box (`tests/ui/windows/live-theme-switch.sh`), which found the bug
in a minute after two rounds of guessing at it.

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

## H. The instrument changes the answer

Everything above is about a check that lies. This one is about a MEASUREMENT
that lies, and it costs whole days rather than whole months, because the wrong
answer it gives is specific, confident and reproducible-looking.

A heavy probe is not free. If it allocates, writes a file, or simply takes long
enough inside a path the subject is sensitive to, the run you measured is not
the run you were asking about - and the difference it reports is a difference
your instrument made.

> Chasing the Windows-only flycast divergence on 2026-09-21, the arena was
> hashed from inside the fault handler after every fault - a gigabyte and a
> half per fault, a thousand faults a frame. It named a guest heap page that
> diverged at a precise moment, which was exactly the lead the investigation
> wanted. Made cheap, the same comparison said only dead stack differed, twice.
> Three readings were discarded in that session for the same reason: one said
> memory diverged at syscall 1400 when a lighter build said 1654, and one
> "differ" turned out to be two runs of the SAME configuration because a knob
> lived in a file that had been reverted between builds.

**What to do.** Three habits, all cheap:

- **Re-run every finding with the lightest instrument that can still see it.**
  A result that survives only under the heavy probe is the probe's result.
- **Run the instrument against itself.** Two runs of the SAME configuration,
  diffed. If they disagree, the instrument is the subject; if they agree, you
  have earned the right to compare two different configurations. This is B
  pointed at the measurement rather than at the check.
- **Say which build produced which number.** When a knob is added, reverted and
  re-added across a session, a number carries the build it came from and not
  the build you think it came from. Reverting a file silently removes a knob
  that another file still reads, and the two configurations you believe you are
  comparing become one.

The tell is a finding that is *too good*: it lands exactly where the theory
predicted, and it is the only place anything differs. Measure it again the
cheap way before you believe it.

---

## How they group

A, C and G are checks that do not execute. B, D and F are checks that execute
but cannot fail. E is a check on the wrong subject. H is not a check at all -
it is the measurement you reach for when a check has gone red and you are
trying to find out why, and it is the one that wastes days rather than months.

The three cheapest audits, by a wide margin:

1. **G** - diff what CI runs against what the gate script contains. Reading
   only, and it tells you which legs are load-bearing.
2. **B** - break the thing, watch the leg go red, revert. Minutes per leg, and
   it converts a belief into a fact.
3. **H** - run the instrument twice on the same configuration before comparing
   two. One extra run, and it tells you whether the number means anything.

## The rule that covers all eight

**A leg that has never been seen to fail is a leg that has not been tested.**

"Fixed by construction" is not fixed. On 2026-09-19 a paletted-texture fix
shipped on that reasoning, with the commit saying plainly that no disc on hand
used the format so the fix "waits for a game of sprites to prove it". The next
day a game of sprites arrived and the fix was itself the bug - it expanded the
palette a second time, and Street Fighter Zero 3 drew a white gi as magenta.
Two plausible readings of the same code cannot both be right, and nothing but
running it decides which.
