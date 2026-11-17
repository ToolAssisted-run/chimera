/* The Emulator menu holds only what applies to the project open (owner,
 * 2026-10-31, issue 428): Chimera's Display Game Time when the game has a
 * timer, then the core's own windows, as its package names them (issue 610:
 * Chimera knows no core tool), and nothing greyed out; the menu is gone when
 * there is nothing. A core's file tools are under Tools (test_core_tools_menu).
 * EmulatorMenu.qml over a stand-in for the session, opened, and the items
 * it shows read. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStringList>
#include <QTest>

#include <cstdio>

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

const char *const kWindow = R"(
import QtQuick
import QtQuick.Controls
import "qrc:/chimera"
ApplicationWindow {
    id: root
    width: 400; height: 400; visible: true
    property alias stub: stub
    property alias menu: menu
    // what EmulatorMenu reads of the session
    QtObject {
        id: stub
        property bool isOpen: true
        property bool isGameCore: false
        property bool hasGameTimer: false
        property bool showGameTime: false
        property string coreWindows: "[]"
    }
    property string asked: ""
    EmulatorMenu { id: menu; session: stub; onOpenCoreWindow: (id) => root.asked = id }
}
)";

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	/* the window's own style: the system's default one (Windows') is not in the bundle */
	QQuickStyle::setStyle(QStringLiteral("ChimeraStyle"));
	QQmlApplicationEngine engine;
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	CHECK(QTest::qWaitForWindowExposed(window));
	QObject *stub = window->property("stub").value<QObject *>(), *menu = window->property("menu").value<QObject *>();
	CHECK(stub != nullptr && menu != nullptr);
	if (stub == nullptr || menu == nullptr) return 1;
	QMetaObject::invokeMethod(menu, "popup");
	CHECK(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }));

	/* the items shown, in order: a divider is "-" */
	auto shown = [&] {
		QCoreApplication::processEvents();
		QStringList out;
		for (int i = 0; i < menu->property("count").toInt(); i++)
		{
			QQuickItem *item = nullptr;
			QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
			if (item == nullptr || !item->isVisible() || item->height() <= 0) continue;
			const QVariant text = item->property("text");
			out << (text.isValid() ? text.toString() : QStringLiteral("-"));
		}
		return out;
	};
	auto set = [&](const char *name, const QVariant &value) { stub->setProperty(name, value); };

	/* a core that offers nothing: no menu */
	CHECK(!menu->property("shown").toBool());
	/* the core's own windows, by its package's words, in its order */
	set("coreWindows", QStringLiteral("[{\"id\":\"kit.registers\",\"menu\":\"CPU Registers\",\"qml\":\"windows/kit/Registers.qml\"},"
		"{\"id\":\"kit.trace\",\"menu\":\"Trace Logger\",\"qml\":\"windows/kit/TraceLogger.qml\"}]"));
	CHECK(menu->property("shown").toBool());
	CHECK(shown() == QStringList({"CPU Registers", "Trace Logger"}));
	/* a game's timer, Chimera's own, parted from the core's windows */
	set("hasGameTimer", true);
	CHECK(shown() == QStringList({"main.display_game_time", "-", "CPU Registers", "Trace Logger"}));
	/* one picked: its id is asked for */
	{
		QQuickItem *item = nullptr;
		QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, menu->property("count").toInt() - 1));
		CHECK(item != nullptr);
		if (item != nullptr) QMetaObject::invokeMethod(item, "triggered");
		CHECK(window->property("asked").toString() == QLatin1String("kit.trace"));
	}
	/* picking an item closed the menu: open it again */
	QMetaObject::invokeMethod(menu, "popup");
	CHECK(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }));
	/* the timer alone, no divider */
	set("coreWindows", QStringLiteral("[]"));
	CHECK(shown() == QStringList({"main.display_game_time"}));
	set("hasGameTimer", false);
	/* no machine open: no menu, whatever was offered */
	set("isOpen", false);
	CHECK(!menu->property("shown").toBool());
	/* a game core's menu is Game */
	set("isOpen", true);
	set("isGameCore", true);
	CHECK(menu->property("title").toString() == QLatin1String("main.game"));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
