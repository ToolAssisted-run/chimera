/* The window's menus, in its own style (issues 449, 450, 452, 463, 593): a
 * menu is as wide as its widest row, is a window of its own that reaches
 * past the bottom of a short window while staying below its menu bar, has
 * its rows scrolled only when it is taller than the screen, and closes when
 * its window loses the keyboard to another. A menu bar's menu opened by a click, as a person
 * opens it. And the main window's menu bar keeps its items' text whole
 * after it was hidden, as full screen hides it (issue 451). */
#include "../menu_letters.hpp"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
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
    property alias longMenu: longMenu
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
        Menu {
            id: longMenu
            objectName: "long"
            title: "Long"
            delegate: MenuEntry {}
            // more rows than any screen holds
            Instantiator {
                model: 120
                delegate: Action { text: "Row " + index }
                onObjectAdded: (index, object) => longMenu.insertAction(index, object)
            }
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
	/* opened, it stays open: being a window of its own does not count as
	 * its parent window losing the keyboard (issue 449's closing) */
	QTest::qWait(300);
	CHECK(opened());
	/* a window of its own, below the bar, not over it, and reaching past
	 * the bottom of the window too short for it (issue 593); all its rows
	 * fit on the screen, so no scroll strips (issue 588) */
	auto *bar = qobject_cast<QQuickItem *>(window->property("menuBar").value<QObject *>());
	auto *popupItem = qobject_cast<QQuickItem *>(tools->property("contentItem").value<QObject *>());
	CHECK(bar != nullptr && popupItem != nullptr);
	if (bar != nullptr && popupItem != nullptr)
	{
		QQuickWindow *menuWindow = popupItem->window();
		CHECK(menuWindow != nullptr && menuWindow != window);
		if (menuWindow != nullptr)
		{
			const QRect menu = menuWindow->geometry(), parent = window->geometry();
			std::printf("window %d,%d %dx%d, bar %.1f high; menu window %d,%d %dx%d\n", parent.x(), parent.y(), parent.width(), parent.height(),
			            bar->height(), menu.x(), menu.y(), menu.width(), menu.height());
			CHECK(menu.top() >= parent.top() + bar->height() - 1);
			CHECK(menu.bottom() > parent.bottom());
		}
		auto *up = popupItem->findChild<QQuickItem *>(QStringLiteral("menuScrollUp"));
		auto *down = popupItem->findChild<QQuickItem *>(QStringLiteral("menuScrollDown"));
		CHECK(up != nullptr && down != nullptr && !up->isVisible() && !down->isVisible());
	}
	QMetaObject::invokeMethod(tools, "close");
	CHECK(QTest::qWaitFor([&] { return !opened(); }, 2000));

	/* a menu taller than the screen (issue 588): a strip at each end, the
	 * bottom one bright and the top one dim; the pointer resting on the
	 * bottom strip scrolls the rows to their end, and on the top strip back
	 * to their start */
	QObject *longMenu = window->property("longMenu").value<QObject *>();
	auto *longItem = window->findChild<QQuickItem *>(QStringLiteral("bar:Long"));
	CHECK(longMenu != nullptr && longItem != nullptr);
	if (longMenu != nullptr && longItem != nullptr)
	{
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, longItem->mapToScene(QPointF(longItem->width() / 2, longItem->height() / 2)).toPoint());
		CHECK(QTest::qWaitFor([&] { return longMenu->property("opened").toBool(); }, 2000));
		auto *rows = qobject_cast<QQuickItem *>(longMenu->property("contentItem").value<QObject *>());
		auto *up = rows != nullptr ? rows->findChild<QQuickItem *>(QStringLiteral("menuScrollUp")) : nullptr;
		auto *down = rows != nullptr ? rows->findChild<QQuickItem *>(QStringLiteral("menuScrollDown")) : nullptr;
		CHECK(up != nullptr && down != nullptr);
		if (up != nullptr && down != nullptr)
		{
			QQuickWindow *menuWindow = rows->window();
			std::printf("long menu: %d rows, window %dx%d on a screen %d high\n", longMenu->property("count").toInt(),
			            menuWindow->width(), menuWindow->height(), menuWindow->screen() != nullptr ? menuWindow->screen()->availableGeometry().height() : -1);
			CHECK(up->isVisible() && down->isVisible());
			CHECK(!up->isEnabled() && down->isEnabled());
			const double top = rows->property("contentY").toDouble();
			QTest::mouseMove(menuWindow, down->mapToScene(QPointF(down->width() / 2, down->height() / 2)).toPoint());
			CHECK(QTest::qWaitFor([&] { return !down->isEnabled(); }, 20000));
			const double bottom = rows->property("contentY").toDouble();
			std::printf("rows scrolled from %.1f to %.1f\n", top, bottom);
			CHECK(bottom > top && up->isEnabled());
			QTest::mouseMove(menuWindow, up->mapToScene(QPointF(up->width() / 2, up->height() / 2)).toPoint());
			CHECK(QTest::qWaitFor([&] { return !up->isEnabled(); }, 20000));
			CHECK(rows->property("contentY").toDouble() == top && down->isEnabled());
		}
	}
	if (longMenu != nullptr)
	{
		QMetaObject::invokeMethod(longMenu, "close");
		CHECK(QTest::qWaitFor([&] { return !longMenu->property("opened").toBool(); }, 2000));
	}
	/* the first menu again, for the next check */
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, barItem->mapToScene(QPointF(barItem->width() / 2, barItem->height() / 2)).toPoint());
	CHECK(QTest::qWaitFor(opened, 2000));
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
			/* closed by clicking its name again, as a person does. While a menu
			 * that is a window of its own is open, the system gives every click
			 * to it (issue 593), so the click goes there, where the bar is */
			auto *helpRows = qobject_cast<QQuickItem *>(helpMenu->property("contentItem").value<QObject *>());
			QWindow *clickWindow = helpRows != nullptr && helpRows->window() != nullptr ? static_cast<QWindow *>(helpRows->window()) : barWindow;
			const QPoint name = help->mapToGlobal(QPointF(help->width() / 2, help->height() / 2)).toPoint();
			QTest::mouseClick(clickWindow, Qt::LeftButton, Qt::NoModifier, clickWindow->mapFromGlobal(name));
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
