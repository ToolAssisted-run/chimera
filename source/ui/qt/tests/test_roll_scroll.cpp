/* TAStudio's piano roll (issue 621): with more columns than room it scrolls
 * along them, and a click still lands on the column shown there; a press on
 * a cell picks that frame, as a press on the frame number does; a drag held
 * past the bottom of the roll scrolls its rows on and goes on setting cells;
 * the middle button is a signal of its own (TAStudio plays or pauses). The
 * clicks go through a real window, the way Qt delivers them. */
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>

#include "../piano_roll_item.hpp"
#include "../qml_view_model.hpp"
#include "chimera/vm/rows.hpp"

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* a thousand frames of thirty buttons */
class Frames : public chimera::vm::RowModel
{
public:
	int64_t rowCount() const override { return 1000; }
	int32_t columnCount() const override { return 30; }
	std::string columnName(int32_t c) const override { return "B" + std::to_string(c); }
	chimera::vm::Value cell(int64_t, int32_t) const override { return {}; }
};

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QQuickWindow window;
	window.resize(200, 400); /* taller than the roll: a drag can leave it and stay in the window */
	chimera::qt::PianoRollItem roll;
	roll.setParentItem(window.contentItem());
	roll.setWidth(200);
	roll.setHeight(160);
	Frames frames;
	chimera::qt::QmlRows rows(frames, nullptr);
	roll.setRows(&rows);

	int middles = 0;
	qint64 pressedRow = -1, firstPicked = -1, lastPicked = -1, draggedTo = -1;
	int pressedColumn = -1, draggedColumn = -1;
	QObject::connect(&roll, &chimera::qt::PianoRollItem::middlePressed, &roll, [&] { middles++; });
	QObject::connect(&roll, &chimera::qt::PianoRollItem::cellPressed, &roll, [&](qint64 row, int column, bool) {
		pressedRow = row;
		pressedColumn = column;
	});
	QObject::connect(&roll, &chimera::qt::PianoRollItem::cellDragged, &roll, [&](qint64 row, int column) {
		draggedTo = row;
		draggedColumn = column;
	});
	QObject::connect(&roll, &chimera::qt::PianoRollItem::selectionRequested, &roll, [&](qint64 first, qint64 last) {
		firstPicked = first;
		lastPicked = last;
		roll.setSelectFirst(first);
		roll.setSelectLast(last);
	});
	QObject::connect(&roll, &chimera::qt::PianoRollItem::topRowRequested, &roll, [&](qint64 row) { roll.setTopRow(row); });
	window.show();
	CHECK(QTest::qWaitForWindowExposed(&window));

	const int h = roll.rowHeight();
	/* the roll is as wide as its columns and the window is not */
	CHECK(roll.contentExtent() > 200);
	CHECK(roll.scrollX() == 0 && roll.scrollMax() == roll.contentExtent() - 200);

	/* a cell of the first column: the frame is picked, and the cell pressed */
	const int y2 = h + 2 * h + h / 2; /* the third frame */
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	CHECK(pressedRow == 2);
	CHECK(firstPicked == 2 && lastPicked == 2);
	const int columnAt150 = pressedColumn;
	/* with Shift held, the frames from there to the one pressed */
	QTest::mouseClick(&window, Qt::LeftButton, Qt::ShiftModifier, QPoint(150, h + 5 * h + h / 2));
	CHECK(firstPicked == 2 && lastPicked == 5);

	/* scrolled along, the same place is a column further on, and never past the end */
	roll.setScrollX(100);
	CHECK(roll.scrollX() == 100);
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	CHECK(pressedColumn > columnAt150);
	roll.setScrollX(1000000);
	CHECK(roll.scrollX() == roll.scrollMax());
	QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(198, y2));
	CHECK(pressedColumn == 29);
	roll.setScrollX(0);

	/* the middle button: a signal, no cell pressed */
	pressedRow = -1;
	QTest::mouseClick(&window, Qt::MiddleButton, Qt::NoModifier, QPoint(150, y2));
	CHECK(middles == 1 && pressedRow == -1);

	/* a drag held past the bottom scrolls the rows on, and sets the cells it reaches */
	roll.setTopRow(0);
	const int shown = roll.visibleRows();
	CHECK(shown > 3 && shown < 20);
	QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	CHECK(pressedRow == 2);
	QTest::mouseMove(&window, QPoint(150, 159));
	QTest::mouseMove(&window, QPoint(150, 159 + 4 * h));
	CHECK(QTest::qWaitFor([&] { return roll.topRow() >= 20; }, 3000));
	CHECK(draggedTo >= shown + 10 && draggedColumn == pressedColumn);
	/* held still it goes on, and the pointer back inside stops it */
	const qint64 top = roll.topRow();
	CHECK(QTest::qWaitFor([&] { return roll.topRow() > top; }, 3000));
	QTest::mouseMove(&window, QPoint(150, y2));
	QTest::qWait(150);
	const qint64 stopped = roll.topRow();
	QTest::qWait(250);
	CHECK(roll.topRow() == stopped);
	QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	/* and let go, it stays still */
	QTest::mouseMove(&window, QPoint(150, 159 + 4 * h));
	QTest::qWait(250);
	CHECK(roll.topRow() == stopped);

	/* up the same way: a drag past the top, from a frame down the roll */
	roll.setTopRow(500);
	QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	QTest::mouseMove(&window, QPoint(150, 0));
	CHECK(QTest::qWaitFor([&] { return roll.topRow() < 480; }, 3000));
	QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 0));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
