/* The pointer over the picture, as the machine is told of it
 * (PicturePointer.qml): a button let go is told where the pointer was, not
 * where Qt's emptied point says (0,0), and a pointer that leaves stays
 * where it was last seen. A Flash game in Ruffle took a button let go in the
 * window's corner as no click (issue 424). Real mouse events, through Qt's
 * test module. */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTest>
#include <QVariantList>

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
import "qrc:/chimera"
Window {
    width: 200; height: 200; visible: true
    // what the pointer said, each [x, y, buttons]
    property var told: []
    PicturePointer {
        anchors.fill: parent
        onNoted: (at, buttons) => told.push([at.x, at.y, buttons])
    }
}
)";

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
	/* the last thing told, or the last told of the buttons (a move may come after a release) */
	auto last = [&](bool buttonsOnly) {
		const QVariantList told = window->property("told").toList();
		for (qsizetype i = told.size() - 1; i >= 0; i--)
			if (!buttonsOnly || told[i].toList().value(2).toInt() >= 0) return told[i].toList();
		return QVariantList();
	};
	auto said = [](const QVariantList &l, double x, double y, int buttons) {
		return l.size() == 3 && l[0].toDouble() == x && l[1].toDouble() == y && l[2].toInt() == buttons;
	};
	auto is = [&](double x, double y, int buttons) { return said(last(buttons >= 0), x, y, buttons); };

	QTest::mouseMove(window, QPoint(50, 60));
	CHECK(QTest::qWaitFor([&] { return is(50, 60, -1); }, 2000));
	QTest::mousePress(window, Qt::LeftButton, {}, QPoint(50, 60));
	CHECK(QTest::qWaitFor([&] { return is(50, 60, 1); }, 2000));
	/* let go: no button held, still at 50,60 - Qt's own point is at 0,0 by then */
	QTest::mouseRelease(window, Qt::LeftButton, {}, QPoint(50, 60));
	CHECK(QTest::qWaitFor([&] { return is(50, 60, 0); }, 2000));
	QTest::qWait(100);
	bool cornered = false;
	for (const QVariant &v : window->property("told").toList())
	{
		const QVariantList l = v.toList();
		cornered = cornered || (l.size() == 3 && l[0].toDouble() == 0 && l[1].toDouble() == 0);
	}
	CHECK(!cornered);
	/* moved and pressed again: told as before */
	QTest::mouseMove(window, QPoint(120, 30));
	CHECK(QTest::qWaitFor([&] { return is(120, 30, -1); }, 2000));
	QTest::mousePress(window, Qt::RightButton, {}, QPoint(120, 30));
	CHECK(QTest::qWaitFor([&] { return is(120, 30, 2); }, 2000));
	QTest::mouseRelease(window, Qt::RightButton, {}, QPoint(120, 30));
	CHECK(QTest::qWaitFor([&] { return is(120, 30, 0); }, 2000));

	std::printf("picture pointer: %d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
