/* Files dropped on the Lua Console (issue 561): a script is opened and run as
 * Open Script opens one, a session as Open Session does, and any other file
 * is left alone. The window over the view model the program uses; the drop
 * is a real one, through Qt's test module.
 *   test-lua-console-drop <work folder> */
#include <QGuiApplication>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "../qml_view_model.hpp"

namespace fs = std::filesystem;
using namespace chimera;

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* the view models' work, into Qt's event loop, as the window has it */
class QtDispatcher : public vm::Dispatcher
{
public:
	explicit QtDispatcher(QObject *context) : context_(context) {}
	void post(std::function<void()> fn) override { QMetaObject::invokeMethod(context_, std::move(fn), Qt::QueuedConnection); }

private:
	QObject *context_;
};

/* a dropped file's URL as a path, as the program's own helper gives it */
const char *const kWindow = R"(
import QtQuick
import "qrc:/chimera"
LuaConsole {
    vm: console; visible: true; width: 700; height: 400
    localPath: (url) => decodeURIComponent(url.toString().replace(/^file:\/\/(\/(?=[A-Za-z]:))?/, ""))
}
)";

/* files dragged in over the window's middle and let go, as the system hands them over */
void dropOn(QQuickWindow *window, const QList<QUrl> &urls)
{
	const QPoint at(window->width() / 2, window->height() / 2);
	QMimeData mime;
	mime.setUrls(urls);
	QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &enter);
	QDragMoveEvent move(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &move);
	QDropEvent let(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &let);
	QCoreApplication::processEvents();
}

QUrl urlOf(const fs::path &p) { return QUrl::fromLocalFile(QString::fromStdString(p.string())); }

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: test-lua-console-drop <work folder>\n");
		return 2;
	}
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	const fs::path work = argv[1];
	std::error_code ec;
	fs::remove_all(work, ec);
	fs::create_directories(work);
	auto write = [&](const char *name, const std::string &text) {
		std::ofstream(work / name, std::ios::binary) << text;
		return work / name;
	};
	const fs::path one = write("one.lua", "print('one ran')\n"), two = write("Two.LUA", "print('two ran')\n");
	const fs::path other = write("notes.txt", "not a script\n");
	const fs::path three = write("three.lua", "print('three ran')\n");
	const fs::path session = write("kept.luases", "1 " + three.string() + "\n");

	QtDispatcher d(&app);
	vm::LuaBridge lua;
	vm::LuaConsoleViewModel console(d, lua);
	qt::QmlViewModel consoleQml(console);

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("console"), &consoleQml);
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	CHECK(QTest::qWaitForWindowExposed(window));

	const vm::RowModel &list = *console.rowLists()[1].model;
	auto names = [&] {
		std::string all;
		for (int64_t r = 0; r < list.rowCount(); r++)
		{
			std::string name;
			vm::fromValue(list.cell(r, 0), name);
			all += (all.empty() ? "" : ",") + name;
		}
		return all;
	};
	/* two scripts (an ending in capitals too) and a file that is not one: the scripts listed and run */
	dropOn(window, {urlOf(one), urlOf(other), urlOf(two)});
	CHECK(QTest::qWaitFor([&] { return list.rowCount() == 2; }, 3000));
	CHECK(names() == "one,Two");
	std::printf("dropped one.lua, notes.txt, Two.LUA: listed [%s]\n", names().c_str());
	/* a session: its list read, as Open Session does */
	dropOn(window, {urlOf(session)});
	CHECK(QTest::qWaitFor([&] { return names() == "three"; }, 3000));
	std::printf("dropped kept.luases: listed [%s]\n", names().c_str());

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
