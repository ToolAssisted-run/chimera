# Chimera's window (issue 142). Two parts:
#   viewmodel/  the state and actions behind each window, in plain C++ with no
#               Qt, over the engine's C interface. Scripts, the MCP server and
#               the tests drive these.
#   qt/         the Qt Quick window: QML for layout and bindings, a generic
#               adapter from any view model to QML, and the items that draw
#               the picture and the piano roll.

vm_inc = include_directories('viewmodel/include')
# the engine's miniz, a copy of the view models' own: a bug report's zip is
# deflated, as Chimera's is. Hidden, so the copy never stands in for the
# engine's own in a program that has both.
vm_miniz = static_library(
  'vm_miniz',
  '../../extern/miniz/miniz.c',
  c_args: ['-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES'],
  gnu_symbol_visibility: 'hidden',
  pic: true,
)
# the build's commit and its time, read again at every build, as Chimera's are
ui_build_h = vcs_tag(
  command: ['git', '-C', repo_root, 'log', '-1', '--format=%H %ct'],
  input: 'viewmodel/src/ui_build.h.in',
  output: 'ui_build.h',
  replace_string: '@BUILD@',
  fallback: 'unknown 0',
)
vm_lib = static_library(
  'chimera-vm',
  [
    ui_build_h,
    'viewmodel/src/view_model.cpp',
    'viewmodel/src/tr.cpp',
    'viewmodel/src/issue_report.cpp',
    'viewmodel/src/mcp_vm.cpp',
    'viewmodel/src/input_log.cpp',
    'viewmodel/src/bindings.cpp',
    'viewmodel/src/json.cpp',
    'viewmodel/src/session_vm.cpp',
    'viewmodel/src/piano_roll_vm.cpp',
    'viewmodel/src/gamepads.cpp',
    'viewmodel/src/sound.cpp',
    'viewmodel/src/lua_bridge.cpp',
    'viewmodel/src/lua_libraries.cpp',
    'viewmodel/src/lua_strings.cpp',
    'viewmodel/src/ram_search_vm.cpp',
    'viewmodel/src/settings_vm.cpp',
    'viewmodel/src/lua_console_vm.cpp',
    'viewmodel/src/tool_windows_vm.cpp',
    'viewmodel/src/branches_vm.cpp',
    'viewmodel/src/firmware.cpp',
    'viewmodel/src/suggestion.cpp',
    'viewmodel/src/european_clock.cpp',
    'viewmodel/src/process_stamp.cpp',
    'viewmodel/src/crash_notes.cpp',
    'viewmodel/src/compiled_code.cpp',
    'viewmodel/src/customize_vm.cpp',
    'viewmodel/src/messages_vm.cpp',
    'viewmodel/src/display_vm.cpp',
    'viewmodel/src/paths_vm.cpp',
    'viewmodel/src/firmware_vm.cpp',
    'viewmodel/src/config_files_vm.cpp',
    'viewmodel/src/data_folder_vm.cpp',
    'viewmodel/src/precompiled_vm.cpp',
    'viewmodel/src/trace_logger_vm.cpp',
    'viewmodel/src/idle_eye.cpp',
    'viewmodel/src/subprocess.cpp',
    'viewmodel/src/nut_muxer.cpp',
    'viewmodel/src/encoder_vm.cpp',
    'viewmodel/src/host_input_vm.cpp',
    'viewmodel/src/game_vm.cpp',
    'viewmodel/src/forms_vm.cpp',
    'viewmodel/src/sha256.cpp',
    'viewmodel/src/comm_vm.cpp',
    'viewmodel/src/sql_vm.cpp',
    'viewmodel/src/surface_viewer_vm.cpp',
    'viewmodel/src/macro_tool_vm.cpp',
    'viewmodel/src/project_details_vm.cpp',
    'viewmodel/src/cores_vm.cpp',
    'viewmodel/src/branch_state.cpp',
    'viewmodel/src/watch_values.cpp',
    'viewmodel/src/settings_form.cpp',
    'viewmodel/src/new_project_vm.cpp',
    'viewmodel/src/hex_editor_vm.cpp',
    'viewmodel/src/controls_vm.cpp',
    'viewmodel/src/hotkeys_vm.cpp',
    'viewmodel/src/themes_vm.cpp',
    'viewmodel/src/ram_watch_vm.cpp',
    'viewmodel/src/config_store.cpp',
    'viewmodel/src/config_names.cpp',
    'viewmodel/src/help_vm.cpp',
    'viewmodel/src/media_maker_vm.cpp',
    'viewmodel/src/cache_manager_vm.cpp',
  ],
  include_directories: [vm_inc, engine_inc, include_directories('../../extern/lua'), include_directories('../../extern/sqlite3/sqlite3')],
  dependencies: [thread_dep, dl_dep],
  override_options: ['cpp_std=c++20'],
  pic: true,
)
vm_dep = declare_dependency(
  link_with: [vm_lib, vm_miniz, chimera_shared, lua54, e_sqlite3_lib],
  include_directories: [vm_inc, engine_inc],
  dependencies: [thread_dep],
)

