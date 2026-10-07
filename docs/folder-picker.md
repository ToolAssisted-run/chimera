# The folder picker, and the option it carries

There is one folder picker in the frontend, `FolderBrowserEx`. On Windows it is
the Explorer-style common item dialog - an address bar, and a box to type or
paste a path into (issue #57, `Win32FolderPicker`). On anything else it is the
toolkit's own dialog. A third path, `SHBrowseForFolder`, is what is left on a
Windows where the item dialog cannot be had.

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

### The three pickers that cannot carry it

`FolderBrowserEx.CheckBoxChecked` is changed in exactly one case: the box was
shown, a person ticked or unticked it, and they pressed Select Folder. In every
other case it comes back exactly as it went in, on purpose:

- **Mono's dialog** has no room for a control of ours.
- **The `SHBrowseForFolder` tree** has none either.
- **A Windows whose shell refuses `IFileDialogCustomize`** - the check button is
  never added, and the dialog still works.

`CheckBoxShown` says which happened, after the fact. `CanShowCheckBox` answers
for the *platform*, before any dialog opens, and is what a window asks when it
is deciding whether to carry the choice itself.

### Where the option is offered

| Where | Windows | Mono |
| --- | --- | --- |
| New Project wizard, firmware step | in the dialog | the check box on the page |
| Firmware Manager, Scan Folder | in the dialog | not offered; scans sub-folders, as it always did |
| Locate Project Files, Scan Folder | in the dialog | not offered; scans sub-folders, as it always did |

The wizard's on-page check box is **hidden, not removed**, where the picker
carries the option: it is still where that page remembers the answer between
scans. Two controls for one setting would be worse than either, so exactly one
of the two is ever visible - which is what `ExactlyOneControlOffersTheChoice`
asserts, for both platforms, on whichever one it runs.

Cancelling a picker never changes the remembered answer.

## What the tests cover, and what they do not

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
