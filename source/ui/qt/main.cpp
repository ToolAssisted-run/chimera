/* Chimera's window (issue 142): the main window over the
 * session and piano roll view models. Everything a window does here goes
 * through the view models' registries, so a script could do the same.
 *
 *   chimera [<project> [<package>] | --project <project> [--core <package>]]
 *           [--fill <frames>] [--play] [--measure <seconds>] [--scroll]
 *           [--screenshot <path>] [--lua <script>] [--describe]
 *
 * --measure runs for that long and prints what it measured, then quits:
 * frames the machine ran, pictures drawn, the window's frame intervals and
 * TAStudio's, and how long each piano roll paint took (with --scroll, while
 * it scrolled). */

#include <QSysInfo>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QPointer>
#include <QClipboard>
#include <QBuffer>
#include <QSurfaceFormat>
#include <QImage>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlError>
#include <QQmlContext>
#include <QQuickStyle>
#include <QProcess>
#include <QFileInfo>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QPalette>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#ifdef _WIN32
#include <QtGui/qpa/qplatformwindow_p.h>
#include <windows.h>
#endif
#include <mutex>
#include <map>
#include <filesystem>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "chimera/engine.h"
#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/tr.hpp"
#include "chimera/vm/project_details_vm.hpp"
#include "chimera/vm/config_store.hpp"
#include "chimera/vm/crash_notes.hpp"
#include "chimera/vm/help_vm.hpp"
#include "chimera/vm/mcp_vm.hpp"
#include "chimera/vm/media_maker_vm.hpp"
#include "chimera/vm/cache_manager_vm.hpp"
#include "chimera/vm/customize_vm.hpp"
#include "chimera/vm/messages_vm.hpp"
#include "chimera/vm/display_vm.hpp"
#include "chimera/vm/paths_vm.hpp"
#include "chimera/vm/firmware_vm.hpp"
#include "chimera/vm/config_files_vm.hpp"
#include "chimera/vm/data_folder_vm.hpp"
#include "chimera/vm/precompiled_vm.hpp"
#include "chimera/vm/trace_logger_vm.hpp"
#include "chimera/vm/idle_eye.hpp"
#include "chimera/vm/surface_viewer_vm.hpp"
#include "chimera/vm/macro_tool_vm.hpp"
#include "chimera/vm/encoder_vm.hpp"
#include "chimera/vm/host_input_vm.hpp"
#include "chimera/vm/game_vm.hpp"
#include "chimera/vm/forms_vm.hpp"
#include "chimera/vm/comm_vm.hpp"
#include "chimera/vm/sql_vm.hpp"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/tool_windows_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/hex_editor_vm.hpp"
#include "chimera/vm/controls_vm.hpp"
#include "chimera/vm/hotkeys_vm.hpp"
#include "chimera/vm/themes_vm.hpp"
#include "theme_palette.hpp"
#include "translations.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "clipboard_relay.hpp"
#include "picture_files.hpp"
#include "overlay_painter.hpp"
#include "pointer_capture.hpp"
#include "portal_file_chooser.hpp"
#include "system_file_dialog.hpp"
#include "file_places.hpp"
#include "button_router.hpp"
#include "jump_list.hpp"
#include "single_instance.hpp"
#include "headless_run.hpp"
#include "key_filter.hpp"
#include "key_names.hpp"
#include "menu_letters.hpp"
#include "picture_item.hpp"
#include "piano_roll_item.hpp"
#include "qml_view_model.hpp"

namespace {

using namespace chimera;

/* Posts work into Qt's event loop: a view model's thread-hopping, from any thread. */
class QtDispatcher : public vm::Dispatcher
{
public:
	explicit QtDispatcher(QObject *context) : context_(context) {}
	void post(std::function<void()> fn) override
	{
		QMetaObject::invokeMethod(context_, std::move(fn), Qt::QueuedConnection);
	}

private:
	QObject *context_;
};

#ifdef _WIN32
/* A crash names the DLL it happened in and where, on stderr. */
LONG WINAPI crashNote(EXCEPTION_POINTERS *e)
{
	void *at = e->ExceptionRecord->ExceptionAddress;
	HMODULE mod = nullptr;
	wchar_t name[MAX_PATH] = L"?";
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			static_cast<LPCWSTR>(at), &mod))
		GetModuleFileNameW(mod, name, MAX_PATH);
	std::fprintf(stderr, "crash: exception %08lx at %ls+0x%llx (thread %lu)\n", e->ExceptionRecord->ExceptionCode, name,
		static_cast<unsigned long long>(static_cast<char *>(at) - reinterpret_cast<char *>(mod)), GetCurrentThreadId());
	std::fflush(stderr);
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

struct Spread
{
	double median = 0, p95 = 0, worst = 0;
};

Spread spread(std::vector<double> v)
{
	Spread s;
	if (v.empty()) return s;
	std::sort(v.begin(), v.end());
	s.median = v[v.size() / 2];
	s.p95 = v[std::min(v.size() - 1, v.size() * 95 / 100)];
	s.worst = v.back();
	return s;
}

/* the picture of the controller a package ships, for Controller Config */
class ControllerPicture : public QQuickImageProvider
{
public:
	ControllerPicture() : QQuickImageProvider(QQuickImageProvider::Image) {}
	QImage requestImage(const QString &, QSize *size, const QSize &) override
	{
		std::lock_guard<std::mutex> lock(mutex_);
		QImage image = QImage::fromData(bytes_);
		if (size != nullptr) *size = image.size();
		return image;
	}
	void set(const std::string &bytes)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		bytes_ = QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
	}

private:
	std::mutex mutex_;
	QByteArray bytes_;
};

} // namespace

#ifdef _WIN32
#define EXE_SUFFIX ".exe"
#else
#define EXE_SUFFIX ""
#endif
/* a program that ships with the window: in the bundle's bin folder, beside
 * the window, or, in a build, where the build puts it; "" when none is */
static QString besideProgram(const QString &name)
{
	const QString here = QCoreApplication::applicationDirPath(), exe = name + QStringLiteral(EXE_SUFFIX);
	for (const QString &dir : {here + QStringLiteral("/bin"), here, here + QStringLiteral("/../..")})
		if (QFileInfo::exists(dir + QStringLiteral("/") + exe)) return QFileInfo(dir + QStringLiteral("/") + exe).absoluteFilePath();
	return QString();
}
/* the headless runner the window asks a core's questions with (CHIMERA_RUN names another) */
static QString runnerProgram()
{
	const QString named = qEnvironmentVariable("CHIMERA_RUN");
	return named.isEmpty() ? besideProgram(QStringLiteral("chimera-run")) : named;
}

/* the first input roll, found among what the windows draw: TAStudio's rolls
 * are made by a Repeater, and those are not among a window's QObject children */
static qt::PianoRollItem *firstRoll()
{
	std::function<qt::PianoRollItem *(QQuickItem *)> walk = [&walk](QQuickItem *item) -> qt::PianoRollItem * {
		auto *roll = qobject_cast<qt::PianoRollItem *>(item);
		if (roll != nullptr && roll->objectName() == QStringLiteral("pianoRoll")) return roll;
		for (QQuickItem *child : item->childItems())
			if (qt::PianoRollItem *found = walk(child)) return found;
		return nullptr;
	};
	for (QWindow *w : QGuiApplication::topLevelWindows())
		if (auto *q = qobject_cast<QQuickWindow *>(w))
			if (qt::PianoRollItem *found = walk(q->contentItem())) return found;
	return nullptr;
}

/* on Windows the window is a DLL in lib\, started by launcher.c */
#ifdef CHIMERA_QT_LIBRARY
extern "C" __declspec(dllexport) int chimera_qt_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
#ifdef _WIN32
	SetUnhandledExceptionFilter(crashNote);
#endif
	/* the build, said before Qt starts: it needs no display, and none may be there */
	for (int i = 1; i < argc; i++)
		if (std::strcmp(argv[i], "--version") == 0)
		{
			std::printf("Commit %s\n", vm::HelpViewModel::thisCommit().c_str());
			return 0;
		}
	/* a run nobody watches is drawn off screen, so it needs no display */
	const bool headless = qt::HeadlessRun::askedFor(argc, argv);
	if (headless) qt::HeadlessRun::prepare();
	/* On Wayland, Qt's text-input code logs "Trying to disable <surface> but 0x0
	 * is focused" whenever a window gives up focus it never held. It is Qt's
	 * own bookkeeping and changes nothing, so that one category is muted unless
	 * the person set their own logging rules. */
	if (!qEnvironmentVariableIsSet("QT_LOGGING_RULES"))
		QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.wayland.textinput.warning=false"));
	QGuiApplication app(argc, argv);
	/* Qt takes the person's locale for the C library too, and with a decimal
	 * comma printf("%g") writes "0,5" into files read as "0.5": numbers stay
	 * the C locale's, as Chimera's runtime keeps them (chimera issue 244) */
	std::setlocale(LC_NUMERIC, "C");
	/* Chimera's name: its settings and caches are kept under it (owner,
	 * 2026-10-26; what the window kept under its old name stays there) */
	QGuiApplication::setApplicationName(QStringLiteral("Chimera"));
#ifdef _WIN32
	/* a crash no handler sees still leaves a note (docs/project.md, "Crash
	 * notes"): armed before anything else is read, with the data folder's
	 * place as it is at first, and pointed at the one in use once known */
	{
		const QString here = QCoreApplication::applicationDirPath();
		QString moduleFolder;
		for (const QString &dir : {here + QStringLiteral("/lib"), here, here + QStringLiteral("/../..")})
			if (QFileInfo::exists(dir + QStringLiteral("/chimera_crash.dll")))
			{
				moduleFolder = QFileInfo(dir).absoluteFilePath();
				break;
			}
		const QString env = qEnvironmentVariable("CHIMERA_DATA_HOME");
		const QString data = !env.trimmed().isEmpty() ? QFileInfo(env).absoluteFilePath()
													   : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
		if (moduleFolder.isEmpty()) std::fprintf(stderr, "[crash] crash notes are off: chimera_crash.dll is not beside the program\n");
		else if (const std::string why = vm::crash::arm(moduleFolder.toStdString(), (data + QStringLiteral("/Crashes")).toStdString()); !why.empty())
			std::fprintf(stderr, "[crash] crash notes are off: %s\n", why.c_str());
	}
#endif
	/* Fusion, with Chimera's check boxes and radio buttons and scroll bars always seen (style/, built in) */
	QQuickStyle::setStyle(QStringLiteral("ChimeraStyle"));
#ifndef _WIN32
	/* Chimera's icon on every window; on Windows it is the program's own (chimera.rc), at every size */
	QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/chimera/chimera.png")));
