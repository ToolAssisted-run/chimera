# Chimera build: mediated by meson, from a Linux host, for BOTH targets.
#
#   Linux artifacts (.so files and the programs):
#     meson setup build/meson-linux --prefix "$(pwd)/build" --libdir dll
#     meson compile -C build/meson-linux
#     meson install -C build/meson-linux
#
#   Windows artifacts (.dll, cross-built via mingw-w64):
#     meson setup build/meson-windows --prefix "$(pwd)/build" --libdir dll \
#         --cross-file extern/meson/mingw-w64.ini
#     meson compile -C build/meson-windows
#     meson install -C build/meson-windows
#
# Everything is built per target, the window too (with -Dqt_ui, source/ui).
# Per the reproducibility pillar, none of this affects movie determinism; that
# belongs to core packages.
project(
  'chimera',
  ['c', 'cpp'],
  version: '0.1',
  default_options: ['buildtype=release', 'cpp_std=c++17'],
  meson_version: '>=1.3',
)

fs = import('fs')
host_windows = host_machine.system() == 'windows'
repo_root = meson.project_source_root()

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)
dl_dep = cc.find_library('dl', required: false)
thread_dep = dependency('threads')

# ---------------------------------------------------------------------------
# chimera_crash: the crash note a Windows fast-fail cannot stop (in-repo source;
# runs inside WerFault.exe, never in Chimera - see source/crash/chimera_crash.c)
# ---------------------------------------------------------------------------
if host_windows
  # the GL calls in a note are named from the same master list the bridge numbers by
  chimera_crash_gl_names = custom_target('chimera-crash-gl-names',
    output: 'gl_names.h',
    command: [find_program('python3'), files('source/crash/gen-gl-names.py'),
      files('extern/chimera-common-minibox/source/gl/gl-entry-points.txt'), '@OUTPUT@'])
  # and the Vulkan ones from the Vulkan bridge's list (chimera issues 276, 278)
  chimera_crash_vk_names = custom_target('chimera-crash-vk-names',
    output: 'vk_names.h',
    command: [find_program('python3'), files('source/crash/gen-vk-names.py'),
      files('extern/chimera-common-minibox/source/vk/vk-entry-points.txt'), '@OUTPUT@'])
  shared_library(
    'chimera_crash',
    ['source/crash/chimera_crash.c', chimera_crash_gl_names, chimera_crash_vk_names],
    name_prefix: '',
    dependencies: [cc.find_library('dbghelp'), cc.find_library('psapi')],
    install: true,
    install_dir: 'dll',
  )
endif

# ---------------------------------------------------------------------------
# lua54: the Lua 5.4 runtime (stock upstream sources, pinned submodule)
# ---------------------------------------------------------------------------
lua_src = []
foreach f : [
  'lapi', 'lauxlib', 'lbaselib', 'lcode', 'lcorolib', 'lctype', 'ldblib',
  'ldebug', 'ldo', 'ldump', 'lfunc', 'lgc', 'linit', 'liolib', 'llex',
  'lmathlib', 'lmem', 'loadlib', 'lobject', 'lopcodes', 'loslib', 'lparser',
  'lstate', 'lstring', 'lstrlib', 'ltable', 'ltablib', 'ltm', 'lundump',
  'lutf8lib', 'lvm', 'lzio',
]
  lua_src += files('extern/lua' / f + '.c')
endforeach
lua54 = shared_library(
  'lua54',
  lua_src,
  name_prefix: host_windows ? '' : 'lib',
  c_args: host_windows ? ['-DLUA_BUILD_AS_DLL'] : ['-DLUA_USE_LINUX'],
  dependencies: [m_dep, dl_dep],
  install: true,
  install_dir: 'dll',
)

# ---------------------------------------------------------------------------
# e_sqlite3: SQLite with the canonical SQLitePCLRaw configuration
# ---------------------------------------------------------------------------
sqlite_args = [
  '-DSQLITE_ENABLE_COLUMN_METADATA', '-DSQLITE_ENABLE_FTS3_PARENTHESIS',
  '-DSQLITE_ENABLE_FTS4', '-DSQLITE_ENABLE_FTS5', '-DSQLITE_ENABLE_JSON1',
  '-DSQLITE_ENABLE_MATH_FUNCTIONS', '-DSQLITE_ENABLE_RTREE',
  '-DSQLITE_ENABLE_SNAPSHOT', '-DSQLITE_DEFAULT_FOREIGN_KEYS=1',
]
if host_windows
  sqlite_args += '-DSQLITE_API=__declspec(dllexport)'
