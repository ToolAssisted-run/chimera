# Graphics settings for the sixth and seventh generation

What ToolAssisted-run/chimera#102 asks for, what each of the six cores can
honestly give, and what was built. Written 2026-09-19.

## What was asked

DmytroM1998: a project can only use each core's stock graphics configuration.
The standalone emulators let you raise the internal resolution and choose a
nearest-neighbour filter instead of a bilinear one, and the picture is visibly
better for it - mostly this is about video encodes, where a native-resolution
frame stretched to 1080p is a blur and a 3x frame scaled down is not. The
example given was xemu at 1x with "bilinear" against standalone xemu upscaled
with "nearest". Precedent: issue #7 turned PCSX2's deinterlacer from a
hardcoded value into a declared project setting on the same grounds.

The request is reasonable and most of it is not grantable. Which parts are, and
why the rest are not, is the whole of this document.

## The rule these settings have to live under

**A Chimera core's machine is a function of the project, and a movie must
replay.** Three things follow, and they are not the ones a frontend developer
expects.

**Every declared setting is already recorded.** There is one kind of setting
(docs/project.md): the wizard writes every exposed setting into the project at
its effective value, the movie header carries it, and the reproduction contract
covers it. So a graphics setting does not need a new mechanism to travel with a
movie, and a movie made at 3x replays at 3x on anybody's machine whether or not
3x changed anything. The interesting question is therefore NOT "will this
replay" - it always will. It is the next one.

**Changing a setting on an existing project is a structural edit.** The
greenzone is cleared, the core restarts, the frame selector goes back to zero
and the input log is KEPT (docs/project.md, Editing). If the setting was only
ever the picture, those inputs replay to exactly the frames they reached
before, and the only cost is the recomputation. If the setting was part of the
machine, the run may quietly become a different run - the same prompt, a much
worse outcome. Somebody who raises the internal resolution the night before
they publish an encode is doing the first thing and must not be doing the
second.

**The picture has to be able to get out.** The host clamps a core's live frame
to the `video.width` / `video.height` the package declares
(source/engine/source/session.cpp), and several of these cores clamp it again
on their own side. A core whose picture path is built around the machine's
native frame does not get an upscaled one by flipping a renderer flag; the
declaration and the readback both have to grow first.

## The test a setting has to pass

1. **Can the emulated CPU read these pixels?** Find every path from the
   renderer back into emulated memory - render-to-texture writeback, EFB and
   XFB copies, surface downloads, colour-buffer writeback - and ask what the
   setting does to what lands there. "The readback is resized back to native"
   is not the answer: the question is whether the BYTES are the bytes a native
   render would have produced. A supersampled frame decimated or filtered back
   down is not the same picture as one drawn at 1x.
2. **Does the setting reach the frontend at all?** A core that decodes its
   picture out of the machine's own memory has no upscaled picture to hand
   over, whatever its renderer did internally.
3. **Can the gate hold it?** The leg is short and it is the whole claim: run
   the same program at both values and require every memory domain to come out
   byte for byte identical while the picture differs. A setting that cannot
   pass that leg is not a picture setting, and a setting that passes the first
   half but not the second was never applied.

## The six cores

### Flycast - YES, and it is built

Upstream has exactly the two knobs the issue asks for, and upstream has already
done the hard thinking about which pixels a Dreamcast game can see.

`config::RenderResolution` (core/cfg/option.cpp:105, a vertical pixel count
where 480 is native) is applied in `getScaledFramebufferSize`
(core/rend/transform_matrix.cpp:256-303), and that function is the evidence:

- the frame on its way to a SCREEN is upscaled;
- a render-to-texture pass is upscaled **only while
  `config::RenderToTextureBuffer` is off** - the case where the result stays on
  the renderer's side and the SH4 never sees it;
- the moment that pass is copied back into video memory, upstream draws it at
  the machine's own size and compensates only for the scaler registers;
- and `config::EmulateFramebuffer`, which writes every frame back into video
  memory, turns upscaling off altogether.

Flycast forces both of those on by product id for the dozen or so games that
read their own picture (core/emulator.cpp:160, 316, 410, 996 - Silent Scope,
Cosmic Smash, Densha de Go! 2, Beach Spikers and the rest), so those games draw
at 1x however the setting is set, which is the right answer. **No upscaled
pixel can reach `vram[]`.** Picture-only.

