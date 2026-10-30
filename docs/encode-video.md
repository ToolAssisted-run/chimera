# Encoding a video

A video is an encode of a run. In Chimera it is one act, in one window, and it
is over when the window closes.

## What it replaced

The BizHawk lineage made a video out of three commands that had nothing to do
with each other, in an order nobody told you:

1. `File > A/V Writer > Config and Record A/V...` - pick a writer, pick a codec,
   pick a file, and the recording starts *now*, wherever the emulator happens to
   be sitting.
2. Play the movie. However you like. At whatever speed. If you forgot, you
   recorded nothing; if you started late, you recorded part.
3. `File > A/V Writer > Stop A/V Writer`. If you forgot this one, the file was
   never closed and some of it was never written.

Every one of those steps could be done wrongly, and the result of doing them
wrongly was a file that looked plausible and was not what you asked for.

## What it is now

`File > Encode Video...` opens a window that asks for everything at once:

- **the file** to write
- **the ffmpeg command** that fills it, written by hand or put there by a
  **preset**; the container it names (`-f`) decides the file's extension
- **the two markers** to encode between, inclusive at both ends
- **the picture**: an output size (or the size the core draws), whether to pad
  or scale to it, whether to sync to audio, and whether the OSD and the Lua
  layer are in shot - the same settings the A/V writer has always read out of
  the config, written to the same place

Start Encode winds the run to the first marker, reproduces it as fast as the
machine will go, writes each frame out, closes the file, and puts the emulator
back on the frame you were on. A video already at that name is not replaced
without asking: the window opens on the same suggested name every time, and
trying another size over the run you just made used to overwrite it with no
word said. The window shows how far along it is, how many
frames a second it is managing and roughly how much longer it will take. Stop
ends it early and leaves a playable file of what got written. Closing the window
stops it too: nothing keeps running behind the dialog.

## Presets

The Preset box above the command offers ready-made commands by name (issue
323): the default (H.264 and AAC in an MP4), four for YouTube at 2160 lines -
landscape or portrait, scaled smoothly (lanczos, for pictures drawn as
photographs) or sharply (nearest neighbour, for pixel art), at a quality
YouTube keeps - and four in full colour in Matroska with FLAC sound: H.264
in YUV 4:4:4, which changes some colours by a level, and three that keep
every pixel and every sample (H.264 in RGB, H.265 in RGB and UT Video).
Picking one writes its command into the command box and nothing else; the
command box is still what the encode uses, and an edit to it makes the
Preset box say "Your own command". A command typed that is a preset's shows
that preset.

Every file is written in the shape the machine shows its picture in, not the
shape of its pixels: a PC-98's 640x400 picture, shown 4:3, is written as
640x400 with pixels 5:6 wide to high, so it plays at 4:3 (issue 375). The
YouTube presets scale to that shape (`dar` in ffmpeg's scale filter) with
square pixels: that picture comes out 2880x2160. A size asked for in the
window is written with square pixels.

With Capture OSD ticked the video is the picture as the window shows it, at
the size of the window's picture area, with the counters, the messages and
what scripts draw, as the old window wrote it (owner, 2026-10-30): a 320x200
picture shown 320x240 in a window at 4x is written 1280x960, in square
pixels. Capture Lua alone writes the machine's own pixels with what scripts
draw over them. The window's "now" beside Current size says the size that
will be written, and Current size puts it in the size boxes.

The default MP4 stores colour at half the width and height of the picture
(YUV 4:2:0), as most players need, so a dither of two colours one pixel
apart becomes their average. The 4:4:4 and lossless presets keep it; the
default stays 4:2:0 because it plays everywhere (owner, 2026-10-28).

## Markers, not frame numbers

The stretch is named by markers because a run already knows where it starts,
where its input stops and where it ends - Chimera gives every movie those three
permanently, and they follow every edit. The window opens on **Run start** to
**Run end**, which is the whole run, which is most encodes. **Last input** is
there for the common case of not wanting to publish a minute of an idle
character after the final press.

## Read only, always

An encode reproduces a movie; it does not author one. While one is running the
movie plays read only and no
greenzone state is kept and no lag is written down for the frames that go by.
A run of a hundred thousand frames costs no memory and leaves the project
exactly as it was found - looking at something should not change it.

Where the emulator was when the encode borrowed it is kept as a state file
in the project's cache folder (`encode-restore-<id>.state`), written and read
by the engine the way a branch's state is, and deleted when the emulator is
handed back. It used to be kept in memory, and a machine whose state is
bigger than 2 GB (a PlayStation 3's often is) could not start an encode at
all (chimera issue 282).

The frames that go into the file are the frames the emulator produced, not a
second rendering of them: the job does not emulate anything itself. It only
makes the main run loop run a frame every iteration, unthrottled, and decides
when to stop. `AvFrameAdvance` writes them out exactly as it always has.

## ffmpeg

ffmpeg ships in the bundle, in `dll/`, put there at build time by
`tools/fetch-ffmpeg.sh`. Chimera used to name a version, a URL and a checksum
and ask the person to go and fetch it the first time they wanted a video; there
is nothing to ask any more.

It is the same static 4.4.1 build the frontend used to fetch on its own, pinned
and checked twice: the archive by its own SHA256, and the binary inside it by
the SHA256 the old code demanded before it would run one. The archive is a .7z,
so the script bootstraps a static 7-Zip out of a .tar.xz when the machine has
none - a build machine needs nothing installed for any of this. Downloads are
cached in `build/deps`, so it costs nothing after the first build.

It is a separate GPL v3 program run over a pipe, and `dll/ffmpeg-LICENSE.txt`
carries its licence, both hashes and the release it was built from.

## From a script

The same job, without the window:

```lua
local err = client.encodevideo("/tmp/run.mkv", 0, 1799, "-c:v ffv1 -c:a pcm_s16le -f matroska")
if err ~= nil then error(err) end
while client.encodingvideo() do emu.frameadvance() end
```

That is what `tests/synth/run-witness.sh` uses to prove the whole path on every
run: it encodes a stretch of a real project with the synthetic core and checks
that the file holds exactly the frames that were asked for, both ends included,
and that the emulator was handed back on the frame it was borrowed from. It
counts them with the ffmpeg the build ships, not a system ffprobe: a gate must
not depend on a tool the machine happens to have, and a CI runner has none.

The command line's `--dump-type` / `--dump-name` / `--dump-length` still exist
and still work; that is a different thing, for a build machine dumping a run
nobody is watching.
