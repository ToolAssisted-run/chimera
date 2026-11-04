# macOS (x86-64)

This is the state of the macOS port: what is built, what has been tested, and
what has not. The port is for the x86-64 processor. On an Apple silicon Mac the
x86-64 build runs under Rosetta 2. A native Apple silicon build is a separate
piece of work and is not done here.

## Which macOS

The build asks for macOS 14.4 or later, which is what Qt 6.12 asks (the cross
file's `-mmacosx-version-min`). Without it the oldest version is the one of the
Mac that builds, and a build made on macOS 27 starts on no other; an Intel Mac
stops at macOS 26. Apple's C++ library has the floating-point `from_chars` and
`to_chars` only from macOS 26, so the code that reads and writes numbers as
text has a macOS path of its own (strtod_l and snprintf_l in the "C" locale:
`plain_numbers.hpp` in the engine, `charconv_apple.hpp` in the view models). The
latter was compared with `std::to_chars` over 801,056 values, floats and
doubles, plain and scientific, with no difference.

## What builds and runs

The engine, `chimera-run`, `chimera-mcp` and every native library the engine
loads (Lua, SQLite, zstd, SDL2, OpenAL Soft, miniBox) build with the same
meson build as Linux, given a cross file that names the processor:

```sh
git submodule update --init --recursive
meson setup build/meson-macos-x86_64 --prefix "$PWD/build/x86_64-macos" --libdir dll \
  --cross-file extern/meson/macos-x86_64.ini --cross-file <qt file> -Dqt_ui=enabled
meson compile -C build/meson-macos-x86_64
meson install -C build/meson-macos-x86_64
meson test -C build/meson-macos-x86_64
```

Requirements: Xcode's command line tools, and Homebrew's meson, ninja and
cmake. The cross file works on an Intel Mac as well. On either kind of Mac the
tests run, because the result can be run (on Apple silicon through Rosetta 2).

## The window (Qt)

The window needs Qt 6.12 with an x86-64 part. Qt's own macOS download is one
build for both processors (Homebrew's is the machine's own, arm64 on an Apple
silicon Mac, and cannot be linked into an x86-64 program). Get it the way
tools/ci-chimera.sh does for Linux:

```sh
python3 -m venv <folder>/aqt-venv && <folder>/aqt-venv/bin/pip install aqtinstall==3.3.0
<folder>/aqt-venv/bin/aqt install-qt mac desktop 6.12.0 clang_64 -m qtshadertools -O <folder>
```

and point meson at it with a second cross file (kept outside git, since the
folder is yours), so that its pkg-config finds that Qt and Homebrew's is
never seen:

```ini
[binaries]
qmake6 = '<folder>/6.12.0/macos/bin/qmake'
[properties]
pkg_config_libdir = '<folder>/6.12.0/macos/lib/pkgconfig'
```

Run it with `build/x86_64-macos/dll/chimera --project <project> --core <package>`.
Encoding a video needs the ffmpeg beside the program:
`tools/fetch-ffmpeg.sh macos build/x86_64-macos/dll` (docs/encode-video.md).
The window finds Qt's frameworks where they were installed (an rpath). A bundle
that carries its own (macdeployqt) is not made yet.

The menus are in the menu bar at the top of the screen, as on every Mac
program, and the windows have no bar of their own (a window's menu bar shows
while that window is the one in front: the main window's or a tool's). Qt does
this for a `MenuBar` that is the window's own menu bar, so the main window and
TAStudio, whose bar shared a row with a button, use the bar itself on a Mac. The
"Show TAStudio" button is at the right of the main window's status bar, and
TAStudio's "Hide" at the right of its status strip. The system draws these
menus: an item's hotkey is not written beside it (a key given to an item as its
shortcut is), the System and Emulator menus are greyed rather than left out
while no game runs, and a tool window's menu bar colour setting has no effect.

Two things are different on a Mac and the tests know it: Qt's Control key is
the Command key (Chimera's Ctrl is Qt's Meta there), and a Mac's menus have
no Alt letters. The file chooser of docs/file-picker.md is Qt's own dialog on a
Mac, so the test of the Linux chooser is not built.

## The app

`tools/build-app-macos.sh` makes `Chimera.app` from an installed build: the
window, Chimera's libraries, Qt's frameworks, plugins and QML (put in by Qt's
`macdeployqt`, then cut to the x86-64 half, since Qt's download is one build
for both processors), the helper programs and ffmpeg, signed ad hoc.

```sh
tools/fetch-ffmpeg.sh macos build/x86_64-macos/dll
tools/build-app-macos.sh --dll build/x86_64-macos/dll --qt <qt folder>/6.12.0/macos --out build/Chimera.app
```

