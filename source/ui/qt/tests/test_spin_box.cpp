/* Chimera's number box (style/SpinBox.qml), in Chimera's own style as the
 * window has it: a value of a thousand or more is a plain number, "-1000"
 * and never "-1,000" (in a locale with a thousands separator, here US
 * English), and any digit of it can be deleted and typed over; before, a
 * digit in front of the comma could not be deleted, as what was left
 * ("-,000") was no number. A box left empty keeps its value, and a box that
 * shows its number its own way (Display's tenths) keeps that. Real keys,
 * through Qt's test module. */
#include <QGuiApplication>
#include <QLocale>
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
    Column {
        SpinBox { objectName: "axis"; editable: true; from: -32768; to: 32767; value: -1000 }
        SpinBox { objectName: "big"; editable: true; from: 0; to: 100000; value: 12345 }
        SpinBox {
            id: tenths
            objectName: "tenths"; editable: true; from: 1; to: 10; value: 5
            textFromValue: (v, locale) => (v / 10).toFixed(1)
            valueFromText: (text, locale) => Math.round(parseFloat(text) * 10)
            validator: DoubleValidator { decimals: 1 }
        }
    }
}
)QML";

QString textOf(QQuickItem *box) { return box->property("contentItem").value<QObject *>()->property("text").toString(); }

} // namespace

int main(int argc, char **argv)
{
	QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("ChimeraStyle"));
	QQmlApplicationEngine engine;
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	window->requestActivate();
	CHECK(QTest::qWaitForWindowActive(window));
	auto *axis = window->findChild<QQuickItem *>(QStringLiteral("axis"));
	auto *big = window->findChild<QQuickItem *>(QStringLiteral("big"));
	auto *tenths = window->findChild<QQuickItem *>(QStringLiteral("tenths"));
	CHECK(axis && big && tenths);
	if (!axis || !big || !tenths) return 1;

	/* plain numbers, no thousands separator */
	CHECK(textOf(axis) == QStringLiteral("-1000"));
	CHECK(textOf(big) == QStringLiteral("12345"));

	/* the thousands digit deleted and typed over, then Enter */
	auto *field = axis->property("contentItem").value<QQuickItem *>();
	field->forceActiveFocus();
	CHECK(QTest::qWaitFor([&] { return field->hasActiveFocus(); }, 2000));
	field->setProperty("cursorPosition", 2); // after "-1"
	QTest::keyClick(window, Qt::Key_Backspace);
	CHECK(textOf(axis) == QStringLiteral("-000"));
	QTest::keyClick(window, '5');
	CHECK(textOf(axis) == QStringLiteral("-5000"));
	QTest::keyClick(window, Qt::Key_Return);
	CHECK(axis->property("value").toInt() == -5000);

	/* a letter is not taken */
	QTest::keyClick(window, 'x');
	CHECK(textOf(axis) == QStringLiteral("-5000"));

	/* emptied and left: the value it had stays */
	QMetaObject::invokeMethod(field, "selectAll");
	QTest::keyClick(window, Qt::Key_Backspace);
	CHECK(textOf(axis).isEmpty());
	QTest::keyClick(window, Qt::Key_Return);
	CHECK(axis->property("value").toInt() == -5000);

	/* a box with its own way of showing its number keeps it */
	CHECK(textOf(tenths) == QStringLiteral("0.5"));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
