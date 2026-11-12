# Chimera's window (issue 142)

Chimera's interface: Qt Quick views over view models with no Qt in them
(docs/qt-interface.md says what was decided and measured).

## How it is built

Three layers, each knowing only the one below it:

- **The engine** (`source/engine`, unchanged): the machine, movies, states.
- **View models** (`viewmodel/`): the state and the actions behind each
  window, in plain C++ with no Qt. A view model names everything it has in
  one registry: its properties (values, with a signal when they change),
  its commands (actions, with a flag saying when they can run), its lists of
  rows and its pictures: `session` (a project opened and played, keys and
  gamepads), `pianoRoll` (one row a frame, one column a button), and one for
  each tool window (`ramSearch`, `hexEditor`, `newProject`...).
- **The window** (`qt/`): Qt Quick. `Main.qml` holds layout and bindings
  only. One generic adapter (`qml_view_model.cpp`) turns any view model into
  QML properties, a `run()` for commands and table models, without knowing
  which view model it is. The picture and the piano roll grid are drawn by
  two C++ items, because they draw many cells or pixels each frame.

Everything a window does goes through a registry, so anything else can do
the same: a Lua script reaches every view model by the same names
(`lua_bridge.cpp`: `chimera.session.play()`,
`chimera.pianoRoll.toggle(5, 0)`, `chimera.onFrame(fn)`), and `describe()`
lists those names as JSON, which is what an MCP server's tools would be
made from. The tests drive the view models the same way, with no window.

The machine runs on a thread of its own and is paced by the clock at its
own frame rate; what it learns is posted to the window's thread. Gamepads
(SDL3) are read on the machine's thread just before each frame and named as
Chimera has always named them, so a package's default bindings apply. Sound
goes to OpenAL Soft as each frame finishes.

## Building and running

Qt 6.12 is needed: Qt's own build for Linux, and for Windows one built with
Chimera's mingw-w64 compiler (`tools/build-qt-windows.sh`, without Qt's
network module, since Chimera has no network code).

```sh
export PKG_CONFIG_PATH=$HOME/qt/6.12.0/gcc_64/lib/pkgconfig
export PATH=$HOME/qt/6.12.0/gcc_64/libexec:$PATH
meson setup build/meson-linux --prefix "$PWD/build" --libdir dll -Dqt_ui=enabled
meson compile -C build/meson-linux
meson test -C build/meson-linux 'view models'
LD_LIBRARY_PATH=$HOME/qt/6.12.0/gcc_64/lib build/meson-linux/source/ui/chimera --project <project> [--core <package>]
```

`chimera --help` lists the options for measuring (`--measure`,
`--scroll`, `--screenshot`), scripts (`--lua`) and the registry
(`--describe`).

## Not done yet

What is still to do is listed in docs/qt-interface.md, under "The rest
of the move, by size".
