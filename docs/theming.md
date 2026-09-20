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

A config being created now starts on **Dark**. A config that already existed
and had never chosen a theme was written before there were any, so it keeps
**Light** - changing somebody's colours under them on an update is a surprise,
not a feature. Either way the answer is written into the config the first time
it matters, so the question is asked once.

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
- **The window's border.** The desktop draws it. The title bar is no longer on
  this list: see below.
- **Progress bars.** Deliberately left alone: setting colours on one under
  visual styles does nothing at all.
- **A list's group headers.** Left to the toolkit, which on Mono does not draw
  them at all, with a theme or without one.

And some colours are not the frontend's to set: a Lua script's canvas and any
window a script builds, a movie's subtitle colours, and the padding colour a
core declares for its own picture.

## The title bar

The title bar belongs to the desktop compositor, not to WinForms - but on
Windows the compositor will draw it dark if the window asks, through one call
to `DwmSetWindowAttribute` with `DWMWA_USE_IMMERSIVE_DARK_MODE`. There is
nothing to draw and nothing to theme: the window says what it is, and the
buttons, their hover states and the inactive shade all follow.

`WindowFrame` makes that call and `ThemedForm` makes it on handle creation and
again on a theme change, so every window is covered by existing. It is
deliberately not made from `ApplyTheme`, which is the hook a window overrides:
a window that overrode it and forgot the base call would be left with a light
frame on a dark window. A theme that follows the desktop asks for nothing,
since those ARE the desktop's colours.

Off Windows it does nothing: there is no dwmapi, and a Unix window manager
owns the frame and has its own opinion about it. A missing dll, an attribute
an older build does not know, and a window whose handle has gone are all
silent.

## The menus

Every image beside a menu item comes from `MenuIcons`, which is one place so
that the gaps are visible as gaps. `MenuIconTests` walks it by reflection and
measures each icon against every theme's menu colour, because an icon has to
read on a white menu and on a near-black one and nobody will re-check that by
eye.

Three kinds of item are bare on purpose:

- **Anything that can be ticked.** A ticked menu item draws its tick in the
  image margin, so an image there fights the tick. Every Display-this toggle,
  every throttle mode, every window scale.
- **Labels that are not commands** - "Loaded core: ...", the movie status line.
- **Exit**, alone after a separator, which no icon says anything useful about.

And one rule with a test behind it, `MenuContractTests`: a menu that fills
itself in `DropDownOpened` must be seeded with at least one item in the
designer. A `ToolStripMenuItem` whose `DropDownItems` is empty does not open a
dropdown at all, so such a menu never opens and its handler never runs. That
is not a hypothetical - it is how the Theme menu shipped, dead, with every
test green.

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

## What the tests cover, and what they do not

Worth reading before trusting a green run, because this feature has now twice
been reported broken by its author while every test passed.

`./tests/ui/run-ui-tests.sh` runs on **Linux, on Mono, under Xvfb**. Chimera's
users run **.NET Framework WinForms on Windows**. Those are two different
implementations of the toolkit, and the difference is not cosmetic: what an
assignment to `BackColor` does, whether a control repaints when it is made, and
whether visual styles override either, are all decided by the toolkit. So:

- **Covered by the suite**: the theme files and their parsing; the roles and
  their contrast; which colour every control ends up holding; and, under Mono,
  what a window looks like as pixels - per theme, round-tripped, and for every
  real window the shot harness can build.
- **Not covered by the suite**: anything that is Windows'. The title bar, which
  is a DWM call that does not exist on Linux - `TitleBarContractTests` checks
  only that every window *asked*, via `WindowFrame.LastAskedFor`. Whether the
  Windows toolkit honours what the walk sets. Whether a window that is already
  open actually repaints when a person picks a theme from the menu.

That last one is where the reported bugs lived, so it has a harness of its own:
`tests/ui/windows/live-theme-switch.sh`. It compiles a small program against
the frontend assemblies this tree has just built and asks, on real Windows, in
one process, the three questions Mono cannot be asked:

- does a window that is already open change when the theme does, and does it
  land exactly where a window born in that theme is;
- does painting a theme onto a window raise EVENTS inside it - a ListView whose
  handle is remade raises a tick for every item it puts back;
- does a real frontend window (Pre-Compiled Modules, the one that crashed) open
  on a dark theme without throwing.

It is not part of the gate, because the gate runs on Linux; run it by hand from
WSL after `dotnet build source/gui/Chimera.sln`, and run it whenever the walk
changes.

It photographs with `PrintWindow`, which renders the window into a bitmap of
the program's own. That is deliberate and worth keeping: it reads no screen
pixels, so it cannot capture anything else on the developer's desktop even by
accident, and it works with the session locked.

### What this suite keeps failing to notice

