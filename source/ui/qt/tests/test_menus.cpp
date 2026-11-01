/* The window's menus, in its own style (issues 449, 450, 452, 463): a
 * menu is as wide as its widest row, stays below the menu bar in a short
 * window with its rows scrolled, and closes when its window loses the
 * keyboard to another. A menu bar's menu opened by a click, as a person
 * opens it. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>
#include <cstdio>

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

const char *const kWindow = R"QML(
import QtQuick
import QtQuick.Controls
import "qrc:/chimera"
ApplicationWindow {
    width: 400; height: 160; visible: true
    property alias tools: tools
    menuBar: MenuBar {
        Menu {
            id: tools
            objectName: "tools"
            title: "Tools"
            delegate: MenuEntry {}
            Action { text: "Core Manager..." }
            Action { text: "Reproducible Media Maker, a long name..." }
            Action { text: "RAM Watch" }
            Action { text: "RAM Search" }
            Action { text: "Lua Console" }
            Action { text: "Hex Editor" }
            Action { text: "Macro Tool" }
            Action { text: "Trace Logger" }
            Action { text: "Surface Viewer" }
            Action { text: "CPU Registers" }
        }
        delegate: MenuBarItem { objectName: "bar:" + text }
    }
}
)QML";

const char *const kOther = R"QML(
import QtQuick
Window { width: 100; height: 100; visible: true }
)QML";

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("ChimeraStyle"));
	QQmlApplicationEngine engine;
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	QObject *tools = window->property("tools").value<QObject *>();
	auto *barItem = window->findChild<QQuickItem *>(QStringLiteral("bar:Tools"));
	CHECK(tools != nullptr && barItem != nullptr);
	if (tools == nullptr || barItem == nullptr) return 1;
	auto opened = [&] { return tools->property("opened").toBool(); };

	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, barItem->mapToScene(QPointF(barItem->width() / 2, barItem->height() / 2)).toPoint());
	CHECK(QTest::qWaitFor(opened, 2000));
	/* as wide as its widest row: every row's text fits */
	double widest = 0, rowWidth = 0;
	for (int i = 0; i < tools->property("count").toInt(); i++)
	{
		QQuickItem *row = nullptr;
		QMetaObject::invokeMethod(tools, "itemAt", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, i));
		if (row == nullptr) continue;
		widest = std::max(widest, row->implicitWidth());
		rowWidth = row->width();
	}
	std::printf("widest row %.1f, rows %.1f wide\n", widest, rowWidth);
	CHECK(widest > 200 && rowWidth >= widest);
	/* in a window too short for it: below the bar, not over it, and no taller than the room */
	auto *bar = qobject_cast<QQuickItem *>(window->property("menuBar").value<QObject *>());
	auto *popupItem = qobject_cast<QQuickItem *>(tools->property("contentItem").value<QObject *>());
	CHECK(bar != nullptr && popupItem != nullptr);
	if (bar != nullptr && popupItem != nullptr)
	{
		const double y = tools->property("y").toDouble(), height = tools->property("height").toDouble();
		std::printf("bar %.1f high, menu at %.1f, %.1f high, window %d\n", bar->height(), y, height, window->height());
		CHECK(y >= bar->height() - 0.5 && y + height <= window->height() + 0.5);
	}
	/* another window takes the keyboard: the menu closes */
	QQmlApplicationEngine otherEngine;
	otherEngine.loadData(kOther);
	auto *other = qobject_cast<QQuickWindow *>(otherEngine.rootObjects().value(0));
	CHECK(other != nullptr);
	if (other != nullptr)
	{
		other->requestActivate();
		CHECK(QTest::qWaitForWindowActive(other));
		CHECK(QTest::qWaitFor([&] { return !opened(); }, 2000));
	}

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
