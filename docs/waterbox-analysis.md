# BizHawk waterbox - architecture analysis (2026-08-11)

Working-level analysis of the waterbox system in the BizHawk checkout at
~/BizHawk (master, 4a16b5c7ee), made in preparation for miniHawk's flavor-(c)
synthetic core. Sources: waterbox/ (guest toolchain + Rust host),
src/BizHawk.Emulation.Cores/Waterbox/ and src/BizHawk.BizInvoke (C# layer).

## What the waterbox actually is

A DETERMINISM sandbox, not a security sandbox. The guest can read and write
host memory freely (each frame the host passes host-side buffer pointers into
the guest, which fills them). What the box guarantees is that the guest is a
pure function of (executable, mounted files, inputs, machine state): no host
addresses, entropy, clocks, or environment reach it, and the ENTIRE guest
machine state is capturable and restorable byte-exactly. It has three layers.

## Layer 1: the guest toolchain (waterbox/)

- A retargeted musl libc ("waterbox" arch): every syscall compiles to a plain
  `call` through a fixed absolute address (0x35f00000080) with SysV argument
  order - no `syscall` instruction, no traps. The trampoline is host-installed
  code; a syscall is just a function call into the host. Deliberately huge
  clobber list (rbx, r12-r15, all vectors): any syscall is a potential
  cothread-switch/state-restore point.
- Guest executables (.wbx) are static, non-PIE ELF64 linked at fixed base
  0x36f00000000 (linkscript.T), -mcmodel=large, -fvisibility=hidden,
  global stack-protector canary (no fs-based TLS, no entropy). Shipped
  zstd-compressed (.wbx.zst).
- Process bring-up is fabricated: _start -> __libc_start_main with
  argv={"waterbox"}, env={WATERBOX=1}, empty auxv; main() never runs. Init
  happens in .init_array (static ctors) and in exported Init functions the
  host calls.
- emulibc provides the discipline primitives:
  - ECL_EXPORT (make a symbol host-visible), ECL_ENTRY (doc marker).
  - alloc_sealed (RO after seal; ROMs/LUTs), alloc_invisible (never
    savestated; framebuffers/scratch), alloc_plain (savestated, never freed);
    ECL_SEALED / ECL_INVISIBLE for statics (.sealed / .invis sections,
    page-aligned in the linkscript because protection is page-granular).
  - __wbxsysinfo: an invisible exported struct the host fills with the layout
    (8 {start,size} ranges: elf, main_thread, alt_thread, sbrk, sealed,
    invis, plain, mmap) - the handshake for the guest bump allocators.
- libco (cothreads) rewritten for the box; co_clean zeroes the host-register
  snapshot before seal so host rsp/rip never enter savestates.
- libc++/libc++abi/libunwind (LLVM 18.1.8) rebuilt with the same flags;
  std::random_device and tz database compiled OUT. C++ exceptions work
  inside the guest (DWARF/libunwind).
- TLS: pthread_self reads [gs:0x18] slot 0 - a host-managed context block,
  not real fs-based TLS. Atomics are mostly non-atomic stubs (single guest
  thread at a time, ever).

## Layer 2: the host (waterbox/waterboxhost, Rust nightly, cdylib)

- Memory: one MemoryBlock per guest instance, spanning the whole layout,
  which must fit a single 4 GiB slice (0x36f_xxxxxxxx). Backing store is a
  shared anonymous file (memfd/CreateFileMapping) mapped twice: at the fixed
  guest address (protection-managed) and at an OS-chosen always-RW MIRROR
  used for all host-side reads/writes that must not trip dirty tracking.
  Multiple hosts sharing a slice are swapped in/out under a per-slice mutex
  (wbx_activate/deactivate = the C# EnterExit monitor).
- Dirty tracking: logically-writable pages are mapped read-only until first
  write; the SIGSEGV handler (Linux) / vectored exception handler (Windows)
  snapshots the pre-write page content, marks dirty, and reprotects RW.
  Stacks on Windows use PAGE_GUARD + lazy VirtualQuery sweep instead (NT
  cannot dispatch exceptions on an unwritable stack).
- Seal (one-shot, after core init): runs guest co_clean + ecl_seal,
  mprotects RO sections (.sealed, .got, .init_array...), clears all dirty
  bits - the live image BECOMES the baseline - and hashes the page-class map.
- Savestate = file-system state + program break + ELF SHA-256 + per-page
  (allocation-status, dirty) arrays + raw 4 KiB contents of each dirty
  non-invisible page (read via the mirror) + green-thread states. Load
  restores baselines for now-clean pages from snapshots and overwrites dirty
  pages from the stream. ELF hash and mounted-file hashes are hard-verified.
  Guest STACKS are invisible (not saved) - states are only taken with the
  guest quiescent (tid 1, between frames). Load errors poison the instance.
- Syscall layer: ~30 syscalls implemented (mmap-anon family, brk, read/
  write/open/close/seek/stat on the VFS, clock_gettime hardcoded to a
  constant, yields, futex, a custom clone). Everything else traps
  (breakpoint) then returns ENOSYS - getrandom, real time, sockets, fork,
  openat etc. simply do not exist.
- VFS: a flat list of named in-memory files (no directories). Read-only
  files are SHA-256-bound to savestates; writable files block savestating
  entirely (transient). stdout/stderr forward to the host console.
- Green threads: purely cooperative, host-side scheduled round-robin at
  yield points; one host thread total. Guest pthreads work (musl clone is
  rerouted to a custom NR_WBX_CLONE=2000).
- Host<->guest transitions: hand-written interop blob at fixed 0x35f00000000
  (nasm -> include_bytes). Host->guest thunks (per-entry 22-byte stubs) swap
  rsp to the guest stack and set [gs:0x18]; guest->host callbacks go through
  64 fixed-address slot thunks (0x35f00000300 + slot*16) so function-pointer
  VALUES stored in guest memory are stable across runs - the slot API exists
  purely for determinism of savestated pointers. Two levels of reentrancy
  (guest->host->guest) via an alt stack pair. On Windows, unwind info is
  registered so SEH can cross the stack switch.

## Layer 3: the C# integration (src/.../Waterbox + BizInvoke)

- WaterboxHostNative: BizInvoke declarations of the ~17 wbx_* C-ABI
  functions (create/destroy/activate/deactivate, get_proc_addr[_raw],
  get_callin_addr, get_callback_addr(slot), seal, mount/unmount_file,
  save/load_state, page introspection).
- WaterboxHost: thin managed wrapper; implements IImportResolver (so
  BizInvoker.GetInvoker<LibXxx> resolves guest exports like a DLL),
  IMonitor (Enter/Exit = activate/deactivate; every touch of guest memory
  is inside using(_exe.EnterExit())), IStatable (streams wbx state),
  ICallbackAdjuster (slot registration).
- CallingConventionAdapters.MakeWaterbox: guest is always SysV; on Windows
  hosts an MsHostSysVGuest adapter generates ABI-translation thunks; on
  Linux it is passthrough. All host callbacks must be registered as slots
  up front. Max 6 int/ptr args, no floats/structs by value, anywhere across
  the boundary.
- WaterboxCore (base class all waterboxed cores extend) + LibWaterboxCore
  (the universal guest ABI):
    ECL_EXPORT void FrameAdvance(MyFrameInfo*);  // extends FrameInfo:
        // VideoBuffer/SoundBuffer (HOST pointers, guest fills), Cycles,
        // Width, Height, Samples, Lagged - plus per-core input fields
    ECL_EXPORT void GetMemoryAreas(MemoryArea[256]); // name/ptr/size/flags,
        // exactly one PRIMARY; ptrs are guest addresses -> memory domains
    ECL_EXPORT void SetInputCallback(cb);
  plus a per-core Init export. Lifecycle: ctor(WaterboxOptions with the five
  heap sizes) -> BizInvoker over guest exports -> mount rom files -> Init ->
  GetMemoryAreas -> Seal -> frames. RTC is frontend-side and frame-derived
  (real time throws under determinism); Nyma cores receive time as a
  FrameInfo field.

## Assessment for miniHawk flavor (c)

1. THE PACKAGE MODEL NEEDS NO FRONTEND CHANGES. Everything waterbox is
   reachable from inside a core package: the adapter DLL can carry the
   WaterboxHost/WaterboxCore equivalents, declare libwaterboxhost as a
   package native, and ship the .wbx as package data. miniHawk's loader,
   contract, and BizInvoke usage require nothing new. Moreover the
   reproducibility pillar argues waterbox machinery SHOULD be package-side:
   the waterbox host version affects emulation determinism, so it belongs
   inside the (movie + package) reproduction contract, not in the frontend.
   (BizHawk treats it as frontend infrastructure; miniHawk's principles
   disagree, conveniently in the direction that costs us nothing.)
2. Deleted machinery inventory: miniHawk removed WaterboxAdapter +
   MsHostSysVGuest from BizInvoke (charter, fifth addendum). On Linux the
   guest ABI is native SysV so nothing is missing; Windows waterboxing would
   need that adapter back - restorable from the transitional fork, or
   carried inside the package's adapter assembly.
3. The Rust host needs NIGHTLY Rust (try_trait_v2 etc.); our toolchain is
   stable 1.97.1. Vendoring it as-is means adopting a nightly toolchain in
   the package build.
4. For the SYNTH waterboxed twin specifically, the full stack is massive
   overkill. Synthcore needs: no threads, no cothreads, no C++, no VFS
   (rom bytes can be passed through an Init export), no mmap beyond fixed
   arenas, and arguably no libc at all (a freestanding build with static
   arenas removes malloc). What defines the flavor per the design doc is
   "the entire machine state is preserved" - i.e. fixed-address loading +
   whole-guest-image savestates. A minimal host proving that essence needs:
   fixed-base static ELF load, W^X dirty-page tracking with sealed baseline,
   dirty-page savestates, and host<->guest call thunking. That is a small,
   auditable component (plausibly plain C or stable Rust), and the same
   guest .wbx discipline (fixed base, sealed/invisible sections) applies.
5. If/when REAL waterboxed cores (mednafen-class) are wanted, the full
   BizHawk machinery (musl + libcxx + syscalls + VFS + green threads) is the
   realistic path - re-implementing that surface is months, not days.
   Vendor-vs-reimplement can be decided per component then.

Open questions for the re-engineering discussion:
- Vendor BizHawk's waterboxhost + toolchain vs build a minimal
  "waterbox-lite" for the synth twin first (and defer the full stack)?
- Package-side vs frontend-side placement of the waterbox host (analysis
  above says package-side; confirms zero-frontend-change).
- Windows support scope for flavor (c) (needs MsHostSysVGuest restoration).
- Whether the synth waterboxed twin should share the .wbx conventions
  (fixed base 0x36f00000000, __wbxsysinfo, sealed/invis sections) so a
  future full host runs it unchanged - recommended: yes.
