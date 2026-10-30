# Picking files and folders

Every window of Chimera picks files with `FileDialog` and folders with
`FolderDialog`, from Chimera's own QML module `ChimeraDialogs`
(`source/ui/qt/dialogs`). They take the properties of Qt's dialogs of those
names that Chimera's windows use (`title`, `fileMode`, `nameFilters`,
`currentFolder`, `selectedFile`, `selectedFiles`, `selectedFolder`,
`defaultSuffix`, `parentWindow`, `visible`, `result`, `open()`, `close()`,
`accepted`, `rejected`), so a window does not know which dialog it got.
Lua's `forms.openfile` uses the same `FileDialog`. Both also take
`checkBoxText` and `checkBoxChecked`: a tick box in the dialog itself (below).

Which dialog is shown:

| System | Dialog |
| --- | --- |
| Windows, macOS | the system's own, through Qt, as before |
| Linux and the BSDs, with a desktop portal | the desktop's own chooser (KDE's, GNOME's, or whichever the desktop is set to), through the XDG desktop portal |
| Linux and the BSDs, with none | Chimera's own chooser, drawn inside the window |

`CHIMERA_FILE_CHOOSER=builtin` in the environment shows Chimera's chooser
even where there is a portal: for a desktop whose portal misbehaves, and for
the tests.

## The desktop's own chooser, through the portal (2026-10-29)

Before this, Linux had Qt's own file dialog, drawn by Qt inside the window.
A person who reported it as unusable listed what it lacked. It is not the
desktop's chooser, which many people prefer. A path cannot be typed into it,
so a folder such as `/usr/share/games/SRB2` has to be clicked to from the top.
It cannot show hidden files and folders. Its folder dialog picks the folder
highlighted in the list, not the one it shows, so to pick a folder you go up
one level and click it. It does not list bookmarks, drives, or even the root
(`/`), which is reached only by pressing Up again and again.

The XDG desktop portal is how a program asks the desktop for its own
chooser. It is a service on the session D-Bus (the desktop's local message
bus: a socket on this machine, not the network). Its
`org.freedesktop.portal.FileChooser` interface draws whichever chooser the
person's desktop is set to, with its bookmarks, drives, typed paths and
hidden files. The Windows Forms frontend did the same in PR 275, and this
follows it.

Qt has its own way to the portal, but only in a Qt built with D-Bus, and
Chimera's Qt is built without it (`tools/build-qt-linux.sh`). So
`PortalFileChooser` (`source/ui/qt/portal_file_chooser.cpp`) loads
`libdbus-1.so.3`, which every Linux desktop has, when a dialog first opens.
Nothing is added to the build or the bundle. `tools/check-no-network.sh`
still passes.

- **When it is used.** When there is a session bus, the portal on it has a
  file chooser, and libdbus loads. Folders need version 3 of the interface or
  later. A run with `--headless` never asks it. Otherwise Chimera's chooser
  is shown.
- **What it is told.** The title; the filters, each Qt name filter as a label
  and its patterns (`*.*` goes as `*`, so a file with no dot in its name is
  not left out); the first filter as the one to open on; the folder to open
  in; for a save, the name to offer; for several files, that several may be
  picked.
- **Which window it belongs to.** Under X11, the window, as `x11:` and its
  id. Under Wayland a parent is a handle the compositor gives out
  (xdg-foreign), which Qt does not ask for, so the chooser is told no parent.
  It is modal either way, since Chimera's windows take no input meanwhile.
- **Cancel.** The portal answers 0 for a choice, 1 for Cancel, and 2 for
  "ended some other way". shana answers every Cancel with 2, so 2 is Cancel
  too. Only a 2 that comes within 300 ms, sooner than a person can press
  Cancel, means the chooser never came up, and then Chimera's is shown
  instead. If the portal leaves the bus while its chooser is open, or the bus
  goes away, Chimera's is shown too, rather than waiting for ever.
- **What is made of the answer.** The `file://` addresses become paths. A
  save with no extension gets the dialog's `defaultSuffix`, as Qt's dialog
  does. The desktop's chooser asks before it overwrites a file on its own.
- **Modal.** While the chooser is open, Chimera's windows take no clicks,
  keys, wheel or closing (an event filter on the application throws them
  away). They are still drawn. They are not disabled: in PR 275 a disabled
  window that was modal itself kept the focus, and the chooser opened behind
  it.