#endif

	QCommandLineParser args;
	args.addHelpOption();
	args.addPositionalArgument(QStringLiteral("project"), QStringLiteral("a project to open (a game file alone starts New Project from it)"));
	args.addPositionalArgument(QStringLiteral("package"), QStringLiteral("the core package the project runs on, when it is not installed"));
	QCommandLineOption fillOpt(QStringLiteral("fill"), QStringLiteral("make the inputs this many frames long"), QStringLiteral("frames"));
	QCommandLineOption playOpt(QStringLiteral("play"), QStringLiteral("start running once open"));
	QCommandLineOption measureOpt(QStringLiteral("measure"), QStringLiteral("measure for this many seconds, print, quit"), QStringLiteral("seconds"));
	QCommandLineOption scrollOpt(QStringLiteral("scroll"), QStringLiteral("while measuring, scroll the piano roll all the time"));
	QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("when measuring ends, save the window as this picture"), QStringLiteral("path"));
	QCommandLineOption luaOpt(QStringLiteral("lua"), QStringLiteral("path; Lua script or Console session to load; implies --luaconsole"), QStringLiteral("script"));
	/* Chimera's own options, so what starts Chimera starts this window */
	QCommandLineOption projectOpt(QStringLiteral("project"), QStringLiteral("path; chimera project to open on launch"), QStringLiteral("path"));
	QCommandLineOption coreOpt(QStringLiteral("core"), QStringLiteral("path of a core package to load the project with, when it is not installed"), QStringLiteral("path"));
	QCommandLineOption configOpt(QStringLiteral("config"), QStringLiteral("path of config file to use"), QStringLiteral("path"));
	QCommandLineOption firmwareOpt(QStringLiteral("firmware"), QStringLiteral("<id>=<path>; use this file as the firmware the core declares under <id>, "
		"ahead of anything remembered (by its hash, where the project pins one). May be repeated"), QStringLiteral("id=path"));
	QCommandLineOption fullscreenOpt(QStringLiteral("fullscreen"), QStringLiteral("launch in fullscreen"));
	QCommandLineOption chromelessOpt(QStringLiteral("chromeless"), QStringLiteral("never show the menu, status bar or window frame, not even in windowed mode"));
	QCommandLineOption luaConsoleOpt(QStringLiteral("luaconsole"), QStringLiteral("open the Lua Console, even if not loading a script"));
	QCommandLineOption versionOpt(QStringLiteral("version"), QStringLiteral("print version information and immediately exit"));
	QCommandLineOption headlessOpt(QStringLiteral("headless"), QStringLiteral("for a run nobody watches: no window is shown, and what would "
		"wait for a person ends the run, said on stderr (a dialog: exit 64; the core stopped: 65, the work kept), as Chimera's"));
	QCommandLineOption userdataOpt(QStringLiteral("userdata"), QStringLiteral("Lua's userdata to start with, as Chimera's: k1:v1;k2:v2 "
		"(true/false a boolean, a whole number or a number as one, else text)"), QStringLiteral("pairs"));
	QCommandLineOption mmfOpt(QStringLiteral("mmf"), QStringLiteral("the memory-mapped file Lua's comm.mmfScreenshot writes to, as Chimera's --mmf"),
		QStringLiteral("name"));
	QCommandLineOption schemeOpt(QStringLiteral("color-scheme"), QStringLiteral("light or dark: Chimera's built-in Light or Dark theme for this run (not remembered)"), QStringLiteral("scheme"));
	QCommandLineOption showOpt(QStringLiteral("show"), QStringLiteral("open a tool window at the start: new-project, movie-import, ram-search, ram-watch, hex, controls, hotkeys, themes, lua, tastudio, undo-history, project-details, comments, subtitles, tastudio-settings, log, report, media, cache, customize, messages, sound, display, budgets, paths, firmware, data, precompiled, trace, debugger, surfaces, macros, encode, cores"), QStringLiteral("tool"));
	QCommandLineOption describeOpt(QStringLiteral("describe"), QStringLiteral("print every view model's properties and commands as JSON, and quit"));
	/* Chimera's --mcp-server and --mcp-allow-changes: the server started for a run nobody clicks in */
	QCommandLineOption mcpServerOpt(QStringLiteral("mcp-server"), QStringLiteral("start the MCP server (Tools > MCP Server...) at once"));
	QCommandLineOption mcpChangesOpt(QStringLiteral("mcp-allow-changes"), QStringLiteral("with --mcp-server: the agent may make changes, for this run"));
	args.addOptions({fillOpt, playOpt, measureOpt, scrollOpt, shotOpt, luaOpt, mmfOpt, userdataOpt, describeOpt, schemeOpt, showOpt, mcpServerOpt,
		mcpChangesOpt, projectOpt, coreOpt, configOpt, firmwareOpt, fullscreenOpt, chromelessOpt, luaConsoleOpt, versionOpt, headlessOpt});
	args.process(app);
	/* what is opened: a project (named alone, or with --project) and its core
	 * package (named after it, or with --core), or a game, which starts New
	 * Project from it - a game runs only in a project (owner, 2026-10-26) */
	const QStringList named = args.positionalArguments();
	auto isProject = [](const QString &f) { return f.endsWith(QStringLiteral(".chimeraProject"), Qt::CaseInsensitive); };
	if (named.size() > 2 || (named.size() == 2 && (!isProject(named[0]) || !named[1].endsWith(QStringLiteral(".chimeraCore"), Qt::CaseInsensitive))) ||
		(args.isSet(projectOpt) && !named.isEmpty() && isProject(named[0])))
	{
		std::fprintf(stderr, "chimera: a game opens only in a project: name a .chimeraProject, and its core package (.chimeraCore) "
							 "when it is not installed\n");
		return 2;
	}
	/* as full paths: a script's code runs in its own folder (Lua's working
	 * folder is the script's, as Chimera's), and the machine reads these on
	 * a thread of its own */
	auto full = [](const QString &f) { return f.isEmpty() ? QString() : QFileInfo(f).absoluteFilePath(); };
	const QString projectGiven = full(args.isSet(projectOpt) ? args.value(projectOpt) : !named.isEmpty() && isProject(named[0]) ? named[0] : QString());
	const QString packageGiven = full(args.isSet(coreOpt) ? args.value(coreOpt) : named.size() == 2 ? named[1] : QString());
	const QString gameGiven = full(!named.isEmpty() && !isProject(named[0]) ? named[0] : QString());
	const bool anyProject = !projectGiven.isEmpty();
	/* a game alone starts New Project, which waits for a person: a run nobody
	 * watches refuses it at once, as Chimera's --headless does */
	if (headless && !anyProject && !gameGiven.isEmpty())
	{
		std::fprintf(stderr, "chimera: a game opens only in a project: name a .chimeraProject, and its core package (.chimeraCore) "
							 "when it is not installed\n");
		return 2;
	}
	/* firmware for this run: <id>=<path>, a pair without an "=" passed over, as Chimera's */
	std::map<std::string, std::string> runFirmware;
	for (const QString &pair : args.values(firmwareOpt))
	{
		const qsizetype eq = pair.indexOf('=');
		if (eq <= 0 || eq == pair.size() - 1) continue;
		runFirmware[pair.left(eq).toStdString()] = QFileInfo(pair.mid(eq + 1)).absoluteFilePath().toStdString();
	}
	/* what the window remembers, in a file of its own (--config, as Chimera's;
	 * CHIMERA_QT_CONFIG for a test): beside the program, so each Chimera
	 * folder keeps its own settings (owner, 2026-10-29, issue 403), else,
	 * when the program's folder cannot be written, in the system's folder
	 * for this person's settings */
	auto settingsBesideProgram = [] {
		const QString folder = QCoreApplication::applicationDirPath();
		const QString path = folder + QStringLiteral("/settings.json");
		if (QFileInfo::exists(path)) return path;
		QTemporaryFile probe(QDir(folder).filePath(QStringLiteral("XXXXXX")));
		return probe.open() ? path : QString();
	};
	const QString configPath = args.isSet(configOpt) ? QFileInfo(args.value(configOpt)).absoluteFilePath()
		: qEnvironmentVariableIsSet("CHIMERA_QT_CONFIG") ? qEnvironmentVariable("CHIMERA_QT_CONFIG")
		: [&] {
			const QString beside = settingsBesideProgram();
			return !beside.isEmpty() ? beside : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/settings.json");
		}();
	/* Chimera's single instance mode (Windows): with a window of this
	 * program already running, the files named are handed to it and this
	 * one ends before anything is made */
	qt::SingleInstance single;
	{
		/* the project or game first, its package after it, as the window takes them */
		QStringList handing;
		if (anyProject) handing << projectGiven;
		else if (!gameGiven.isEmpty()) handing << gameGiven;
		if (!handing.isEmpty() && !packageGiven.isEmpty()) handing << packageGiven;
		/* a run nobody watches is its own, whatever else is running */
		if (!headless && vm::ConfigStore::switchIn(configPath.toStdString(), "customize.singleInstance", false) && single.forwardIfRunning(handing))
			return 0;
	}
	/* Chimera's GameCube adapter support: off, SDL is told before it starts
	 * to leave USB devices alone (SDL2 reads it only from the environment) */
	if (!vm::ConfigStore::switchIn(configPath.toStdString(), "customize.gameCubeAdapter", false))
		qputenv("SDL_HIDAPI_DISABLE_LIBUSB", "1");
	/* the language first: the view models' first words are in it (docs/translations.md) */
	qt::useLanguage(app, qt::startLanguage(QString::fromStdString(vm::ConfigStore::textIn(configPath.toStdString(), "customize.language", ""))));
	QtDispatcher dispatcher(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(dispatcher, inputs);
	qt::HeadlessRun headlessRun(headless, session);
	vm::PianoRollViewModel pianoRoll(dispatcher, inputs, session);
	vm::RamSearchViewModel ramSearch(dispatcher, session);
	vm::RamWatchViewModel ramWatch(dispatcher, session);
	ramSearch.setRamWatch(&ramWatch);
	vm::HexEditorViewModel hexEditor(dispatcher, session);
	hexEditor.setRamWatch(&ramWatch);
	vm::BranchesViewModel branches(dispatcher, inputs, session, pianoRoll);
	vm::ProjectDetailsViewModel projectDetails(dispatcher, session);
	vm::ControlsViewModel controls(dispatcher, session, &pianoRoll);
	vm::ThemesViewModel themes(dispatcher);
	vm::Connection themesWatch;
	/* Chimera's hotkeys, with its keys and in its order, for what this window
	 * has: each one the hotkeys' to bind. What a menu or a dialog does is the
	 * window's, and the relay hands it over; the rest is done here. */
	vm::HotkeysViewModel hotkeys(dispatcher);
	vm::LuaConsoleViewModel *luaConsoleOf = nullptr;
	qt::HotkeyRelay relay;
	/* the main window, once it is made: the screen copied from it */
	QQuickWindow *mainWindow = nullptr;
	/* a screenshot to a file ("" for the usual name), and the picture to the
	 * clipboard: the hotkeys' and the scripts' (client.screenshot) */
	std::function<void(const QString &)> takeScreenshot;
	std::function<void()> rawToClipboard;
	{
		auto run = [](vm::ViewModel &vm, std::string command, vm::Args args = {}) {
			std::string e;
			vm.run(command, args, e);
		};
		auto window = [&hotkeys, &relay](const std::string &group, const std::string &name, const std::string &keys) {
			hotkeys.add(group, name, keys, [&relay, name](bool down) {
				if (down) relay.press(name);
			});
		};
		hotkeys.add("General", "Frame Advance", "F", [&session](bool down) { session.holdFrameAdvance(down); });
		hotkeys.add("General", "Rewind", "Shift+R", [&session](bool down) { session.holdRewind(down); });
		hotkeys.add("General", "Pause", "Pause", [&session, run](bool down) {
			if (down) run(session, session.running.get() ? "pause" : "play");
		});
		hotkeys.add("General", "Fast Forward", "Tab", [&session](bool down) { session.holdFastForward(down); });
		/* held: as fast as it goes, nothing drawn or heard */
		hotkeys.add("General", "Turbo", "Shift+Tab", [&session](bool down) { session.holdTurbo(down); });
		hotkeys.add("General", "Toggle Throttle", "", [&session, run](bool down) {
			if (down) run(session, "toggleThrottle");
		});
		/* what is on screen, through the window: the picture area as drawn,
		 * with the counters and messages over it (Capture OSD), or the picture
		 * alone; the grab comes back later */
		auto grabShown = [&mainWindow](bool withOsd, std::function<void(const QImage &)> then) {
			if (mainWindow == nullptr) return false;
			auto *item = mainWindow->findChild<QQuickItem *>(withOsd ? QStringLiteral("pictureArea") : QStringLiteral("picture"));
			if (item == nullptr || item->width() <= 0 || item->height() <= 0) return false;
			const QSharedPointer<QQuickItemGrabResult> grab = item->grabToImage();
			if (grab.isNull()) return false;
			QObject::connect(grab.data(), &QQuickItemGrabResult::ready, grab.data(), [grab, then] { then(grab->image().convertToFormat(QImage::Format_RGB32)); });
			return true;
		};
		/* the picture on screen as a PNG, named as Chimera names one: the
		 * game, the date and time, and a number when that is taken */
		auto saveScreenshot = [&session](const QImage &picture) {
			QDir().mkpath(qt::PictureFiles::screenshotFolder());
			const QString base = qt::PictureFiles::screenshotBase(QString::fromStdString(session.gameName.get()));
			/* the bare name, unless a (0) is there; a bare one already there becomes (0) */
			QString path = base + QStringLiteral(" (0).png");
			if (!QFileInfo::exists(path))
			{
				if (QFileInfo::exists(base + QStringLiteral(".png"))) QFile::rename(base + QStringLiteral(".png"), path);
				else path = base + QStringLiteral(".png");
			}
			for (int n = 0; QFileInfo::exists(path); n++) path = base + QStringLiteral(" (%1).png").arg(n);
			if (picture.save(path, "PNG")) session.status.set(vm::tr("screenshot.saved", {QFileInfo(path).fileName().toStdString()}));
			else session.status.set(vm::tr("screenshot.not_written", {path.toStdString()}));
		};
		auto rawPicture = [&session](QImage &out) {
			vm::Picture::Frame f;
			if (!session.screen.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0) return false;
			out = QImage(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32).copy();
			return true;
		};
		/* to a file named, as Chimera does: its folder made,
		 * a JPEG when it is named .jpg, else a PNG */
		takeScreenshot = [&session, grabShown, saveScreenshot, rawPicture](const QString &path) {
			std::function<void(const QImage &)> save = saveScreenshot;
			if (!path.isEmpty())
				save = [&session, path](const QImage &picture) {
					QDir().mkpath(QFileInfo(path).absolutePath());
					const bool jpeg = QFileInfo(path).suffix().compare(QStringLiteral("jpg"), Qt::CaseInsensitive) == 0;
					if (picture.save(path, jpeg ? "JPG" : "PNG")) session.status.set(vm::tr("screenshot.saved", {QFileInfo(path).fileName().toStdString()}));
					else session.status.set(vm::tr("screenshot.not_written", {path.toStdString()}));
				};
			QImage picture;
			if (session.captureOsd.get() && grabShown(true, save)) return;
			if (!rawPicture(picture))
			{
				session.status.set(vm::tr("screenshot.no_picture"));
				return;
			}
			save(picture);
		};
		hotkeys.add("General", "Screenshot", "F12", [&takeScreenshot](bool down) {
			if (down) takeScreenshot(QString());
		});
		/* the picture as the machine made it (with what is over it, at the
		 * window's size, when Capture OSD is on), or the picture area as the
		 * window shows it, to the clipboard, as Chimera's */
		rawToClipboard = [&session, grabShown, rawPicture] {
			auto done = [&session](const QImage &image) {
				QGuiApplication::clipboard()->setImage(image);
				session.status.set(vm::tr("screenshot.raw_to_clipboard"));
			};
			QImage picture;
			if (session.captureOsd.get() && grabShown(true, done)) return;
			if (rawPicture(picture)) done(picture);
		};
		hotkeys.add("General", "Screen Raw to Clipboard", "", [&rawToClipboard](bool down) {
			if (down) rawToClipboard();
		});
		hotkeys.add("General", "Screen Client to Clipboard", "Ctrl+Shift+C", [&session, grabShown](bool down) {
			if (!down) return;
			grabShown(session.captureOsd.get(), [&session](const QImage &image) {
				QGuiApplication::clipboard()->setImage(image);
				session.status.set(vm::tr("screenshot.client_to_clipboard"));
			});
		});
		window("General", "Toggle Menu", "");
		window("General", "Toggle Stay on Top", "");
		window("General", "Full Screen", "Alt+Enter");
		hotkeys.add("General", "Autofire", "", [&session](bool down) { session.armAutofire(down); });
		hotkeys.add("General", "Autohold", "", [&session](bool down) { session.armAutohold(down); });
		hotkeys.add("General", "Clear Autohold", "", [&session, run](bool down) {
			if (down) run(session, "clearAutohold");
		});
		auto flip = [](vm::Property<bool> &p) { p.set(!p.get()); };
		hotkeys.add("General", "Display FPS", "", [&session, flip](bool down) {
			if (down) flip(session.showFps);
		});
		hotkeys.add("General", "Frame Counter", "", [&session, flip](bool down) {
			if (down) flip(session.showFrameCounter);
		});
		hotkeys.add("General", "Lag Counter", "", [&session, flip](bool down) {
			if (down) flip(session.showLagCounter);
		});
		hotkeys.add("General", "Game Time", "", [&session, flip](bool down) {
			if (down && session.hasGameTimer.get()) flip(session.showGameTime);
		});
		hotkeys.add("General", "Input Display", "", [&session, flip](bool down) {
			if (down) flip(session.showInput);
		});
		/* Chimera's two names for one switch: a gamepad heard behind other windows */
		auto background = [&session, flip](bool down) {
			if (!down) return;
			flip(session.acceptBackgroundInput);
			session.status.set(vm::tr(session.acceptBackgroundInput.get() ? "main.background_input_on" : "main.background_input_off"));
		};
		hotkeys.add("General", "Toggle BG Input", "", background);
		window("General", "Open Project", "Ctrl+O");
		/* the window's: a project with changes is asked about first */
		window("General", "Close Project", "Ctrl+W");
		window("General", "Load Last Project", "");
		hotkeys.add("General", "Reboot Core", "Ctrl+R", [&session, run](bool down) {
			if (down) run(session, "restart");
		});
		hotkeys.add("General", "Volume Up", "", [&session, run](bool down) {
			if (down) run(session, "volumeUp");
		});
		hotkeys.add("General", "Volume Down", "", [&session, run](bool down) {
			if (down) run(session, "volumeDown");
		});
		/* Chimera's Encode Video: recording started (asking where) or stopped */
		window("General", "Encode Video", "");
		/* the window a size up or down, 1x to 10x the picture */
		window("General", "Larger Window", "Alt+Up");
		window("General", "Smaller Window", "Alt+Down");
		hotkeys.add("General", "Increase Speed", "Equals", [&session, run](bool down) {
			if (down) run(session, "speedUp");
		});
		hotkeys.add("General", "Decrease Speed", "Minus", [&session, run](bool down) {
			if (down) run(session, "speedDown");
		});
		hotkeys.add("General", "Reset Speed", "Shift+Equals", [&session, run](bool down) {
			if (down) run(session, "speedReset");
		});
		hotkeys.add("General", "Toggle Sound", "", [&session, run](bool down) {
			if (down) run(session, "toggleSound");
		});
		hotkeys.add("General", "Toggle Skip Lag Frame", "", [&session, flip](bool down) {
			if (!down) return;
			flip(session.skipLagFrames);
			session.status.set(vm::tr(session.skipLagFrames.get() ? "main.skip_lag_frames_on" : "main.skip_lag_frames_off"));
		});
		hotkeys.add("General", "Toggle Key Priority", "", [&hotkeys, run](bool down) {
			if (down) run(hotkeys, "cycleKeyPriority");
		});
		/* paused, a frame; running, a pause */
		hotkeys.add("General", "Frame Inch", "", [&session, run](bool down) {
			if (down) run(session, session.running.get() ? "pause" : "frameAdvance");
		});
		hotkeys.add("General", "Toggle Messages", "", [&session, flip](bool down) {
			if (down) flip(session.showMessages);
		});
		hotkeys.add("General", "Toggle Display Nothing", "", [&session, flip](bool down) {
			if (down) flip(session.displayNothing);
		});
		hotkeys.add("General", "Accept Background Input", "", background);
		/* Chimera's: the middle button over the picture, by default */
		window("General", "Capture Mouse", "WMouse M");
		window("General", "Exit Program", "");
		window("Tools", "RAM Watch", "");
		window("Tools", "RAM Search", "");
		window("Tools", "Hex Editor", "");
		window("Tools", "Trace Logger", "");
		window("Tools", "Lua Console", "");
		/* TAStudio shown again when it was hidden */
		window("Tools", "TAStudio", "");
		/* the console is made below; the key reaches it once it is */
		/* every freeze switched on or off, said as Chimera says it */
		hotkeys.add("Tools", "Toggle All Freezes", "", [&session, run](bool down) {
			if (down) run(session, "toggleAllFreezes");
		});
		hotkeys.add("Tools", "Toggle Last Lua Script", "", [&luaConsoleOf, run](bool down) {
			if (down && luaConsoleOf != nullptr) run(*luaConsoleOf, "toggleLast");
		});
		/* Chimera's Movie group, as TAStudio answers it: open a project, make
		 * one, a new empty movie for the game, the project read again from
		 * its file, the project saved */
		/* TAStudio's recording mode, as Chimera's TAStudio takes the hotkey; with no project, Chimera's words */
		hotkeys.add("Movie", "Toggle read-only", "Q", [&session, run](bool down) {
			if (!down) return;
			if (session.hasProject.get()) run(session, "toggleRecording");
			else session.status.set(vm::tr("main.no_movie_active"));
		});
		window("Movie", "Play Movie", "");
		window("Movie", "Record Movie", "");
		window("Movie", "Stop Movie", "");
		window("Movie", "Play from beginning", "");
		window("Movie", "Save Movie", "");
		hotkeys.add("RAM Search", "New Search", "", [&ramSearch, run](bool down) {
			if (down) run(ramSearch, "start");
		});
		hotkeys.add("RAM Search", "Do Search", "", [&ramSearch, run](bool down) {
			if (down) run(ramSearch, "search");
		});
		/* what a search compares with (five ways) and how (seven), round */
		auto cycle = [](vm::Property<int64_t> &p, int64_t by, int64_t ways) { p.set(((p.get() + by) % ways + ways) % ways); };
		hotkeys.add("RAM Search", "Previous Compare To", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.compare, -1, 5);
		});
		hotkeys.add("RAM Search", "Next Compare To", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.compare, 1, 5);
		});
		hotkeys.add("RAM Search", "Previous Operator", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.op, -1, 7);
		});
		hotkeys.add("RAM Search", "Next Operator", "", [&ramSearch, cycle](bool down) {
			if (down) cycle(ramSearch.op, 1, 7);
		});
		hotkeys.add("TAStudio", "Add Branch", "Alt+Insert", [&branches, run](bool down) {
			if (down) run(branches, "create");
		});
		hotkeys.add("TAStudio", "Delete Branch", "Alt+Delete", [&branches, run](bool down) {
			if (down && branches.selected.get() >= 0) run(branches, "remove", {branches.selected.get()});
		});
		for (int slot = 1; slot <= 10; slot++)
		{
			const std::string n = std::to_string(slot), key = "F" + n;
			hotkeys.add("TAStudio", "Save Branch " + n, "Shift+" + key, [&branches, run, slot](bool down) {
				if (down) run(branches, "saveSlot", {int64_t{slot - 1}});
			});
			hotkeys.add("TAStudio", "Load Branch " + n, key, [&branches, run, slot](bool down) {
				if (down) run(branches, "loadWithWarning", {int64_t{slot - 1}});
			});
		}
		hotkeys.add("TAStudio", "Show Cursor", "", [&pianoRoll](bool down) {
			if (down) pianoRoll.topFrame.set(std::max<int64_t>(0, pianoRoll.currentFrame.get() - pianoRoll.visibleFrames.get() / 2));
		});
		hotkeys.add("TAStudio", "Select Current Frame", "", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "select", {pianoRoll.currentFrame.get(), pianoRoll.currentFrame.get()});
		});
		hotkeys.add("TAStudio", "Seek To Selected Frame", "", [&pianoRoll, &session, run](bool down) {
			if (down && pianoRoll.selectFirst.get() >= 0) run(session, "seek", {pianoRoll.selectFirst.get()});
		});
		window("TAStudio", "Seek To...", "Ctrl+G");
		hotkeys.add("TAStudio", "Toggle Follow Cursor", "Shift+F", [&pianoRoll](bool down) {
			if (down) pianoRoll.followPlayback.set(!pianoRoll.followPlayback.get());
		});
		hotkeys.add("TAStudio", "Toggle Auto-Restore", "Alt+R", [&session](bool down) {
			if (down) session.autoRestore.set(!session.autoRestore.get());
		});
		hotkeys.add("TAStudio", "Seek To Green Arrow", "R", [&session, run](bool down) {
			if (down) run(session, "seekToRestore");
		});
		hotkeys.add("TAStudio", "Toggle Turbo Seek", "Shift+S", [&session](bool down) {
			if (down) session.turboSeek.set(!session.turboSeek.get());
		});
		hotkeys.add("TAStudio", "Cycle Greenzone", "", [&session, run](bool down) {
			if (down) run(session, "cycleGreenzone");
		});
		window("TAStudio", "Undo", "Ctrl+Z");
		window("TAStudio", "Redo", "Ctrl+Y");
		auto seekMarker = [&pianoRoll, &session, run](const char *which) {
			std::string e;
			const auto to = pianoRoll.run(which, {pianoRoll.currentFrame.get()}, e);
			int64_t frame = -1;
			if (to && vm::fromValue(*to, frame) && frame >= 0) run(session, "seek", {frame});
		};
		hotkeys.add("TAStudio", "Seek To Prev Marker", "Shift+PageUp", [seekMarker](bool down) {
			if (down) seekMarker("previousMarker");
		});
		hotkeys.add("TAStudio", "Seek To Next Marker", "Shift+PageDown", [seekMarker](bool down) {
			if (down) seekMarker("nextMarker");
		});
		/* a seek that plays (Turbo seek off) stopped where it is, as the row menu's */
		hotkeys.add("TAStudio", "Cancel Seek", "", [&session, run](bool down) {
			if (down && session.seekingTo.get() >= 0) run(session, "pause");
		});
		hotkeys.add("TAStudio", "Set Marker", "M", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "addMarker", {pianoRoll.currentFrame.get(), std::string()});
		});
		hotkeys.add("TAStudio", "Delete Marker", "Ctrl+M", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "removeMarker", {pianoRoll.currentFrame.get()});
		});
		window("TAStudio", "Sel. bet. Markers", "Ctrl+A");
		window("TAStudio", "Select All", "Ctrl+Shift+A");
		hotkeys.add("TAStudio", "Reselect Clip.", "Ctrl+B", [&pianoRoll, run](bool down) {
			if (down) run(pianoRoll, "reselectClipboard");
		});
		window("TAStudio", "Clear Frames", "Delete");
		window("TAStudio", "Delete Frames", "Ctrl+Delete");
		window("TAStudio", "Insert Frame", "Insert");
		window("TAStudio", "Insert # Frames", "Shift+Insert");
		window("TAStudio", "Clone Frames", "Ctrl+Insert");
		window("TAStudio", "Clone # Times", "Ctrl+Shift+Insert");
		window("TAStudio", "Paste Insert", "Ctrl+Shift+V");
		/* the picked analog cell's value; nothing when none is picked */
		for (const auto &[name, keys, how] : std::initializer_list<std::tuple<const char *, const char *, const char *>>{
				 {"Analog Increment", "Up", "up"}, {"Analog Decrement", "Down", "down"}, {"Analog Incr. by 10", "Shift+Up", "up10"},
				 {"Analog Decr. by 10", "Shift+Down", "down10"}, {"Analog Maximum", "Right", "max"}, {"Analog Minimum", "Left", "min"}})
		{
			const std::string move = how;
			hotkeys.add("TAStudio", name, keys, [&pianoRoll, run, move](bool down) {
				if (down) run(pianoRoll, "nudgeAxis", {move});
			});
		}
	}
	vm::SettingsViewModel settings(dispatcher, session);
	vm::HelpViewModel help(dispatcher, session, settings);
	/* the system, for a bug report: as Qt names it */
	help.osName.set((QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::currentCpuArchitecture() + QStringLiteral(")")).toStdString());
	help.exeFolder.set(QCoreApplication::applicationDirPath().toStdString());
	/* the cores and the firmware: beside the program, as Chimera keeps them,
	 * unless the environment names others */
	vm::CoresViewModel cores(dispatcher);
	if (cores.coresFolder.get().empty())
		cores.coresFolder.set((QCoreApplication::applicationDirPath() + QStringLiteral("/cores")).toStdString());
	if (cores.firmwareFolder.get().empty())
		cores.firmwareFolder.set((QCoreApplication::applicationDirPath() + QStringLiteral("/firmware")).toStdString());
	vm::NewProjectViewModel newProject(dispatcher, session, cores);
	/* the core's suggested settings for a game, asked as an import is */
	/* Chimera's compile step: its sessions are the headless runner, and what
	 * they compile is kept where the machine's compiled code is */
	{
		const QString runner = runnerProgram();
		newProject.compileRunner.set(runner.toStdString());
		newProject.compileRoot.set(session.compileCacheRoot.get());
	}
	vm::Connection compileRootWatch = session.compileCacheRoot.changed.connect([&] { newProject.compileRoot.set(session.compileCacheRoot.get()); });
	const vm::NewProjectViewModel::SuggestRunner suggestRunner = [&app](const std::string &package, const std::string &game,
									const std::vector<std::pair<std::string, std::string>> &files, const std::string &slots,
									const std::string &settings, std::function<void(std::string, std::string)> done) {
		const QString runner = runnerProgram();
		if (runner.isEmpty())
		{
			done({}, vm::tr("chimera_run.missing"));
			return;
		}
		auto *proc = new QProcess(&app);
		/* a game runs only in a project (owner, 2026-10-26): the core is asked
		 * of one of the files and settings so far, written where the process
		 * can open it, for as long as it lives */
		auto *folder = new QTemporaryDir();
		QObject::connect(proc, &QObject::destroyed, [folder] { delete folder; });
		const std::string project = (folder->path() + QStringLiteral("/suggest.chimeraProject")).toStdString();
		std::vector<std::string> dirs;
		std::string why;
		if (!folder->isValid() || !vm::saveAskingProject(package, vm::askFilesOf(package, game, files, slots), settings, project, dirs, why))
		{
			proc->deleteLater();
			done({}, why.empty() ? vm::tr("chimera_run.no_folder") : why);
			return;
		}
		QStringList args{QStringLiteral("--project"), QString::fromStdString(project), QString::fromStdString(package), QStringLiteral("--suggest")};
		for (const std::string &d : dirs) args << QStringLiteral("--files") << QString::fromStdString(d);
		/* two minutes, as Chimera gives a core to answer */
		QTimer::singleShot(120000, proc, [proc] { proc->setProperty("tooLong", true); proc->kill(); });
		QObject::connect(proc, &QProcess::finished, proc, [proc, done](int code, QProcess::ExitStatus) {
			QByteArray answer;
			for (const QByteArray &l : proc->readAllStandardOutput().split('\n'))
				if (l.trimmed().startsWith('{')) answer = l.trimmed();
			const QByteArray err = proc->readAllStandardError().trimmed();
			proc->deleteLater();
			if (proc->property("tooLong").toBool()) done({}, vm::tr("chimera_run.too_long"));
			else if (code == 0 && !answer.isEmpty()) done(answer.toStdString(), {});
			else done({}, err.isEmpty() ? vm::tr("chimera_run.no_answer") : err.split('\n').last().toStdString());
		});
		/* a runner that cannot start never finishes: say so, as the import does */
		QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, done](QProcess::ProcessError e) {
			if (e != QProcess::FailedToStart) return;
			proc->deleteLater();
			done({}, vm::tr("chimera_run.not_started"));
		});
		proc->start(runner, args);
	};
	newProject.setSuggestRunner(suggestRunner);
	vm::MediaMakerViewModel mediaMaker(dispatcher, [&cores] {
		std::vector<vm::MediaMakerViewModel::Recipe> recipes;
		for (const auto &r : cores.mediaRecipes()) recipes.push_back({r.core, r.label, r.json});
		return recipes;
	});
	/* a movie import asks the core in a process of its own (one machine to a
	 * process), as Chimera does: chimera-run, beside the program or, in the
	 * build tree, where it is built (CHIMERA_RUN names another) */
	const vm::NewProjectViewModel::ImportRunner importRunner = [&app](const std::string &package, const std::string &movie,
								   const std::vector<vm::AskFile> &files, const std::string &settings,
								   std::function<void(std::string, std::string)> done) {
		const QString runner = runnerProgram();
		if (runner.isEmpty())
		{
			done({}, vm::tr("chimera_run.missing"));
			return;
		}
		/* asked of a project (owner, 2026-10-26): the files with a slot in it,
		 * the import options its settings, the rest mounted by name */
		auto *folder = new QTemporaryDir();
		const std::string project = (folder->path() + QStringLiteral("/import.chimeraProject")).toStdString();
		std::vector<std::string> dirs;
		std::string why;
		if (!folder->isValid() || !vm::saveAskingProject(package, files, settings, project, dirs, why))
		{
			delete folder;
			done({}, why.empty() ? vm::tr("chimera_run.no_folder") : why);
			return;
		}
		QStringList args{QStringLiteral("--project"), QString::fromStdString(project), QString::fromStdString(package),
			QStringLiteral("--import-movie"), QString::fromStdString(movie)};
		for (const std::string &d : dirs) args << QStringLiteral("--files") << QString::fromStdString(d);
		for (const vm::AskFile &f : files)
			if (f.slot.empty()) args << QStringLiteral("--mount") << QString::fromStdString(f.name + "=" + f.path);
		auto *proc = new QProcess(&app);
		QObject::connect(proc, &QObject::destroyed, [folder] { delete folder; });
		/* two minutes, as Chimera gives a core to answer */
		QTimer::singleShot(120000, proc, [proc] { proc->setProperty("tooLong", true); proc->kill(); });
		QObject::connect(proc, &QProcess::finished, proc, [proc, done](int code, QProcess::ExitStatus) {
			/* the answer is the last line that is a JSON object; a core may print */
			const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
			QByteArray answer;
			for (const QByteArray &l : lines)
				if (l.trimmed().startsWith('{')) answer = l.trimmed();
			const QByteArray err = proc->readAllStandardError().trimmed();
			proc->deleteLater();
			if (proc->property("tooLong").toBool()) done({}, vm::tr("chimera_run.too_long"));
			else if (code == 0 && !answer.isEmpty()) done(answer.toStdString(), {});
			else done({}, err.isEmpty() ? vm::tr("chimera_run.no_answer") : err.split('\n').last().toStdString());
		});
		QObject::connect(proc, &QProcess::errorOccurred, proc, [proc, done](QProcess::ProcessError e) {
			if (e != QProcess::FailedToStart) return;
			proc->deleteLater();
			done({}, vm::tr("chimera_run.not_started"));
		});
		proc->start(runner, args);
	};
	newProject.setImportRunner(importRunner);
	/* the MCP server's create_project: a New Project form of its own, never
	 * shown, which the agent's answers fill as Chimera's wizard is filled */
	vm::NewProjectViewModel agentProject(dispatcher, session, cores);
	agentProject.setSuggestRunner(suggestRunner);
	agentProject.setImportRunner(importRunner);
	agentProject.compileRunner.set(newProject.compileRunner.get());
	agentProject.compileRoot.set(newProject.compileRoot.get());
	vm::Connection agentCompileRootWatch = session.compileCacheRoot.changed.connect([&] { agentProject.compileRoot.set(session.compileCacheRoot.get()); });
	session.setFinders([&cores](const std::string &n, const std::string &h, std::string &note) { return cores.packageFor(n, h, note); },
		[&cores] { return cores.firmwareFolders(); });
	session.setRunFirmware(runFirmware);
	session.setFirmwareKeeper([&cores](const std::string &core, const std::string &id, const std::string &path) { cores.rememberFirmware(core, id, path); });

	/* what the window remembers (configPath): folders the environment names
	 * are the environment's, and not remembered */
	vm::ConfigStore config(configPath.toStdString());
	if (!qEnvironmentVariableIsSet("CHIMERA_CORES_DIR")) config.keep(cores, "coresFolder");
	config.keep(cores, "chosenFirmware");
	config.keep(cores, "shows");
	config.keep(newProject, "kind");
	/* Config > Data Directory: where the window keeps things for the person;
	 * a change asked for last
	 * time is carried out now, before anything in it is open */
	vm::DataFolderViewModel dataFolder(dispatcher);
	dataFolder.standard.set(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation).toStdString());
	/* Chimera's CHIMERA_DATA_HOME: when it is set, it decides */
	if (const QString env = qEnvironmentVariable("CHIMERA_DATA_HOME"); !env.trimmed().isEmpty())
		dataFolder.environment.set(QFileInfo(env).absoluteFilePath().toStdString());
	for (const char *name : {"current", "pending", "pendingMove"}) config.keep(dataFolder, name);
	if (const std::string said = dataFolder.applyPending(); !said.empty()) dataFolder.status.set(said);
	const std::string dataHome = dataFolder.inUse();
	/* crash notes go to the data folder in use */
	session.crashFolder.set(dataHome + "/Crashes");
	help.crashFolder.set(dataHome + "/Crashes");
	if (const std::string why = vm::crash::repoint(dataHome + "/Crashes"); !why.empty())
		std::fprintf(stderr, "[crash] notes stay where they were: %s\n", why.c_str());
	/* what a project can make again (its branches' states), a folder a
	 * project (CHIMERA_QT_CACHE for a test) */
	session.cacheFolder.set(qEnvironmentVariableIsSet("CHIMERA_QT_CACHE") ? qEnvironmentVariable("CHIMERA_QT_CACHE").toStdString()
																		  : (std::filesystem::path(dataHome) / "projects").string());
	/* unsaved work kept against a crash: beside the caches, never in one, so
	 * removing a cache cannot take it (CHIMERA_QT_RECOVERY for a test) */
	session.recoveryFolder.set(qEnvironmentVariableIsSet("CHIMERA_QT_RECOVERY") ? qEnvironmentVariable("CHIMERA_QT_RECOVERY").toStdString()
																				: (std::filesystem::path(dataHome) / "recovery").string());
	/* the hashes of the files read (firmware, discs) outlive the session, so the first rescan or
	 * opening of each session does not read every disc image again, as Chimera's (issue 368;
	 * CHIMERA_QT_HASHES for a test) */
	{
		const std::filesystem::path hashes = qEnvironmentVariableIsSet("CHIMERA_QT_HASHES")
			? std::filesystem::path(qEnvironmentVariable("CHIMERA_QT_HASHES").toStdString())
			: std::filesystem::path(dataHome) / "file-hashes.txt";
		std::error_code ec;
		std::filesystem::create_directories(hashes.parent_path(), ec);
		ce_sha1_keep_in(hashes.string().c_str());
	}
	qt::QmlViewModel dataFolderQml(dataFolder);
	qt::QmlViewModel settingsQml(settings);
	/* what cores compiled for a game, kept between sessions (CHIMERA_QT_COMPILE_CACHE for a test) */
	session.compileCacheRoot.set(qEnvironmentVariableIsSet("CHIMERA_QT_COMPILE_CACHE") ? qEnvironmentVariable("CHIMERA_QT_COMPILE_CACHE").toStdString()
																						  : (std::filesystem::path(dataHome) / "cache" / "precompiled").string());
	vm::PrecompiledViewModel precompiled(dispatcher);
	config.keep(precompiled, "sessionGigabytes");
	/* the compile step counts a session at what the setting says */
	auto sessionMemory = [&precompiled, &newProject, &agentProject] {
		const int64_t gb = std::clamp<int64_t>(precompiled.sessionGigabytes.get(), 1, 256);
		newProject.compileSessionGigabytes.set(gb);
		agentProject.compileSessionGigabytes.set(gb);
	};
	sessionMemory();
	vm::Connection sessionMemoryWatch = precompiled.sessionGigabytes.changed.connect(sessionMemory);
	precompiled.root.set(session.compileCacheRoot.get());
	vm::Connection precompiledInUse = session.compileCacheDir.changed.connect([&] { precompiled.inUse.set(session.compileCacheDir.get()); });
	vm::Connection precompiledCompiling = newProject.compileFolder.changed.connect([&] { precompiled.compiling.set(newProject.compileFolder.get()); });
	qt::QmlViewModel precompiledQml(precompiled);
	vm::TraceLoggerViewModel traceLogger(dispatcher, session);
	qt::QmlViewModel traceLoggerQml(traceLogger);
	vm::DebuggerViewModel debugger(dispatcher, session);
	qt::QmlViewModel debuggerQml(debugger);
	vm::GuiOverlayViewModel guiOverlay(dispatcher);
	qt::QmlViewModel guiOverlayQml(guiOverlay);
	/* the picture while nothing is open: Chimera's mark, read here and handed in */
	vm::IdleEyeViewModel idleEye(dispatcher);
	{
		const QImage mark = QImage(QStringLiteral(":/chimera/chimera.png")).convertToFormat(QImage::Format_ARGB32);
		const int side = std::min(mark.width(), mark.height());
		std::vector<uint32_t> argb(static_cast<size_t>(side) * side);
		for (int y = 0; y < side; y++)
			for (int x = 0; x < side; x++) argb[static_cast<size_t>(y) * side + x] = mark.pixel(x, y);
		idleEye.setMark(side, argb);
	}
	for (const char *name : {"snowWhen", "snowIntensity", "snowBias", "snowFramerate"})
		config.keep(idleEye, name);
	qt::QmlViewModel idleEyeQml(idleEye);
	vm::SurfaceViewerViewModel surfaceViewer(dispatcher, session);
	qt::QmlViewModel surfaceViewerQml(surfaceViewer);
	vm::MacroToolViewModel macroTool(dispatcher, pianoRoll, session);
	qt::QmlViewModel macroToolQml(macroTool);
	/* Chimera's Encode Video, with the ffmpeg the bundle keeps beside the program;
	 * its choices kept, as Chimera keeps them in its config */
	vm::EncoderViewModel encoder(dispatcher, session, QCoreApplication::applicationDirPath().toStdString());
	for (const char *name : {"command", "audioSync", "resize", "width", "height", "pad", "captureOsd", "captureLua"}) config.keep(encoder, name);
	qt::QmlViewModel encoderQml(encoder);
	/* the person's own keys, pads and pointer, for Lua's input library */
	vm::HostInputViewModel hostInput(dispatcher, session);
	qt::QmlViewModel hostInputQml(hostInput);
	config.keep(session, "autoRestore");
	config.keep(session, "turboSeek");
	config.keep(pianoRoll, "mainSplit");
	config.keep(pianoRoll, "sideSplit");
	config.keep(session, "throttle");
	config.keep(session, "vsyncEnabled");
	config.keep(session, "speedPercent");
	config.keep(session, "unthrottled");
	config.keep(session, "fastDrawOneIn");
	config.keep(session, "soundOn");
	config.keep(session, "volume");
	config.keep(session, "showFps");
	config.keep(session, "showFrameCounter");
	config.keep(session, "showLagCounter");
	config.keep(session, "showInput");
	config.keep(session, "showAverageFps");
	config.keep(session, "showRerecords");
	config.keep(session, "showMessages");
	config.keep(session, "skipLagFrames");
	config.keep(session, "fastForwardPercent");
	config.keep(session, "greenzoneBudgetMb");
	config.keep(session, "greenzoneNearStride");
	config.keep(session, "pictureSettleFrames");
	for (const char *name : {"soundDevice", "soundBufferMs", "soundNormalOn", "soundFastOn", "fastVolume", "muteFrameAdvance"})
		config.keep(session, name);
	config.keep(session, "displayNothing");
	config.keep(session, "acceptBackgroundInput");
	/* the window in front or not: a gamepad is heard behind other windows only when asked */
	session.windowActive.set(QGuiApplication::applicationState() == Qt::ApplicationActive);
	QObject::connect(qApp, &QGuiApplication::applicationStateChanged, [&session](Qt::ApplicationState state) {
		session.windowActive.set(state == Qt::ApplicationActive);
	});
	config.keep(session, "showStatusBar");
	config.keep(session, "captureOsd");
	config.keep(session, "captureMouse");
	config.keep(session, "showGameTime");
	config.keep(session, "autofireOn");
	config.keep(session, "autofireOff");
	config.keep(session, "autofireSkipsLag");
	/* backups where Chimera puts them, beside the program, unless kept elsewhere */
	/* Config > Paths: the folders, relative to the program's own at first;
	 * the backups and the firmware follow them */
	vm::PathsViewModel paths(dispatcher);
	paths.programFolder.set(QCoreApplication::applicationDirPath().toStdString());
	for (const char *name : {"base", "changed", "systems", "systemChanged", "recentForRoms", "lastRomFolder"}) config.keep(paths, name);
	/* the temporary folder the system gave, put back when Temp Files is emptied */
	struct TempWas
	{
		const char *name;
		bool set;
		QByteArray value;
	};
	std::vector<TempWas> tempWas;
	for (const char *name : {"TMPDIR", "TMP", "TEMP", "TEMPDIR"}) tempWas.push_back({name, qEnvironmentVariableIsSet(name), qgetenv(name)});
	auto followPaths = [&paths, &session, &cores, &tempWas] {
		session.backupFolder.set(paths.resolve("Movie backups"));
		if (!qEnvironmentVariableIsSet("CHIMERA_FIRMWARE_DIR") && cores.firmwareFolder.get() != paths.resolve("Firmware"))
			cores.firmwareFolder.set(paths.resolve("Firmware"));
		/* the Movies folder is there for the project dialogs to open in, as
		 * Chimera makes it (issue 399) */
		std::error_code ec;
		std::filesystem::create_directories(paths.resolve("Movies"), ec);
		/* Temp Files, when set, is the whole program's temporary folder, as
		 * Chimera's (issue 402); emptied, the system's own again */
		const std::string temp = paths.tempFolder();
		if (!temp.empty()) std::filesystem::create_directories(temp, ec);
		for (const TempWas &t : tempWas)
		{
			if (!temp.empty()) qputenv(t.name, QString::fromStdString(temp).toLocal8Bit());
			else if (t.set) qputenv(t.name, t.value);
			else qunsetenv(t.name);
		}
	};
	followPaths();
	vm::Connection pathsFollowed = paths.moved.connect(followPaths);
	/* the firmware folder chosen elsewhere (the Cores window) is the Paths' too */
	vm::Connection firmwareFollowed = cores.firmwareFolder.changed.connect([&paths, &cores] {
		if (qEnvironmentVariableIsSet("CHIMERA_FIRMWARE_DIR") || cores.firmwareFolder.get() == paths.resolve("Firmware")) return;
		std::string e;
		paths.run("setPath", {std::string("Firmware"), cores.firmwareFolder.get()}, e);
	});
	/* screenshots go to the system's own folder, as Chimera does; a system opened gets its folders */
	qt::PictureFiles::folderFor() = [&paths, &session] { return QString::fromStdString(paths.resolveFor(session.systemId.get(), "Screenshots")); };
	vm::Connection systemFolders = session.opens.changed.connect([&paths, &session] { paths.useSystem(session.systemId.get()); });
	qt::QmlViewModel pathsQml(paths);
	vm::FirmwareViewModel firmware(dispatcher, cores);
	config.keep(firmware, "shows");
	qt::QmlViewModel firmwareQml(firmware);
	vm::ConfigFilesViewModel configFiles(dispatcher, config);
	qt::QmlViewModel configFilesQml(configFiles);
	vm::Connection configFilesSay = configFiles.status.changed.connect([&] { session.status.set(configFiles.status.get()); });
	config.keep(projectDetails, "defaultAuthor");
	config.keep(pianoRoll, "maxUndoSteps");
	config.keep(pianoRoll, "copyFrameNumbers");
	config.keep(pianoRoll, "followScroll");
	config.keep(pianoRoll, "scrollSync");
	config.keep(pianoRoll, "followAlways");
	config.keep(pianoRoll, "wheelRows");
	config.keep(pianoRoll, "editHiddenColumns");
	config.keep(pianoRoll, "patternMode");
	config.keep(pianoRoll, "patternKind");
	config.keep(pianoRoll, "denoteStatesColor");
	config.keep(pianoRoll, "denoteStatesIcons");
	config.keep(pianoRoll, "denoteMarkersColor");
	config.keep(pianoRoll, "denoteMarkersIcons");
	config.keep(pianoRoll, "rollColors");
	config.keep(pianoRoll, "rollFont");
	config.keep(pianoRoll, "recentMacros");
	config.keep(pianoRoll, "markersKeepStates");
	config.keep(pianoRoll, "bindMarkers");
	config.keep(session, "pauseAtEnd");
	config.keep(session, "rewindStep");
	config.keep(session, "rewindStepFast");
	config.keep(session, "autosaveSeconds");
	config.keep(session, "autosaveAsBackup");
	config.keep(session, "backupOnSave");
	config.keep(branches, "loadOnDoubleClick");
	config.keep(branches, "oldControlScheme");
	config.keep(branches, "previewSide");
	config.keep(projectDetails, "showSubtitles");
	config.keep(session, "greenzoneEvery");
	config.keep(ramWatch, "showOnScreen");
	config.keep(controls, "saved");
	config.keep(controls, "savedAutofire");
	config.keep(controls, "savedAnalog");
	config.keep(controls, "autoTab");
	config.keep(controls, "mouseSensitivity");
	/* the themes: Chimera's built-in two, and the folder beside the program
	 * (CHIMERA_THEMES_DIR for a test); --color-scheme picks Light or Dark for
	 * this run only */
	for (const QString &file : {QStringLiteral(":/chimera/themes/light.json"), QStringLiteral(":/chimera/themes/dark.json")})
	{
		QFile f(file);
		std::string why;
		if (f.open(QIODevice::ReadOnly)) themes.addBuiltIn(f.readAll().toStdString(), why);
	}
	/* the Paths window's Themes folder (CHIMERA_THEMES_DIR for a test) */
	auto themesFollow = [&themes, &paths] {
		themes.folder.set(qEnvironmentVariableIsSet("CHIMERA_THEMES_DIR") ? qEnvironmentVariable("CHIMERA_THEMES_DIR").toStdString()
			: paths.resolve("Themes"));
	};
	themesFollow();
	vm::Connection themesFollowPaths = paths.moved.connect(themesFollow);
	if (args.isSet(schemeOpt))
		themes.current.set(args.value(schemeOpt) == QStringLiteral("light") ? "Light" : "Dark");
	else
		config.keep(themes, "current");
	auto applyTheme = [&themes] { qt::applyTheme(QString::fromStdString(themes.colors.get())); };
	themesWatch = themes.colors.changed.connect(applyTheme);
	applyTheme();
	config.keep(hotkeys, "saved");
	config.keep(hotkeys, "keyPriority");
	config.keep(hotkeys, "noMixedPriority");
	config.keep(hotkeys, "autoTab");
	config.keep(session, "recentProjects");
	config.keep(pianoRoll, "followPlayback");
	config.keep(pianoRoll, "moveWithMainWindow");
	if (!config.problem().empty()) session.status.set(config.problem());
	/* what the start did with a data folder change asked for */
	if (!dataFolder.status.get().empty()) session.status.set(dataFolder.status.get());
	qt::QmlViewModel sessionQml(session);
	qt::QmlViewModel pianoRollQml(pianoRoll);
	qt::QmlViewModel ramSearchQml(ramSearch);
	qt::QmlViewModel ramWatchQml(ramWatch);
	qt::QmlViewModel newProjectQml(newProject);
	qt::QmlViewModel hexEditorQml(hexEditor);
	/* the picture of the controller a package ships, for Controller Config:
	 * "image://controller/<count>", decoded from the view model's bytes. Told
	 * before the window is, so the window asks for the new one */
	auto *controllerPicture = new ControllerPicture;
	vm::Connection controllerPictureShown = controls.pictureShown.changed.connect([&controls, controllerPicture] {
		controllerPicture->set(controls.picture());
	});
	qt::QmlViewModel controlsQml(controls);
	qt::QmlViewModel themesQml(themes);
	qt::QmlViewModel hotkeysQml(hotkeys);
	qt::KeyNamer keyNamer;
	app.installEventFilter(&keyNamer);
	/* the menus' letters, drawn while the keyboard works the menus (issue 405) */
	qt::MenuLetters menuLetters;
	app.installEventFilter(&menuLetters);
	qt::ThemeColors themeColors;
	qt::QmlViewModel helpQml(help);
	/* Tools > MCP Server...: an AI agent working on the open project (docs/mcp-server.md) */
	vm::McpViewModel mcp(dispatcher, session, pianoRoll, branches);
	config.keep(mcp, "allowChanges");
	{
		/* the program an agent's program starts, in the bundle's bin folder or, in a build, beside chimera-run */
		QString program = besideProgram(QStringLiteral("chimera-mcp"));
		if (program.isEmpty()) program = QCoreApplication::applicationDirPath() + QStringLiteral("/bin/chimera-mcp") + QStringLiteral(EXE_SUFFIX);
		mcp.setupCommand.set(("claude mcp add chimera -- \"" + QDir::toNativeSeparators(program) + "\"").toStdString());
	}
	mcp.projectsFolder = [&paths] { return paths.resolve("Movies"); };
	mcp.serverInfo = [&paths, &cores, &help, configPath] {
		vm::json::Json info;
		info.kind = vm::json::Json::Kind::Object;
		auto add = [&](const char *name, vm::json::Json v) { info.members.emplace_back(name, std::move(v)); };
		auto text = [](const std::string &t) { return vm::json::Json::string(t); };
		add("chimera", text(help.buildName.get()));
		vm::json::Json engine;
		std::string error;
		const char *built = ce_build_info();
		if (!vm::json::parse(built != nullptr ? built : "{}", engine, error)) engine = text(built != nullptr ? built : "");
		if (const vm::json::Json *c = engine.find("commit")) add("commit", *c);
		add("engine", engine);
		add("window", vm::json::Json::boolean_(true));
		add("program_folder", text(QCoreApplication::applicationDirPath().toStdString()));
		add("config_file", text(configPath.toStdString()));
		vm::json::Json folders;
		folders.kind = vm::json::Json::Kind::Object;
		folders.members.emplace_back("projects", text(paths.resolve("Movies")));
		vm::json::Json coreFolders;
		coreFolders.kind = vm::json::Json::Kind::Array;
		coreFolders.items.push_back(text(cores.coresFolder.get()));
		folders.members.emplace_back("cores", coreFolders);
		folders.members.emplace_back("firmware", text(paths.resolve("Firmware")));
		folders.members.emplace_back("lua", text(paths.resolve("Lua")));
		add("folders", folders);
		const char *endpoint = ce_mcp_endpoint();
		add("mcp_name", text(endpoint != nullptr ? endpoint : ""));
		return vm::json::write(info);
	};
	/* list_cores and describe_core read the cores folder */
	auto mcpCores = [&cores] { ce_mcp_cores_folders(("[" + vm::json::quote(cores.coresFolder.get()) + "]").c_str()); };
	mcpCores();
	vm::Connection mcpCoresFollow = cores.coresFolder.changed.connect(mcpCores);
	qt::QmlViewModel mcpQml(mcp);
	qt::QmlViewModel mediaMakerQml(mediaMaker);
	vm::CacheManagerViewModel cacheManager(dispatcher);
	/* the session's cache folder, the one it lists */
	cacheManager.cacheFolder.set(session.cacheFolder.get());
	vm::Connection cacheFolderFollows = session.cacheFolder.changed.connect([&] { cacheManager.cacheFolder.set(session.cacheFolder.get()); });
	cacheManager.recoveryFolder.set(session.recoveryFolder.get());
	vm::Connection recoveryFolderFollows = session.recoveryFolder.changed.connect([&] { cacheManager.recoveryFolder.set(session.recoveryFolder.get()); });
	config.keep(cacheManager, "autoClean");
	config.keep(cacheManager, "limitGb");
	config.keep(cacheManager, "floorGb");
	/* Chimera's auto-clean: at the start, and when a project closes - never
	 * while one is worked on, and never the one just closed */
	auto autoCleanSaying = [&cacheManager, &session](const std::string &spare) {
		std::string e;
		const auto said = cacheManager.run("runAutoClean", {spare}, e);
		std::string text;
		if (said && vm::fromValue(*said, text) && !text.empty()) session.status.set(text);
	};
	autoCleanSaying("");
	std::string lastProjectId = session.projectId.get();
	vm::Connection cacheInUseFollows = session.projectId.changed.connect([&] {
		cacheManager.inUse.set(session.projectId.get());
		const std::string closed = std::exchange(lastProjectId, session.projectId.get());
		if (!closed.empty()) autoCleanSaying(closed);
	});
	qt::QmlViewModel cacheManagerQml(cacheManager);
	vm::CustomizeViewModel customize(dispatcher, session);
	for (const char *name : {"startPaused", "startFullscreen", "neverAskSave", "luaDuringTurbo",
			 "showContextMenu", "alternateLayouts", "mergeModifiers", "singleInstance", "gameCubeAdapter", "language"})
		config.keep(customize, name);
	/* the window's words in the language chosen in Config > Language; at the
	 * first open, the first of the system's languages it has a translation
	 * for, else English (docs/translations.md, owner 2026-10-28) */
	{
		QStringList offered;
		for (const QString &l : qt::languages()) offered << l + QLatin1Char('\t') + qt::languageName(l);
		customize.languages.set(offered.join(QLatin1Char('\n')).toStdString());
		const QString start = qt::startLanguage(QString::fromStdString(customize.language.get()));
		qt::useLanguage(app, start);
		customize.language.set(start.toStdString());
	}
	auto keyNaming = [&customize] { qt::setKeyNaming(customize.alternateLayouts.get(), customize.mergeModifiers.get()); };
	keyNaming();
	vm::Connection alternateLayoutsWatch = customize.alternateLayouts.changed.connect(keyNaming);
	vm::Connection mergeModifiersWatch = customize.mergeModifiers.changed.connect(keyNaming);
	qt::QmlViewModel customizeQml(customize);
	vm::MessagesViewModel messages(dispatcher);
	for (const char *name : {"positions", "messagesColor", "alertColor", "lastInputColor", "movieInputColor", "messageSeconds", "stackMessages"})
		config.keep(messages, name);
	qt::QmlViewModel messagesQml(messages);
	vm::DisplayViewModel display(dispatcher);
	for (const char *name : {"aspectMode", "customWidth", "customHeight", "ratioX", "ratioY", "letterbox", "integerScale", "cropLeft",
			 "cropTop", "cropRight", "cropBottom", "finalFilter", "prescale", "autoPrescale", "scaleOsd", "osdFollowsWindow", "saveWindowPosition", "windowGeometry",
			 "allowDoubleClickFullscreen", "menuWindowed", "captionWindowed", "frameWindowed", "menuFullscreen", "statusBarFullscreen",
			 "autohideMouseFullscreen", "fullscreenHacks", "stayOnTop", "mouseCaptureForcesTop", "minimalDisplay", "staticWindowTitles", "titleSuffix", "resizeWithPicture",
			 "windowScales"})
		config.keep(display, name);
	qt::QmlViewModel displayQml(display);
	qt::QmlViewModel branchesQml(branches);
	qt::QmlViewModel projectDetailsQml(projectDetails);
	qt::QmlViewModel coresQml(cores);

	/* scripts reach the same view models, by the same names */
	vm::LuaBridge lua;
	/* what scripts print goes to the console window, and to stdout too */
	vm::LuaConsoleViewModel luaConsole(dispatcher, lua, [](const std::string &line) {
		std::printf("lua: %s\n", line.c_str());
		std::fflush(stdout);
	});
	luaConsoleOf = &luaConsole;
	mcp.lua = &luaConsole;
	mcp.newProject = &agentProject;
	for (const char *name : {"disableOnLoad", "toggleAllIfNoneSelected", "reloadOnChange", "duplicateToStdout", "recentSessions", "recentScripts"})
		config.keep(luaConsole, name);
	/* New Script's template is in the Paths' Lua folder */
	luaConsole.luaFolder.set(paths.resolve("Lua"));
	vm::Connection luaFolderWatch = paths.changed.changed.connect([&luaConsole, &paths] { luaConsole.luaFolder.set(paths.resolve("Lua")); });
	qt::QmlViewModel luaConsoleQml(luaConsole);
	/* each tool window's Save Window Position, Stay on Top and Float From Main Window, and its place */
	vm::ToolWindowsViewModel toolWindows(dispatcher);
	config.keep(toolWindows, "settings");
	for (const char *name : {"doubleClickToPoke", "recentFiles", "hiddenColumns", "previousType"}) config.keep(ramWatch, name);
	for (const char *name : {"previewMode", "autoSearchAccountForLag", "alwaysExcludeRamWatch", "useUndoHistory", "recentFiles", "hiddenColumns"})
		config.keep(ramSearch, name);
	for (const char *name : {"domain", "size", "bigEndian", "recentTables", "colors"}) config.keep(hexEditor, name);
	qt::QmlViewModel toolWindowsQml(toolWindows);
	lua.add(session);
	lua.add(pianoRoll);
	lua.add(ramSearch);
	lua.add(ramWatch);
	lua.add(newProject);
	lua.add(hexEditor);
	lua.add(controls);
	lua.add(themes);
	lua.add(hotkeys);
	lua.add(settings);
	lua.add(luaConsole);
	lua.add(toolWindows);
	lua.add(branches);
	lua.add(projectDetails);
	lua.add(cores);
	lua.add(help);
	lua.add(mediaMaker);
	lua.add(cacheManager);
	lua.add(customize);
	lua.add(messages);
	lua.add(display);
	lua.add(paths);
	lua.add(firmware);
	lua.add(configFiles);
	lua.add(dataFolder);
	lua.add(precompiled);
	lua.add(traceLogger);
	lua.add(debugger);
	lua.add(guiOverlay);
	lua.add(surfaceViewer);
	lua.add(macroTool);
	lua.add(encoder);
	lua.add(hostInput);
	/* the snowy null core's settings, for client.getconfig (the MCP server's
	 * are not given to scripts: an agent's script would change what it may do) */
	lua.add(idleEye);
	/* a game core's properties by name, for Lua's game library */
	vm::GamePropertiesViewModel gameProperties(dispatcher, session);
	lua.add(gameProperties);
	qt::QmlViewModel gamePropertiesQml(gameProperties);
	/* a script's own windows; a click or a close goes back to the script */
	vm::FormsViewModel forms(dispatcher);
	qt::QmlViewModel formsQml(forms);
	lua.add(forms);
	vm::Connection formClicks = forms.clicked.connect([&lua](int64_t h) { lua.notify("__chimera_forms_click", h); });
	vm::Connection formCloses = forms.closed.connect([&lua](int64_t h) { lua.notify("__chimera_forms_closed", h); });
	/* tastudio's callbacks: the greenzone invalidated, a branch loaded, made or updated, removed */
	vm::Connection zoneCalls = session.greenzoneInvalidated.connect([&lua](int64_t f) { lua.notify("__chimera_greenzone_invalidated", f); });
	vm::Connection branchLoads = branches.branchLoaded.connect([&lua](int64_t i) { lua.notify("__chimera_branch_loaded", i); });
	vm::Connection branchSaves = branches.branchSaved.connect([&lua](int64_t i) { lua.notify("__chimera_branch_saved", i); });
	vm::Connection branchRemoves = branches.branchRemoved.connect([&lua](int64_t i) { lua.notify("__chimera_branch_removed", i); });
	/* memory-mapped files shared with other programs; the screenshot is a PNG, as Chimera's */
	vm::CommViewModel comm(dispatcher, [&session] {
		vm::Picture::Frame f;
		if (!session.screen.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0) return std::string();
		QByteArray png;
		QBuffer buffer(&png);
		buffer.open(QIODevice::WriteOnly);
		QImage(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32).save(&buffer, "PNG");
		return std::string(png.constData(), static_cast<size_t>(png.size()));
	});
	if (args.isSet(mmfOpt)) comm.screenshotFile.set(QFileInfo(args.value(mmfOpt)).absoluteFilePath().toStdString());
	lua.add(comm);
	/* a SQLite database for scripts, as Chimera's SQL library */
	vm::SqlViewModel sql(dispatcher);
	lua.add(sql);
	if (args.isSet(describeOpt))
	{
		std::printf("%s", lua.describe().c_str());
		return 0;
	}
	/* Scripts run on the machine's thread, before and after every frame, as
	 * Chimera's do, and reach the view models by waiting on the window
	 * (owner, 2026-10-24) */
	lua.toScripts = [&session](const std::function<void()> &job) { return session.onMachineThreadWait(job); };
	lua.toWindow = [&session](const std::function<void()> &job) { return session.onWindowWait(job); };
	lua.toScriptsLater = [&session](std::function<void()> job) { session.onMachine([job = std::move(job)](ce_session *) { job(); }); };
	std::atomic<bool> consoleSaid{false};
	session.setScriptTurns([&lua] { lua.frameStart(); },
		[&lua, &luaConsole, &dispatcher, &consoleSaid] {
			lua.frameDone();
			/* the console's list and functions said again, once a pass of the
			 * window, as the scripts are seen here, between frames: asked from
			 * the window, it waited for the frame the machine was running */
			if (!consoleSaid.exchange(true))
				dispatcher.post([&luaConsole, &consoleSaid, seen = lua.seen()] {
					consoleSaid = false;
					luaConsole.afterFrame(seen);
				});
		});
	/* when no frame is run, the scripts that wait in emu.yield go on, as Chimera's loop resumes them */
	session.setScriptIdle([&lua] { return lua.running(); },
		[&lua, &luaConsole, &dispatcher, &consoleSaid] {
			lua.yieldTurn();
			if (!consoleSaid.exchange(true))
				dispatcher.post([&luaConsole, &consoleSaid, seen = lua.seen()] {
					consoleSaid = false;
					luaConsole.afterFrame(seen);
				});
		});
	/* a script's event.oninputpoll: at each read of the controls a core reports, inside its frame */
	session.setInputPollTurn([&lua] { lua.inputPoll(); });
	/* a script's show_future: asked before each frame played ahead */
	session.setFutureTurn([&lua](int64_t played) { return lua.ask("__chimera_future", {vm::Value(played)}) == "true"; });
	/* a machine opened or closed: the running scripts start again, as Chimera's
	 * Lua Console restarts on a core change (once the window has the new one) */
	auto restartScripts = [&dispatcher, &luaConsole] { dispatcher.post([&luaConsole] { luaConsole.restartScripts(); }); };
	/* not when a script rebooted the machine itself (client.reboot_core): it goes on, as Chimera's */
	vm::Connection scriptsOnOpen = session.opens.changed.connect([&session, restartScripts] {
		if (!session.takeScriptReboot()) restartScripts();
	});
	vm::Connection scriptsOnClose = session.isOpen.changed.connect([&session, restartScripts] {
		if (!session.isOpen.get() && !session.scriptRebooting()) restartScripts();
	});
	/* scripts while turbo is held only when asked (Customize) */
	session.scriptsDuringTurbo(customize.luaDuringTurbo.get());
	vm::Connection turboScripts = customize.luaDuringTurbo.changed.connect(
		[&session, &customize] { session.scriptsDuringTurbo(customize.luaDuringTurbo.get()); });
	/* the machine lets go of the scripts before they go */
	struct LetGo
	{
		vm::SessionViewModel &session;
		~LetGo()
		{
			session.setScriptTurns({}, {});
			session.setScriptIdle({}, {});
			session.setFutureTurn({});
			session.setInputPollTurn({});
		}
	} letGo{session};

	qmlRegisterType<qt::PictureItem>("Chimera", 1, 0, "PictureItem");
	qmlRegisterType<qt::PianoRollItem>("Chimera", 1, 0, "PianoRollItem");

	qt::ClipboardRelay clipboard;
	qt::PictureFiles pictureFiles(session.screen);
	/* Capture Mouse: the pointer kept over the picture */
	qt::PointerCapture pointerCapture;
	/* the pointer's buttons, routed as keys once the key routing is made */
	qt::ButtonRouter buttonRouter;
	/* files and folders picked with the desktop's own chooser, and what
	 * Chimera's own lists where there is none (dialogs/PickerBase.qml) */
	qt::PortalFileChooser portalChooser;
	qt::FilePlaces filePlaces;
	/* Windows' own dialog, for one that carries a tick box (Qt's takes none) */
	qt::SystemFileDialog systemDialog;
	pictureFiles.say = [&session](const std::string &text) { session.status.set(text); };
	QQmlApplicationEngine engine;
	/* Qt picks the look of the dialogs it draws itself by the style's name,
	 * and knows Chimera's only as ChimeraStyle: told it is Fusion's, they
	 * are Fusion's, a picked line in the theme's selection colour (#389) */
	engine.setExtraFileSelectors({QStringLiteral("Fusion")});
	/* every QML warning counted: CHIMERA_QT_STRICT_QML makes any of them a
	 * failure at the end (the window test), since a binding to a name no
	 * view model has reads as nothing and only warns */
	int qmlWarnings = 0;
	QObject::connect(&engine, &QQmlEngine::warnings, [&qmlWarnings](const QList<QQmlError> &list) { qmlWarnings += list.size(); });
	engine.addImageProvider(QStringLiteral("controller"), controllerPicture);
	engine.setInitialProperties({
		{QStringLiteral("chromeless"), args.isSet(chromelessOpt)},
		{QStringLiteral("session"), QVariant::fromValue<QObject *>(&sessionQml)},
		{QStringLiteral("pianoRoll"), QVariant::fromValue<QObject *>(&pianoRollQml)},
		{QStringLiteral("ramSearch"), QVariant::fromValue<QObject *>(&ramSearchQml)},
		{QStringLiteral("ramWatch"), QVariant::fromValue<QObject *>(&ramWatchQml)},
		{QStringLiteral("newProject"), QVariant::fromValue<QObject *>(&newProjectQml)},
		{QStringLiteral("hexEditor"), QVariant::fromValue<QObject *>(&hexEditorQml)},
		{QStringLiteral("controls"), QVariant::fromValue<QObject *>(&controlsQml)},
		{QStringLiteral("themes"), QVariant::fromValue<QObject *>(&themesQml)},
		{QStringLiteral("themeColors"), QVariant::fromValue<QObject *>(&themeColors)},
		{QStringLiteral("hotkeys"), QVariant::fromValue<QObject *>(&hotkeysQml)},
		{QStringLiteral("keyNamer"), QVariant::fromValue<QObject *>(&keyNamer)},
		{QStringLiteral("hotkeyRelay"), QVariant::fromValue<QObject *>(&relay)},
		{QStringLiteral("clipboard"), QVariant::fromValue<QObject *>(&clipboard)},
		{QStringLiteral("pictureFiles"), QVariant::fromValue<QObject *>(&pictureFiles)},
		{QStringLiteral("pointerCapture"), QVariant::fromValue<QObject *>(&pointerCapture)},
		{QStringLiteral("buttonRouter"), QVariant::fromValue<QObject *>(&buttonRouter)},
		{QStringLiteral("help"), QVariant::fromValue<QObject *>(&helpQml)},
		{QStringLiteral("mcp"), QVariant::fromValue<QObject *>(&mcpQml)},
		{QStringLiteral("mediaMaker"), QVariant::fromValue<QObject *>(&mediaMakerQml)},
		{QStringLiteral("cacheManager"), QVariant::fromValue<QObject *>(&cacheManagerQml)},
		{QStringLiteral("customize"), QVariant::fromValue<QObject *>(&customizeQml)},
		{QStringLiteral("messages"), QVariant::fromValue<QObject *>(&messagesQml)},
		{QStringLiteral("display"), QVariant::fromValue<QObject *>(&displayQml)},
		{QStringLiteral("paths"), QVariant::fromValue<QObject *>(&pathsQml)},
		{QStringLiteral("firmware"), QVariant::fromValue<QObject *>(&firmwareQml)},
		{QStringLiteral("configFiles"), QVariant::fromValue<QObject *>(&configFilesQml)},
		{QStringLiteral("dataFolder"), QVariant::fromValue<QObject *>(&dataFolderQml)},
		{QStringLiteral("settings"), QVariant::fromValue<QObject *>(&settingsQml)},
		{QStringLiteral("precompiled"), QVariant::fromValue<QObject *>(&precompiledQml)},
		{QStringLiteral("traceLogger"), QVariant::fromValue<QObject *>(&traceLoggerQml)},
		{QStringLiteral("debuggerTool"), QVariant::fromValue<QObject *>(&debuggerQml)},
		{QStringLiteral("guiOverlay"), QVariant::fromValue<QObject *>(&guiOverlayQml)},
		{QStringLiteral("idleEye"), QVariant::fromValue<QObject *>(&idleEyeQml)},
		{QStringLiteral("surfaceViewer"), QVariant::fromValue<QObject *>(&surfaceViewerQml)},
		{QStringLiteral("macroTool"), QVariant::fromValue<QObject *>(&macroToolQml)},
		{QStringLiteral("encoder"), QVariant::fromValue<QObject *>(&encoderQml)},
		{QStringLiteral("hostInput"), QVariant::fromValue<QObject *>(&hostInputQml)},
		{QStringLiteral("forms"), QVariant::fromValue<QObject *>(&formsQml)},
		{QStringLiteral("projectDetails"), QVariant::fromValue<QObject *>(&projectDetailsQml)},
		{QStringLiteral("luaConsole"), QVariant::fromValue<QObject *>(&luaConsoleQml)},
		{QStringLiteral("toolWindows"), QVariant::fromValue<QObject *>(&toolWindowsQml)},
		{QStringLiteral("gameProperties"), QVariant::fromValue<QObject *>(&gamePropertiesQml)},
		{QStringLiteral("branches"), QVariant::fromValue<QObject *>(&branchesQml)},
		{QStringLiteral("cores"), QVariant::fromValue<QObject *>(&coresQml)},
	});
	/* Chimera's VSync Enabled: the window's pictures given to the screen at its refresh */
	{
		QSurfaceFormat format = QSurfaceFormat::defaultFormat();
		format.setSwapInterval(session.vsyncEnabled.get() ? 1 : 0);
		QSurfaceFormat::setDefaultFormat(format);
	}
	/* a path as a file URL and back, for every window, as Qt makes them (a
	 * Windows path written into "file://" by hand came back as "/C:/...") */
	engine.rootContext()->setContextProperty(QStringLiteral("localFiles"), &pictureFiles);
	/* --headless, for every window: none is shown, and a dialog that opens ends the run */
	engine.rootContext()->setContextProperty(QStringLiteral("headlessRun"), &headlessRun);
	engine.rootContext()->setContextProperty(QStringLiteral("portalChooser"), &portalChooser);
	engine.rootContext()->setContextProperty(QStringLiteral("filePlaces"), &filePlaces);
	engine.rootContext()->setContextProperty(QStringLiteral("systemDialog"), &systemDialog);
	/* every menu item reads it, the style's own (style/MenuItem.qml) */
	engine.rootContext()->setContextProperty(QStringLiteral("menuLetters"), &menuLetters);
	/* another language chosen in Config > Language: the window's words change at once */
	vm::Connection languageWatch = customize.language.changed.connect([&app, &engine, &customize] {
		if (qt::useLanguage(app, QString::fromStdString(customize.language.get()))) engine.retranslate();
	});
	engine.load(QUrl(QStringLiteral("qrc:/chimera/Main.qml")));
	if (engine.rootObjects().isEmpty()) return 1;
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
	mainWindow = window;
	/* Encode Video's Capture Lua and Capture OSD, as Chimera's. Capture Lua:
	 * the shapes scripts draw, painted into each frame at the machine's own
	 * size. Capture OSD: the frame as the picture area shows it, at that
	 * area's size (so the window's 2x or 3x is the video's), with what
	 * scripts draw and the counters and messages as the window lays them
	 * out (Main.qml's osdLayer) */
	encoder.compose = [window, &guiOverlay](std::vector<uint32_t> &pixels, int32_t &w, int32_t &h, double shownW, double shownH, bool osd) {
		auto *picture = window->findChild<qt::PictureItem *>(QStringLiteral("picture"));
		if (!osd || picture == nullptr)
		{
			QImage image(reinterpret_cast<uchar *>(pixels.data()), w, h, w * 4, QImage::Format_RGB32);
			qt::paintShapes(image, QString::fromStdString(guiOverlay.shapes.get()));
			return;
		}
		qt::PictureItem::Drawn shown = picture->drawnAs(pixels.data(), w, h, shownW, shownH);
		qt::ShapesPlace place;
		place.surface = shown.surface;
		place.scaleX = shown.scaleX;
		place.scaleY = shown.scaleY;
		place.textScale = window->property("osdScale").toDouble();
		place.pictureArea = true;
		qt::paintShapes(shown.image, QString::fromStdString(guiOverlay.shapes.get()), place);
		QVariant layer;
		QMetaObject::invokeMethod(window, "osdLayer", Q_RETURN_ARG(QVariant, layer), Q_ARG(QVariant, shown.image.width()),
			Q_ARG(QVariant, shown.image.height()));
		std::vector<qt::OverlayText> texts;
		for (const QVariant &v : layer.toList())
		{
			const QVariantMap m = v.toMap();
			texts.push_back({m.value(QStringLiteral("text")).toString(), m.value(QStringLiteral("x")).toInt(), m.value(QStringLiteral("y")).toInt(),
				QColor::fromString(m.value(QStringLiteral("color")).toString())});
		}
		qt::paintTexts(shown.image, texts, static_cast<int>(std::lround(12 * place.textScale)));
		w = shown.image.width();
		h = shown.image.height();
		pixels.resize(static_cast<size_t>(w) * h);
		for (int y = 0; y < h; y++)
			std::memcpy(pixels.data() + static_cast<size_t>(y) * w, shown.image.constScanLine(y), static_cast<size_t>(w) * 4);
	};
	/* the place and size kept last time, when asked (Display) */
	if (display.saveWindowPosition.get())
	{
		const QStringList g = QString::fromStdString(display.windowGeometry.get()).split(QLatin1Char(','));
		if (g.size() == 4 && g[2].toInt() > 100 && g[3].toInt() > 100) window->setGeometry(g[0].toInt(), g[1].toInt(), g[2].toInt(), g[3].toInt());
	}
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, [&display, window] {
		if (!display.saveWindowPosition.get() || window->visibility() == QWindow::FullScreen) return;
		const QRect r = window->geometry();
		display.windowGeometry.set(QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()).toStdString());
	});