The icon is `docs/Chimera.icon`, made in Icon Composer (Xcode 26 or later): the
mark in two layers, `icon.svg` for light and `icon-dark.svg` for dark, whose
opacity is set per appearance, so each shows in its own. The script compiles it
with `actool` into `Assets.car` (light, dark and tinted, which the system makes
from the light one) and an `.icns` for the rest, and names it in `Info.plist`.
`--icon` names another. Without an Xcode that can compile it, the app gets a plain
icon drawn from its `icon.svg`, with no dark one, and says so. Icon Composer
cannot be run from a terminal, so a change to the icon is made in the app and
saved in `docs/Chimera.icon`. The two SVGs are kept once, inside it, in
`Chimera.icon/Assets`, and the README shows them from there. They are not links
to or from `docs/`: `actool` refuses an icon whose assets are links, and GitHub
serves a link's target path instead of the picture, which broke the README's image.

What a person keeps is in `~/Library/Application Support/Chimera`, as other
Mac programs keep theirs (owner, 2026-11-03), and not beside the program as on
Linux and Windows: `Cores/`, `Firmware/`, `settings.json`, and the folders of
Config > Paths (`Movies/` and the rest, which are relative to it). That is the
folder that already held the projects and the recovery files. It is the same
for the window run from a build folder, so a core put in `Cores/` there is
found by both. The app carries no cores: put a core's package in
`~/Library/Application Support/Chimera/Cores`, or give the Core Manager another
folder. What ships in the app stays in it: `Contents/MacOS/bin` (the helper
programs and ffmpeg), the libraries, and `Themes/`, whose themes are shown
until the person's own Themes folder has one.

Sound and gamepads need no setup: OpenAL Soft and SDL2 are in
`Contents/Frameworks` and found there.

It is ad hoc signed, not notarised, so another Mac asks before opening it
(right-click, Open). Chimera writes nothing into the app, so its signature
keeps verifying (`codesign --verify --deep --strict` passes after a run).

## miniBox on macOS

miniBox is the sandbox every core runs in. Two things are different on macOS.

**Memory.** Linux makes a block of the guest's memory with a memory file mapped
twice (the guest's view, whose protection miniBox manages, and the host's view,
which is always writable). macOS has no such file, so the block is one mapping
that nobody touches, and each view is a `mach_vm_remap` of it. Both views see
every write, and each has a protection of its own.

**The `gs` register.** A guest finds its context through `gs:0x18`. On Linux
nothing else uses `gs`, so miniBox points it at its own block for good. On
macOS the operating system uses `gs` for the thread's own data (`gs:0` is the
`pthread_t`, `gs:0x18` is the Mach thread port), and a program cannot read or
write the `gs` base with `rdgsbase` and `wrgsbase` (they are illegal
instructions, Rosetta 2 included). So the `gs` base is swapped every time
control crosses between the guest and the host: the guest's block while guest
code runs, the operating system's whenever host code runs. The swap is made
with the system call that libsystem itself makes to set a thread's data
pointer. It happens when a guest is entered and left, when the guest makes a
system call, when it calls back into the host, and in every signal handler.

Measured under Rosetta 2 (macOS 27, an M1): the swap works and restores
exactly, and a swap in and out costs about 530 ns. A real Intel Mac has not
been tried.

A guest that uses `%fs` for its thread-local storage (a Rust guest does)
cannot run on macOS, because `%fs` cannot be set from a program either. The C
and C++ guests miniBox's own toolchain makes use `gs` and are not affected.

A write to a protected page raises `SIGBUS` on macOS, where Linux raises
`SIGSEGV`. miniBox handles both the same way.

## The GPU bridge

OpenGL is Apple's, through CGL, a context with no window. It stops at OpenGL
4.1, so the entry points of 4.2 to 4.6 are refused, as the bridge refuses any
entry point a driver does not have. Mesa and Vulkan are not part of this port.

## What has been tested

On an Apple M1, macOS 27, the x86-64 build under Rosetta 2:

- miniBox's unit tests: all 13 pass.
- The engine's tests: all 33 pass.
- The window's tests that do not need the synthetic test core: pass (47 of 60
  in all). The other 13 need `synth-box` and its test game, which are built
  with miniBox's Linux guest toolchain, so they were not run.
- Cores, published x86-64 packages with a game of the user's own: quickerNES
  (Super Mario Bros.), Mesen (the same), Snes9x (Plok) and Genesis Plus GX
  (Sonic, Master System) each ran 200 to 300 frames with a save and a load of
  the whole machine around every frame (`--rerecord`). Two runs of quickerNES
  gave the same memory byte for byte, and so did a run with the round trip.
- The window plays Super Mario Bros. through quickerNES at 61.5 frames a
  second (the machine's own rate is 60.1), drawn by Qt's software renderer
  (offscreen).

Not tried: a real Intel Mac, the window on the screen (Cocoa) and with a GPU
renderer, a core that uses threads heavily or a GPU, the Mesa path, and a
bundle for others to run. A guest with `%fs`-direct thread storage cannot run.