## Chimera's own chooser (2026-10-29)

Where there is no portal, `BuiltinChooser.qml` is shown in place of Qt's
dialog, with what the report found Qt's lacked:

- **A path can be typed or pasted** into the Location box at the top
  (Ctrl+L goes there). Enter goes to a folder. In a file dialog, Enter on a
  file's path picks it, and a save's path is saved to. `~` is home, and a
  path that does not start with `/` is read from the folder shown. A file's
  name, or a path, can also be typed into the File name box.
- **Show hidden files** (Ctrl+H) lists the files and folders whose names
  start with a dot.
- **A folder dialog picks the folder it shows.** Double-click a folder to go
  into it. Select This Folder picks the one shown, which the Location box
  names, and the dialog says so under the list.
- **Places beside the list**: the root (`/`), home, the person's Desktop,
  Documents, Downloads, Music, Pictures and Videos, the desktop's bookmarks
  (GTK's `~/.config/gtk-3.0/bookmarks` and KDE's
  `~/.local/share/user-places.xbel`, without KDE's own entries and hidden
  ones), and the drives mounted under `/media`, `/run/media` and `/mnt`
  (`file_places.cpp`). Up (Alt+Up, or Backspace in the list) goes to the
  folder above.
- A save over a file that is there asks first: Replace, or Save again.
- Several files are picked with Ctrl and Shift.

Its texts are the `file_chooser.*` keys of the language tables. It draws in
the window's theme. Its places' names are drawn in the window's text colour,
set on each row: a row that only inherits the theme kept the desktop's
colour.

## A tick box in the dialog itself (2026-10-29)

Three windows scan a folder somebody points at: New Project's firmware page,
Project Files, and Config > Firmware. Whether to look in the folder's
sub-folders too is a choice about the folder being picked, so it is a tick
box in the folder dialog, "Include sub-folders", ticked to begin with, as
PR 275 made it in the Windows Forms frontend. New Project and Project Files
had it as a box beside their Scan Folder button; Config > Firmware always
looked in sub-folders and now asks too.

A dialog carries a box when its `checkBoxText` is set. `checkBoxChecked`
says whether it starts ticked and, after Accept, how the person left it.

- **The desktop's chooser, through the portal:** the box goes as a portal
  "choice" with no options (`a(ssa(ss)s)`, id `checkbox`, value `"true"` or
  `"false"`), which the chooser draws as a tick box, and comes back in the
  answer's `choices`. A chooser that does not draw it (termfilechooser) sends
  none back, and the box stays as it was.
- **Chimera's own chooser:** a tick box in its bottom row.
- **Windows:** Qt's Windows dialog takes no controls of a caller's, so a
  dialog with a box is Windows' own common item dialog shown by Chimera
  (`win32_file_dialog.cpp`, through `system_file_dialog.cpp`), with the box
  added by `IFileDialogCustomize::AddCheckButton`. A dialog without a box is
  still Qt's. If the dialog cannot be made, Qt's is shown, without the box.
- **macOS:** Qt's dialog, without the box.

## Tests

`meson test -C build/meson-linux "file chooser"` runs
`source/ui/qt/tests/file-chooser.py`, which runs `test-file-chooser` twice:

- With no session bus: Chimera's chooser, driven as a person would. A folder
  dialog picks the folder shown. A typed path is gone to and picked, and a
  folder that is not there is said. The root, home and a GTK bookmark are
  among the places. Hidden files show when asked. A typed name is opened. A
  save gets its suffix, and one over a file asks first. A dialog's tick box is
  drawn, and unticked there it is unticked in the dialog.
- With a stand-in portal (`stand-in-portal.py`, on a private bus of the
  test's own): the requests (title, filters, folder, name, several files, a
  folder) and the answers (a file, several, a save with its suffix added,
  Cancel, a chooser that never came up, a late "failed" answer as Cancel).
  It also checks that a key pressed in Chimera's window while the chooser is
  open is thrown away, and one pressed after reaches it, and that a tick box
  goes as a choice with no options and comes back as the person left it (or
  as it was, from a chooser that does not draw it). This half is skipped,
  and says so, where `dbus-daemon` or Python's `dbus` and `gi` modules are
  missing.

The helpers that turn Qt's filters into the portal's, URIs into paths, and
read the bookmark and mount files, are checked in both runs.
