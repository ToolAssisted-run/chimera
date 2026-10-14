# The file and folder pickers, and the option the folder picker carries

There is one folder picker in the frontend, `FolderBrowserEx`. On Windows it is
the Explorer-style common item dialog - an address bar, and a box to type or
paste a path into (issue #57, `Win32FolderPicker`). A third path,
`SHBrowseForFolder`, is what is left on a Windows where the item dialog cannot
be had. On Linux and the BSDs it is the desktop's own chooser, through the
desktop portal (below), and Mono's toolkit dialog where there is no portal.

Files are picked with `OpenFilePicker` and `SaveFilePicker`. They take the
same properties as WinForms' `OpenFileDialog` and `SaveFileDialog`, and show
the same dialogs on Windows. On Linux and the BSDs they ask the portal first.
Nothing in the frontend makes an `OpenFileDialog`, `SaveFileDialog` or
`FolderBrowserDialog` of its own.

## The desktop's own chooser, through the portal (2026-10-14)

Mono's file dialog is its own drawing of one: no bookmarks, no recent places,
no typing a path with completion, and nothing like the dialog every other
program on the desktop shows. The XDG desktop portal is how a program asks the
desktop for that dialog: a service on the session D-Bus (the desktop's local
message bus - a socket on this machine, not the network) whose
`org.freedesktop.portal.FileChooser` interface draws whichever chooser the
person's desktop is set to, KDE's or GNOME's or another.

`PortalFileChooser` asks it, through `libdbus-1` (`DBusImports`), which every
Linux desktop has. No package was added for it.

- **When it is used.** When there is a session bus and the portal on it has a
  file chooser. Folders need version 3 of the interface or later. Otherwise -
  no bus, no portal, no `libdbus`, or a portal whose file chooser is turned
  off - the toolkit dialog is shown, as before.
- **What it is told.** The title; the filters, converted from the WinForms
  filter string (`*.*` goes as `*`, so a file with no dot in its name is not
  left out); the filter to open on; the folder to open in; for a save, the
  name to offer; for the folder picker, the sub-folders tick box, as a portal
  "choice" with no options.
- **Which window it belongs to.** In an X11 session, Chimera's window, as
  `x11:` and the window's frame (Mono's handle is the client area, so the frame
  is read from Mono's own bookkeeping; if that fails it says "no parent", which
  the portal allows). In a Wayland session (`WAYLAND_DISPLAY` set) Chimera's
  windows belong to XWayland and the chooser is told no parent. An `x11:` parent
  there crashed GNOME's chooser (Nautilus, under niri, whichever X11 window it
  named) and attached no dialog anywhere it was tried. The chooser is modal
  either way, because Chimera's windows take no input while it is open.
- **Cancel.** The portal answers 0 for a choice, 1 for Cancel and 2 for "ended
  some other way". shana answers every Cancel with 2, so 2 is taken as Cancel
  too. Only a 2 that comes back within 300 ms, sooner than a person can press
  Cancel, means the chooser never came up, and then the toolkit dialog is
  shown instead.
- **What is made of the answer.** The `file://` addresses become paths. The
  filter in use comes back as the filter index. A save typed without an
  extension gets the one the filter in use names, or the default extension,
  as the toolkit dialog does. The portal's chooser asks before it overwrites,
  on its own, so `OverwritePrompt` is only for the toolkit dialog.
- **Modal.** While the chooser is open, Chimera's windows take no clicks, keys
  or closing: a message filter (`Application.AddMessageFilter`) throws them
  away. They are still painted and still see the mouse move. Emulation does
  not run meanwhile, as with any dialog. They are not disabled: a Mono window
  that is modal itself (Config > Firmware is one) kept the focus once it was
  disabled, so the chooser opened behind it and could not be brought to the
  front (Plasma and GNOME on Wayland).

## Why the sub-folders option lives in the dialog

Three places in the frontend scan a folder somebody points at: the New Project
wizard's firmware step, the Firmware Manager, and locating a project's files.
All three walk sub-folders, which is right for somebody pointing at a firmware
folder and wrong for somebody pointing at a collection - so the choice has to be
offerable, and it appeared as a check box beside the wizard's Scan Folder
button. One page out of three, and forgotten in the other two.

The choice belongs to the act of choosing a folder, not to whichever page has
the button. So on Windows it is **in the picker**, as a check button added
through `IFileDialogCustomize` (`AddCheckButton` / `GetCheckButtonState`), and
every Scan Folder in the frontend gets it for free.