test_vm = executable(
  'test_vm',
  'viewmodel/tests/test_vm.cpp',
  dependencies: [vm_dep],
  cpp_args: ['-UNDEBUG'],
  override_options: ['cpp_std=c++20'],
  build_by_default: true,
)
# the contract between Chimera and the core packages it is given: its rules on
# packages made up here, and every package in build/Cores (a core's CI runs it
# on its own package: CHIMERA_CORES_DIR=<folder> test-packages)
test_packages = executable(
  'test-packages',
  'viewmodel/tests/test_packages.cpp',
  dependencies: [vm_dep],
  cpp_args: ['-UNDEBUG'],
  override_options: ['cpp_std=c++20'],
  build_by_default: true,
)
test('package rules', test_packages, args: ['--self'])
test('installed packages', test_packages, args: [repo_root / 'build/Cores'])
# The packages and the project below are made from build/Cores/synth-box.chimeraCore,
# which tests/synth/build-package.sh makes after a build: they are made when a
# test that names them runs, not with every build (a fresh checkout's first
# build has no synth package yet).
# a second core that takes the synthetic core's files, for choosing between them
synth_again = custom_target(
  'synth-again',
  output: 'synth-again.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Again', '@OUTPUT@'],
  build_always_stale: true,
  build_by_default: false,
)
# a package of two machines with declared slots, for New Project
synth_slots = custom_target(
  'synth-slots',
  output: 'synth-slots.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/slots-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', '@OUTPUT@'],
  build_always_stale: true,
  build_by_default: false,
)
# a firmware that declares the hash of its original file, in a core that is
# not a game and in one that is
logo_firmware = '[{"id": "logo", "display": "The logo", "name": "logo.bin", "sha1": "CE31CFD83A86A850B5B110A3AC05B634F186C1F1", "required": true}]'
synth_hashed = custom_target(
  'synth-hashed',
  output: 'synth-hashed.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Hashed', '@OUTPUT@', '', logo_firmware],
  build_always_stale: true,
  build_by_default: false,
)
# a controller with a second player, keyboard keys and a console button
more_buttons = '["P2 A", "P2 B", "Power", "Key A", "Key B", "Key C", "Key D", "Key E", "Key F", "Key G", "Key H", "Key I", "Key J", "Key K", "Key L", "Key M", "Key N", "Key O", "Key P", "Key Q", "Key R", "Key S", "Key T", "Key U", "Key V", "Key W", "Key X", "Key Y", "Key Z", "Key 0", "Key 1", "Key 2", "Key 3", "Key 4", "Key 5", "Key 6", "Key 7", "Key 8", "Key 9"]'
synth_keys = custom_target(
  'synth-keys',
  output: 'synth-keys.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Keys', '@OUTPUT@', '', '', '', more_buttons, '', '', '',
    # its players declared, Power given to player 2: the core says whose a control is, not its name
    '{"Power": 2}'],
  build_always_stale: true,
  build_by_default: false,
)
# a game core that needs no firmware, for the Game menu
# ten releases of one firmware, each pinned by its hash, for the Firmware window's
# releases shown only when on hand (a release is "bios rel NN", 11 bytes)
bios_releases = '[{"id": "bios", "display": "Synth BIOS", "label": "v01", "size": 11, "sha1": "444D8D5C7C6427A941F9906A2D44082967BDE98E", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v02", "size": 11, "sha1": "102BF07EB78D59FB0CC16FABEE0979DA2CD440B7", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v03", "size": 11, "sha1": "AE71FDD67F43A8C1E74F3E70141C2B45168C3FB6", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v04", "size": 11, "sha1": "6A2CE8F5C5383505614052D817CE6C60D3DD3E03", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v05", "size": 11, "sha1": "10558C2BC8A1F4DBAE702864F80B790774E0C89E", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v06", "size": 11, "sha1": "A593A76AE19439D2849DCEDCE9A1940B50F507D7", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v07", "size": 11, "sha1": "E8ACBD4E69F007361237ED95A1F9275519EF1E78", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v08", "size": 11, "sha1": "82B46AA791D1A44FDD3264C53538E0CB0AE977FB", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v09", "size": 11, "sha1": "41DEFEBBB5CF7B7BCC676CD965D6C2D6E8F25BFD", "required": true}, {"id": "bios", "display": "Synth BIOS", "label": "v10", "size": 11, "sha1": "AF94B35ECFFF546BBA669623E5DAE616DC6C5401", "required": true}]'
synth_bios = custom_target(
  'synth-bios',
  output: 'synth-bios.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Bios', '@OUTPUT@', '', bios_releases],
  build_always_stale: true,
  build_by_default: false,
)
synth_plain_game = custom_target(
  'synth-plain-game',
  output: 'synth-plain-game.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Plain Game', '@OUTPUT@', 'game'],
  build_always_stale: true,
  build_by_default: false,
)
synth_game = custom_target(
  'synth-game',
  output: 'synth-game.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Game', '@OUTPUT@', 'game', logo_firmware,
    '2026-10-18T16:35:00Z'],
  build_always_stale: true,
  build_by_default: false,
)
# a core that compiles a game's code before a project is made, as RPCS3's (the
# synthetic core's compile session: six modules, a share a session)
synth_compile = custom_target(
  'synth-compile',
  output: 'synth-compile.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Compile', '@OUTPUT@', '', '', '', '', 'precompile'],
  build_always_stale: true,
  build_by_default: false,
)
# a core that declares no virtual size and has room for a wider picture than it
# draws, as Ruffle's: its picture is shown at its own shape
synth_square = custom_target(
  'synth-square',
  output: 'synth-square.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Square', '@OUTPUT@', '', '', '', '', '', 'square'],
  build_always_stale: true,
  build_by_default: false,
)
# a core that says it mixes its sound at 48000 Hz, heard at 44100 (issue 372)
synth_48k = custom_target(
  'synth-48k',
  output: 'synth-48k.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth 48k', '@OUTPUT@', '', '', '', '', '', '', '48000'],
  build_always_stale: true,
  build_by_default: false,
)
# a controller wider than the 64 buttons one number carries: 80 keys after
# the synthetic core's eight (issue 411)
wide_buttons = '["Key 01", "Key 02", "Key 03", "Key 04", "Key 05", "Key 06", "Key 07", "Key 08", "Key 09", "Key 10", "Key 11", "Key 12", "Key 13", "Key 14", "Key 15", "Key 16", "Key 17", "Key 18", "Key 19", "Key 20", "Key 21", "Key 22", "Key 23", "Key 24", "Key 25", "Key 26", "Key 27", "Key 28", "Key 29", "Key 30", "Key 31", "Key 32", "Key 33", "Key 34", "Key 35", "Key 36", "Key 37", "Key 38", "Key 39", "Key 40", "Key 41", "Key 42", "Key 43", "Key 44", "Key 45", "Key 46", "Key 47", "Key 48", "Key 49", "Key 50", "Key 51", "Key 52", "Key 53", "Key 54", "Key 55", "Key 56", "Key 57", "Key 58", "Key 59", "Key 60", "Key 61", "Key 62", "Key 63", "Key 64", "Key 65", "Key 66", "Key 67", "Key 68", "Key 69", "Key 70", "Key 71", "Key 72", "Key 73", "Key 74", "Key 75", "Key 76", "Key 77", "Key 78", "Key 79", "Key 80"]'
synth_wide = custom_target(
  'synth-wide',
  output: 'synth-wide.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Wide', '@OUTPUT@', '', '', '', wide_buttons],
  build_always_stale: true,
  build_by_default: false,
)
test(
  'view models',
  test_vm,
  args: [repo_root / 'build/Cores/synth-box.chimeraCore', repo_root / 'tests/synth/roms/gridWalker.testrom',
    repo_root / 'tests/synth/movies', repo_root / 'tests/synth/goldens', synth_again, synth_slots, synth_hashed, synth_game, synth_keys, synth_plain_game, synth_bios, synth_compile, synth_square, synth_48k, synth_wide],
  # OpenAL's device that plays to nowhere: the sound check runs with no speakers;
  # the ffmpeg the build ships, for the encode
  # and the headless runner, which compile sessions are
  env: ['ALSOFT_DRIVERS=null', 'CHIMERA_FFMPEG=' + (repo_root / 'build/dll/ffmpeg'),
    'CHIMERA_RUN=' + (meson.project_build_root() / 'chimera-run')],
  timeout: 180,
)