`config::TextureFiltering` (0 default, 1 force nearest, 2 force linear) is a
sampler state (core/rend/gles/gldraw.cpp:145-209). It cannot reach video memory
either - with one exception that has to be said out loud: for those same
render-to-texture-to-VRAM games, the pixels it changes are pixels the game then
reads, so on those titles it is part of the machine. The declaration says so.

Neither means anything under the reference rasteriser, which draws the
machine's own 640x480 and samples the way the PVR does. They need `renderer` to
be `opengl` or `opengl-hw`.

Cost: two settings, about forty lines of adapter, and the declared buffer grown
from 640x640 to 1920x1920 so a 3x frame (1920x1440, or 1440x1920 turned for a
vertical cabinet) can get out. That capacity is what the HOST reserves; a
savestate carries only the pages that were written (miniBox
docs/MACHINE-SPEC.md), so a project at 1x costs what it always did.

    { "name": "internalResolution", "display": "Internal Resolution",
      "type": "enum", "options": ["1x", "2x", "3x"], "default": "1x" }
    { "name": "textureFiltering", "display": "Texture Filtering",
      "type": "enum", "options": ["machine", "nearest", "linear"],
      "default": "machine" }

Built, with a gate leg. See "What was built" below.

### PCSX2 - YES, and it is the next one to do

`UpscaleMultiplier` (Config.h:725, default 1.0) is a hardware-renderer setting:
the software rasteriser rasterises straight into `GSLocalMemory` at the PS2's
own resolution and has no notion of a scale factor at all. Under the hardware
renderer the readback is safe, and for the same structural reason as Flycast's:
`GSTextureCache::Read` (GS/Renderers/HW/GSTextureCache.cpp:7340-7463) builds a
source rect scaled by `t->m_scale` and a DESTINATION rect at the caller's
native size, stretches one into the other, and only then calls
`m_mem.WritePixel32/16` into `GSLocalMemory`. The EE and the VUs address
`GSLocalMemory` and nothing else, so what they read back is native-sized - but
note the honest caveat, which is the same one that kills xemu below: those
bytes are a resample of a supersampled image, not the bytes a 1x render would
have produced. The difference is smaller than xemu's (a filtered stretch rather
than corner decimation) but it is not nothing, and the gate leg is what would
settle it per-content rather than in the abstract.

`TextureFiltering` (`BiFiltering`: Nearest / Forced / PS2 / Forced_But_Sprite,
Config.h:314-320, default PS2) is a sampler choice on the hardware path and
reaches `GSLocalMemory` only through the same readback.

Both do nothing under the default `software` renderer, which is a real wart:
the setting most people want is the one the default renderer cannot obey. The
declaration has to say that in its first sentence, exactly as Flycast's does.

The adapter side is two lines - `si.SetFloatValue("EmuCore/GS",
"upscale_multiplier", n)` and `si.SetIntValue("EmuCore/GS", "filter", n)`
beside the `deinterlace` block that issue #7 put there - and the frame already
arrives at whatever size the merged output texture is
(waterbox/gs-device.cpp:745). Proposed shape:

    { "name": "upscale", "display": "Internal Resolution",
      "type": "enum", "options": ["1x", "2x", "3x"], "default": "1x" }
    { "name": "textureFiltering", "display": "Texture Filtering",
      "type": "enum", "options": ["machine", "nearest", "linear", "nearestSprites"],
      "default": "machine" }

**Not built today, and the reason is worth writing down, because it is the one
thing about this that a Dreamcast does not have.** A Dreamcast displays 640x480
and that is that, so a buffer three times it covers every game. A PS2's display
circuits are programmed by the game and run from 256x224 to 1280x1024, and
`MAX_WIDTH`/`MAX_HEIGHT` (waterbox/cinterface.cpp:77) CROP anything larger to
the top-left corner. So:

- a buffer for 3x of the 640x480 nearly every game uses is 1920x1440, and a
  1080i game at 3x (3840x3072) comes out cropped to a quarter of itself - a
  visibly wrong picture, which is worse than no setting;
- a buffer for 3x of the largest mode is 3840x3072, which is 47 MB the host
  reserves for every PS2 project whether or not it upscales;
