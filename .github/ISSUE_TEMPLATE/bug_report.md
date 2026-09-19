---
name: Bug report
about: Something is wrong - a crash, a wrong picture, an input lost, a movie that does not replay
title: "[<core or area>] <what happens, in one line>"
labels: bug
---

<!-- Fill what applies; the more of it there is, the fewer round trips. -->

**Chimera build:** <!-- the exact string from Help > About or the release name, e.g. Nightly 2026-09-19 (cd4b89cd) -->

**Core and version:** <!-- as the Core Manager shows it, e.g. RPCS3 2026-09-19 (4e678a9). Frontend and core should be from the same day or later: a build from before a core's format changes cannot read what that core writes. -->

**OS and GPU:** <!-- e.g. Windows 11, RTX 4060 Ti -->

**Game:** <!-- the file name(s) exactly as they appear in the project, and which slot each went in -->

**Renderer and non-default settings:** <!-- software / opengl / opengl-hw, and any project setting you changed -->

**Steps, from New Project:**
1.
2.
3.

**Expected:**

**Happened:**

**Attach:**
- [ ] the `.chimeraProject` (it names every file by hash, and carries the inputs)
- [ ] a screenshot
- [ ] for a crash: the three files Chimera writes - `<date> pid<N>.txt` and `.dmp` from the data directory's `Crashes` folder (Config > Data Directory... opens it), and `minibox-diag.log` from next to `Chimera.exe`
- [ ] for RPCS3 "no picture / wrong picture / does not boot": the core's own log (add a firmware entry named `logtrace` to the project; the core then writes its log into Chimera's)

- [ ] I tried the newest nightly
