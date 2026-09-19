# Issue intake and handling - AGREED PLAN (2026-09-19)

Scope: ToolAssisted-run/chimera and the chimera-core-* repos. Written after the
24-issue triage of 2026-09-19. Sergio decided every DECIDE point on
2026-09-19 (see section 10); the DECIDE paragraphs below are kept for the
reasoning. Section 10 is what to execute.

## 1. What the triage taught

- Half the time went to reconstructing facts a template would have carried:
  which nightly, which core commit, which game file, whether the frontend and
  the core were from the same day (#110 was a version mix; #94 and #100 were
  already fixed hours after the reporter's build).
- Crash reports with the note + dump + minibox-diag.log were solvable in
  minutes off the attachments (#109: two of three); the one without a dump
  is stuck.
- Game-specific RPCS3/Flycast reports need either the game or the core's own
  log; asking for both up front removes one round trip.
- Feature requests and product calls (#41 #42 #69 #71 #72 #102) sat unlabelled
  next to crashes. They need a bucket, not a queue position.

## 2. Issue templates (.github/ISSUE_TEMPLATE, chimera repo; core repos link to it)

Three forms. GitHub form templates (YAML) can make fields REQUIRED, so a report
cannot be filed without the build identifiers.

### Bug report (required fields marked *)
- *Chimera build: the exact string from Help > About or the release name
  (`Nightly 2026-09-19 (cd4b89cd)` / `Development build (5c6a32af)`).
- *Core and version: as the Core Manager shows it (`RPCS3 2026-09-19 (4e678a9)`).
  Note: frontend and core must be from the same day or later; a report
  mixing days is answered "retest matched" first.
- *OS and GPU (the crash note carries it; type it anyway).
- *Game: full file name(s) as in the project (redump name; for arcade the
  MAME set name), and which slot each went in.
- *Steps: numbered, from New Project. What was expected, what happened.
- Renderer setting (software / opengl / opengl-hw) and any non-default
  project setting.
- Attach: the .chimeraProject (inputs are small; it names every file by hash),
  a screenshot, and for a crash the three files Chimera writes:
  `<date> pid<N>.txt` (crash note), the `.dmp`, and `minibox-diag.log` - all
  three, every time. For "no picture / wrong picture / no boot" on RPCS3:
  the core log (firmware entry `logtrace`).
- Checkbox: "I tried the newest nightly" (required).

### Core / game support request (new machine, new system, a game that does
not boot) - lighter: build, core, game name, what happens, and for a new
core/system a link to the upstream emulator and whether it has a software
renderer / determinism story.

### Feature request / UI - free text with one required line: "what I cannot
do today". Auto-labelled `feature`.

**DECIDE 2a**: form templates (fields enforced, slightly bureaucratic) vs
markdown templates (a checklist the reporter can ignore). Recommendation:
forms for Bug; markdown for the other two.

**DECIDE 2b**: also require the reporter to name the SHA1 the project shows
for the game? (The project file carries it; asking again is redundant if the
project is attached.) Recommendation: no - require the project instead.

## 3. Labels (one from each group; the triager sets them)

Kind: `bug`, `crash` (guest or process death), `desync/timing` (movie or
frame-rate semantics), `feature`, `core-request`, `question`, `chore`.

Area: `frontend`, `engine`, `tastudio`, `core:<id>` (one label per core:
`core:rpcs3`, `core:flycast`, ...), `ci/release`, `docs`.

State: `needs-info`, `needs-repro` (we have the info but cannot reproduce
yet), `confirmed`, `in-progress`, `fixed-pending-release` (commit named,
not yet in a nightly), `decision` (Sergio's call), `upstream` (the emulator
itself, reported there).

Severity (optional, only when it matters): `blocks-tas` (inputs lost, crash
on save/load, desync), `cosmetic`.

**DECIDE 3a**: do issues about a core live in the core repo or in chimera?
Options: (i) everything in chimera, `core:<id>` label (one inbox, what
reporters do today); (ii) core bugs in the core repo, frontend in chimera
(clean history per repo, two inboxes, reporters get it wrong). Recommendation:
(i), and the fixing commit in the core repo cites `ToolAssisted-run/chimera#N`
as done today.

## 4. Cadence and roles

- Intake pass: once per working day, or on request ("triage the inbox").
  Claude reads every new/updated issue, sets Kind/Area/State, posts one
  comment per issue (see 6), and reports the buckets to Sergio in one
  message. Nothing is pushed or closed without Sergio's word except the two
  mechanical cases in 5 and 7.
- Sergio decides: `decision` items, anything changing movie semantics
  (frame definitions, defaults that alter sync), new cores, priorities.
- Work order default: crash / blocks-tas > desync > bug > chore > feature.
  Sergio can reorder in the triage message.
- A weekly "state of the inbox" line count per State label, in the same
  message, so drift is visible.

**DECIDE 4a**: daily intake by Claude with Sergio's approval per batch (as
today), or Claude also allowed to close A-bucket ("already fixed in <hash>,
in nightly <date>") without asking? Recommendation: allowed for A-bucket
closes and needs-info comments; everything else waits.

## 5. needs-info: time-boxed

- The needs-info comment names exactly what is missing, as a checklist.
- 14 days without an answer: a reminder comment (automatic, GitHub Action
  `actions/stale` on the `needs-info` label).
- 28 days: closed as "not enough information; reopen with the items above
  and it goes straight back into the queue." Reopening is one click and
  loses nothing.
- `needs-repro` is NOT time-boxed: that is on us.

**DECIDE 5a**: 14/28 days, or 7/21? Recommendation: 14/28 - TASers file in
bursts around nightlies.

## 6. What the reporter sees (comment conventions, already in use)

- Fixed: "Fixed in <repo> <hash>; in the next nightly (or: in nightly
  <date> onwards). Please reopen if <date>+ still shows it." Issue closed
  by the commit message (`Closes #N`) or by the comment.
- Already fixed: same line, plus which build the reporter was on and why
  it predates the fix.
- Needs info: checklist of the missing items, and the one sentence of why
  each matters (the reporter who understands why attaches the dump).
- Cannot reproduce: what was tried (build, game, steps, frames), and what
  would settle it.
- Decision: "This is a design choice; noted for Sergio" + label, no promise.
- Upstream: link to the upstream emulator's issue/commit, label, keep open
  only if we carry a patch for it.
- Tone: plain, one paragraph, no apologies, no emoji.

## 7. Duplicates and feature requests

- Duplicate: comment "Same as #N (<one-line reason>)", label `duplicate`,
  close; the surviving issue gets a line "also reported in #M with <extra
  detail>". The older, better-documented issue survives, not the first one.
- Feature requests: labelled `feature`, one triage comment ("what it would
  take, in a sentence"), then they wait for `decision` or a release theme.
  Not time-boxed. Closed only by Sergio ("won't do" with the reason, or done).
- Core requests: `core-request` + the feasibility questions from the form; a
  new core is a milestone, not an issue - once accepted it becomes a repo
  with its own PLAN.md and the issue closes pointing at it.

**DECIDE 7a**: a GitHub Discussion board for feature ideas (keeps the issue
list to defects), or keep everything as issues? Recommendation: keep issues
for now; the volume does not justify a second inbox.

## 8. Mechanics to set up (once decided)

1. `.github/ISSUE_TEMPLATE/bug.yml`, `core-request.md`, `feature.md` +
   `config.yml` (blank issues off, link to Discord for questions).
2. Labels created in chimera (and mirrored in core repos if 3a = ii).
3. `actions/stale` workflow for `needs-info` (14/28).
4. README "Reporting a problem" section: where the three crash files are
   (`%LOCALAPPDATA%\Chimera\crashes\` or wherever crash-notes-wer puts them -
   verify), how to read the build string, "same-day frontend and core".
5. Crash dialog: a button "Open the crash folder" so the three files are one
   click away (small frontend change; worth it given #109).

## 9. Open questions for Sergio (the DECIDE list, gathered)

2a forms vs markdown; 2b require SHA1 or the project; 3a where core issues
live; 4a Claude may close A-bucket alone; 5a needs-info clock; 7a Discussions
or issues for features. Plus: should the intake pass also cover Discord
reports that never became issues (someone would have to file them)?

## 10. Decisions (Sergio, 2026-09-19) and execution

- 2a: markdown templates, NOT enforced forms (a reporter can delete the
  checklist; that is the accepted trade).
- 2b: no SHA1 field; the attached .chimeraProject carries the hashes.
- 3a: ALL bugs are filed in ToolAssisted-run/chimera, `core:<id>` labels;
  core repos' commits cite `ToolAssisted-run/chimera#N`. Core repos get a
  `config.yml` that points new issues at chimera.
- 4a: Claude may, without asking: close already-fixed issues ("Fixed in
  <repo> <hash>, in nightly <date>+"), post needs-info checklists, set
  labels. Everything else (code, closes for other reasons, decisions) waits
  for Sergio.
- 5a: needs-info clock 14 days reminder / 28 days close, via actions/stale;
  reopen-on-comment; `needs-repro` never times out.
- 7a: features stay issues (no Discussions board).

Enforcement: templates, labels and the stale bot are GitHub-side and run
on their own. The intake pass (triage, labels, comments, bucket report to
Sergio) is run by Claude ON REQUEST ("triage the inbox" / "go" on a batch)
- manual for the first two weeks, then a daily scheduled routine if the
comments hold up. Discord reports are turned into issues by a person,
manually, using the template.

Execution order (main session, not this fork):
1. chimera `.github/ISSUE_TEMPLATE/`: `bug_report.md`, `core_request.md`,
   `feature_request.md`, `config.yml` (blank issues off; "Questions ->
   Discord" link). Core repos: `.github/ISSUE_TEMPLATE/config.yml` only,
   blank issues off, one contact link "Report it in chimera" ->
   https://github.com/ToolAssisted-run/chimera/issues/new/choose.
2. Labels in chimera (gh label create): kind, area (incl. one `core:<id>`
   per roster entry), state, severity, as in section 3, plus `duplicate`.
   Apply them to the 17 open issues per the 2026-09-19 triage.
3. `.github/workflows/stale.yml` on `needs-info`: 14/28, exempt everything
   else, reopen on comment (stale removes the label on activity).
4. README "Reporting a problem" section: build string, same-day
   frontend+core rule, where the three crash files are (verify the folder
   crash-notes-wer writes to before writing it down), the logtrace trick
   for RPCS3.
5. Later, with #111: crash dialog "Open the crash folder" button.
