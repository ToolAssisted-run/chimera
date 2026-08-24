# The C++ engine migration

2026-08-24. Decision: everything functional moves to C++ ("the engine"); C# remains
only for the WinForms GUI. Mono then carries windows and dialogs, not emulation.

## Why

- **Testing.** The witness gate today needs Mono + WinForms + Xvfb to test *emulation
  determinism*. An engine is testable as a binary: no display, no runtime, fast CI.
- **Linkability.** Solvers and bots (jaffarPlus) can link the engine as a library
  instead of puppeteering a GUI process.
- **Performance where it counts.** Not realtime play (cores are already native) but
  the multiplied paths: unthrottled runs, savestate churn, scripting. Lua moves from
  reflection-based NLua to the real thing.
- **One toolchain.** Engine, miniBox, and cores all build from the same meson tree.

## The boundary

The engine owns exactly **what a headless movie run touches**: waterbox host
management, core/package loading, firmware resolution, bundles, rom identification,
input log, movie record/playback/rerecord, savestates, memory domains, AV dumping,
Lua. The GUI keeps windows, rendering, input devices, and config presentation.

Rules, in the same spirit as the waterbox boundary:

1. **The ABI is a frozen spec.** Flat C ABI in `engine/include/chimera/engine.h`,
   prefix `ce_`, versioned by `CE_ABI_VERSION` / `ce_abi_version()`. Additive changes
   only; anything else bumps the version and the C# side checks it at load.
2. **Bulk data crosses as buffers.** Never a call per byte/cell/frame. Borrowed
   pointers are documented with their invalidation rule at the declaration.
3. **The engine owns movie-correctness state.** The GUI cannot desync what it cannot
   reach.
4. **Formats do not change.** Movies, bundles, packages, configs written before and
   after a component moves are byte-compatible. Where the old code had a quirk, the
   engine reproduces the quirk and a comment names it.

## The method (per component)

Move one component at a time, smallest verifiable seam first. For each: implement in
`engine/` with meson-run C++ tests carrying byte-exact fixtures; wire the C# call
sites through the ABI (BizInvoke, same pattern as libminiboxhost); keep a C# parity
test comparing engine behaviour against the legacy behaviour where the witness gate
does not reach; witness gate + full test suite green before the next component starts.
The C# implementation is deleted in the same change that wires its replacement - no
long-lived dual paths.

## Order

1. **Movie input log** (pilot: proves build, ABI, dlopen, parity mechanics) - DONE
2. **Movie text lumps** (Header.txt, Comments.txt, subtitle lines) - DONE
   (the zip container itself is shared with savestates and moves with them)
3. **The zip-of-lumps container** (ZipStateSaver/Loader; miniz vendored in
   extern/miniz, zstd loaded from beside the engine) - DONE. The engine owns
   the format; C# keeps the temp-file/backup plumbing and hands buffers
   across. What a savestate CONTAINS (SavestateFile's lump set, the greenzone
   Zwinder ring) is session policy and moves with step 5.
4. **Rom identification + bundles + firmware** - DONE. The engine owns SHA1
   identity hashing (`ce_sha1_hex`), the .gameBundle format/naming rules/
   ContentId (cJSON vendored in extern/cjson; strict JSON now - no comments or
   trailing commas; the path rule is pure string logic so both platforms agree),
   the firmware verdict, and the canonical "<id>=<sha1>" movie line. Config,
   registry and the filesystem stay C#; GameBundle remains the UI's model
   object and delegates format concerns.
5. Core/package loading + session (the frame loop; the witness gate moves onto the
   engine binary here, dropping Mono/Xvfb from Level B)
   - 5a **package container** - DONE. `ce_package_*`: what makes a path a
     package (zip or dev directory), its identity hash, entry access; the
     engine's first (read-only) filesystem capability, which the native
     session needs anyway. A corrupt zip stays listable with its error; an
     ordinary zip stays quietly invisible. waterbox.config's MEANING still
     parses in C# and moves with 5b.
   - 5b **native session, first half** - DONE. `ce_session_*`: package +
     waterbox.config (parsed engine-side) + rom + effective settings +
     firmware in; frame advance (buttons/axes/video/audio/lag), whole-machine
     save/load state, memory domains out. libminiboxhost is loaded from
     beside the engine; on Windows guest calls bridge through the host's
     departN trampolines (host_dyn.cpp, the WaterboxAbiShim transliterated).
     `chimera-run` replays movies headlessly, and the witness gate's new
     Level E proves it byte-identical to the Level A goldens - no Mono, no
     Xvfb, no frontend.
   - 5b **second half: WaterboxCore rewired onto ce_session** - DONE. The C#
     adapter is now a thin frontend shell: the machine (host, mounts, Init,
     frame loop, savestates, domains, live settings) is the engine's session,
     the SAME one chimera-run drives - LibMiniBoxHost.cs and WaterboxAbiShim.cs
     are deleted. The optional tooling/persistent-data groups still reach the
     guest through the session's TRANSITIONAL guest-proc bridge
     (ce_session_guest_proc) until they migrate as their own components.
   - 5b **tooling + persistent data into the session** - DONE. The four
     tooling groups (surfaces, registers, buses, trace) and the persistent-
     data channel are probed and driven by the session
     (`ce_session_surface_*`/`_register_*`/`_bus_*`/`_trace_*`/`_persist_*`);
     the transitional guest-proc bridge is gone. The session owns the trace
     flag and re-asserts it inside load_state (movie-correctness state
     belongs to the engine). Known gap: the synth core exports none of the
     optional groups, so the gate witnesses only the groups-absent path -
     the groups-present path rides the 1:1 transliteration and real cores.
   - remaining in 5: movie playback/record and the greenzone into the session
6. Memory domains + tooling services
7. Lua (real Lua replaces NLua; script API preserved)
8. AV dumping

The GUI is untouched throughout; at the end it is a WinForms shell over `ce_` calls,
and porting it (or not) is a separate, optional decision.