qt6 = import('qt6')
qt_dep = dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'Quick', 'QuickControls2'], required: get_option('qt_ui'))
if qt_dep.found()
  qt_moc = qt6.compile_moc(
    headers: ['qt/qml_view_model.hpp', 'qt/picture_item.hpp', 'qt/piano_roll_item.hpp', 'qt/key_names.hpp', 'qt/menu_letters.hpp', 'qt/theme_palette.hpp', 'qt/clipboard_relay.hpp', 'qt/picture_files.hpp', 'qt/pointer_capture.hpp', 'qt/button_router.hpp', 'qt/headless_run.hpp', 'qt/portal_file_chooser.hpp', 'qt/file_places.hpp', 'qt/system_file_dialog.hpp'],
    include_directories: [vm_inc, engine_inc],
    dependencies: [qt_dep],
  )
  # the resource compiler runs on the build machine; its Qt may pack with
  # zstd, which a Qt built without zstd (the Windows one) cannot unpack
  qt_rcc = qt6.compile_resources(sources: ['qt/ui.qrc'], extra_args: ['--no-zstd'])
  qt_dialogs_rcc = qt6.compile_resources(name: 'dialogs', sources: ['qt/dialogs.qrc'], extra_args: ['--no-zstd'])
  # Chimera's icon on the program and its windows on Windows, and what the
  # program's file says it is (name, version with the commit, licence),
  # the version read again at every build
  qt_icon = []
  if host_machine.system() == 'windows'
    rc_version_h = vcs_tag(
      command: ['git', '-C', repo_root, 'log', '-1', '--abbrev=8', '--format=' + meson.project_version() + ' (%h)'],
      input: 'qt/rc_version.h.in',
      output: 'rc_version.h',
      replace_string: '@VERSION@',
      fallback: meson.project_version(),
    )
    version_numbers = meson.project_version().split('.') + ['0', '0', '0']
    qt_icon = import('windows').compile_resources('qt/chimera.rc',
      depend_files: 'qt/chimera.manifest',
      depends: rc_version_h,
      include_directories: include_directories('.'),
      args: ['-DCHIMERA_VERSION_NUMBERS=' + ','.join(version_numbers[0], version_numbers[1], version_numbers[2], version_numbers[3])])
  endif
  qt_sources = [
    'qt/main.cpp',
    'qt/headless_run.cpp',
    'qt/qml_view_model.cpp',
    'qt/picture_item.cpp',
    'qt/piano_roll_item.cpp',
    'qt/key_names.cpp',
    'qt/key_filter.cpp',
    'qt/menu_letters.cpp',
    'qt/single_instance.cpp',
    'qt/theme_palette.cpp',
    'qt/translations.cpp',
    'qt/pointer_capture.cpp',
    'qt/overlay_painter.cpp',
    'qt/jump_list.cpp',
    'qt/portal_file_chooser.cpp',
    'qt/file_places.cpp',
    'qt/system_file_dialog.cpp',
    'qt/win32_file_dialog.cpp',
    qt_moc,
    qt_rcc,
    qt_dialogs_rcc,
  ]
  if host_machine.system() == 'windows'
    # the window is a DLL in lib\ with everything it needs, and chimera.exe
    # a launcher that loads it from there (qt/launcher.c); qt.conf, built in,
    # sends Qt to lib\ for its plugins and QML
    qt_conf = qt6.compile_resources(name: 'windows_conf', sources: ['qt/windows.qrc'], extra_args: ['--no-zstd'])
    shared_library(
      'chimera-qt-app',
      qt_sources + [qt_conf],
      name_prefix: '',
      # Gui's private headers for its Windows window's interface (the border in full screen)
      dependencies: [vm_dep, qt_dep, dependency('qt6', modules: ['Core', 'Gui'], private_headers: true)],
      # the view models have a Signal::emit; Qt's "emit" keyword would eat it
      cpp_args: ['-DQT_NO_KEYWORDS', '-DCHIMERA_QT_LIBRARY'],
      # the jump list's COM and shell calls
      link_args: ['-lole32', '-lshell32', '-lshlwapi', '-luuid', '-lpropsys'],
      override_options: ['cpp_std=c++20'],
      install: true,
      install_dir: 'dll',
    )
    chimera_qt = executable(
      'chimera',
      ['qt/launcher.c', qt_icon],
      # a release opens no console window beside the window; a debug build
      # keeps one for what the window writes
      win_subsystem: get_option('debug') ? 'console' : 'windows',
      install: true,
      install_dir: 'dll',
    )
  else
    chimera_qt = executable(
      'chimera',
      qt_sources,
      # libdbus is loaded when the desktop's file chooser is asked for (portal_file_chooser.cpp)
      dependencies: [vm_dep, qt_dep, dl_dep],
      # the view models have a Signal::emit; Qt's "emit" keyword would eat it
      cpp_args: ['-DQT_NO_KEYWORDS'],
      override_options: ['cpp_std=c++20'],
      install: true,
      install_dir: 'dll',
    )
  endif
  # the window's tests play a game in a project of its own, as a game runs
  # only in a project (owner, 2026-10-26)
  window_game = custom_target(
    'window-game',
    output: ['gridWalker.chimeraProject', 'gridWalker.testrom'],
    command: [find_program('python3'), files('qt/tests/game-project.py'), repo_root / 'build/Cores/synth-box.chimeraCore',
      repo_root / 'tests/synth/roms/gridWalker.testrom', '@OUTPUT0@'],
    build_by_default: false,
  )
  # the whole window loads (every QML file, every view model bound), runs a
  # game for a second and quits cleanly: a QML mistake fails here, not at
  # someone's desk
  window_run = files('qt/tests/window-run.py')
  test(
    'window loads',
    find_program('python3'),
    args: [window_run, meson.current_build_dir() / 'window-test', '50', chimera_qt, window_game[0],
      repo_root / 'build/Cores/synth-box.chimeraCore', '--play', '--measure', '1', '--show', 'lua'],
    env: ['CHIMERA_QT_STRICT_QML=1'],
    timeout: 60,
  )
  # client.getconfig's names and client.gettool's tools, where every view
  # model is: a name tied to a setting the window lacks reads nil and fails
  test(
    'lua config names',
    find_program('python3'),
    args: [window_run, meson.current_build_dir() / 'lua-config-test', '50', chimera_qt, window_game[0],
      repo_root / 'build/Cores/synth-box.chimeraCore', '--play', '--lua', files('qt/tests/lua-config-names.lua')],
    timeout: 60,
  )
  # a script's questions about TAStudio's cells reach the rolls the window
  # draws, which TAStudio's window makes after the program has started
  test(
    'tastudio queries',
    find_program('python3'),
    args: [window_run, meson.current_build_dir() / 'tastudio-queries-test', '50', chimera_qt, window_game[0],
      repo_root / 'build/Cores/synth-box.chimeraCore', '--play', '--lua', files('qt/tests/tastudio-queries.lua')],
    timeout: 60,
  )
  # Chimera's command line, as the window that replaces it takes it
  test(
    'command line',
    find_program('python3'),
    args: [files('qt/tests/command-line.py'), chimera_qt, meson.current_build_dir() / 'command-line-test', window_game[0],
      repo_root / 'build/Cores/synth-box.chimeraCore'],
    timeout: 300,
  )
  # Chimera's --headless, for runs nobody watches (the cores' tests): no
  # display, and what would wait for a person ends the run, said
  test(
    'headless',
    find_program('python3'),
    args: [files('qt/tests/headless.py'), chimera_qt, meson.current_build_dir() / 'headless-test',
      repo_root / 'build/Cores/synth-box.chimeraCore', repo_root / 'tests/synth/roms/gridWalker.testrom'],
    timeout: 300,
  )
  # New Project's window waits for the project it makes, and says why one
  # is refused (issue 422); no page waits on a change Qt never sends
  test(
    'new project window',
    find_program('python3'),
    args: [files('qt/tests/new-project-window.py'), chimera_qt, meson.current_build_dir() / 'new-project-window-test',
      repo_root / 'build/Cores/synth-box.chimeraCore', repo_root / 'tests/synth/roms/gridWalker.testrom',
      meson.current_source_dir() / 'qt'],
    timeout: 300,
  )
  # what the window draws, seen in its pictures: Capture OSD writes the
  # picture area's size, a script's text breaks at new lines, and the Lua
  # Console's list has its grid
  test(
    'window drawing',
    find_program('python3'),
    args: [files('qt/tests/window-drawing.py'), chimera_qt, meson.current_build_dir() / 'window-drawing-test', window_game[0],
      repo_root / 'build/Cores/synth-box.chimeraCore', files('qt/tests/window-drawing.lua')],
    timeout: 300,
  )
  # a theme reaches Qt Quick's controls, which take the platform's colours
  # and not the application's unless each window is given the theme
  test_theme = executable(
    'test-theme',
    ['qt/tests/test_theme.cpp', 'qt/theme_palette.cpp', 'qt/portal_file_chooser.cpp', 'qt/file_places.cpp',
      qt6.compile_moc(headers: ['qt/theme_palette.hpp', 'qt/portal_file_chooser.hpp', 'qt/file_places.hpp'], dependencies: [qt_dep]), qt_dialogs_rcc],
    dependencies: [qt_dep, dl_dep],
    override_options: ['cpp_std=c++20'],
    cpp_args: ['-DCHIMERA_QT_QML_DIR="' + meson.current_source_dir() / 'qt' + '"'],
  )
  test('theme', test_theme, env: ['QT_QPA_PLATFORM=offscreen', 'QT_QUICK_BACKEND=software', 'CHIMERA_FILE_CHOOSER=builtin'], timeout: 60)
  # the file and folder dialogs on Linux: the desktop's chooser through its
  # portal (a stand-in one on a bus of the test's own), and Chimera's own
  # where there is none (docs/file-picker.md)
  if host_machine.system() != 'windows'
    test_file_chooser = executable(
      'test-file-chooser',
      ['qt/tests/test_file_chooser.cpp', 'qt/portal_file_chooser.cpp', 'qt/file_places.cpp',
        qt6.compile_moc(headers: ['qt/portal_file_chooser.hpp', 'qt/file_places.hpp'], dependencies: [qt_dep]), qt_dialogs_rcc],
      dependencies: [qt_dep, dl_dep],
      override_options: ['cpp_std=c++20'],
    )
    test('file chooser', find_program('python3'), args: [files('qt/tests/file-chooser.py'), test_file_chooser], timeout: 180)
  endif
  # the window's texts come from the engine's language tables, in the
  # language the person's system or choice gives (docs/translations.md)
  test_translations = executable(
    'test-translations',
    ['qt/tests/test_translations.cpp', 'qt/translations.cpp'],
    include_directories: [engine_inc],
    link_with: [chimera_shared],
    dependencies: [qt_dep],
    override_options: ['cpp_std=c++20'],
  )
  test('translations', test_translations, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 30)
  # keys are named by their place, as Chimera's, whatever the layout, with
  # the modifier keys merged or each side its own
  test_key_names = executable(
    'test-key-names',
    ['qt/tests/test_key_names.cpp', 'qt/key_names.cpp', qt6.compile_moc(headers: ['qt/key_names.hpp'], dependencies: [qt_dep])],
    dependencies: [qt_dep],
    override_options: ['cpp_std=c++20'],
  )
  test('key names', test_key_names, env: ['QT_QPA_PLATFORM=offscreen'], timeout: 60)
  # a key a hotkey takes is not a menu's, and the menus' letters are drawn
  # while the keyboard works them (issue 405); Qt's test module drives the
  # keys, and the Windows Qt is built without it
  qt_test_dep = dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'Quick', 'QuickControls2', 'Test'], required: false)
  if qt_test_dep.found()
    test_key_filter = executable(
      'test-key-filter',
      ['qt/tests/test_key_filter.cpp', 'qt/key_filter.cpp', 'qt/key_names.cpp', 'qt/menu_letters.cpp',
        qt6.compile_moc(headers: ['qt/key_names.hpp', 'qt/menu_letters.hpp'], dependencies: [qt_dep]), qt_rcc],
      dependencies: [qt_test_dep, vm_dep],
      override_options: ['cpp_std=c++20'],
      cpp_args: ['-DQT_NO_KEYWORDS'],
    )
    test('key filter', test_key_filter, env: ['QT_QPA_PLATFORM=offscreen', 'ALSOFT_DRIVERS=null'], timeout: 60)
  endif
  # single instance mode's hand-over of the files named, in Chimera's payload
  test_single_instance = executable(
    'test-single-instance',
    ['qt/tests/test_single_instance.cpp', 'qt/single_instance.cpp'],
    dependencies: [qt_dep, vm_dep],
    override_options: ['cpp_std=c++20'],
  )
  test('single instance', test_single_instance, timeout: 60)
  # the MCP server in the window: the witness's agent, reading and then
  # changing the open project through chimera-mcp; the window quits by itself
  if host_machine.system() != 'windows'
    foreach mode : ['', 'changes']
      test(
        mode == '' ? 'mcp window' : 'mcp window changes',
        find_program('bash'),
        args: [files('qt/tests/mcp-window.sh'), chimera_qt, meson.project_build_root() / 'chimera-mcp', repo_root,
          meson.current_build_dir() / 'mcp-window-test', mode],
        is_parallel: false,
        timeout: 360,
      )
    endforeach
  endif
endif
