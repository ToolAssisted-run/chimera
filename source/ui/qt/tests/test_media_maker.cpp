/* The Reproducible Media Maker's window (MediaMaker.qml), after a folder is
 * packed (issue 619): the lines of text do not overlap each other or the
 * buttons, however long what the maker says is and however large the font
 * (Windows' own is 9 points at 100% and larger at 125% and more). The
 * window over a stand-in for its view model, drawn as the program draws it. */
#include <QGuiApplication>
#include <QJSEngine>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <algorithm>
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
import "qrc:/chimera"
MediaMaker {
    visible: true
    vm: QtObject {
        property string folder: "C:/Games/Sonic Unleashed/PS3_GAME"
        property string output: "C:/Games/Sonic Unleashed (USA) (En,Ja,Fr,De,Es,It) (Media Maker).iso"
        property int format: 1
        property string formatHelp: "An ISO 9660 image with Joliet names, as a game disc is."
        property string recognised: ""
        property real progress: 1
        property string current: ""
        property string error: ""
        property string result: ""
        property bool running: false
        function run(name, args) { return name === "shapes" ? "Stored zip\nISO 9660 + Joliet\nDisk image" : "" }
    }
}
)QML";

/* every item under `item`, depth first */
void collect(QQuickItem *item, QList<QQuickItem *> &out)
{
	out.append(item);
	for (QQuickItem *c : item->childItems()) collect(c, out);
}

/* a text that is drawn in a box too small for it, or that cuts itself short */
int clipped(QQuickItem *root)
{
	QList<QQuickItem *> all;
	collect(root, all);
	int bad = 0;
	for (QQuickItem *i : all)
	{
		const QVariant ch = i->property("contentHeight"), cw = i->property("contentWidth"), text = i->property("text");
		if (!ch.isValid() || !text.isValid() || !i->isVisible() || i->metaObject()->className() == QLatin1String("QQuickTextInput")) continue;
		if (qstrcmp(i->metaObject()->className(), "QQuickText") != 0 && !QString::fromLatin1(i->metaObject()->className()).contains("Label")) continue;
		if (ch.toReal() > i->height() + 0.5 && i->property("elide").toInt() == 0) { bad++; std::fprintf(stderr, "  text taller than its box: %s\n", qPrintable(text.toString().left(30))); }
		if (cw.toReal() > i->width() + 0.5 && i->property("elide").toInt() == 0) { bad++; std::fprintf(stderr, "  text wider than its box: %s\n", qPrintable(text.toString().left(30))); }
	}
	return bad;
}

/* items of the form's column that overlap the one before them, and any that reaches past the window */
int overlapping(QQuickWindow *window)
{
	QQuickItem *column = window->findChild<QQuickItem *>(QStringLiteral("mediaForm"));
	if (column == nullptr)
	{
		QList<QQuickItem *> all;
		collect(window->contentItem(), all);
		for (QQuickItem *i : all)
			if (i->objectName() == QLatin1String("mediaForm")) column = i;
	}
	if (column == nullptr) return 1000;
	QList<QQuickItem *> kids = column->childItems();
	kids.removeIf([](QQuickItem *k) { return !k->isVisible() || k->height() <= 0; });
	std::sort(kids.begin(), kids.end(), [](QQuickItem *a, QQuickItem *b) { return a->y() < b->y(); });
	int bad = 0;
	for (qsizetype i = 1; i < kids.size(); i++)
		if (kids[i]->y() < kids[i - 1]->y() + kids[i - 1]->height() - 0.5)
		{
			bad++;
			std::fprintf(stderr, "  %s at %g (height %g) is over %s at %g\n", kids[i]->metaObject()->className(), kids[i]->y(), kids[i]->height(), kids[i - 1]->metaObject()->className(), kids[i - 1]->y());
		}
	if (!kids.isEmpty() && kids.last()->y() + kids.last()->height() > window->height() + 0.5) bad++;
	return bad;
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	QQmlApplicationEngine engine;
	QJSValue files = engine.evaluate(QStringLiteral("({folderUrl: function(p) { return 'file:///' + p }})"));
	engine.rootContext()->setContextProperty(QStringLiteral("localFiles"), QVariant::fromValue(files));
	const QString sha = QString(40, QLatin1Char('a'));
	const QString longPath = QStringLiteral("C:/Users/Someone With A Long Name/Documents/Chimera/Media Maker Output/Sonic Unleashed (USA) (En,Ja,Fr,De,Es,It) (Media Maker).iso");
	const QString results[] = {
		QStringLiteral("Written %1 (%2 bytes), SHA1 %3.").arg(longPath).arg(8000000000LL).arg(sha),
		QStringLiteral("Wrote the file. Its SHA1 is %1. It is 7.45 GiB.").arg(sha),
		QString(),
	};
	for (const int points : {9, 12, 16})
	{
		QFont font = app.font();
		font.setPointSize(points);
		app.setFont(font);
		engine.loadData(kWindow);
		auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().last());
		CHECK(window != nullptr);
		if (window == nullptr) return 1;
		window->requestActivate();
		CHECK(QTest::qWaitForWindowExposed(window));
		QObject *vm = window->property("vm").value<QObject *>();
		CHECK(vm != nullptr);
		if (vm == nullptr) return 1;
		for (int r = 0; r < 4; r++)
		{
			const bool asError = r == 3;
			vm->setProperty("result", asError ? QString() : results[r]);
			vm->setProperty("error", asError ? QStringLiteral("Could not write ") + longPath + QStringLiteral(": there is no room left on the disk, 12345 bytes were needed more than were free.") : QString());
			vm->setProperty("formatHelp", points == 16 ? QStringLiteral("An ISO 9660 image with Joliet names, as a game disc is, which a core that opens discs reads as it reads the real one.") : QStringLiteral("An ISO 9660 image with Joliet names, as a game disc is."));
			QTest::qWait(50);
			std::fprintf(stderr, "font %d points, case %d, window %dx%d\n", points, r, window->width(), window->height());
			CHECK(overlapping(window) == 0);
			CHECK(clipped(window->contentItem()) == 0);
		}
		delete window;
	}
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
