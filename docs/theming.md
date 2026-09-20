# Themes

Chimera's colours are data. A theme is one colour for every role the frontend
paints with, written in a JSON file; two are compiled in (Light and Dark) and
any number more can be dropped in a folder. This file says how to write one and
how the frontend applies it.

## Choosing one

Config > Theme lists what is on offer and turns one on straight away - every
open window repaints, including TAStudio. The choice is saved with the rest of
the config and read back before the first window exists, so a start is never a
flash of the wrong colours.

The same menu has:

- **Open Themes Folder** - where a theme file goes. It is `Themes/` under the
  data directory (Config > Data Directory says where that is; by default
  `~/.local/share/chimera` on Linux and `%LOCALAPPDATA%\Chimera` on Windows).
- **Write a Copy to Edit...** - writes the theme that is on into that folder as
  a complete file. This is the easy way to start: every role is already there
  with its current value, so a new theme is a matter of changing the ones you
  want and the name at the top.
- **Reload Themes** - re-reads the folder without restarting.

A file that will not load is left out and the menu says how many and why. It is
never applied half way: a theme with one colour missing is refused whole, so
there is no such thing as a window that is dark except for one panel.

## The file

A theme is a JSON object. `name` is what the menu shows and what the config
records, so it has to be unique; `dark` tells the frontend this is a dark theme
(a few places pick a drawing rather than a colour, and guessing from the
background gets that wrong for anything in between). `colors` is a flat map of
role name to colour. There is one more, `desktop`, which only the built-in
Light theme sets - see the last section.

A colour is `#RRGGBB`, or `#AARRGGBB` where it is meant to show through what is
behind it - the frame-number wash and the alternate-player stripe in the piano
roll are the two that are, and they are laid over the row colour.

```json
{
	"name": "Solarized Dark",
	"description": "Ethan Schoonover's palette, the dark one",
	"author": "you",
	"dark": true,
	"basedOn": "Dark",
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

`basedOn` is the reason that file is nine colours rather than ninety: a theme
that names another takes every colour it does not give for itself from that
one. Without `basedOn`, every role has to be present - which is what "Write a
Copy to Edit" produces, and what a theme meant to stand on its own should be.

Misspell a role and the error names it and guesses what you meant. Leave one
out without a `basedOn` and the error lists what is missing. Neither loads.

## The roles

The authoritative list is `ThemeColorRole` in
`source/gui/Chimera.Client.Common/theming/ThemeColorRole.cs`, where every role
carries a sentence saying what it paints. They fall into groups:

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

Some of what a window is made of is drawn by the operating system out of the
desktop's own colours, and WinForms gives no way to ask for anything else:

- **Scroll bars.** Every scroll bar stays the desktop's. This is the most
  visible thing about a dark Chimera and there is no fix short of replacing
  every scrolling control.
- **The check inside a check box, and the dot inside a radio button.** The
  caption beside them is themed; the box itself is the OS's. (The tick boxes
  inside a list ARE themed - that list is drawn here.)
- **The window's title bar and border.** The desktop draws those.
- **Progress bars.** Deliberately left alone: setting colours on one under
  visual styles does nothing at all.
- **A list's group headers.** Left to the toolkit, which on Mono does not draw
  them at all, with a theme or without one.

And some colours are not the frontend's to set: a Lua script's canvas and any
window a script builds, a movie's subtitle colours, and the padding colour a
core declares for its own picture.

## How it is applied

WinForms has no theming, so it is explicit, and it lives in two files.

`ThemeEngine` walks a control tree and sets each control's colours from what
kind of control it is, with the surfaces a `BackColor` cannot reach handled by
name: tool strips through a renderer, `DataGridView` through its cell styles,
`LinkLabel` through `LinkColor`, and `ListView` and `ListBox` by drawing them.
A control that paints itself implements `IThemedControl` and is handed the
theme instead of being guessed at.

A list in Details view is drawn here entirely, because three of the things it
paints answer to no property at all: the column headers, the highlight behind a
chosen row, and the strip to the right of the last column. The first two are
owner-drawn - background, tick box, small image, text with its own font,
alignment and per-item colour. The third cannot be painted by anybody, since no
event is raised for it, so the last column is grown to the edge instead and
given its width back when the window is narrowed or the theme goes back to the
desktop's.

`ThemedForm` is what every window in the frontend derives from, and it runs
that walk when the window's handle is created - so a window is themed by
existing rather than by its author remembering to. `ThemingContractTests` fails
the build if a `Form` is added that does not. A window that genuinely must keep
its own colours (a Lua script's) still derives from it and overrides
`ThemingEnabled`.

Three things follow from that and are worth knowing when adding to the
frontend:

- **A colour that means something is declared, not assigned.**
  `control.SetForeRole(ThemeColorRole.AccentError)` instead of
  `control.ForeColor = Color.Firebrick`. The walk would overwrite the second
  one, and the first follows a theme change for free.
- **A control made after the window opened is themed when it arrives** - the
  walk hooks each container it passes. TAStudio's piano rolls are made when a
  project opens and would otherwise be the one white thing on a dark window.
- **A new colour role that carries text belongs in `ThemeContrastTests`,**
  paired with the background it lands on. A theme is ninety numbers somebody
  typed, and two of them being the same shade is not something anyone spots by
  reading the file - it is something they find later, as a row that looks
  blank. Every theme that is not the desktop's is held to 3:1, which is the
  accessibility floor for anything a person has to pick out.

## Looking at it

`./tests/ui/run-ui-tests.sh --shots` renders every window this project can
build, once per theme, into `tests/ui/shots/`. Those pictures are also
inspected as they are taken, every run, with or without `--shots`: under a
theme of its own, no part of a list's header band or of a chosen row may still
be one of the toolkit's own colours, and a chosen row's text has to have
contrast against what it is written on. That check exists because the core
manager once shipped a chosen row that was a beige bar with invisible writing
on it, in a picture this very code had produced, while every colour read off
every control was correct.

## Why Light is not a new palette

Light is the palette Chimera had before there were themes, written down, and it
has to stay that way: nobody asked for the frontend to look different, only for
it to be able to. So the roles that were system colours say so - `"WindowText":
"system:ControlText"` - and resolve to whatever the desktop answers. The one
exception is `system:Control`, which Mono answers with a beige nobody wants and
which every window Chimera derives from `FormBase` has replaced with WhiteSmoke
for years; that substitution is part of what `system:` means here.

`system:` is there for that one job. A theme somebody writes should use hex.

Light also carries `"desktop": true`, and that is the part that actually
guarantees it changes nothing. Under a theme that says so, the walk assigns
nothing by control type: it applies the roles the frontend declared by name,
hands the theme to the controls that paint themselves, and otherwise leaves
every control exactly as the toolkit drew it. This matters because assigning a
colour is not free even when it is the same colour - a `Button` handed a
`BackColor` stops using visual styles, a sunken border redrawn becomes a line,
a menu given a colour table stops going through the system renderer. Same
colours, different drawing. `TheDesktopThemeAssignsNothingItWasNotAskedFor`
holds that, and every screenshot in `tests/ui/shots` was compared pixel by
pixel against the same window built from the commit before this work: they are
identical.

`LightThemeBaselineTests` holds the other half - that the hex values in
`light.json` are still the ones the code used - so that a theme which does
paint (a contributor's light theme, or Dark) paints what Chimera had.

Only the built-in Light theme sets `desktop`. A copy written out by "Write a
Copy to Edit" deliberately does not: it is about to be given colours of its
own, and those have to be painted.
