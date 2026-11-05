/* Tools > Core-Specific Tools offers every installed core's own tools,
 * grouped by core, whatever is open (owner, 2026-11-01): a submenu a core,
 * its tools in its package's words and order, greyed out while no core has
 * any. CoreToolsMenu.qml over a stand-in for the installed cores, and what
 * it holds read, a picked tool's core and id among it. */
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
    width: 400; height: 400; visible: true
    property alias stub: stub
    property alias menu: menu
    property string picked: ""
    // what CoreToolsMenu reads of the installed cores
    QtObject {
        id: stub
        property string coreTools: "[]"
    }
    CoreToolsMenu { id: menu; cores: stub; onOpenCoreTool: (core, id) => picked = core + "/" + id }
    // each core's submenu and the items in it: "Core: Item, Item"
    function held() {
        let out = []
        for (let i = 0; i < menu.count; i++) {
            const sub = menu.menuAt(i)
            if (!sub) continue
            let items = []
            for (let j = 0; j < sub.count; j++) items.push(sub.itemAt(j).text)
            out.push(sub.title + ": " + items.join(", "))
        }
        return out
    }
    function pick(i, j) { menu.menuAt(i).itemAt(j).triggered() }
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

	auto held = [&] {
		QCoreApplication::processEvents();
		QVariant v;
		QMetaObject::invokeMethod(window, "held", Q_RETURN_ARG(QVariant, v));
		return v.toStringList();
	};

	/* no core has tools: the submenu is there, greyed out */
	CHECK(menu->property("title").toString() == QLatin1String("main.core_specific_tools"));
	CHECK(!menu->property("enabled").toBool());
	CHECK(held().isEmpty());
	/* two cores, one with two tools: a submenu each, in the list's order */
	stub->setProperty("coreTools", QStringLiteral(
		"[{\"core\":\"DSDA-Doom\",\"package\":\"a\",\"tools\":[{\"id\":\"import_demo\",\"menu\":\"Import Demo (LMP)...\"}]},"
		"{\"core\":\"Eden\",\"package\":\"b\",\"tools\":[{\"id\":\"nsz\",\"menu\":\"Decompress NSZ...\"},{\"id\":\"xcz\",\"menu\":\"Decompress XCZ...\"}]}]"));
	CHECK(menu->property("enabled").toBool());
	CHECK(held() == QStringList({"DSDA-Doom: Import Demo (LMP)...", "Eden: Decompress NSZ..., Decompress XCZ..."}));
	/* a tool picked says its core and id */
	QMetaObject::invokeMethod(window, "pick", Q_ARG(QVariant, 1), Q_ARG(QVariant, 1));
	CHECK(window->property("picked").toString() == QLatin1String("Eden/xcz"));
	/* the cores folder looked at again, one core gone: its submenu goes */
	stub->setProperty("coreTools", QStringLiteral(
		"[{\"core\":\"Eden\",\"package\":\"b\",\"tools\":[{\"id\":\"nsz\",\"menu\":\"Decompress NSZ...\"}]}]"));
	CHECK(held() == QStringList({"Eden: Decompress NSZ..."}));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
