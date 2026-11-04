/* TAStudio's Branches and Markers lists (RollTable.qml): a click picks a row,
 * Ctrl+click picks one more or lets one go, Shift+click picks every row from
 * the row clicked before, as a Windows list does, so many branches or
 * markers can be removed at once (issue 494). The window is told the row
 * clicked, and a row the window picks is the only one picked. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
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
    width: 300; height: 300; visible: true
    property string told: ""
    ListModel {
        id: rows
        ListElement { name: "a" } ListElement { name: "b" } ListElement { name: "c" } ListElement { name: "d" }
        ListElement { name: "e" } ListElement { name: "f" } ListElement { name: "g" } ListElement { name: "h" }
    }
    RollTable {
        objectName: "table"
        anchors.fill: parent
        model: rows
        columns: [{ title: "Name", width: 200 }]
        cellText: (roles, row, column) => roles.name
        // as TAStudio's: the row clicked is the window's row
        onClicked: (row, column) => { told += (told === "" ? "" : ",") + row; current = row }
    }
}
)";

QString picked(QObject *table)
{
	QStringList out;
	for (const QVariant &v : table->property("selection").toList()) out << QString::number(v.toInt());
	return out.join(QLatin1Char(','));
}

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
	auto *table = window->findChild<QQuickItem *>(QStringLiteral("table"));
	CHECK(table != nullptr);
	if (table == nullptr) return 1;
	const int h = table->property("rowHeight").toInt();
	CHECK(h > 5);
	/* the middle of a row: below the list's edge and the column titles */
	auto click = [&](int row, Qt::KeyboardModifiers mods) {
		QTest::mouseClick(window, Qt::LeftButton, mods, QPoint(60, 1 + h + row * h + h / 2));
		QTest::qWait(20);
	};
	auto current = [&] { return table->property("current").toInt(); };

	click(1, Qt::NoModifier);
	CHECK(picked(table) == QStringLiteral("1") && current() == 1);
	click(3, Qt::ControlModifier);
	CHECK(picked(table) == QStringLiteral("1,3") && current() == 3);
	/* from the row clicked before (3) to this one */
	click(5, Qt::ShiftModifier);
	CHECK(picked(table) == QStringLiteral("3,4,5") && current() == 5);
	/* and again from 3, not from 5 */
	click(1, Qt::ShiftModifier);
	CHECK(picked(table) == QStringLiteral("1,2,3") && current() == 1);
	/* Ctrl lets one go; the window's row is the last one still picked */
	click(1, Qt::ControlModifier);
	CHECK(picked(table) == QStringLiteral("2,3") && current() == 3);
	click(2, Qt::ControlModifier);
	click(3, Qt::ControlModifier);
	CHECK(picked(table).isEmpty() && current() == -1);
	CHECK(window->property("told").toString() == QStringLiteral("1,3,5,1,3,3,-1"));
	/* a plain click picks one again */
	click(6, Qt::NoModifier);
	CHECK(picked(table) == QStringLiteral("6") && current() == 6);
	/* a row the window picks (a branch made) is the only one picked */
	click(4, Qt::ControlModifier);
	CHECK(picked(table) == QStringLiteral("6,4"));
	table->setProperty("current", 0);
	CHECK(picked(table) == QStringLiteral("0"));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