Five bugs reached the author of this program through a green run of it while
this branch was being written. They are not five unrelated mistakes; they fall
into three kinds, and the kinds are worth knowing before adding to any of this.

**1. States of a control the frontend draws itself.** A list the theme has
taken over is drawn ENTIRELY here. There is no toolkit underneath, so a state
nobody wrote code for is not drawn in the toolkit's colours - it is not drawn
at all. It has now happened three times: the CHOSEN row came out a beige bar
with invisible writing on it; the row under the POINTER came out a bar with no
writing at all; and the check box, the small image and the disabled list were
each written only once someone went looking. A test that renders a control in
one state tells you nothing about the others, and "it looked right in the
screenshot" means the screenshot was of the resting state.
`ListRowStateTests` now enumerates the states and asserts every one of them -
and the enumeration, not any single assertion, is the thing to keep current.

**2. Events raised as a side effect of painting.** Theming writes properties,
and writing a property can make the toolkit remake a control's handle, and
remaking a handle makes the toolkit replay the control's contents - ticking
every ticked row on the way past. The window on the other end has no way to
know those events are not real. This crashed Pre-Compiled Modules on being
opened. Painting is now done with those events held off (`ThemeEngine.Quietly`)
and the walk no longer brings a control to life just to read its scroll
position, but the general rule is the one to remember: **a coat of paint must
not be able to tell a window that something happened in it.**

**3. Anything that is the Windows toolkit's.** Covered above, and it is the
reason the harness exists. Worth saying once more because it interacts with the
other two: the states a control can be in and the events a handle recreation
raises are both decided by the toolkit, and the toolkit under the tests is not
the toolkit under the users.

One honest note on the second kind: the crash was diagnosed from the stack
trace the author sent and fixed at both ends - the walk no longer causes the
events, and the window no longer answers one by walking a collection that may
be mid-rebuild, which is what its own comments always said it should do. The
exact timing was not reproduced in a harness. The Windows harness opens that
window on a dark theme and would catch a plain regression; it is not proof that
the original race cannot recur.

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
hands the theme to the controls that paint themselves, and otherwise puts every
control back the way the toolkit had it. This matters because assigning a
colour is not free even when it is the same colour - a `Button` handed a
`BackColor` stops using visual styles, a sunken border redrawn becomes a line,
a menu given a colour table stops going through the system renderer. Same
colours, different drawing. `TheDesktopThemeAssignsNothingItWasNotAskedFor`
holds that, and every screenshot in `tests/ui/shots` was compared pixel by
pixel against the same window built from the commit before this work: they are
identical.

"Puts back" is doing real work in that paragraph, and it was missing at first.
The first time a control is walked - whichever theme that is - everything the
walk is about to change is recorded: the two colours, the border style, the
visual-style flag, the flat appearance, a link's three link colours, a property
grid's six, a data grid's cell styles, a strip's renderer. That record is the
undo, and the desktop theme is the undo being run. Without it the desktop theme
was not "leave this alone", it was "leave this DARK": a window wearing Dark had
nothing to be put back to, and choosing Light changed the title bar and nothing
else. `ThemeRoundTripTests` compares a window taken Light - Dark - Light
against one that started Light, in both directions and pixel by pixel, and the
screenshot check does the same for every real window it photographs.

A consequence worth knowing when adding to the walk: **anything the walk
writes has to be captured next to the line that writes it.** A property changed
without being recorded makes the theme one-way again, silently, and only in the
direction nobody tests by hand.

And a second one, which cost two more rounds to find: **the capture is a pass
of its own, before the walk paints anything.** A control that was never given a
colour of its own does not have one - asking it returns its parent's. The walk
paints a parent before it reaches the children, so a capture taken as each
control was painted read, for every child, the colour the theme had just put on
its parent. That was recorded as "what the toolkit gave it" and written back on
the way out, which nailed the theme on permanently.

It was invisible for as long as the frontend opened on Light, because then the
value being read was the right one anyway. Making Dark the default is what
exposed it: every window was now born Dark, captured wearing Dark, and choosing
Light put the dark colours back. What the user saw was a theme that "only works
after restarting" - the title bar changed, because that is set from the theme
directly rather than restored, and nothing else did. `ThemeEngine.Prime` is
that pass, and `ThemeRoundTripTests` now asks the question the old round trip
could not: not "does Dark and back come out Light", whose two ends are both
Light whether the middle worked or not, but "does a window **born** Dark and
told to be Light look like one born Light".

`LightThemeBaselineTests` holds the other half - that the hex values in
`light.json` are still the ones the code used - so that a theme which does
paint (a contributor's light theme, or Dark) paints what Chimera had.

Only the built-in Light theme sets `desktop`. A copy written out by "Write a
Copy to Edit" deliberately does not: it is about to be given colours of its
own, and those have to be painted.
