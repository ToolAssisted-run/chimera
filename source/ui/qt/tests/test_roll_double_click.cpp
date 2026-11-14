/* TAStudio's piano roll double-clicked (issue 582). Two quick clicks on a
 * button's cell are two presses, so the button is changed twice and ends as
 * it was. A double click on a frame number still sets a marker there (issue
 * 436). The clicks go through a real window, the way Qt delivers them: press,
 * release, press, double click, release. */
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

/* ten frames of one button */
class Frames : public chimera::vm::RowModel
{
public:
	int64_t rowCount() const override { return 10; }
	int32_t columnCount() const override { return 1; }
	std::string columnName(int32_t) const override { return "A"; }
	chimera::vm::Value cell(int64_t, int32_t) const override { return {}; }
};

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QQuickWindow window;
	window.resize(300, 300);
	chimera::qt::PianoRollItem roll;
	roll.setParentItem(window.contentItem());
	roll.setWidth(300);
	roll.setHeight(300);
	Frames frames;
	chimera::qt::QmlRows rows(frames, nullptr);
	roll.setRows(&rows);

	int presses = 0, markers = 0;
	qint64 pressedRow = -1, markerRow = -1;
	int pressedColumn = -1;
	QObject::connect(&roll, &chimera::qt::PianoRollItem::cellPressed, &roll, [&](qint64 row, int column, bool) {
		presses++;
		pressedRow = row;
		pressedColumn = column;
	});
	QObject::connect(&roll, &chimera::qt::PianoRollItem::frameDoubleClicked, &roll, [&](qint64 row) {
		markers++;
		markerRow = row;
	});
	window.show();
	CHECK(QTest::qWaitForWindowExposed(&window));

	/* the third frame's line: the header is one line tall */
	const int h = roll.rowHeight();
	const int y = h + 2 * h + h / 2;

	/* the button's cell (the last pixels before the roll's right edge):
	 * each click is a press of its own, two in all, and no marker */
	QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(roll.contentExtent() - 4, y));
	CHECK(presses == 2);
	CHECK(pressedRow == 2 && pressedColumn == 0);
	CHECK(markers == 0);

	/* the frame number: a marker on that frame, once, and no button pressed */
	QTest::mouseDClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(40, y));
	CHECK(markers == 1 && markerRow == 2);
	CHECK(presses == 2);

	std::printf("%d presses on the cell, %d markers; %d checks, %d failed\n", presses, markers, checks, failures);
	return failures == 0 ? 0 : 1;
}