One delegate carries it, `PickScanFolder`:

```csharp
public delegate string PickScanFolder(ref bool includeSubfolders);
```

The flag goes **in** as the state to open on and comes back **out** as what the
person settled on. A null return means no folder was chosen, and then the flag
means nothing and must not be read.

### The pickers that cannot carry it

`FolderBrowserEx.CheckBoxChecked` is changed in exactly one case: the box was
shown, a person ticked or unticked it, and they pressed Select Folder. In every
other case it comes back exactly as it went in, on purpose:

- **Mono's dialog** has no room for a control of ours.
- **The `SHBrowseForFolder` tree** has none either.
- **A Windows whose shell refuses `IFileDialogCustomize`** - the check button is
  never added, and the dialog still works.
- **A portal chooser that does not draw choices** - the box is sent and not
  shown, and nothing comes back for it. termfilechooser is one. There the
  wizard's own box is hidden as well, so the scan uses the remembered answer;
  that is for termfilechooser to change.

`CheckBoxShown` says which happened, after the fact. `CanShowCheckBox` answers
for the *platform*, before any dialog opens - Windows, or a portal that can pick
folders - and is what a window asks when it
is deciding whether to carry the choice itself.

### Where the option is offered

| Where | Windows | Linux, with a portal | Linux, without one |
| --- | --- | --- | --- |
| New Project wizard, firmware step | in the dialog | in the dialog | the check box on the page |
| Firmware Manager, Scan Folder | in the dialog | in the dialog | not offered; scans sub-folders, as it always did |
| Locate Project Files, Scan Folder | in the dialog | in the dialog | not offered; scans sub-folders, as it always did |

The wizard's on-page check box is **hidden, not removed**, where the picker
carries the option: it is still where that page remembers the answer between
scans. Two controls for one setting would be worse than either, so exactly one
of the two is ever visible - which is what `ExactlyOneControlOffersTheChoice`
asserts, for both platforms, on whichever one it runs.

Cancelling a picker never changes the remembered answer.

## What the tests cover, and what they do not

### The portal

`PortalFileChooserTests` cover what needs no bus: the filters as converted, a
`file://` address as a path, the request's name, and the extension a save is
given. The suite has no portal to talk to, and does not get one.

What was tried by hand, on 2026-10-14: a stand-in portal (a short Python D-Bus
service) on a private bus that could start no other service, driven through the
real `OpenFilePicker`, `SaveFilePicker` and `FolderBrowserEx`. One file, several
files, Cancel, a save typed without an extension, a folder with the box unticked,
a folder from a chooser that ignores the box, an owner window (disabled while
the chooser was open, and given to the portal as `x11:` and a window), a portal
too old to pick folders, no portal on the bus, and no bus at all. The calls it
received were read back, option by option.

And in the running frontend, with KDE's chooser behind the portal
(2026-10-14): Open Project, the wizard's firmware Scan Folder (the sub-folders
box was in KDE's dialog, and not on the page), the data directory, and a save
typed without its extension. All four worked.

And with every other chooser that could be installed, under niri, on
2026-10-14. Each ran behind a second portal on a private bus, with a config of
its own naming that chooser, so the desktop's own settings were not touched;
`dbus-monitor` recorded every request and answer. The four checks were Open
Project, the wizard's firmware Scan Folder, the data directory and a save.

| Chooser | Result |
| --- | --- |
| KDE (`xdg-desktop-portal-kde`) | works; the sub-folders box is in the dialog |
| GNOME (`xdg-desktop-portal-gnome` 51.0, which hands the dialog to Nautilus) | works. Given an `x11:` parent, Nautilus crashed on every request, and a plain `gdbus` request naming any X11 window did the same; without a parent it does not |
| GTK (`xdg-desktop-portal-gtk`) | works, with a pause before the dialog |
| COSMIC (`xdg-desktop-portal-cosmic`) | works, no pause |
| LXQt (`xdg-desktop-portal-lxqt`) | works, with a pause before the dialog |
| shana, sending files to LXQt's chooser and folders and saves to KDE's | works. It answers Cancel with 2, which reopened the chooser and then showed Mono's dialog until 2 was taken as Cancel |
| xapp | has no file chooser; the portal used GNOME's instead, which worked |
| termfilechooser (yazi in kitty) | works; no sub-folders box |
| a portal with its file chooser set to `none` | Mono's dialog, and no request sent to the portal |
| no session bus | Mono's dialogs, and the wizard's own sub-folders box |