endif
e_sqlite3_lib = shared_library(
  'e_sqlite3',
  files('extern/sqlite3/sqlite3/sqlite3.c'),
  name_prefix: host_windows ? '' : 'lib',
  c_args: sqlite_args,
  dependencies: [m_dep, dl_dep, thread_dep],
  install: true,
  install_dir: 'dll',
)

# ---------------------------------------------------------------------------
# zstd: savestate compression; upstream's own meson build, nested
# ---------------------------------------------------------------------------
nested_tool = files('extern/meson/nested-build.sh')
# The three nested builds below are asked at every build (build_always_stale):
# meson cannot see their sources, so a bump of the submodule left the old
# library in an existing build directory. Each sub-build is incremental and
# reconfigures itself when its own build files change, so asking costs a
# no-op there, and the library is copied out only when it changed.
cross_arg = host_windows ? '--cross-file=' + repo_root / 'extern/meson/mingw-w64.ini' : ''
zstd_out = host_windows ? 'libzstd.dll' : 'libzstd.so'
custom_target(
  'zstd',
  output: zstd_out,
  build_always_stale: true,
  command: [
    nested_tool, 'meson',
    repo_root / 'extern/zstd/build/meson',
    '@OUTDIR@' / 'zstd-build',
    '@OUTPUT@',
    cross_arg,
    '-Dbin_programs=false', '-Dbin_tests=false', '-Dbin_contrib=false',
  ],
  console: true,
  install: true,
  install_dir: 'dll',
)

# ---------------------------------------------------------------------------
# SDL2 + OpenAL Soft: cmake-driven, via the wrapper script
# ---------------------------------------------------------------------------
cmake_toolchain = host_windows ? repo_root / 'extern/meson/mingw-toolchain.cmake' : ''
custom_target(
  'sdl2',
  output: host_windows ? 'SDL2.dll' : 'libSDL2.so',
  build_always_stale: true,
  command: [
    nested_tool, 'cmake',
    repo_root / 'extern/SDL2',
    '@OUTDIR@' / 'sdl2-build',
    '@OUTPUT@',
    cmake_toolchain,
  ],
  console: true,
  install: true,
  install_dir: 'dll',
)
custom_target(
  'openal',
  output: host_windows ? 'OpenAL32.dll' : 'libopenal.so',
  build_always_stale: true,
  command: [
    nested_tool, 'cmake',
    repo_root / 'extern/openal-soft',
    '@OUTDIR@' / 'openal-build',
    '@OUTPUT@',
    cmake_toolchain,
    '-DALSOFT_UTILS=OFF', '-DALSOFT_EXAMPLES=OFF', '-DALSOFT_TESTS=OFF',
    '-DLIBTYPE=SHARED',
  ],
  console: true,
  install: true,
  install_dir: 'dll',
)

# ---------------------------------------------------------------------------
# libminiboxhost: the miniBox waterbox host (from the extern/chimera-common-minibox submodule).
# Chimera is waterbox-only: this host loads every core's core.wbx, so it ships
# with the frontend. It is self-contained C (no external deps, no guest
# toolchain), so we compile it directly here rather than driving miniBox's own
# meson build - the guest toolchain (musl) is a core-author kit, never built here.
# ---------------------------------------------------------------------------
minibox_host = repo_root / 'extern/chimera-common-minibox/source/host'
minibox_host_src = files(
  minibox_host / 'diag.c',
  minibox_host / 'sha256.c',
  minibox_host / 'memblock.c',
  minibox_host / 'tripguard.c',
  minibox_host / 'context.c',
  minibox_host / 'elf.c',
  minibox_host / 'fs.c',
  minibox_host / 'host.c',
  minibox_host / 'threading.c',
  minibox_host / 'cinterface.c',
  minibox_host / (host_windows ? 'pal_win.c' : 'pal_linux.c'),
)
# The machine's own code - the interop area and the guarded call - is written
# for the CPU the host runs on (as miniBox's source/host/meson.build selects it).
if host_machine.cpu_family() == 'aarch64'
  minibox_host_src += files(minibox_host / 'interop_aarch64.S', minibox_host / 'guarded_aarch64.S')
