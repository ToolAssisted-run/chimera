/* The window's menus, in its own style (issues 449, 450, 452, 463): a
 * menu is as wide as its widest row, stays below the menu bar in a short
 * window with its rows scrolled, and closes when its window loses the
 * keyboard to another. A menu bar's menu opened by a click, as a person
 * opens it. And the main window's menu bar keeps its items' text whole
 * after it was hidden, as full screen hides it (issue 451). */
#include "../menu_letters.hpp"

#include <QGuiApplication>
#include <QQmlContext>
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

/* the main window's menu bar: hidden while full screen (Display's default),
 * shown again when windowed; one item left out, as System is with no machine */
const char *const kBar = R"QML(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "qrc:/chimera"
ApplicationWindow {
    width: 600; height: 200; visible: true
    menuBar: RowLayout {
        spacing: 0
        MenuBar {
            Layout.fillWidth: true
            delegate: MenuBarEntry { objectName: "bar:" + menuLetters.plain(text) }
            Menu { title: "&File"; Action { text: "Open" } }
            Menu { title: "&System"; readonly property bool shown: false; Action { text: "Reboot" } }
            Menu { title: "&View"; Action { text: "Zoom" } }
            Menu { title: "&Config"; Action { text: "Paths" } }
            Menu { title: "&Tools"; Action { text: "RAM Watch" } }
            Menu { objectName: "helpMenu"; title: "&Help"; Action { text: "About" } }
        }
    }
    // the game: it has the keyboard, and counts the Spaces it is given
    property int spaces: 0
    Item { objectName: "game"; anchors.fill: parent; focus: true; Keys.onSpacePressed: spaces++ }
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
	chimera::qt::MenuLetters menuLetters;
	app.installEventFilter(&menuLetters);
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

	/* the menu bar hidden and shown again, as full screen and back hide and
	 * show it, then Alt pressed for the letters: every item as wide as its
	 * whole text, which is not cut (issue 451) */
	QQmlApplicationEngine barEngine;
	barEngine.rootContext()->setContextProperty(QStringLiteral("menuLetters"), &menuLetters);
	barEngine.loadData(kBar);
	auto *barWindow = qobject_cast<QQuickWindow *>(barEngine.rootObjects().value(0));
	CHECK(barWindow != nullptr);
	if (barWindow != nullptr)
	{
		barWindow->requestActivate();
		CHECK(QTest::qWaitForWindowActive(barWindow));
		auto *chrome = qobject_cast<QQuickItem *>(barWindow->property("menuBar").value<QObject *>());
		CHECK(chrome != nullptr);
		auto whole = [&](const char *when) {
			for (const char *title : {"File", "View", "Config", "Tools", "Help"})
			{
				auto *item = barWindow->findChild<QQuickItem *>(QStringLiteral("bar:") + QLatin1String(title));
				auto *label = item != nullptr ? qobject_cast<QQuickItem *>(item->property("contentItem").value<QObject *>()) : nullptr;
				QQuickItem *text = label != nullptr && !label->childItems().isEmpty() ? label->childItems().constLast() : nullptr;
				CHECK(text != nullptr);
				if (text == nullptr) continue;
				if (item->width() + 0.5 < item->implicitWidth() || text->property("truncated").toBool())
				{
					std::fprintf(stderr, "%s, %s: %.1f wide of %.1f, text cut\n", when, title, item->width(), item->implicitWidth());
					CHECK(false);
				}
			}
			auto *system = barWindow->findChild<QQuickItem *>(QStringLiteral("bar:System"));
			CHECK(system != nullptr && !system->isVisible() && system->width() == 0);
		};
		whole("at first");
		if (chrome != nullptr)
		{
			/* Alt+Enter in and out, as Full Screen's key is: Alt shows and
			 * hides the letters, so the text changes while the bar is hidden */
			QTest::keyPress(barWindow, Qt::Key_Alt, Qt::AltModifier);
			barWindow->showFullScreen();
			chrome->setVisible(false);
			QTest::keyRelease(barWindow, Qt::Key_Alt);
			QTest::qWait(100);
			QTest::keyPress(barWindow, Qt::Key_Alt, Qt::AltModifier);
			barWindow->showNormal();
			chrome->setVisible(true);
			QTest::keyRelease(barWindow, Qt::Key_Alt);
			QTest::qWait(100);
			whole("shown again");
			QTest::keyPress(barWindow, Qt::Key_Alt, Qt::AltModifier);
			QTest::qWait(100);
			CHECK(menuLetters.shown());
			whole("with the letters");
			QTest::keyRelease(barWindow, Qt::Key_Alt);
			QTest::qWait(100);
		}
		/* a menu opened with the mouse and closed gives the keyboard back to
		 * what had it (issue 552): Space then goes to the game, not to the
		 * menu bar, which opened the menu again */
		auto *game = barWindow->findChild<QQuickItem *>(QStringLiteral("game"));
		auto *help = barWindow->findChild<QQuickItem *>(QStringLiteral("bar:Help"));
		auto *helpMenu = barWindow->findChild<QObject *>(QStringLiteral("helpMenu"));
		CHECK(game != nullptr && help != nullptr && helpMenu != nullptr);
		if (game != nullptr && help != nullptr && helpMenu != nullptr)
		{
			game->forceActiveFocus();
			QTest::mouseClick(barWindow, Qt::LeftButton, Qt::NoModifier, help->mapToScene(QPointF(help->width() / 2, help->height() / 2)).toPoint());
			CHECK(QTest::qWaitFor([&] { return helpMenu->property("opened").toBool(); }, 2000));
			/* closed by clicking its name again, as a person does */
			QTest::mouseClick(barWindow, Qt::LeftButton, Qt::NoModifier, help->mapToScene(QPointF(help->width() / 2, help->height() / 2)).toPoint());
			CHECK(QTest::qWaitFor([&] { return !helpMenu->property("visible").toBool(); }, 2000));
			QTest::qWait(100);
			const int before = barWindow->property("spaces").toInt();
			QTest::keyClick(barWindow, Qt::Key_Space);
			QTest::qWait(200);
			if (!game->hasActiveFocus())
				std::fprintf(stderr, "the keyboard is %s's\n", barWindow->activeFocusItem() != nullptr ? barWindow->activeFocusItem()->metaObject()->className() : "nobody");
			CHECK(game->hasActiveFocus());
			CHECK(barWindow->property("spaces").toInt() == before + 1);
			CHECK(!helpMenu->property("visible").toBool());
		}
	}

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