The chooser behind Chimera's windows (Plasma and GNOME on Wayland, from
Config > Firmware) was found with KWin's own record of its windows, read by a
small KWin script while a test chooser was open. The chooser sat below
Chimera's main window and its modal window, and asking KWin to activate it
did not hold: within a second the modal window was active again. With no
modal window the chooser opened on top. With no chooser at all, a disabled
Mono modal window kept another program's window from taking the focus, and
an enabled one did not. With the message filter in place of disabling, the
chooser opened on top and active, and could be brought back to the front
after another window took the focus. Keys and closing (through KWin and
`wmctrl`) reached Chimera's window before the chooser opened and nothing did
while it was open; the clicks `xdotool` sent did not arrive reliably even
before, so blocking clicks rests on the code and on trying it by hand. Tried
by hand afterwards on Plasma Wayland, GNOME and niri: the chooser in front,
Chimera's windows not answering while it was open, and Cancel and a choice
as before.

Under their own desktops, by hand (2026-10-14): Plasma on Wayland and on X11,
where the parent window is sent and the chooser is attached to Chimera's
window, GNOME and COSMIC. All worked. LXQt's chooser was not tried under LXQt.

The pauses with GTK and LXQt were not looked into. The test bus could not
start gvfs (the desktop's own was already mounted), which GTK's chooser waits
on, so they may belong to the test and not to the chooser.

### The Windows dialog

Read this before trusting a green run. This is COM interop against the Windows
shell, exercised from a suite that runs on the other platform - `docs/gates.md`
mode E, exactly.

`./tests/ui/run-ui-tests.sh` runs on **Linux, on Mono, under Xvfb**. It can see
the C# either side of the dialog and nothing of the dialog itself.

- **Covered by the suite**: that `ProjectFolderScan.Enumerate` and
  `ProjectFolderScan.Resolve` both honour the flag either way; that the wizard
  offers the answer it holds, acts on what comes back, remembers it for the next
  scan, and ignores it entirely when the picker returned no folder; and that a
  page shows a check box of its own exactly where the picker cannot carry one -
  the half of that which is true on the platform the suite is standing on.
- **NOT covered by the suite**: the other half of "exactly one control offers
  the choice". On Linux the page carries the box, and that nothing hides it on
  Windows is checked by the harness below, not here. Making the suite ask both
  halves needs production code that will lie about which platform it is on, and
  a seam that exists only for a test is not worth the rule it proves - so it was
  removed (2026-09-20, Sergio's call).
- **NOT covered by the suite**: everything else that is Windows'. Whether the dialog
  draws the check button at all. Whether `AddCheckButton` and
  `GetCheckButtonState` are at the vtable offsets this code believes they are -
  a wrong offset calls some other method, and on Linux nothing calls anything.
  Whether the state read back after Show is the one the person left. Whether
  `QueryInterface` for `IFileDialogCustomize` succeeds on a given Windows.

**A green Linux run is not evidence that the Windows dialog works.**

### The harness that does ask Windows

`tests/ui/windows/folder-picker-checkbox.sh` compiles a small program against
the frontend assemblies this tree has just built and runs it on real Windows,
the way `live-theme-switch.sh` does for theming. It opens the real
`FolderBrowserEx`, finds the check button among the dialog's child windows,
and:

- checks the control is there and written with the label it was given;
- checks it opened in the state the caller passed in;
- clicks it, presses Select Folder, and checks the caller gets `false` back;
- opens a second dialog, unticks it, presses **Cancel**, and checks the caller's
  value is untouched;
- writes a picture of the dialog, so a person can see where the box sits.

It is not part of the gate, because the gate runs on Linux. Run it by hand from
WSL after `dotnet build source/gui/Chimera.sln -c Release`, and run it whenever
`Win32FolderPicker` changes - especially if a vtable slot is added, because the
offsets below it all move.

Pass `--by-hand` to drive the dialogs yourself instead. That is the one check
nothing automated replaces: that the box looks like part of the dialog and can
be clicked with a mouse.

It photographs with `PrintWindow`, which renders one window - the dialog this
program opened - into a bitmap of its own. It reads no screen pixels, so nothing
else on the developer's desktop can be captured even by accident.