- even 2x crops a 1280x1024 game.

Three ways out, none of them one line: clamp the multiplier once the display
size is known and say in the log that it was clamped; downscale an oversize
frame in the adapter on the way out; or offer the setting as a TARGET height
("native", "720p", "1080p") and compute the multiplier per display mode, which
is what the target really is and which needs an upstream patch rather than a
config write. The first is probably right and is a decision, not a patch, so it
is recorded here rather than guessed at. The gate leg is the same shape as
Flycast's either way, and PCSX2's runners already take a `settings` file per
working directory.

### PPSSPP - NO, and nothing to expose

The core is `GPUCORE_SOFTWARE` (waterbox/psp-driver.cpp:437) and PPSSPP's
software rasteriser refuses to scale: `SoftGPU::NotifyRenderResized`
(GPU/Software/SoftGpu.cpp:726-736) forces the render parameters to 480x272
whatever `iInternalResolution` says, with a comment saying as much. There is no
upscaled picture to declare a buffer for.

Texture filtering is worse than unavailable - it is available and it is part of
the machine. `GPU/Software/Rasterizer.cpp:123,125,456,459` branch on
`g_Config.iTexFiltering` while sampling, and the software rasteriser draws into
the PSP's own VRAM, which this core exposes as the `VRAM` memory domain and the
gate hashes. Changing the filter changes machine state. That is not a bug in
PPSSPP; it is what a software renderer IS.

Exposing either would mean adopting a hardware backend, which would mean the
GPU bridge, which would mean giving up the one core here whose picture is
deterministic on every machine. Not worth it for a filter.

### Dolphin - NO, not without redesigning the picture path

