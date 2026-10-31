/* Files dropped on a slot of New Project's files page, all of them: each file
 * added builds the list of slots again, which took away the slot that got
 * the drop, so its loop over the files ended after the first (issue 425).
 * The window over the view model the program uses, with the synthetic core;
 * the drop is a real one, through Qt's test module.
 *   test-new-project-drop <package> <game> <work folder> */
#include <QGuiApplication>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QUrl>

#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>

#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/input_log.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "../file_places.hpp"
#include "../portal_file_chooser.hpp"
#include "../qml_view_model.hpp"

namespace fs = std::filesystem;
using namespace chimera;

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* the view models' work, into Qt's event loop, as the window has it */
class QtDispatcher : public vm::Dispatcher
{
public:
	explicit QtDispatcher(QObject *context) : context_(context) {}
	void post(std::function<void()> fn) override { QMetaObject::invokeMethod(context_, std::move(fn), Qt::QueuedConnection); }

private:
	QObject *context_;
};

const char *const kWindow = R"(
import QtQuick
import "qrc:/chimera"
NewProject { vm: wizard; visible: true }
)";

/* the first item of a kind under an item */
QQuickItem *findKind(QQuickItem *item, const char *kind)
{
	if (QString::fromLatin1(item->metaObject()->className()).startsWith(QLatin1String(kind))) return item;
	for (QQuickItem *child : item->childItems())
		if (QQuickItem *found = findKind(child, kind)) return found;
	return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: test-new-project-drop <package> <game> <work folder>\n");
		return 2;
	}
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	const fs::path package = argv[1], game = argv[2], work = argv[3];
	std::error_code ec;
	fs::remove_all(work, ec);
	fs::create_directories(work / "Cores");
	fs::copy_file(package, work / "Cores" / package.filename());
	QList<QUrl> dropped;
	for (const char *name : {"one.testrom", "two.testrom", "three.testrom"})
	{
		fs::copy_file(game, work / name);
		dropped.push_back(QUrl::fromLocalFile(QString::fromStdString((work / name).string())));
	}

	QtDispatcher d(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(d, inputs);
	vm::CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (work / "Cores").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	vm::NewProjectViewModel wizard(d, session, installed);
	qt::QmlViewModel wizardQml(wizard);

	QQmlApplicationEngine engine;
	qt::PortalFileChooser portalChooser;
	qt::FilePlaces filePlaces;
	engine.rootContext()->setContextProperty(QStringLiteral("portalChooser"), &portalChooser);
	engine.rootContext()->setContextProperty(QStringLiteral("filePlaces"), &filePlaces);
	engine.rootContext()->setContextProperty(QStringLiteral("wizard"), &wizardQml);
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	CHECK(QTest::qWaitForWindowExposed(window));

	/* the synthetic core, and its files page: one slot, a list that takes drops */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	QMetaObject::invokeMethod(window, "showPage", Q_ARG(QVariant, 1));
	QQuickItem *drop = nullptr;
	CHECK(QTest::qWaitFor([&] { return (drop = findKind(window->contentItem(), "QQuickDropArea")) != nullptr && drop->isVisible(); }));
	if (drop == nullptr) return 1;
	const QPoint at = drop->mapToScene(QPointF(drop->width() / 2, drop->height() / 2)).toPoint();

	/* three files dragged in and let go, as the system hands them over */
	QMimeData mime;
	mime.setUrls(dropped);
	QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &enter);
	QDragMoveEvent move(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &move);
	QDropEvent let(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &let);
	QCoreApplication::processEvents();

	/* every file is in the slot, in the order dropped */
	const vm::RowModel *slotList = nullptr;
	for (const auto &list : wizard.rowLists())
		if (list.name == "slots") slotList = list.model;
	CHECK(slotList != nullptr && slotList->rowCount() == 1);
	std::string files;
	if (slotList != nullptr && slotList->rowCount() == 1)
		for (int32_t c = 0; c < slotList->columnCount(); c++)
			if (slotList->columnName(c) == "files") files = std::get<std::string>(slotList->cell(0, c));
	CHECK(files == "one.testrom\ntwo.testrom\nthree.testrom");
	if (files != "one.testrom\ntwo.testrom\nthree.testrom") std::fprintf(stderr, "  files: [%s]\n", files.c_str());

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
