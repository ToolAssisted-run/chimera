# Themes

Chimera's colours are data. A theme is one colour for every role the window
paints with, written in a JSON file. Two are built in (Light and Dark), sixty
more ship in the `themes` folder beside the program, and any number more can
be dropped in that folder. This file says how to choose one, how to write one
and how the window applies it.

## Choosing one

Config > Theme opens a window. On the left is every theme on offer: Light and
Dark first, then the rest by name. On the right is a small drawing of Chimera
in the theme picked in the list: menus, fields and buttons, the coloured words
and marks, a list with a chosen row, a piano roll with every kind of row, and
the status bar. Pointing at a part of the drawing names the colour it is
painted with. Under the drawing are the theme's description, its author and
where it came from.

Picking a theme in the list only changes the drawing. **OK** turns it on:
every open window repaints, TAStudio included. The choice is kept with the
rest of the settings and read back before the first window exists. Settings
that never chose a theme start on **Dark**. `--color-scheme light` or `dark`
on the command line picks Light or Dark for one run without changing what is
kept.

The same window has three buttons:

- **Open Themes Folder** - where a theme file goes: the Themes path in
  Config > Paths, which is the `themes` folder beside the program unless you
  choose another.
- **Write a Copy to Edit** - writes the theme picked in the list into that
  folder as a complete file, named "<theme> copy" (or "copy 2", and so on: it
  never writes over a file that is already there). This is the easy way to
  start: every role is already there with its value, so a new theme is a
  matter of changing the ones you want and the name at the top.
- **Reload Themes** - reads the folder again without restarting.

A file that will not load is left out, and the line above the buttons says how
many and, behind "why?", the reason for each.

## Where themes are read from

The Themes path in Config > Paths: by default `themes` beside the program,
where the release ships sixty themes. Every file there whose name ends in
`.chimeraTheme.json` is read. Changing the path reads the themes again at once.
A file whose theme has the name of a built-in one, or of a file read before it
(files are read in the order of their names), is left out.

## The themes Chimera ships

The sixty themes in the repository's `Themes` folder were made by
Nelson/Geferulf and are shipped under Chimera's licence (user-decided,
2026-10-19). The release bundle carries them in `themes` beside the program
(`tools/build-bundle.sh` takes them from the repository, so a theme renamed or
removed since the last build does not ship).

They are taken as their author made them. Some have text with little contrast
against what it is written on, and that was the owner's choice. Someone who
finds a shipped theme hard to read can pick another, or write a copy and
change the colours.

What was changed from the files as they were given: each was named after its
theme in ASCII without spaces, each got `"author": "Nelson/Geferulf"`, line
breaks inside four descriptions were written as JSON escapes, and four
descriptions that quoted song lyrics or a translated poem at length were cut to
a few words (Electric Eye, Plumbers, Skellington, Dim Sum). No colour was
changed.

## The file

A theme is a JSON object. `name` is what the list shows and what the settings
record, so it has to be unique; `dark` says this is a dark theme (a few places
pick a drawing rather than a colour). `description` and `author` are shown
under the drawing. `colors` is a flat map of role name to colour.

A colour is `#RRGGBB`, or `#AARRGGBB` where it is meant to show through what is
behind it - the frame-number wash and the alternate-player stripe in the piano
roll are the two that are, and they are laid over the row colour. A colour can
also be the desktop's own, by name: `system:ControlText`. The built-in Light
theme is written that way.

```json
{
	"name": "Solarized Dark",
	"description": "Ethan Schoonover's palette, the dark one",
	"author": "you",
	"dark": true,
	"colors": {
		"WindowBackground": "#002B36",
		"WindowText": "#839496",
		"InputBackground": "#073642",
		"InputText": "#93A1A1",
		"Selection": "#268BD2",
		"SelectionText": "#FDF6E3",
		"AccentGood": "#859900",
		"AccentWarning": "#B58900",
		"AccentError": "#DC322F"
	}
}
```

A file with no name or no `colors` does not load. A role a theme does not give
is painted in the window's own colour for that place, and a role name the
window does not know is ignored, so a misspelled role shows as the window's
colour rather than the theme's. Starting from "Write a Copy to Edit" avoids
both.

## The roles

The two built-in themes name every role there is
(`source/ui/qt/themes/light.json` and `dark.json`, 107 roles). They fall into
groups:

- **Window chrome** - `WindowBackground`, `WindowText`, `DisabledText`,
  `DisabledBackground`, `MutedText`, `Border`, `LinkText`, `ShadedBackground`,
  `GlyphForeground`, `GlyphShadow`.
- **Fields** - `InputBackground`, `InputText`, `ReadOnlyBackground`,
  `InputAwaitingBackground` (a key-binding box waiting for a press).
