/* A menu's rows keep their text after Alt has come and gone, as Alt-Tab
 * presses it (issue 471): a row whose text has a letter to underline
 * ("&Copy") drew nothing afterwards, in a dark window plainly so. And the
 * letter is underlined only while Alt is held (issue 405). The rows are
 * read in the window's picture: each must hold light pixels, its text's,
 * on the dark menu. */
#include "../menu_letters.hpp"

#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>
#include <utility>

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
    width: 400; height: 300; visible: true
    color: "#202020"
    palette.window: "#202020"; palette.windowText: "#ffffff"; palette.base: "#202020"; palette.text: "#ffffff"
    palette.button: "#303030"; palette.buttonText: "#ffffff"; palette.highlight: "#3d8bff"; palette.highlightedText: "#ffffff"
    property alias menu: menu
    menuBar: MenuBar {
        Menu {
            id: menu
            title: "&Window"
            delegate: MenuEntry {}
            Action { text: "&Copy" }
            Action { text: "Stay on Top" }
            Action { text: "&Float From Main Window" }
            MenuItem { text: "Save Window &Position"; checkable: true }
        }
        delegate: MenuBarItem { objectName: "bar:" + menuLetters.plain(text) }
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
	chimera::qt::MenuLetters menuLetters;
	app.installEventFilter(&menuLetters);
	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("menuLetters"), &menuLetters);
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	QObject *menu = window->property("menu").value<QObject *>();
	auto *barItem = window->findChild<QQuickItem *>(QStringLiteral("bar:Window"));
	CHECK(menu != nullptr && barItem != nullptr);
	if (menu == nullptr || barItem == nullptr) return 1;

	auto open = [&] {
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
			barItem->mapToScene(QPointF(barItem->width() / 2, barItem->height() / 2)).toPoint());
		CHECK(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 2000));
	};
	auto close = [&] {
		QMetaObject::invokeMethod(menu, "close");
		QTest::qWaitFor([&] { return !menu->property("opened").toBool(); }, 2000);
	};
	/* each row: its light pixels when the menu is open, and whether its
	 * letter is underlined ("-" for a row without one); whether every
	 * row's text is drawn */
	auto rows = [&](const char *when, bool look) {
		QTest::qWait(100);
		const QImage picture = window->grabWindow();
		QString seen;
		bool allDrawn = true;
		for (int i = 0; i < menu->property("count").toInt(); i++)
		{
			QQuickItem *row = nullptr;
			QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, i));
			if (row == nullptr) continue;
			const QString text = row->property("text").toString();
			auto *label = row->findChild<QObject *>(QStringLiteral("label"));
			const QString underlined = !text.contains(QLatin1Char('&')) || label == nullptr ? QStringLiteral("-")
				: label->property("mnemonicVisible").toBool() ? QStringLiteral("yes") : QStringLiteral("no");
			seen += QStringLiteral(" [%1: underlined %2").arg(text, underlined);
			if (look)
			{
				const QRect r = row->mapRectToScene(QRectF(0, 0, row->width(), row->height())).toRect();
				int light = 0;
				for (int y = r.top(); y <= r.bottom(); y++)
					for (int x = r.left(); x <= r.right(); x++)
						if (qGray(picture.pixel(x, y)) > 150) light++;
				allDrawn = allDrawn && light > 0;
				seen += QStringLiteral(", %1 light pixels").arg(light);
			}
			seen += QLatin1Char(']');
		}
		std::printf("%s:%s\n", when, qPrintable(seen));
		return std::make_pair(allDrawn, seen);
	};

	open();
	const auto first = rows("opened", true);
	CHECK(first.first && !first.second.contains(QStringLiteral("underlined yes")));
	close();

	/* Alt-Tab: Alt pressed (the letters underlined), another window, back, Alt let go */
	QTest::keyPress(window, Qt::Key_Alt);
	const auto held = rows("Alt held", false);
	CHECK(held.second.contains(QStringLiteral("underlined yes")) && !held.second.contains(QStringLiteral("underlined no")));
	QQmlApplicationEngine otherEngine;
	otherEngine.loadData(kOther);
	auto *other = qobject_cast<QQuickWindow *>(otherEngine.rootObjects().value(0));
	CHECK(other != nullptr);
	if (other != nullptr)
	{
		other->requestActivate();
		CHECK(QTest::qWaitForWindowActive(other));
	}
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	QTest::keyRelease(window, Qt::Key_Alt);
	open();
	const auto back = rows("after Alt-Tab", true);
	CHECK(back.first && !back.second.contains(QStringLiteral("underlined yes")));
	close();

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
