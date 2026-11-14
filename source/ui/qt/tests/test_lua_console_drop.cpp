/* Files dropped on the Lua Console (issue 561): a script is opened and run as
 * Open Script opens one, a session as Open Session does, and any other file
 * is left alone. And the script list's Script column widened and narrowed by
 * dragging its heading's right edge, its width kept in the view model, which
 * the settings keep (issue 586). The window over the view model the program
 * uses; the drop and the drag are real ones, through Qt's test module.
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

/* an item by its objectName anywhere under `item`, a list's rows too (they
 * are its children as items, not always as objects) */
QQuickItem *itemNamed(QQuickItem *item, const QString &name)
{
	if (item == nullptr) return nullptr;
	if (item->objectName() == name) return item;
	for (QQuickItem *child : item->childItems())
		if (QQuickItem *found = itemNamed(child, name)) return found;
	return nullptr;
}

/* the left button pressed on `at`, moved by dx, and let go, as a person drags */
void drag(QQuickWindow *window, QPoint at, int dx)
{
	QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
	for (int step = 1; step <= 4; step++) QTest::mouseMove(window, at + QPoint(dx * step / 4, 0));
	QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at + QPoint(dx, 0));
	QCoreApplication::processEvents();
}

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

	/* the Script column: 92 pixels at first; its heading's right edge dragged
	 * 60 to the right makes it 152, kept in the view model, and the rows'
	 * names as wide; dragged far to the left, it stops at 40 */
	QQuickItem *heading = itemNamed(window->contentItem(), QStringLiteral("luaScriptHeading"));
	QQuickItem *edge = itemNamed(heading, QStringLiteral("columnEdge"));
	CHECK(heading != nullptr && edge != nullptr);
	if (heading != nullptr && edge != nullptr)
	{
		CHECK(console.scriptColumnWidth.get() == 92 && qRound(heading->width()) == 92);
		const QPoint edgeAt = edge->mapToScene(QPointF(edge->width() / 2, edge->height() / 2)).toPoint();
		drag(window, edgeAt, 60);
		CHECK(QTest::qWaitFor([&] { return console.scriptColumnWidth.get() == 152; }, 2000));
		CHECK(QTest::qWaitFor([&] { return qRound(heading->width()) == 152; }, 2000));   /* (the heading row lays itself out at the next frame) */
		QQuickItem *name = itemNamed(window->contentItem(), QStringLiteral("luaScriptName"));
		CHECK(name != nullptr && qRound(name->width()) == 148);
		std::printf("Script column dragged 60 to the right: %lld wide\n", static_cast<long long>(console.scriptColumnWidth.get()));
		const QPoint widerEdge = edge->mapToScene(QPointF(edge->width() / 2, edge->height() / 2)).toPoint();
		drag(window, widerEdge, -400);
		CHECK(QTest::qWaitFor([&] { return console.scriptColumnWidth.get() == 40; }, 2000));
		CHECK(QTest::qWaitFor([&] { return qRound(heading->width()) == 40; }, 2000));   /* (the heading row lays itself out at the next frame) */
	}

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
