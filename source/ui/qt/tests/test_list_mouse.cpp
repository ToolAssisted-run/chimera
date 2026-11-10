/* The mouse over RAM Search's and RAM Watch's rows (ListRowsMouse.qml): a
 * drag picks every row from where it began to the one under the pointer,
 * and held past the list's bottom it scrolls the list and picks on (issue
 * 554); a click, Ctrl+click and Shift+click pick as before, and a click
 * below the rows lets every row go. The window here keeps the rows picked
 * as RAM Search's view model does. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
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
    width: 300; height: 220; visible: true
    // as RAM Search's view model: the rows picked, and the row a range counts from
    property var picked: []
    property int anchorRow: -1
    property int menus: 0
    property int opened: -1
    property string pickedText: picked.join(",")
    function choose(row, mode) {
        if (mode === "none") { picked = []; anchorRow = -1 }
        else if (mode === "toggle") {
            picked = picked.indexOf(row) >= 0 ? picked.filter(r => r !== row) : picked.concat([row]).sort((a, b) => a - b)
            anchorRow = row
        } else if (mode === "range" && anchorRow >= 0) {
            const out = []
            for (let r = Math.min(anchorRow, row); r <= Math.max(anchorRow, row); r++) out.push(r)
            picked = out
        } else { picked = [row]; anchorRow = row }
    }
    ListView {
        id: list
        objectName: "list"
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: 40
        ScrollBar.vertical: ScrollBar {}
        delegate: Rectangle { required property int index; width: list.width; height: 22; color: index % 2 ? "#eee" : "#fff" }
        ListRowsMouse {
            list: list
            isPicked: (r) => picked.indexOf(r) >= 0
            onPick: (row, mode) => choose(row, mode)
            onMenu: menus++
            onActivated: (row) => opened = row
        }
    }
}
)";

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
	CHECK(QTest::qWaitForWindowExposed(window));
	auto *list = window->findChild<QQuickItem *>(QStringLiteral("list"));
	CHECK(list != nullptr);
	if (list == nullptr) return 1;
	auto picked = [&] { return window->property("pickedText").toString(); };
	auto at = [](int row) { return QPoint(60, row * 22 + 11); };

	/* a drag from row 1 to row 4 picks 1 to 4, and back up to 2 picks 1 and 2 */
	QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(1));
	CHECK(picked() == QStringLiteral("1"));
	for (int y = at(1).y(); y <= at(4).y(); y += 5) QTest::mouseMove(window, QPoint(60, y));
	QTest::mouseMove(window, at(4));
	CHECK(picked() == QStringLiteral("1,2,3,4"));
	QTest::mouseMove(window, at(2));
	CHECK(picked() == QStringLiteral("1,2"));
	QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(2));
	/* the list did not scroll under the drag */
	CHECK(list->property("contentY").toDouble() == 0);
	/* moved after the release, nothing more is picked */
	QTest::mouseMove(window, at(6));
	CHECK(picked() == QStringLiteral("1,2"));

	/* held below the list's bottom (10 rows show of 40): the list scrolls and
	 * the drag picks the rows coming into view */
	QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(3));
	QTest::mouseMove(window, at(9));
	QTest::mouseMove(window, QPoint(60, 230));
	QTest::qWait(600);
	const QStringList far = picked().split(QLatin1Char(','));
	CHECK(far.first() == QStringLiteral("3") && far.last().toInt() >= 12);
	CHECK(list->property("contentY").toDouble() > 0);
	QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(60, 230));
	std::printf("held below the list: rows %s to %s picked, the list scrolled %.0f pixels\n", qPrintable(far.first()),
		qPrintable(far.last()), list->property("contentY").toDouble());

	/* back at the top: a click, Ctrl+click and Shift+click as before */
	list->setProperty("contentY", 0);
	QTest::qWait(20);
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at(1));
	CHECK(picked() == QStringLiteral("1"));
	QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, at(3));
	CHECK(picked() == QStringLiteral("1,3"));
	QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, at(5));
	CHECK(picked() == QStringLiteral("3,4,5"));
	/* a right click on a picked row keeps them, and opens the menu */
	QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, at(4));
	CHECK(picked() == QStringLiteral("3,4,5") && window->property("menus").toInt() == 1);
	/* a double click is the row's */
	QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, at(7));
	CHECK(window->property("opened").toInt() == 7);

	/* a click below the rows lets every row go: the list scrolled to its end */
	window->setHeight(1000);
	QTest::qWait(50);
	QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(60, 40 * 22 + 50));
	CHECK(picked().isEmpty());

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
