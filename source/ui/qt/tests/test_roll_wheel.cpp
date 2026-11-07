/* TAStudio's piano roll scrolled by a trackpad (issue 542): a trackpad sends
 * many small steps, each well short of a wheel's notch (120), and the roll
 * counted each alone as nothing, so only a fast, wide swipe moved it. What
 * is short of a notch is kept for the next step now, and a trackpad's pixels
 * move the roll a row for each row's height. */
#include <QGuiApplication>
#include <QWheelEvent>

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

/* a thousand frames of one column, for the keys */
class Frames : public chimera::vm::RowModel
{
public:
	int64_t rowCount() const override { return 1000; }
	int32_t columnCount() const override { return 1; }
	std::string columnName(int32_t) const override { return "A"; }
	chimera::vm::Value cell(int64_t, int32_t) const override { return {}; }
};

void wheel(chimera::qt::PianoRollItem &roll, QPoint pixels, QPoint angle)
{
	QWheelEvent e(QPointF(10, 10), QPointF(10, 10), pixels, angle, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
	QCoreApplication::sendEvent(&roll, &e);
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	chimera::qt::PianoRollItem roll;
	QObject::connect(&roll, &chimera::qt::PianoRollItem::topRowRequested, &roll, [&roll](qint64 row) { roll.setTopRow(row); });
	roll.setProperty("wheelRows", 6);
	const int h = roll.rowHeight();
	CHECK(h > 0);

	/* a wheel: 15 steps of 8 (a notch, 120, in all) scroll its 6 rows up */
	roll.setTopRow(100);
	for (int i = 0; i < 15; i++) wheel(roll, QPoint(), QPoint(0, 8));
	CHECK(roll.topRow() == 94);
	/* and a notch down at once, 6 down */
	wheel(roll, QPoint(), QPoint(0, -120));
	CHECK(roll.topRow() == 100);

	/* a trackpad: 2 pixels at a time, 10 rows' height down in all, is 10 rows */
	roll.setTopRow(100);
	for (int i = 0; i < 10 * h; i += 2) wheel(roll, QPoint(0, -2), QPoint(0, -2));
	CHECK(roll.topRow() == 110);
	/* never above the first row */
	roll.setTopRow(1);
	for (int i = 0; i < 5 * h; i++) wheel(roll, QPoint(0, 1), QPoint(0, 1));
	CHECK(roll.topRow() == 0);

	/* the keys (issue 542), with no rows: nothing to move */
	CHECK(!roll.navigate(Qt::Key_Down, false));
	/* a thousand frames, ten rows tall: a key picks a frame and keeps it in view */
	Frames frames;
	chimera::qt::QmlRows rows(frames, nullptr);
	roll.setRows(&rows);
	QObject::connect(&roll, &chimera::qt::PianoRollItem::selectionRequested, &roll, [&roll](qint64 first, qint64 last) {
		roll.setSelectFirst(first);
		roll.setSelectLast(last);
	});
	roll.setWidth(200);
	roll.setHeight(roll.rowHeight() * 11);
	const int shown = roll.visibleRows();
	CHECK(shown >= 5);
	roll.setTopRow(100);
	CHECK(roll.navigate(Qt::Key_Down, false));
	CHECK(roll.selectFirst() == 101 && roll.selectLast() == 101);
	CHECK(roll.navigate(Qt::Key_Up, false) && roll.navigate(Qt::Key_Up, false));
	CHECK(roll.selectFirst() == 99 && roll.selectLast() == 99 && roll.topRow() == 99);
	/* Shift: from 99 down three, 99 to 102 picked; up again, 99 to 101 */
	for (int i = 0; i < 3; i++) roll.navigate(Qt::Key_Down, true);
	CHECK(roll.selectFirst() == 99 && roll.selectLast() == 102);
	roll.navigate(Qt::Key_Up, true);
	CHECK(roll.selectFirst() == 99 && roll.selectLast() == 101);
	/* End, the last frame, in view; Home, the first */
	CHECK(roll.navigate(Qt::Key_End, false));
	CHECK(roll.selectLast() == 999 && roll.topRow() <= 999 && roll.topRow() + shown > 999);
	CHECK(roll.navigate(Qt::Key_Home, false));
	CHECK(roll.selectFirst() == 0 && roll.topRow() == 0);
	/* Page Down: a page less one further */
	CHECK(roll.navigate(Qt::Key_PageDown, false));
	CHECK(roll.selectFirst() == shown - 1);
	CHECK(!roll.navigate(Qt::Key_Left, false));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