elif host_machine.cpu_family() == 'x86_64'
  minibox_host_src += files(minibox_host / 'interop_bin.c', minibox_host / 'guarded.S',
                            minibox_host / 'calling_convention_adapters.c')
else
  error('miniBox runs on x86-64 and aarch64 only, not ' + host_machine.cpu_family())
endif
# Provenance compiled into the host, matching what miniBox's own build stamps: the
# frontend shows it and a movie records it, so every binary in the emulation path can
# say where it came from. Inputs only, never a timestamp.
#
# Read at every build (vcs_tag), not when meson configured: a pull and a compile
# kept reporting the commit the build directory was set up at. "-dirty" marks a
# build of a tree with uncommitted changes. A pattern no tag has makes describe
# answer with the commit alone.
minibox_commit_h = vcs_tag(
  command: ['git', '-C', repo_root / 'extern/chimera-common-minibox', 'describe',
    '--always', '--abbrev=12', '--dirty', '--match=chimera-no-tag-has-this-name'],
  input: 'source/engine/source/build_commit.h.in',
  output: 'mb_build_commit.h',
  replace_string: '@COMMIT@',
  fallback: 'unknown',
)
minibox_build_os = run_command(
  'sh', '-c', '. /etc/os-release 2>/dev/null && printf "%s %s" "$ID" "$VERSION_ID" || uname -s',
  check: false).stdout().strip()

shared_library(
  'miniboxhost',
  minibox_host_src + [minibox_commit_h],
  name_prefix: 'lib',  # libminiboxhost.{so,dll} on both targets (the adapter resolves this name)
  include_directories: include_directories('extern/chimera-common-minibox/source/host'),
  c_args: [
    '-fno-strict-aliasing',
    '-include', meson.current_build_dir() / 'mb_build_commit.h',
    '-DMB_BUILD_COMMIT=CE_GENERATED_BUILD_COMMIT',
    '-DMB_BUILD_COMPILER="' + meson.get_compiler('c').get_id() + ' ' + meson.get_compiler('c').version() + '"',
    '-DMB_BUILD_OS="' + minibox_build_os + '"',
  ],
  dependencies: [dl_dep, thread_dep],
  gnu_symbol_visibility: 'default',
  install: true,
  install_dir: 'dll',
)

