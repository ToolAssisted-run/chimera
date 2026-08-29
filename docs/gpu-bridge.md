# The GPU bridge

A waterboxed core draws on the CPU. That is the point: a picture decided
entirely by code Chimera compiles is a picture every machine agrees on, which
is what a movie needs. For most consoles it is also fast enough.

For a PlayStation 2 it is not. PCSX2's own software rasteriser is quick but
skips what the GL renderer does; PCSX2's GL renderer is the one upstream tests,
and running it against a Mesa softpipe compiled into the sandbox costs about
eleven times the software rasteriser. Accurate and unusable.

The bridge is the way out: the core's OpenGL calls leave the sandbox and land
on a real context on the machine Chimera is running on.

## What it is

The renderer stays in the guest, unmodified. It reaches OpenGL through glad,
glad fills its table from the bridge's `GetProcAddress`, and every entry point
it gets back is a generated wrapper that packs its arguments into a struct and
hands the address to the one callback a guest may call out through. The host
half is in the same address space, so it reads those arguments - and the vertex
data and textures they point at - in place. No copying, no marshalling.

Both halves are generated from one list of entry points in miniBox
(`extern/tools/chimera-common-minibox/source/gl/`). An opcode is an index into
that master list, so every core and this engine mean the same thing by the same
number. A guest asks how long the host's list is and declines a host that knows
fewer entry points than it was built against.

One rule decides everything about safety: **no host pointer is ever handed to
the guest.** A mapped buffer is a pointer into the driver, and a sandbox is
right to refuse it. PCSX2 already had two streaming paths that keep their own
CPU buffer, for drivers where mapping is slow; the bridged build takes one of
those (its patch 0015), so the bridge is a plain pass-through.

## Determinism, and what a movie says

A core drawn this way is **not deterministic**. The GPU is outside the sandbox
and outside the savestate, it differs between machines, and drivers differ
between versions of themselves.

Chimera does not hide that:

- `ce_session_deterministic` returns 0 for a session a GPU drew.
- A movie recorded on one writes a `GpuRenderer` header naming the driver.

What is NOT affected is the machine. The emulated console's memory does not
depend on how its picture was rasterised: EE RAM, IOP RAM, the scratchpad, both
vector units, the audio and the lag count are byte-identical across the
software rasteriser, the softpipe and the bridge. Only the picture differs -
which is exactly what a desync would be made of if a game ever read its own
rendered pixels back, and that case is untested.

Measured through the frontend, 900 frames of a PS2 disc, on a machine with no
GPU at all (llvmpipe - so this is CPU against CPU, and a real driver should do
better):

| renderer | time |
|---|---|
| `software` - PCSX2's own rasteriser | 37s |
| `opengl` - Mesa softpipe inside the sandbox | 62s |
| `opengl-hw` - through the bridge | 36s |

EE RAM was byte-identical between the first and the last.

## Turning it on

Two things have to be true, and either may not be:

1. **The project asks.** The core declares a renderer whose value ends in
   `-hw` (PCSX2's is `opengl-hw`, shown in the wizard as "Hardware (OpenGL)"),
   and the project chose it. That suffix is the whole convention: the frontend
   recognises any core's hardware renderer without knowing the core.
2. **This build has a bridge, and a driver gives it a context.** `-Dgl_bridge`
   is on by default and needs EGL on Linux (`libegl-dev`) and `opengl32` on
   Windows.

When either fails the core draws the way it draws without a GPU - the
deterministic way - and the session simply does not claim otherwise. A run that
did get one says so on screen when it starts ("GPU: <driver> - this run is not
deterministic"), which is the only place a person can see WHICH driver drew.
There is a line on standard error either way:

    chimera gl: 4.5 (Core Profile) Mesa 25.2.8 on llvmpipe    <- a context
    chimera gl: no context (...); drawing in software         <- no context

`chimera-run --gpu` makes the same ask from the command line.

## The context is borrowed, never kept

"Current context" is one slot per thread, and the frontend draws its own
picture - the emulated screen, the OSD, everything - through that same slot.
The bridge takes it at the first GL call of a frame and gives back exactly what
was there when the frame ends (`ce_gl_release`, called by the engine after each
frame advance, after Init, and after a savestate load).

Keeping it is not merely rude, it is invisible. Chimera binds its context
through SDL, and `SDL_GL_MakeCurrent` short-circuits on SDL's own cache when it
believes its context is already current - which it does, because
`DisplayManager` deliberately never releases it ("workaround for slow context
switching on intel GPUs"). A raw `wglMakeCurrent` behind SDL's back therefore
makes the frontend go on drawing into the bridge's hidden 64x64 window for the
rest of the session. The symptom is a pitch black screen with working sound and
no OSD, and it is what Windows did the first time this ran against a real
display.

## What is not proven

- **Hardware.** Every measurement so far is on a machine with no GPU, against
  llvmpipe; a real driver should do better, and nobody has checked.
- **Windows.** The host half builds against WGL and has never been run.
- **Readback.** A game that reads rendered pixels into machine state would feed
  GPU output into the savestate, and that is where a desync stops being a
  possibility and becomes a certainty.
