/* The Emulator menu holds only what the core open offers (owner,
 * 2026-10-31, issue 428): an item the core does not offer is not in it, not
 * greyed out, and the menu is gone when the core offers nothing. Chimera has
 * no item of its own there: an importer's item is named by the core's
 * package. EmulatorMenu.qml over a stand-in for the session, opened, and
 * the items it shows read. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
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
        property bool hasSaveData: false
        property string movieImportMenu: ""
        property bool hasRegisters: false
        property string surfaces: "[]"
        property bool hasTrace: false
    }
    EmulatorMenu { id: menu; session: stub }
}
)";

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
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
	/* registers alone: CPU Registers, and nothing greyed beside it */
	set("hasRegisters", true);
	CHECK(menu->property("shown").toBool());
	CHECK(shown() == QStringList({"main.cpu_registers"}));
	/* a game's timer, the core's importer by its package's words, the trace:
	 * the two groups parted */
	set("hasRegisters", false);
	set("hasGameTimer", true);
	set("movieImportMenu", QStringLiteral("Import Demo (LMP)..."));
	set("hasTrace", true);
	CHECK(shown() == QStringList({"main.display_game_time", "Import Demo (LMP)...", "-", "main.trace_logger"}));
	/* save data and the surfaces, no divider with one group only */
	set("hasGameTimer", false);
	set("movieImportMenu", QString());
	set("hasTrace", false);
	set("hasSaveData", true);
	CHECK(shown() == QStringList({"main.export_save_data_dialog"}));
	set("hasSaveData", false);
	set("surfaces", QStringLiteral("[{\"name\":\"VRAM\"}]"));
	CHECK(shown() == QStringList({"main.surface_viewer"}));
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