# ---------------------------------------------------------------------------
# libchimera: the engine, everything functional under Chimera's window. Its C
# ABI in engine/include/chimera/engine.h is a frozen spec.
# ---------------------------------------------------------------------------
engine_inc = include_directories('source/engine/include')
engine_src = files(
  'source/engine/source/engine.cpp',
  'source/engine/source/cache_bridge.cpp',
  'source/engine/source/movie_log.cpp',
  'source/engine/source/movie_text.cpp',
  'source/engine/source/state_container.cpp',
  'source/engine/source/state_format.cpp',
  'source/engine/source/zstd_dyn.cpp',
  'source/engine/source/sha1.cpp',
  'source/engine/source/progress.cpp',
  'source/engine/source/firmware.cpp',
  'source/engine/source/conditions.cpp',
  'source/engine/source/multifile.cpp',
  'source/engine/source/manifest_util.cpp',
  'source/engine/source/project.cpp',
  'source/engine/source/file_io.cpp',
  'source/engine/source/package.cpp',
  'source/engine/source/host_dyn.cpp',
  'source/engine/source/movie_entry.cpp',
  'source/engine/source/control_names.cpp',
  'source/engine/source/session.cpp',
  'source/engine/source/resampler.cpp',
  'source/engine/source/core_log.cpp',
  'source/engine/source/state_history.cpp',
  'source/engine/source/work_thread.cpp',
  'source/engine/source/media_maker.cpp',
  'source/engine/source/media_iso.cpp',
  'source/engine/source/media_fat12.cpp',
  'source/engine/source/gl_bridge.cpp',
  'source/engine/source/ram_search.cpp',
  'source/engine/source/average_fps.cpp',
  'source/engine/source/compile_sessions.cpp',
  'source/engine/source/host_machine.cpp',
  'source/engine/source/kept_pictures.cpp',
  'source/engine/source/game_properties.cpp',
  'source/engine/source/local_pipe.cpp',
  'source/engine/source/mcp.cpp',
  'source/engine/source/mcp_local.cpp',
  'source/engine/source/language.cpp',
  'extern/miniz/miniz.c',  # vendored (MIT): the zip half of the state container
  'extern/cjson/cJSON.c',  # vendored (MIT): bundle JSON
)
# Provenance: this repo's commit, since the engine lives in-tree. Inputs only.
# Read at every build, as the host's is above: it is in every state file the
# engine writes (state_format.cpp) and in ce_build_info.
engine_commit_h = vcs_tag(
  command: ['git', '-C', repo_root, 'describe',
    '--always', '--abbrev=12', '--dirty', '--match=chimera-no-tag-has-this-name'],
  input: 'source/engine/source/build_commit.h.in',
  output: 'ce_build_commit.h',
  replace_string: '@COMMIT@',
  fallback: 'unknown',
)
engine_src += [engine_commit_h]
# Chimera's language tables, one file per language beside the engine
subdir('languages')
engine_build_args = [
  '-I' + repo_root / 'extern/chimera-common-minibox/source/cache',
  '-include', meson.current_build_dir() / 'ce_build_commit.h',
  '-DCE_BUILD_COMMIT=CE_GENERATED_BUILD_COMMIT',
  # where the tree is, for the one test that reads it: test_state_format checks
  # the sandbox's own magic strings still say what the state layout claims
  '-DCE_REPO_ROOT="' + repo_root + '"',
  '-DCE_BUILD_COMPILER="' + meson.get_compiler('cpp').get_id() + ' ' + meson.get_compiler('cpp').version() + '"',
  '-DCE_BUILD_OS="' + minibox_build_os + '"',
  # miniz's zlib-name-compat macros (#define compress mz_compress) leak into
  # everything that includes it; we use the mz_ names
  '-DMINIZ_NO_ZLIB_COMPATIBLE_NAMES',
  # cJSON reads and writes numbers with strtod and sprintf, which use the
  # decimal separator of the process's locale. Built this way it asks the
  # locale what that is and translates, so JSON is always written and read
  # with a dot. A frontend may put the process in the user's locale (Qt
  # does); with a decimal comma, "0.5" in a core's declarations did
  # not parse and a fraction was written as "0,5" (issue 244).
  '-DENABLE_LOCALES',
  # the ce_ ABI is exported explicitly (see engine.h); cJSON must not export
  # its own symbols, which on mingw would also disable auto-exports
  '-DCHIMERA_BUILDING',
  '-DCJSON_HIDE_SYMBOLS',
]

# The GPU bridge (docs/gpu-bridge.md): a real GL context in the engine, so a
# sandboxed core's renderer can drive a GPU instead of a software rasteriser.
# Opt in - it is the one part of the engine that depends on the machine it runs
# on, and a core drawn this way is no longer deterministic.
gl_bridge = get_option('gl_bridge')
gl_bridge_src = []
gl_bridge_deps = []
if gl_bridge
  # Both halves of the bridge are generated from miniBox's master list, which
  # is where the numbering lives so that every core and this host agree.
  gl_bridge_gen = custom_target('ce-gl-bridge',
    output: ['gl-bridge-host.inc', 'gl-bridge-ops.h', 'gl-bridge-guest.cpp'],
    command: [find_program('python3'),
      repo_root / 'extern/chimera-common-minibox/source/gl/gen-gl-bridge.py',
      repo_root / 'extern/glad/include/glad/gl.h',
      repo_root / 'extern/chimera-common-minibox/source/gl/gl-entry-points.txt',
      '@OUTDIR@'],
    # the list and the generator are inputs: without saying so, appending an
    # entry point leaves the host still answering the old numbering
    depend_files: [
      files('extern/chimera-common-minibox/source/gl/gl-entry-points.txt'),
      files('extern/chimera-common-minibox/source/gl/gen-gl-bridge.py'),
      # and the header it reads the signatures from: a glad bump otherwise
      # left the old generated code in an existing build directory
      files('extern/glad/include/glad/gl.h'),
    ])

  gl_bridge_src = [gl_bridge_gen[0], files('extern/glad/src/gl.c')]
  engine_build_args += ['-DCE_GL_BRIDGE',
    '-I' + repo_root / 'extern/glad/include',
    '-I' + repo_root / 'extern/chimera-common-minibox/source/gl',
    '-I' + meson.current_build_dir()]

  if host_windows
    gl_bridge_src += files('extern/glad/src/wgl.c')
    gl_bridge_deps = [meson.get_compiler('cpp').find_library('opengl32', required: true)]
  else
    gl_bridge_deps = [meson.get_compiler('cpp').find_library('EGL', required: true)]
  endif

  # The Vulkan bridge, for a core whose renderer has no OpenGL. Its host half
  # is miniBox's own file - one implementation for the frontend and for a
  # core's test runner - with its dispatch generated here from the registry
  # miniBox carries. It links against nothing: the machine's Vulkan loader is
  # looked for when a core asks, so a machine without one runs everything
  # else.
  vk_bridge_dir = repo_root / 'extern/chimera-common-minibox/source/vk'
  vk_headers_dir = repo_root / 'extern/chimera-common-minibox/extern/vulkan-headers'
  vk_bridge_gen = custom_target('ce-vk-bridge',
    output: ['vk-bridge-ops.h', 'vk-bridge-guest.c', 'vk-bridge-host.inc'],
    command: [find_program('python3'),
      vk_bridge_dir / 'gen-vk-bridge.py',
      vk_headers_dir / 'registry/vk.xml',
      vk_bridge_dir / 'vk-entry-points.txt',
      '@OUTDIR@'],
    depend_files: [
      files('extern/chimera-common-minibox/source/vk/vk-entry-points.txt'),
      files('extern/chimera-common-minibox/source/vk/gen-vk-bridge.py'),
      files('extern/chimera-common-minibox/extern/vulkan-headers/registry/vk.xml'),
    ])
  gl_bridge_src += [vk_bridge_gen[0], vk_bridge_gen[2],
    files('extern/chimera-common-minibox/source/vk/vk-host.c')]
  engine_build_args += ['-DCE_VK_BRIDGE',
    '-I' + vk_bridge_dir,
    '-I' + vk_headers_dir / 'include']