- **Buttons** - `ButtonBackground`, `ButtonText`, `ButtonBorder`.
- **Menus and strips** - `MenuBackground`, `MenuText`,
  `MenuSelectedBackground`, `MenuSelectedText`, `MenuBorder`, `MenuSeparator`,
  `ToolStripBackground`, `ToolStripText`, `StatusBarBackground`,
  `StatusBarText`.
- **Selection** - `Selection`, `SelectionText`, `InactiveSelection`,
  `InactiveSelectionText`.
- **Lists and grids** - `GridLines`, `HeaderBackground`, `HeaderText`,
  `AlternateRowBackground`.
- **Meaning** - `AccentGood`, `AccentReady`, `AccentWarning`, `AccentError`,
  `AccentWarningBackground`, and the five that colour the firmware icons:
  `GlyphGood`, `GlyphWarning`, `GlyphWarningInk`, `GlyphError`,
  `GlyphNeutral`.
- **The piano roll's own chrome** - `RollBackground`, `RollText`,
  `RollGridLines`, `RollColumnBackground`, `RollColumnBorder`, `RollSelection`,
  `RollSelectionText`, `RollHintText`, `RollEmphasisColumn`.
- **TAStudio's row states** - `TasCurrentFrame`, the four greenzone shades
  (`TasGreenZone`, `TasGreenZoneInput`, `TasGreenZoneInputStated`,
  `TasGreenZoneInputInvalidated`), the four lag ones, `TasMarker`,
  `TasPermanentMarker`, `TasAnalogEdit`, `TasDefaultRow`, `TasCursorColumn`,
  `TasFrameColumnWash`, `TasAlternatePlayer`, and the five icon colours
  (`TasIconPlayback`, `TasIconRecording`, `TasIconMarker`, `TasIconAnchor`,
  `TasIconLagAnchor`).
- **Lists with a state** - `RowDefault`, `RowInvalid`, `RowActive`,
  `RowPaused`, `RowExcluded`, `RowExcludedActive`: RAM search and watch, the
  Lua console's script list, breakpoints, the disassembler's current line.
- **The hex editor** - `HexBackground`, `HexText`, `HexMenuBar`, `HexFreeze`,
  `HexHighlight`, `HexHighlightFreeze`.
- **The analog-range widget** - `AnalogRangeBackground`, `AnalogRangeField`,
  `AnalogRangeDot`, `AnalogRangeAxis`, `AnalogRangeLimit`.
- **The recording level meter** - `MeterTrough`, `MeterLow`, `MeterMid`,
  `MeterHigh`, `MeterPeak`, `MeterText`.
- **The emulated picture** - `EmulatorViewport`, behind and around the game.
  Black in both built-in themes: anything else shows up as a border round the
  picture.
- **The on-screen display** - `OsdMessage`, `OsdAlert`, `OsdLastInput`,
  `OsdMovieInput`, `OsdStickyInput`, `OsdCurrentAndPreviousInput`,
  `OsdAutoHold`. These are drawn over the game, not over the frontend, so both
  built-in themes give them the same values. They are also settable per-user in
  Config > Messages, and a value chosen there wins over the theme.

## Where a theme does not reach

- **The window's frame and title bar** are the desktop's.
- **A window a Lua script builds**, and anything a script draws, keep the
  colours the script gives them.
- **A movie's subtitle colours** and **the padding colour a core declares**
  for its own picture are theirs, not the theme's.

## How it is applied

A theme becomes each window's palette (`source/ui/qt/theme_palette.cpp`), and
the controls take their colours from it: Qt Quick's controls read a window's
palette, not the program's, so each window is given the theme as its own,
including one opened later and every open one when the theme changes. The few
controls Chimera draws itself are in `source/ui/qt/style` (check boxes, radio
buttons, scroll bars, dialogs). What draws colours of its own, such as the
piano roll's rows, asks for the theme's colour by role.

The file, folder, colour, font and message dialogs Qt draws itself (on Linux;
Windows shows its own) are drawn inside the window that opens them
(`source/ui/qt/dialogs`) and in Fusion's look, the look Chimera's controls
have. Their folder bar and the places beside the list are drawn in the
palette's "light" colour, which a theme gives its field colour
(`InputBackground`), and the places' names and icons take the button text,
which Qt's Fusion dialogs leave out.

`test-theme` (run by `meson test`) holds that a theme reaches a window's
buttons and fields, its menus, a window opened later, the file dialog open in
a window (on Linux Chimera's own chooser, its places in the window's text,
[file-picker.md](file-picker.md)), and a theme chosen while they are open, and
that a window marked unthemed (a Lua form) keeps the colours it set itself.
