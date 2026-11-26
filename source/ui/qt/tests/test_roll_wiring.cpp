/* The piano roll over the real wiring TAStudio.qml uses (issue 659): its top
 * row is bound to the view model's topFrame and a request to scroll sets that
 * property. The roll ends a drag that was drawing when its top row changes
 * without the person having asked; for that it relies on the answer to a
 * request arriving while the request is still being made. Here the answer goes
 * through QML and a QmlViewModel, as in the window. The view model here is a
 * small one with the three properties that matter (a real piano roll view model
 * needs a machine open for its top row to stay within the movie): its top
 * frame, the rows shown, and the current frame, which moves the top frame as
 * PianoRollViewModel::follow does. */
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>

#include "../piano_roll_item.hpp"
#include "../qml_view_model.hpp"
#include "chimera/vm/rows.hpp"
#include "chimera/vm/view_model.hpp"

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

class Frames : public chimera::vm::RowModel
{
public:
	int64_t rowCount() const override { return 1000; }
	int32_t columnCount() const override { return 30; }
	std::string columnName(int32_t c) const override { return "B" + std::to_string(c); }
	chimera::vm::Value cell(int64_t, int32_t) const override { return {}; }
};

class RollStub : public chimera::vm::ViewModel
{
public:
	explicit RollStub(chimera::vm::Dispatcher &d) : ViewModel("pianoRoll", d)
	{
		expose("topFrame", "the first frame shown", topFrame, true);
		expose("visibleFrames", "how many frames are shown", visibleFrames, true);
		expose("currentFrame", "the frame the machine is on", currentFrame, true);
		watch_ = currentFrame.changed.connect([this] {
			const int64_t now = currentFrame.get(), top = topFrame.get();
			if (now < top || now >= top + visibleFrames.get()) topFrame.set(now);
		});
	}
	chimera::vm::Property<int64_t> topFrame{0}, visibleFrames{0}, currentFrame{0};

private:
	chimera::vm::Connection watch_;
};

/* what the window's dispatcher does: runs the function in the event loop, later */
class QtDispatcher : public chimera::vm::Dispatcher
{
public:
	void post(std::function<void()> fn) override
	{
		QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
	}
};

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	qmlRegisterType<chimera::qt::PianoRollItem>("Chimera", 1, 0, "PianoRollItem");
	QtDispatcher dispatcher;
	RollStub pianoRoll(dispatcher);
	chimera::qt::QmlViewModel model(pianoRoll);
	Frames frames;
	chimera::qt::QmlRows rows(frames, nullptr);

	QQmlEngine engine;
	engine.rootContext()->setContextProperty("m", &model);
	QQmlComponent component(&engine);
	/* TAStudio.qml's lines: topRow from the view model, a request answered by setting it */
	component.setData(R"(
		import QtQuick
		import Chimera 1.0
		PianoRollItem {
			width: 200; height: 160
			topRow: m.topFrame
			onTopRowRequested: (row) => { m.topFrame = row }
			onVisibleRowsChanged: m.visibleFrames = visibleRows
		}
	)", QUrl());
	auto *roll = qobject_cast<chimera::qt::PianoRollItem *>(component.create());
	if (roll == nullptr)
	{
		std::fprintf(stderr, "FAIL: the roll did not load: %s\n", qPrintable(component.errorString()));
		return 1;
	}
	QQuickWindow window;
	window.resize(200, 400);
	roll->setParentItem(window.contentItem());
	roll->setRows(&rows);
	int dragged = 0;
	qint64 draggedTo = -1, pressedRow = -1;
	QObject::connect(roll, &chimera::qt::PianoRollItem::cellPressed, roll, [&](qint64 row, int, bool) { pressedRow = row; });
	QObject::connect(roll, &chimera::qt::PianoRollItem::cellDragged, roll, [&](qint64 row, int) { dragged++; draggedTo = row; });
	QObject::connect(roll, &chimera::qt::PianoRollItem::selectionRequested, roll, [&](qint64 a, qint64 b) {
		roll->setSelectFirst(a);
		roll->setSelectLast(b);
	});
	window.show();
	CHECK(QTest::qWaitForWindowExposed(&window));
	QTest::qWait(50);
	const int h = roll->rowHeight();
	const int y2 = h + 2 * h + h / 2; /* the third row shown */
	CHECK(roll->visibleRows() > 3);
	/* the window sets it from the roll's size as it is laid out; here once it is */
	pianoRoll.visibleFrames.set(roll->visibleRows());

	/* the bound property follows the view model */
	pianoRoll.topFrame.set(500);
	CHECK(roll->topRow() == 500);

	/* a drag past the bottom edge scrolls the roll (the person asked) and goes on drawing */
	QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	CHECK(pressedRow == 502);
	QTest::mouseMove(&window, QPoint(150, 159));
	QTest::mouseMove(&window, QPoint(150, 159 + 4 * h));
	CHECK(QTest::qWaitFor([&] { return roll->topRow() >= 520; }, 3000));
	CHECK(pianoRoll.topFrame.get() == roll->topRow());
	CHECK(dragged >= roll->visibleRows() && draggedTo > 520);
	/* every row from the one pressed on to the last reached was set, each once */
	CHECK(dragged == draggedTo - pressedRow);
	QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 159 + 4 * h));

	/* a drag that stays inside the rows goes on setting each row it passes */
	pianoRoll.topFrame.set(500);
	dragged = 0;
	QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	QTest::mouseMove(&window, QPoint(150, y2 + h));
	CHECK(dragged == 1);
	QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2 + h));

	/* the view model moves the roll while a press is held (the roll follows the frame, which a
	 * seek back sends far up): the drawing stops, and no row between is set */
	pianoRoll.topFrame.set(500);
	dragged = 0;
	QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	pianoRoll.topFrame.set(100);
	CHECK(roll->topRow() == 100);
	QTest::mouseMove(&window, QPoint(150, y2 + 1));
	QTest::mouseMove(&window, QPoint(150, y2 + 3 * h));
	CHECK(dragged == 0);
	QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2 + 3 * h));

	/* the same by the follow itself: the current frame leaves the rows shown */
	pianoRoll.topFrame.set(500);
	QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2));
	pianoRoll.currentFrame.set(50);
	CHECK(roll->topRow() == 50);
	QTest::mouseMove(&window, QPoint(150, y2 + 3 * h));
	CHECK(dragged == 0);
	QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, y2 + 3 * h));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
