/* A button clicked ends the editing of the box that had the keyboard: a number
 * typed in a box is its value by the time OK reads it. Qt's buttons on a Mac
 * do not take the keyboard when clicked, and the Display window's User
 * prescale was lost there (issue 540); Chimera's style gives every button the
 * focus a click, as on Windows and Linux. Real keys and a real click, through
 * Qt's test module. */
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

const char *const kWindow = R"QML(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    width: 400; height: 200; visible: true
    property int seen: -1
    Column {
        anchors.centerIn: parent
        spacing: 10
        SpinBox { objectName: "box"; from: 1; to: 16; value: 1; editable: true }
        Button { objectName: "ok"; text: "OK"; onClicked: seen = parent.children[0].value }
    }
}
)QML";

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
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	auto *box = window->findChild<QQuickItem *>(QStringLiteral("box"));
	auto *ok = window->findChild<QQuickItem *>(QStringLiteral("ok"));
	CHECK(box != nullptr && ok != nullptr);
	if (box == nullptr || ok == nullptr) return 1;

	/* every platform's button takes the focus when clicked */
	CHECK(ok->property("focusPolicy").toInt() == Qt::StrongFocus);

	/* 7 typed over the 1, then OK clicked, Enter never pressed: OK sees 7 */
	auto *field = qobject_cast<QQuickItem *>(box->property("contentItem").value<QObject *>());
	CHECK(field != nullptr);
	if (field == nullptr) return 1;
	QTest::mouseClick(window, Qt::LeftButton, {}, field->mapToScene(QPointF(field->width() / 2, field->height() / 2)).toPoint());
	CHECK(QTest::qWaitFor([&] { return box->hasActiveFocus(); }, 2000));
	QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
	QTest::keyClick(window, Qt::Key_7);
	QTest::mouseClick(window, Qt::LeftButton, {}, ok->mapToScene(QPointF(ok->width() / 2, ok->height() / 2)).toPoint());
	CHECK(QTest::qWaitFor([&] { return window->property("seen").toInt() != -1; }, 2000));
	CHECK(window->property("seen").toInt() == 7);
	CHECK(box->property("value").toInt() == 7);

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