endif
# The engine is compiled ONCE, into a static library, and that is linked
# whole into libchimera, the tools and every test. Each of them used to list
# the engine's sources as its own, so a full build compiled every one of them
# 29 times (about 2300 s of CPU, 6.4 s for each copy of session.cpp).
# link_whole keeps every object, as compiling them in did: nothing that only
# registers itself is dropped by the linker.
engine_generated = [engine_commit_h]   # headers the engine's users include too
if gl_bridge
  engine_generated += [gl_bridge_gen[1], vk_bridge_gen[0]]
endif
engine_lib = static_library(
  'chimera_engine',
  engine_src + gl_bridge_src,
  include_directories: engine_inc,
  cpp_args: engine_build_args,
  c_args: engine_build_args,
  dependencies: [dl_dep] + gl_bridge_deps,
  pic: true,
  gnu_symbol_visibility: 'default',
)
engine_dep = declare_dependency(
  link_whole: engine_lib,
  sources: engine_generated,
  include_directories: engine_inc,
  dependencies: [dl_dep] + gl_bridge_deps,  # zstd_dyn loads libzstd from beside the library
)
chimera_shared = shared_library(
  'chimera',
  name_prefix: 'lib',  # libchimera.{so,dll} on both targets (the frontend resolves this name)
  dependencies: [engine_dep],
  gnu_symbol_visibility: 'default',
  install: true,
  install_dir: 'dll',
)
executable(
  'chimera-run',
  ['source/engine/tools/chimera-run.cpp'],
  include_directories: engine_inc,
  cpp_args: engine_build_args,
  c_args: engine_build_args,
  dependencies: [engine_dep],
  # CommandLineToArgvW: the tool takes its arguments from the wide command line on Windows
  link_args: host_machine.system() == 'windows' ? ['-lshell32'] : [],
  install: true,
  install_dir: 'dll',  # beside libminiboxhost + libzstd, which it loads from its own directory
)
# The program an AI agent's program starts to reach a running Chimera
# (docs/mcp-server.md). It talks on its standard input and output and passes
# each call to Chimera over a local pipe; it opens no socket.
executable(
  'chimera-mcp',
  ['source/engine/tools/chimera-mcp.cpp'],
  include_directories: engine_inc,
  cpp_args: engine_build_args,
  c_args: engine_build_args,
  dependencies: [engine_dep],
  # on Windows the compiler's runtime is linked in: the agent's program starts
  # it from wherever it is, with no folder of Chimera's DLLs on its PATH
  link_args: host_machine.system() == 'windows' ? ['-static'] : [],
  install: true,
  install_dir: 'dll',
)