Dolphin has `iEFBScale` and the whole enhancement family, and none of it can
reach the frontend. Patch 0021 ("the picture comes from the machine, not from
vram") forces `bChimeraXfbFromRamOnly` and makes `GetXFBTexture` decode the XFB
out of the console's own RAM, so the picture Chimera shows is always the VI's
native scanout - at most 720x576, clamped again in
waterbox/dolphin-driver.cpp:159-160 - however large the render target was. The
package's own comment already says what that buys and what it costs:
"Presenting from RAM costs the upscaled picture ... it exists nowhere in a
savestate."

Behind that, Dolphin is the console where the picture IS the machine, more
completely than any other here. `EFB_Read`/`EFB_Write`
(Core/PowerPC/MMU.cpp:139-165) are ordinary PowerPC loads and stores landing on
rendered pixels through `PeekColor`/`PeekDepth`; EFB and XFB copies land in
MEM1 through `memory.GetPointerForRange` (VideoCommon/TextureCacheBase.cpp:2582)
and this core forces them on. And the fallback renderer, Dolphin's own
software backend, hardcodes `EFB_WIDTH`/`EFB_HEIGHT` at 640x528 throughout and
never mentions `iEFBScale`.

Exposing internal resolution here means a second picture path that presents
from the render target instead of from RAM, and that path has to answer the
savestate question the patch was written to close. That is a milestone, not a
setting.

### xemu - NO, and the specific thing that was asked for is not a core setting

This is the core the issue used as its example, and it is the one where the
answer is hardest.

`display.quality.surface_scale` exists and works (hw/xbox/nv2a/pgraph/gl/
surface.c:79-80). The Xbox is UMA, and `surface_download`
(gl/surface.c:768-790) writes rendered surfaces straight into
`d->vram_ptr + surface->vram_addr` - machine RAM - and marks the range dirty
for the scanout and for future texture fetches. A scaled surface is shrunk
first, and `assert(pg->surface_scale_factor == 1 || downscale)` makes sure of
it, so the SIZE that lands in RAM is right. The CONTENT is not: the shrink is
`surface_copy_shrink_row` (gl/surface.c:658-680), which steps by the factor and
copies every Nth pixel. Corner decimation of a supersampled image is not the
image a 1x render produces. A game that reads its own render target - which on
the Xbox is an ordinary technique - reads different bytes.

The core's own gate already says this in as many words: its GPU leg asserts
that the machine state with the GL renderer DIFFERS from the state without
("gpu leg - the GPU left no trace (did it draw at all?)"), because on this
machine the GPU's output feeds machine state by design. A setting that changes
the GPU's output is a setting that changes the machine, and it would fail the
leg this document says every picture setting must pass. It is not offerable as
a picture setting, and offering it as a machine setting means every project
picks its resolution at creation and can never change its mind.

And the second half of the request - "set nearest filter method" - is not a
core setting at all. xemu's `display.filtering` is consumed only in
ui/xui/gl-helpers.cc:892, the blit that puts the finished frame in xemu's own
window. Nothing under hw/xbox/nv2a/ reads it. In Chimera that job belongs to
the frontend's display scaler and to the encoder's `-sws_flags`, neither of
which is the core's business. Half of what the screenshots in #102 show is
therefore a frontend question that this document deliberately does not answer.

### RPCS3 - NO, and it would be expensive to be wrong about

`resolution_scale_percent` (Emu/system_config.h:166) exists, and RPCS3's own
settings dialog disables it whenever strict rendering mode is on
(rpcs3qt/settings_dialog.cpp:588-596) - upstream treats scaling and
readback-exactness as mutually exclusive, and says so in the panel's title.
They are right to. When a flushable section writes back into PS3 memory,
`copy_texture` (GL/GLTextureCache.h:296-378) blits the scaled render target
into a texture at the native transfer size with `linear_interp = true`, and
that resample is what gets memcpy'd into the guest's RAM.

This core ships `writeColorBuffers` ON by default, and the reason is in its own
declaration: Oblivion draws its save thumbnail from the screen, and without the
writeback the icon is black. A core that turned readback on because games read
their picture back cannot then hand the user a switch that changes what those
readbacks contain and call it a picture setting.

`min_scalable_dimension` (default 16) is upstream's guard for the same hazard
at the small end - surfaces at or below it are left at 1:1 precisely so scaling
does not corrupt GPU memory that is not a picture.

Mechanically it is also the most expensive: the declared buffer is 1920x1080
and the driver drops any frame over 4096 on a side
(waterbox/rpcs3-driver.cpp:282, 313), so 2x of 1080p would need both raised,
and a rebuild here is LLVM-from-source territory.

## What was built

Flycast, on this repository's working tree, commit local only:

- `internalResolution` (1x / 2x / 3x) and `textureFiltering` (machine /
  nearest / linear) declared in waterbox/waterbox.config, read in
  waterbox/cinterface.cpp beside the renderer choice, applied to
  `config::RenderResolution` and `config::TextureFiltering` only when one of
  the OpenGL renderers actually came up.
- `video.width` / `video.height` raised from 640x640 to 1920x1920 and the
  guest's frame buffer with them, so a 3x frame reaches the frontend instead of
  being cropped to 640x480 by the clamp that used to be there.
- A gate leg, `picture:onlyThePicture`, which is the claim: the same program at
  1x, 2x and 3x and at both forced filters, with the four memory domains, the
  audio and the lag count required identical across all of them, and the three
  scales required to produce three different pictures at exactly 640x480,
  1280x960 and 1920x1440. A setting that was silently ignored fails the second
  half; a setting that touched the machine fails the first.

What that leg does NOT witness is the filter changing a picture, because no
test program in the Flycast repository draws a texture yet (its docs/PLAN.md
already lists that as open) and an untextured triangle looks the same however
it would have been sampled. The filter's half of the leg is the machine half,
which is the half the promise rests on. It is also sandbox-only: the OpenGL
renderer needs the Mesa built for the guest, so the native reference has the
rasteriser and nothing else.

## What is left

- **PCSX2** is the next core, on the shape above.
- **A picture setting still erases the greenzone.** Every setting is structural
  (docs/project.md), so changing the internal resolution before an encode
  clears the cached states, restarts the core and sends the selector back to
  frame zero. The inputs replay exactly - that is what the gate leg proves -
  but the recomputation is real and the warning the user is shown talks about
  desync, which for these two settings is not true. The mechanism to do better
  already exists on the frontend side (`PutSettingsDirtyBits.ScreenLayoutChanged`
  applies without a reboot) and the declaration would need a way to say which
  kind a setting is. That is a frontend change and deliberately out of scope
  here.
- **A textured test program** for Flycast would give the filter leg its second
  half.
- **The display scaler and the encoder's own filter** are the other half of
  what #102's screenshots show, and they belong to the frontend, not to any
  core.
