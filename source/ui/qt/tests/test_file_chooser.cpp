/* The window's file and folder dialogs (dialogs/PickerBase.qml) on Linux:
 * the desktop's own chooser asked through the XDG desktop portal, and
 * Chimera's own chooser where there is none (docs/file-picker.md).
 *
 *   test-file-chooser --portal    a stand-in portal answers on the session
 *                                 bus (file-chooser.py starts both); its
 *                                 answer and the requests it was sent are
 *                                 the files CHIMERA_TEST_ANSWER and
 *                                 CHIMERA_TEST_REQUESTS name
 *   test-file-chooser --builtin   no portal: Chimera's chooser, driven as a
 *                                 person would, in the folder
 *                                 CHIMERA_TEST_FOLDER names */
#include "../file_places.hpp"
#include "../portal_file_chooser.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

using namespace chimera::qt;

namespace {
int checks = 0, failed = 0;

void check(bool ok, const char *what, int line)
{
	checks++;
	if (ok) return;
	failed++;
	std::printf("FAIL %s:%d: %s\n", __FILE__, line, what);
}
#define CHECK(x) check((x), #x, __LINE__)

const char *const kWindow = R"QML(
import QtQuick
import QtQuick.Controls
import ChimeraDialogs
ApplicationWindow {
    id: win
    width: 900; height: 700; visible: true
    property alias open: openDialog
    property alias many: manyDialog
    property alias save: saveDialog
    property alias folder: folderDialog
    property alias box: boxDialog
    property alias field: typed
    property int accepted: 0
    property int rejected: 0
    TextField { id: typed; focus: true }
    FileDialog {
        id: openDialog
        title: "Pick"
        nameFilters: ["Projects (*.chimeraProject)", "All files (*)"]
        onAccepted: win.accepted++
        onRejected: win.rejected++
    }
    FileDialog { id: manyDialog; fileMode: FileDialog.OpenFiles; onAccepted: win.accepted++; onRejected: win.rejected++ }
    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["Settings (*.json)"]
        onAccepted: win.accepted++
        onRejected: win.rejected++
    }
    FolderDialog { id: folderDialog; onAccepted: win.accepted++; onRejected: win.rejected++ }
    FolderDialog {
        id: boxDialog
        checkBoxText: "Include sub-folders"
        checkBoxChecked: true
        onAccepted: win.accepted++
        onRejected: win.rejected++
    }
}
)QML";

bool waitFor(const std::function<bool()> &done, int ms = 5000)
{
	QElapsedTimer t;
	t.start();
	while (!done() && t.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
	return done();
}

QObject *dialog(QObject *window, const char *name)
{
	return window->property(name).value<QObject *>();
}

/* Chimera's own chooser of a dialog, and a part of it by name */
QObject *chooser(QObject *d)
{
	return d->property("builtin").value<QObject *>();
}

QObject *part(QObject *d, const char *name)
{
	QObject *c = chooser(d);
	return c ? c->findChild<QObject *>(QString::fromLatin1(name)) : nullptr;
}

QString localOf(const QVariant &v)
{
	return QDir::cleanPath(v.toUrl().toLocalFile());
}

void answer(const QJsonObject &o)
{
	QFile f(qEnvironmentVariable("CHIMERA_TEST_ANSWER"));
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(o).toJson());
}

/* the last request the stand-in portal was sent */
QJsonObject lastRequest()
{
	QFile f(qEnvironmentVariable("CHIMERA_TEST_REQUESTS"));
	if (!f.open(QIODevice::ReadOnly)) return {};
	const QList<QByteArray> lines = f.readAll().trimmed().split('\n');
	return lines.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(lines.last()).object();
}