#ifdef _WIN32
	/* Chimera's Windows Fullscreen Hacks: a border kept round the window in
	 * full screen (Qt's own way of keeping Windows from taking the screen
	 * for the window alone), set now and whenever it changes */
	auto fullscreenBorder = [&display, window] {
		window->create();
		if (auto *native = window->nativeInterface<QNativeInterface::Private::QWindowsWindow>())
			native->setHasBorderInFullScreen(display.fullscreenHacks.get());
	};
	fullscreenBorder();
	vm::Connection fullscreenHacksWatch = display.fullscreenHacks.changed.connect(fullscreenBorder);
#endif
	if (!headless && (args.isSet(fullscreenOpt) || customize.startFullscreen.get())) window->showFullScreen();
	/* each picture shown: the vsync throttle's tick, and the next one asked for while it is on */
	QObject::connect(window, &QQuickWindow::frameSwapped, window, [&session, window] {
		session.vsyncTick();
		if (session.wantsVsync()) window->update();
	}, Qt::DirectConnection);
	qt::KeyFilter keys(session, hotkeys, hostInput);
	buttonRouter.route = [&keys](const QString &name, bool down) { keys.button(name, down); };
	window->installEventFilter(&keys);
	/* TAStudio passes the keys on too, as Chimera's does: the machine's and the hotkeys */
	if (auto *tastudio = window->findChild<QQuickWindow *>(QStringLiteral("tastudioWindow"))) tastudio->installEventFilter(&keys);
	/* --show ram-search or settings: the tool windows by name; --lua and
	 * --luaconsole open the Lua Console, as Chimera's */
	{
		static const std::pair<const char *, const char *> tools[] = {{"new-project", "newProjectWindow"},
			{"movie-import", "movieImportWindow"}, {"ram-search", "ramSearchWindow"}, {"ram-watch", "ramWatchWindow"},
			{"hex", "hexEditorWindow"}, {"controls", "controllerConfigWindow"}, {"hotkeys", "hotkeyConfigWindow"},
			{"themes", "themesWindow"}, {"lua", "luaConsoleWindow"}, {"tastudio", "tastudioWindow"},
			{"undo-history", "undoHistoryWindow"}, {"project-details", "headerInfoWindow"}, {"comments", "editCommentsWindow"},
			{"subtitles", "editSubtitlesWindow"}, {"tastudio-settings", "tastudioSettingsWindow"}, {"log", "logWindow"},
			{"report", "reportIssueWindow"}, {"media", "mediaMakerWindow"}, {"cache", "cacheManagerWindow"},
			{"customize", "customizeWindow"}, {"messages", "messagesWindow"}, {"sound", "soundWindow"},
			{"display", "displayWindow"}, {"budgets", "greenzoneBudgetsWindow"}, {"paths", "pathsWindow"},
			{"firmware", "firmwareWindow"}, {"data", "dataFolderWindow"}, {"precompiled", "precompiledWindow"},
			{"trace", "traceLoggerWindow"}, {"debugger", "debuggerWindow"}, {"surfaces", "surfaceViewerWindow"},
			{"macros", "macroToolWindow"}, {"encode", "encodeVideoWindow"}, {"cores", "coresWindow"}};
		auto show = [window, headless](const char *objectName) {
			if (headless) return;
			if (auto *tool = window->findChild<QQuickWindow *>(QString::fromLatin1(objectName))) tool->show();
		};
		if (args.isSet(showOpt))
		{
			const QString asked = args.value(showOpt);
			const auto *found = std::find_if(std::begin(tools), std::end(tools), [&asked](const auto &t) { return asked == QLatin1String(t.first); });
			if (found != std::end(tools)) show(found->second);
			else std::fprintf(stderr, "chimera: --show %s: no tool window has that name\n", qPrintable(asked));
		}
		if (args.isSet(luaOpt) || args.isSet(luaConsoleOpt)) show("luaConsoleWindow");
	}
	/* forms.openfile: the window's file dialog (the desktop's chooser on
	 * Linux), shown and waited for, modal as Chimera's */
	forms.openFile = [&engine, window, &headlessRun](const std::string &fileName, const std::string &folder, const std::vector<std::string> &filters) {
		/* nobody to choose: the run ends, as on any dialog */
		if (headlessRun.enabled())
		{
			headlessRun.blocked(QStringLiteral("Open"), QString::fromStdString(fileName));
			return std::string();
		}
		QQmlComponent component(&engine);
		component.setData("import ChimeraDialogs\nFileDialog { fileMode: FileDialog.OpenFile }", QUrl());
		QStringList names;
		for (const std::string &f : filters) names << QString::fromStdString(f);
		QVariantMap initial{{QStringLiteral("nameFilters"), names}, {QStringLiteral("parentWindow"), QVariant::fromValue<QObject *>(window)}};
		if (!folder.empty()) initial.insert(QStringLiteral("currentFolder"), QUrl::fromLocalFile(QString::fromStdString(folder)));
		if (!fileName.empty()) initial.insert(QStringLiteral("selectedFile"), QUrl::fromLocalFile(QString::fromStdString(fileName)));
		const std::unique_ptr<QObject> dialog(component.createWithInitialProperties(initial));
		if (!dialog) return std::string();
		/* until it is answered, either way */
		QEventLoop wait;
		QObject::connect(dialog.get(), SIGNAL(accepted()), &wait, SLOT(quit()));
		QObject::connect(dialog.get(), SIGNAL(rejected()), &wait, SLOT(quit()));
		QMetaObject::invokeMethod(dialog.get(), "open");
		wait.exec();
		/* the dialog's result: 1 accepted */
		if (dialog->property("result").toInt() != 1) return std::string();
		return dialog->property("selectedFile").toUrl().toLocalFile().toStdString();
	};
	/* what scripts ask of the window (Chimera's client library) */
	toolWindows.screenshot = [&takeScreenshot](const std::string &path) { takeScreenshot(QString::fromStdString(path)); };
	toolWindows.screenshotToClipboard = [&rawToClipboard] { rawToClipboard(); };
	toolWindows.resizeWindow = [window] { QMetaObject::invokeMethod(window, "frameBufferResized", Q_ARG(QVariant, QVariant(true))); };
	toolWindows.windowPosition = [window] { return std::pair<int64_t, int64_t>{window->x(), window->y()}; };
	/* the agent's Lua runs where the person sees it: the Lua Console opened, as a person opens it */
	mcp.showLuaConsole = [window, headless] {
		if (headless) return;
		if (auto *tool = window->findChild<QQuickWindow *>(QStringLiteral("luaConsoleWindow")); tool != nullptr && !tool->isVisible()) tool->show();
	};
	/* a tool window a script asks for (Chimera's client.opentasstudio), by the same names */
	vm::Connection showAskedWatch = toolWindows.showAsked.changed.connect([&toolWindows, window, headless] {
		const QString asked = QString::fromStdString(toolWindows.showAsked.get());
		if (asked.isEmpty() || headless) return;
		const QString name = asked == QStringLiteral("tastudio") ? QStringLiteral("tastudioWindow")
			: asked == QStringLiteral("lua") ? QStringLiteral("luaConsoleWindow")
			: asked == QStringLiteral("ram-watch") ? QStringLiteral("ramWatchWindow")
			: asked == QStringLiteral("ram-search") ? QStringLiteral("ramSearchWindow")
			: asked == QStringLiteral("hex") ? QStringLiteral("hexEditorWindow") : asked;
		if (auto *tool = window->findChild<QQuickWindow *>(name))
		{
			tool->show();
			tool->raise();
		}
	});

	std::string error;
	/* Chimera's --userdata pairs, in the scripts' userdata before any script runs */
	if (args.isSet(userdataOpt))
	{
		auto quoted = [](const QString &text) {
			std::string out = "\"";
			for (const unsigned char c : text.toStdString())
			{
				if (c == '\\' || c == '"') out += '\\', out += static_cast<char>(c);
				else if (c < 32 || c == 127)
				{
					char digits[8];
					std::snprintf(digits, sizeof digits, "\\%03d", c);
					out += digits;
				}
				else out += static_cast<char>(c);
			}
			return out + "\"";
		};
		std::string code;
		for (const QString &pair : args.value(userdataOpt).split(QLatin1Char(';')))
		{
			const qsizetype colon = pair.indexOf(QLatin1Char(':'));
			if (colon < 0)
			{
				std::printf("userdata: malformed (';' without ':')\n");
				code.clear();
				break;
			}
			code += "__chimera_userdata_pair(" + quoted(pair.left(colon)) + ", " + quoted(pair.mid(colon + 1)) + ")\n";
		}
		if (!code.empty() && !lua.run(code, "userdata", error)) std::printf("userdata: %s\n", error.c_str());
	}
	/* opened in the console and started there, as Chimera's --lua does (a
	 * .luases file is a session); after the game or project the command line
	 * opens is open, as Chimera starts it after --project, or at once when
	 * nothing is opened (or the opening never finishes) */
	auto startScript = [&args, &luaOpt, &luaConsole, &session] {
		std::string e;
		const std::string script = QFileInfo(args.value(luaOpt)).absoluteFilePath().toStdString();
		if (!QFileInfo::exists(args.value(luaOpt)))
			std::printf("lua: could not read %s\n", qPrintable(args.value(luaOpt)));
		else if (args.value(luaOpt).endsWith(QStringLiteral(".luases"), Qt::CaseInsensitive))
			luaConsole.run("openSession", {script}, e);
		else
			luaConsole.run("openScript", {script}, e);
		/* its first lines have run: the machine goes on */
		session.holdFramesForScript(false);
	};
	vm::Connection scriptAfterOpen, scriptAfterProject;
	bool scriptStarted = false;
	if (args.isSet(luaOpt))
	{
		/* no frame before the script starts, as in Chimera, which starts it before the first */
		if (anyProject) session.holdFramesForScript(true);
		if (!anyProject)
		{
			scriptStarted = true;
			startScript();
		}
		else
		{
			/* a project is open once TAStudio has it (its movie, markers and branches) */
			auto ready = [&] {
				if (scriptStarted || !session.isOpen.get() || !session.hasProject.get()) return;
				scriptStarted = true;
				/* after the restart the machine's opening asked for, so it is not started twice */
				dispatcher.post(startScript);
			};
			scriptAfterOpen = session.opens.changed.connect(ready);
			scriptAfterProject = session.hasProject.changed.connect(ready);
			QTimer::singleShot(60000, &app, [&] {
				if (scriptStarted) return;
				scriptStarted = true;
				startScript();
			});
		}
	}
	/* the movie is the machine's: it is made longer once one is open */
	vm::Connection fillWatch;
	if (args.isSet(fillOpt))
	{
		const int64_t frames = static_cast<int64_t>(args.value(fillOpt).toLongLong());
		fillWatch = session.opens.changed.connect([&pianoRoll, &fillWatch, frames] {
			std::string e;
			pianoRoll.run("fill", {frames}, e);
			fillWatch.disconnect();
		});
	}
	/* a project, with its package when one was named (else its core is
	 * found among the installed ones); a game starts New Project from it, as
	 * a file dropped on the window does */
	if (anyProject)
		session.run("openProject", packageGiven.isEmpty() ? vm::Args{projectGiven.toStdString()}
			: vm::Args{projectGiven.toStdString(), packageGiven.toStdString()}, error);
	else if (!gameGiven.isEmpty())
		QMetaObject::invokeMethod(window, "newProjectFrom", Q_ARG(QVariant, gameGiven));
	/* the files a later start hands over: the first opened as the window opens a file (asking
	 * about the open project's changes first), as Chimera's opens the first argument */
	/* a run nobody watches takes nothing handed over */
	if (!headless) single.listen([window](const QStringList &handed) {
		if (handed.isEmpty() || handed.first().isEmpty()) return;
		/* a project's package, named after it as on the command line, goes with it */
		const QString package = handed.size() >= 2 && handed[1].endsWith(QStringLiteral(".chimeraCore"), Qt::CaseInsensitive) ? handed[1] : QString();
		QMetaObject::invokeMethod(window, "openHanded", Q_ARG(QVariant, handed.first()), Q_ARG(QVariant, package));
	});
	/* Chimera's jump list: the taskbar's Recent list, and each project opened
	 * on it - not one a run nobody watches opened */
	if (!headless) qt::JumpList::showRecent();
	vm::Connection jumpListWatch = session.opens.changed.connect([&session, headless] {
		const std::string path = session.projectPath.get();
		if (headless) return;
		if (!path.empty()) qt::JumpList::addRecent(path, std::filesystem::path(path).filename().string());
	});
	if (args.isSet(mcpServerOpt))
	{
		std::string why;
		if (const std::optional<vm::Value> at = mcp.run("startForRun", {args.isSet(mcpChangesOpt)}, why))
		{
			std::string endpoint;
			vm::fromValue(*at, endpoint);
			std::printf("[mcp] the server is started at %s\n", endpoint.c_str());
		}
		else
			std::fprintf(stderr, "[mcp] --mcp-server: the server could not be started: %s\n", why.c_str());
		std::fflush(stdout);
	}
	if (args.isSet(playOpt))
	{
		/* play once a machine is open: the one named here, or one a script
		 * opened */
		auto connection = std::make_shared<vm::Connection>();
		*connection = session.opens.changed.connect([&session, connection] {
			std::string e;
			session.run("play", {}, e);
			connection->disconnect();
		});
	}

	/* measuring */
	std::mutex swapM;
	std::vector<qint64> swaps, rollSwaps;
	QElapsedTimer clock;
	clock.start();
	std::vector<double> paints;
	QPointer<qt::PianoRollItem> roll;
	/* the script asked about each cell as a roll is drawn (Lua's tastudio.onqueryitem...):
	 * the window's thread waits while the roll is drawn, so the script is not running */
	qt::PianoRollItem::setQuery([&lua](int kind, qint64 row, const QString &column) {
		static const char *const hooks[] = {"__chimera_query_bg", "__chimera_query_text", "__chimera_query_icon"};
		return QString::fromStdString(lua.query(hooks[std::clamp(kind, 0, 2)], row, column.toStdString()));
	});
	auto *picture = window->findChild<qt::PictureItem *>(QStringLiteral("picture"));
	int64_t framesAtStart = -1;
	qint64 shownAtStart = 0;
	QElapsedTimer runClock;
	vm::Connection startWatch;
	int exitCode = 0;
	if (args.isSet(measureOpt))
	{
		const double seconds = args.value(measureOpt).toDouble();
		QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
			std::lock_guard<std::mutex> lock(swapM);
			swaps.push_back(clock.nsecsElapsed());
		}, Qt::DirectConnection);
		auto *scroller = new QTimer(&app);
		if (args.isSet(scrollOpt))
		{
			pianoRoll.followPlayback.set(false);
			QObject::connect(scroller, &QTimer::timeout, &app, [&] {
				const int64_t count = std::max<int64_t>(1, inputs.size());
				pianoRoll.topFrame.set((pianoRoll.topFrame.get() + 997) % count);
			});
			scroller->start(16);
		}
		/* the clock starts when the machine does */
		startWatch = session.running.changed.connect([&] {
			if (session.running.get() && framesAtStart < 0)
			{
				framesAtStart = session.frame.get();
				shownAtStart = picture != nullptr ? picture->framesShown() : 0;
				runClock.start();
				/* the roll's paints, and its window's frames when it has one of its own (TAStudio's) */
				roll = firstRoll();
				if (roll != nullptr)
				{
					QObject::connect(roll, &qt::PianoRollItem::painted, roll, [&] { paints.push_back(roll->lastPaintMs()); });
					if (roll->window() != nullptr && roll->window() != window)
						QObject::connect(roll->window(), &QQuickWindow::frameSwapped, roll->window(), [&] {
							std::lock_guard<std::mutex> lock(swapM);
							rollSwaps.push_back(clock.nsecsElapsed());
						}, Qt::DirectConnection);
				}
			}
		});
		QTimer::singleShot(static_cast<int>(seconds * 1000), &app, [&] {
			std::vector<double> intervals, rollIntervals;
			{
				std::lock_guard<std::mutex> lock(swapM);
				for (size_t i = 1; i < swaps.size(); i++) intervals.push_back((swaps[i] - swaps[i - 1]) / 1.0e6);
				for (size_t i = 1; i < rollSwaps.size(); i++) rollIntervals.push_back((rollSwaps[i] - rollSwaps[i - 1]) / 1.0e6);
			}
			const double ran = runClock.isValid() ? runClock.nsecsElapsed() / 1.0e9 : 0;
			const int64_t frames = framesAtStart >= 0 ? session.frame.get() - framesAtStart : 0;
			const qint64 shown = picture != nullptr ? picture->framesShown() - shownAtStart : 0;
			const Spread w = spread(intervals), p = spread(paints);
			std::printf("status: %s\n", session.status.get().c_str());
			std::printf("machine: %lld frames in %.2f s = %.2f fps (the machine's own rate is its vsync)\n",
				static_cast<long long>(frames), ran, ran > 0 ? frames / ran : 0.0);
			std::printf("pictures drawn: %lld\n", static_cast<long long>(shown));
			/* asked to play, a run that ran nothing failed (the window test) */
			if (args.isSet(playOpt) && frames <= 0) exitCode = 2;
			std::printf("window frames: %zu, interval ms median %.2f, 95th %.2f, worst %.2f\n",
				intervals.size() + (swaps.empty() ? 0 : 1), w.median, w.p95, w.worst);
			std::printf("piano roll: %lld rows, %zu paints, ms median %.3f, 95th %.3f, worst %.3f\n",
				static_cast<long long>(inputs.size()), paints.size(), p.median, p.p95, p.worst);
			if (!rollSwaps.empty())
			{
				const Spread r = spread(rollIntervals);
				std::printf("piano roll's window frames: %zu, interval ms median %.2f, 95th %.2f, worst %.2f\n",
					rollSwaps.size(), r.median, r.p95, r.worst);
			}
			if (args.isSet(shotOpt))
			{
				std::printf("screenshot: %s\n", window->grabWindow().save(args.value(shotOpt)) ? "saved" : "FAILED");
				/* every other window open, beside it: shot.png -> shot-ramSearchWindow.png */
				for (QWindow *w : QGuiApplication::topLevelWindows())
				{
					auto *q = qobject_cast<QQuickWindow *>(w);
					if (q == nullptr || q == window || !q->isVisible()) continue;
					QString path = args.value(shotOpt);
					path.insert(path.lastIndexOf('.'), QStringLiteral("-") + q->objectName());
					/* where it is, beside the main window's place */
					std::printf("screenshot: %s %s, at %d,%d %dx%d (the main window at %d,%d %dx%d)\n", qPrintable(q->objectName()),
						q->grabWindow().save(path) ? "saved" : "FAILED", q->x(), q->y(), q->width(), q->height(),
						window->x(), window->y(), window->width(), window->height());
				}
			}
			std::fflush(stdout);
			app.exit(exitCode);
		});
	}

	int rc = app.exec();
	/* no roll asks the script once the window has stopped */
	qt::PianoRollItem::setQuery({});
	/* a run nobody watches ends with what ended it, whatever else asked to close meanwhile */
	if (headlessRun.endedWith() >= 0) rc = headlessRun.endedWith();
	/* nothing more handed over once the window's things start going */
	single.stop();
	if (rc == 0 && qmlWarnings > 0 && qEnvironmentVariableIsSet("CHIMERA_QT_STRICT_QML"))
	{
		std::fprintf(stderr, "%d QML warning(s), and CHIMERA_QT_STRICT_QML is set\n", qmlWarnings);
		return 1;
	}
	return rc;
}