if meson.is_cross_build()
  # The one unit test that is also built for Windows, by hand
  # (meson compile -C build/meson-windows test_mcp): the pipe under the MCP
  # server is different code on each system, and the Windows half can be run
  # from Linux through WSL. The other tests are built for the host only.
  executable(
    'test_mcp',
    ['source/engine/tests/test_mcp.cpp'],
    include_directories: engine_inc,
    cpp_args: engine_build_args + ['-UNDEBUG'],
    c_args: engine_build_args,
    dependencies: [engine_dep],
    build_by_default: false,
  )
  # The same by hand for the files under a folder whose name is not plain
  # ASCII (meson compile -C build/meson-windows test_utf8_paths): the C
  # library reads such a path differently on Windows, so it is proved there.
  # The same for the stubs a call into the guest takes on Windows
  # (meson compile -C build/meson-windows test_guest_calls; run it with
  # libminiboxhost.dll beside it): on Linux a guest call needs none.
  executable(
    'test_guest_calls',
    ['source/engine/tests/test_guest_calls.cpp'],
    include_directories: engine_inc,
    cpp_args: engine_build_args + ['-UNDEBUG'],
    c_args: engine_build_args,
    dependencies: [engine_dep],
    build_by_default: false,
  )
  # And for the GL bridge's context on Windows (WGL; run it on a machine with a
  # GPU): a session's objects go with it.
  executable(
    'test_gl_session',
    ['source/engine/tests/test_gl_session.cpp'],
    include_directories: engine_inc,
    cpp_args: engine_build_args + ['-UNDEBUG'],
    c_args: engine_build_args,
    dependencies: [engine_dep],
    build_by_default: false,
  )
  executable(
    'test_utf8_paths',
    ['source/engine/tests/test_utf8_paths.cpp'],
    include_directories: engine_inc,
    cpp_args: engine_build_args + ['-UNDEBUG'],
    c_args: engine_build_args,
    dependencies: [engine_dep],
    build_by_default: false,
  )
endif

if not meson.is_cross_build()
  # Not a test: what a captured frame costs when the budget is full and
  # stretches are going to disk, in line and on the writer. Built with the
  # tests because it needs the engine; run by tests/perf/run-spillbench.sh.
  executable(
    'spillbench',
    ['tests/perf/spillbench.cpp'],
    include_directories: engine_inc,
    cpp_args: engine_build_args,
    c_args: engine_build_args,
    dependencies: [engine_dep],
    build_by_default: false,
  )

  # no text written in the code: every text a person reads is a key in the
  # language tables (docs/translations.md, owner 2026-10-28)
  test('texts', find_program('python3'), args: [files('tools/check-texts.py'), meson.project_source_root()])
  # the synthetic core's: every key its code returns is in its table
  test('synth_texts', find_program('python3'),
    args: [files('tools/check-core-texts.py'), meson.project_source_root() / 'tests/synth/package-box'])

  foreach engine_test : ['sha1', 'movie_log', 'movie_text', 'movie_entry', 'control_names', 'state_container', 'state_format', 'package', 'multifile', 'project', 'firmware', 'state_history', 'media', 'gl_flight', 'gl_sync', 'gl_buffer_pool', 'stride_tuner', 'plain_numbers', 'greenzone_shape', 'ram_search', 'kept_pictures', 'game_properties', 'mcp', 'average_fps', 'compile_sessions', 'host_machine', 'utf8_paths', 'work_thread', 'guest_calls', 'gl_session', 'resampler', 'language']
    test(
      'engine_' + engine_test,
      executable(
        'test_' + engine_test,
        ['source/engine/tests/test_' + engine_test + '.cpp'],
        include_directories: engine_inc,
        cpp_args: engine_build_args + ['-UNDEBUG'],  # these tests ARE asserts
        c_args: engine_build_args,
        dependencies: [engine_dep],
      ),
      # (the state container test finds libzstd the way the shipped library
      # does: beside its own binary, which in the build dir it is)
      # RAM search takes 9 s here and 28 to 30 s on a CI runner
      timeout: engine_test == 'ram_search' ? 120 : 30,
    )
  endforeach
endif

# Chimera's window (Qt 6.12, issue 142): source/ui/meson.build
if get_option('qt_ui').allowed()
  subdir('source/ui')
endif