void helpers()
{
	/* Qt's name filters as the portal's: a label and its globs, "*.*" every file */
	const QList<portal::Filter> filters = portal::filtersFrom({QStringLiteral("Projects (*.chimeraProject *.json)"), QStringLiteral("All files (*.*)"), QStringLiteral("*.lua"), QStringLiteral("  ")});
	CHECK(filters.size() == 3);
	CHECK(filters.size() == 3 && filters[0].label == QStringLiteral("Projects (*.chimeraProject *.json)"));
	CHECK(filters.size() == 3 && filters[0].globs == QStringList({QStringLiteral("*.chimeraProject"), QStringLiteral("*.json")}));
	CHECK(filters.size() == 3 && filters[1].globs == QStringList({QStringLiteral("*")}));
	CHECK(filters.size() == 3 && filters[2].globs == QStringList({QStringLiteral("*.lua")}));
	/* the portal's answers are URIs, with what is not plain escaped */
	CHECK(portal::pathFromUri(QStringLiteral("file:///home/a/My%20Games/x%27y.bin")) == QStringLiteral("/home/a/My Games/x'y.bin"));
	CHECK(portal::pathFromUri(QStringLiteral("https://example.org/x")).isEmpty());
	CHECK(portal::requestPathPart(QStringLiteral(":1.42")) == QStringLiteral("1_42"));
	CHECK(portal::withDefaultSuffix(QStringLiteral("/a/b"), QStringLiteral(".json")) == QStringLiteral("/a/b.json"));
	CHECK(portal::withDefaultSuffix(QStringLiteral("/a/b.txt"), QStringLiteral("json")) == QStringLiteral("/a/b.txt"));
	CHECK(portal::withDefaultSuffix(QStringLiteral("/a.d/b"), QString()) == QStringLiteral("/a.d/b"));

	/* the places beside Chimera's chooser, as the desktop keeps them */
	const QVariantList gtk = places::fromGtkBookmarks(QStringLiteral("file:///home/a/My%20Games Games\nfile:///srv\nsftp://host/x Remote\n"));
	CHECK(gtk.size() == 2);
	CHECK(gtk.size() == 2 && gtk[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Games"));
	CHECK(gtk.size() == 2 && localOf(gtk[0].toMap().value(QStringLiteral("url"))) == QStringLiteral("/home/a/My Games"));
	CHECK(gtk.size() == 2 && gtk[1].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("srv"));
	const QVariantList kde = places::fromKdeBookmarks(QStringLiteral(
		"<xbel><bookmark href=\"file:///home/a\"><title>Home</title><info><metadata owner=\"http://freedesktop.org\"><isSystemItem>true</isSystemItem></metadata></info></bookmark>"
		"<bookmark href=\"file:///usr/share/games\"><title>Shared games</title></bookmark>"
		"<bookmark href=\"file:///hidden\"><title>H</title><info><metadata owner=\"http://www.kde.org\"><IsHidden>true</IsHidden></metadata></info></bookmark>"
		"<bookmark href=\"trash:/\"><title>Trash</title></bookmark></xbel>"));
	CHECK(kde.size() == 1);
	CHECK(kde.size() == 1 && kde[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Shared games"));
	const QVariantList mounts = places::fromMounts(QStringLiteral(
		"/dev/sda1 / ext4 rw 0 0\n/dev/sdb1 /run/media/a/USB\\040Stick vfat rw 0 0\n/dev/sdc1 /mnt/games ext4 rw 0 0\nproc /proc proc rw 0 0\n"));
	CHECK(mounts.size() == 2);
	CHECK(mounts.size() == 2 && mounts[0].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("USB Stick"));
	CHECK(mounts.size() == 2 && mounts[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("drive"));
}

void portalCases(QObject *window)
{
	const QString folder = QDir::cleanPath(qEnvironmentVariable("CHIMERA_TEST_FOLDER"));
	QObject *open = dialog(window, "open");
	auto counts = [window](const char *name) { return window->property(name).toInt(); };

	/* one file: the request carries the title, the filters, the folder; the
	 * answer is the dialog's file, accepted */
	answer({{"response", 0}, {"uris", QJsonArray{QStringLiteral("file:///tmp/a%20b.chimeraProject")}}});
	open->setProperty("currentFolder", QUrl::fromLocalFile(folder));
	QMetaObject::invokeMethod(open, "open");
	CHECK(waitFor([&] { return counts("accepted") == 1; }));
	CHECK(localOf(open->property("selectedFile")) == QStringLiteral("/tmp/a b.chimeraProject"));
	CHECK(open->property("result").toInt() == 1);
	CHECK(!open->property("visible").toBool());
	QJsonObject asked = lastRequest();
	QJsonObject options = asked.value("options").toObject();
	CHECK(asked.value("method").toString() == QStringLiteral("OpenFile"));
	CHECK(asked.value("title").toString() == QStringLiteral("Pick"));
	CHECK(options.value("modal").toBool());
	CHECK(!options.value("multiple").toBool());
	CHECK(options.value("current_folder").toString() == folder);
	const QJsonArray filters = options.value("filters").toArray();
	CHECK(filters.size() == 2);
	CHECK(filters.size() == 2 && filters[0].toArray()[0].toString() == QStringLiteral("Projects (*.chimeraProject)"));
	CHECK(filters.size() == 2 && filters[0].toArray()[1].toArray()[0].toArray()[1].toString() == QStringLiteral("*.chimeraProject"));
	CHECK(filters.size() == 2 && filters[1].toArray()[1].toArray()[0].toArray()[1].toString() == QStringLiteral("*"));

	/* Cancel */
	answer({{"response", 1}});
	QMetaObject::invokeMethod(open, "open");
	CHECK(waitFor([&] { return counts("rejected") == 1; }));
	CHECK(open->property("result").toInt() == 0);
	CHECK(counts("accepted") == 1);

	/* while it is open, Chimera's windows take no keys; afterwards they do */
	answer({{"response", 1}, {"delay_ms", 800}});
	QMetaObject::invokeMethod(open, "open");
	QObject *field = dialog(window, "field");
	if (auto *w = qobject_cast<QQuickWindow *>(window))
	{
		QKeyEvent press(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
		QKeyEvent release(QEvent::KeyRelease, Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
		QCoreApplication::sendEvent(w, &press);
		QCoreApplication::sendEvent(w, &release);
		CHECK(field->property("text").toString().isEmpty());
		CHECK(waitFor([&] { return counts("rejected") == 2; }));
		QCoreApplication::sendEvent(w, &press);
		QCoreApplication::sendEvent(w, &release);
		CHECK(field->property("text").toString() == QStringLiteral("x"));
	}

	/* several files */
	QObject *many = dialog(window, "many");
	answer({{"response", 0}, {"uris", QJsonArray{QStringLiteral("file:///tmp/one"), QStringLiteral("file:///tmp/two")}}});
	QMetaObject::invokeMethod(many, "open");
	CHECK(waitFor([&] { return counts("accepted") == 2; }));
	CHECK(many->property("selectedFiles").value<QList<QUrl>>().size() == 2);
	CHECK(lastRequest().value("options").toObject().value("multiple").toBool());

	/* a save: the name the dialog was given (only a name, "file:///<name>"),
	 * and the default suffix added to an answer without one */
	QObject *save = dialog(window, "save");
	answer({{"response", 0}, {"uris", QJsonArray{QStringLiteral("file:///tmp/out")}}});
	save->setProperty("selectedFile", QUrl(QStringLiteral("file:///settings.json")));
	QMetaObject::invokeMethod(save, "open");
	CHECK(waitFor([&] { return counts("accepted") == 3; }));
	CHECK(localOf(save->property("selectedFile")) == QStringLiteral("/tmp/out.json"));
	asked = lastRequest();
	CHECK(asked.value("method").toString() == QStringLiteral("SaveFile"));
	CHECK(asked.value("options").toObject().value("current_name").toString() == QStringLiteral("settings.json"));

	/* a folder */
	QObject *folderDialog = dialog(window, "folder");
	answer({{"response", 0}, {"uris", QJsonArray{QUrl::fromLocalFile(folder).toString()}}});
	QMetaObject::invokeMethod(folderDialog, "open");
	CHECK(waitFor([&] { return counts("accepted") == 4; }));
	CHECK(localOf(folderDialog->property("selectedFolder")) == folder);
	CHECK(lastRequest().value("options").toObject().value("directory").toBool());

	/* a tick box goes as a choice with no options, and comes back as the
	 * person left it; a chooser that does not draw it leaves it as it was */
	QObject *box = dialog(window, "box");
	answer({{"response", 0}, {"uris", QJsonArray{QUrl::fromLocalFile(folder).toString()}}, {"choices", QJsonArray{QJsonArray{QStringLiteral("checkbox"), QStringLiteral("false")}}}});
	QMetaObject::invokeMethod(box, "open");
	CHECK(waitFor([&] { return counts("accepted") == 5; }));
	const QJsonArray choices = lastRequest().value("options").toObject().value("choices").toArray();
	const QJsonArray choice = choices.at(0).toArray();
	CHECK(choices.size() == 1);
	CHECK(choice.at(0).toString() == QStringLiteral("checkbox"));
	CHECK(choice.at(1).toString() == QStringLiteral("Include sub-folders"));
	CHECK(choice.at(2).isArray() && choice.at(2).toArray().isEmpty());
	CHECK(choice.at(3).toString() == QStringLiteral("true"));
	CHECK(!box->property("checkBoxChecked").toBool());
	answer({{"response", 0}, {"uris", QJsonArray{QUrl::fromLocalFile(folder).toString()}}});
	QMetaObject::invokeMethod(box, "open");
	CHECK(waitFor([&] { return counts("accepted") == 6; }));
	CHECK(lastRequest().value("options").toObject().value("choices").toArray().at(0).toArray().at(3).toString() == QStringLiteral("false"));
	CHECK(!box->property("checkBoxChecked").toBool());
	/* a dialog without one sends no choices */
	answer({{"response", 0}, {"uris", QJsonArray{QUrl::fromLocalFile(folder).toString()}}});
	QMetaObject::invokeMethod(folderDialog, "open");
	CHECK(waitFor([&] { return counts("accepted") == 7; }));
	CHECK(!lastRequest().value("options").toObject().contains("choices"));

	/* a portal whose chooser could not come up (an answer that fails at once):
	 * Chimera's own chooser instead, and its Cancel is the dialog's */
	answer({{"response", 2}, {"delay_ms", 5}});
	QMetaObject::invokeMethod(open, "open");
	CHECK(waitFor([&] { return chooser(open) && chooser(open)->property("opened").toBool(); }));
	if (chooser(open)) QMetaObject::invokeMethod(chooser(open), "close");
	CHECK(waitFor([&] { return counts("rejected") == 3; }));
	/* a "failed" answer after a person could have pressed Cancel is Cancel */
	answer({{"response", 2}, {"delay_ms", 500}});
	QMetaObject::invokeMethod(open, "open");
	CHECK(waitFor([&] { return counts("rejected") == 4; }));
	CHECK(!chooser(open)->property("opened").toBool());
}

void builtinCases(QObject *window)
{
	const QString root = QDir::cleanPath(qEnvironmentVariable("CHIMERA_TEST_FOLDER"));
	QDir(root).mkpath(QStringLiteral("sub"));
	QDir(root).mkpath(QStringLiteral(".hidden"));
	for (const char *name : {"file.txt", ".hiddenfile"})
	{
		QFile f(root + QLatin1Char('/') + QLatin1String(name));
		if (f.open(QIODevice::WriteOnly)) f.write("x");
	}
	auto counts = [window](const char *name) { return window->property(name).toInt(); };
	auto opened = [](QObject *d) { return chooser(d) && chooser(d)->property("opened").toBool(); };
	auto rows = [](QObject *d) { QObject *list = part(d, "chooserFiles"); return list ? list->property("count").toInt() : -1; };
	auto press = [](QObject *d, const char *name) { if (QObject *o = part(d, name)) QMetaObject::invokeMethod(o, "clicked"); };
	auto type = [](QObject *d, const char *name, const QString &text) {
		if (QObject *o = part(d, name))
		{
			o->setProperty("text", text);
			QMetaObject::invokeMethod(o, "accepted");
		}
	};

	/* a folder dialog picks the folder it shows: Select with nothing picked */
	QObject *folderDialog = dialog(window, "folder");
	folderDialog->setProperty("currentFolder", QUrl::fromLocalFile(root));
	QMetaObject::invokeMethod(folderDialog, "open");
	CHECK(waitFor([&] { return opened(folderDialog); }));
	CHECK(part(folderDialog, "chooserLocation") && part(folderDialog, "chooserLocation")->property("text").toString() == root);
	/* the places: the root and home first, the desktop's bookmarks among
	 * them, drawn beside the list (the texts are their keys: no language
	 * table is loaded here) */
	QStringList drawn;
	std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
		if (item == nullptr) return;
		if (item->objectName() == QStringLiteral("chooserPlace")) drawn << item->property("text").toString();
		for (QQuickItem *c : item->childItems()) walk(c);
	};
	waitFor([&] {
		drawn.clear();
		if (QObject *c = chooser(folderDialog)) walk(c->property("contentItem").value<QQuickItem *>());
		return drawn.contains(QStringLiteral("Test games"));
	});
	CHECK(drawn.contains(QStringLiteral("file_chooser.root")));
	CHECK(drawn.contains(QStringLiteral("file_chooser.home")));
	CHECK(drawn.contains(QStringLiteral("Test games")));
	press(folderDialog, "chooserAccept");
	CHECK(waitFor([&] { return counts("accepted") == 1; }));
	CHECK(localOf(folderDialog->property("selectedFolder")) == root);

	/* a path typed into the location box is gone to, and then picked */
	QMetaObject::invokeMethod(folderDialog, "open");
	CHECK(waitFor([&] { return opened(folderDialog); }));
	type(folderDialog, "chooserLocation", root + QStringLiteral("/sub"));
	CHECK(localOf(chooser(folderDialog)->property("folder")) == root + QStringLiteral("/sub"));
	press(folderDialog, "chooserAccept");
	CHECK(waitFor([&] { return counts("accepted") == 2; }));
	CHECK(localOf(folderDialog->property("selectedFolder")) == root + QStringLiteral("/sub"));
	/* a folder that is not there is said, and nothing is picked */
	QMetaObject::invokeMethod(folderDialog, "open");
	CHECK(waitFor([&] { return opened(folderDialog); }));
	type(folderDialog, "chooserLocation", root + QStringLiteral("/nowhere"));
	CHECK(opened(folderDialog));
	CHECK(chooser(folderDialog)->property("note").toString().startsWith(QStringLiteral("file_chooser.no_such_folder")));
	QMetaObject::invokeMethod(chooser(folderDialog), "close");
	CHECK(waitFor([&] { return counts("rejected") == 1; }));

	FilePlaces places;
	const QVariantList all = places.all();
	CHECK(all.size() >= 2);
	CHECK(all.size() >= 2 && all[0].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("root"));
	CHECK(all.size() >= 2 && all[1].toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("home"));
	bool bookmark = false;
	for (const QVariant &p : all)
		bookmark = bookmark || (p.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Test games") && localOf(p.toMap().value(QStringLiteral("url"))) == root + QStringLiteral("/sub"));
	CHECK(bookmark);

	/* a tick box is drawn in the chooser, ticked as the dialog says, and the
	 * dialog takes it as the person left it; a dialog without one draws none */
	QObject *box = dialog(window, "box");
	box->setProperty("currentFolder", QUrl::fromLocalFile(root));
	QMetaObject::invokeMethod(box, "open");
	CHECK(waitFor([&] { return opened(box); }));
	QObject *tick = part(box, "chooserCheckBox");
	CHECK(tick && tick->property("visible").toBool());
	CHECK(tick && tick->property("text").toString() == QStringLiteral("Include sub-folders"));
	CHECK(tick && tick->property("checked").toBool());
	if (tick)
	{
		QMetaObject::invokeMethod(tick, "toggle");
		QMetaObject::invokeMethod(tick, "toggled");
	}
	press(box, "chooserAccept");
	CHECK(waitFor([&] { return counts("accepted") == 3; }));
	CHECK(!box->property("checkBoxChecked").toBool());
	CHECK(chooser(folderDialog) && chooser(folderDialog)->property("checkBoxText").toString().isEmpty());

	/* hidden files are shown when asked */
	QObject *open = dialog(window, "open");
	open->setProperty("nameFilters", QStringList{QStringLiteral("All files (*)")});
	open->setProperty("currentFolder", QUrl::fromLocalFile(root));
	QMetaObject::invokeMethod(open, "open");
	CHECK(waitFor([&] { return opened(open) && rows(open) == 2; }));
	if (QObject *hidden = part(open, "chooserHidden")) hidden->setProperty("checked", true);
	CHECK(waitFor([&] { return rows(open) == 4; }));
	/* a name typed is opened */
	type(open, "chooserName", QStringLiteral("file.txt"));
	CHECK(waitFor([&] { return counts("accepted") == 4; }));
	CHECK(localOf(open->property("selectedFile")) == root + QStringLiteral("/file.txt"));

	/* a save: the default suffix added, and a file there asked about first */
	QObject *save = dialog(window, "save");
	save->setProperty("currentFolder", QUrl::fromLocalFile(root));
	QMetaObject::invokeMethod(save, "open");
	CHECK(waitFor([&] { return opened(save); }));
	type(save, "chooserName", QStringLiteral("new"));
	CHECK(waitFor([&] { return counts("accepted") == 5; }));
	CHECK(localOf(save->property("selectedFile")) == root + QStringLiteral("/new.json"));
	QMetaObject::invokeMethod(save, "open");
	CHECK(waitFor([&] { return opened(save); }));
	type(save, "chooserName", QStringLiteral("file.txt"));
	CHECK(opened(save));
	CHECK(chooser(save)->property("note").toString().startsWith(QStringLiteral("file_chooser.replace_question")));
	/* Save again is Replace */
	press(save, "chooserAccept");
	CHECK(waitFor([&] { return counts("accepted") == 6; }));
	CHECK(localOf(save->property("selectedFile")) == root + QStringLiteral("/file.txt"));
}
} // namespace

int main(int argc, char **argv)
{
	const bool withPortal = argc > 1 && std::strcmp(argv[1], "--portal") == 0;
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	helpers();

	QQmlEngine engine;
	PortalFileChooser portalChooser;
	FilePlaces filePlaces;
	engine.rootContext()->setContextProperty(QStringLiteral("portalChooser"), &portalChooser);
	engine.rootContext()->setContextProperty(QStringLiteral("filePlaces"), &filePlaces);
	QQmlComponent component(&engine);
	component.setData(kWindow, QUrl());
	std::unique_ptr<QObject> window(component.create());
	if (!window) std::printf("%s\n", qPrintable(component.errorString()));
	CHECK(window != nullptr);
	if (window)
	{
		waitFor([] { return false; }, 100);
		if (withPortal) portalCases(window.get());
		else builtinCases(window.get());
	}
	std::printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
