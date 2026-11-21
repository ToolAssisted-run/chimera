/* A .wch file dropped on RAM Watch (issue 623): it opens as File > Open opens
 * it. With unsaved changes the window asks first (Escape leaves the list as it
 * is, No lets the changes go and opens the file); without, it opens at once.
 * Other files are left alone. The window over the view model the program uses;
 * the drop and the clicks are real ones, through Qt's test module.
 *   test-ram-watch-drop <work folder> */
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
#include <fstream>
#include <functional>
#include <string>

#include "chimera/vm/input_log.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "../qml_view_model.hpp"

namespace fs = std::filesystem;

/* the list's path names this file: compared as paths, since Windows writes it
 * with '/' (from the dropped URL) where fs::path::string() has '\\' */
static bool isFile(const std::string &path, const fs::path &file)
{
	return !path.empty() && fs::path(path).lexically_normal() == file.lexically_normal();
}
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
RamWatch { vm: watches; visible: true }
)";

/* files dragged in over the window's middle and let go, as the system hands them over */
void dropOn(QQuickWindow *window, const QList<QUrl> &urls)
{
	const QPoint at(window->width() / 2, window->height() / 2);
	QMimeData mime;
	mime.setUrls(urls);
	QDragEnterEvent enter(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &enter);
	QDragMoveEvent move(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &move);
	QDropEvent let(at, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
	QCoreApplication::sendEvent(window, &let);
	QCoreApplication::processEvents();
}

QUrl urlOf(const fs::path &p) { return QUrl::fromLocalFile(QString::fromStdString(p.string())); }

/* the button whose text is `text`, anywhere under `item` (the dialog is drawn inside the window) */
QQuickItem *buttonIn(QQuickItem *item, const QString &text)
{
	if (QString(item->metaObject()->className()).contains(QLatin1String("Button")) && item->property("text").toString().remove('&') == text &&
		item->isVisible())
		return item;
	for (QQuickItem *child : item->childItems())
		if (QQuickItem *found = buttonIn(child, text)) return found;
	return nullptr;
}

bool opened(QObject *dialog) { return dialog != nullptr && dialog->property("visible").toBool(); }

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: test-ram-watch-drop <work folder>\n");
		return 2;
	}
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	const fs::path work = argv[1];
	std::error_code ec;
	fs::remove_all(work, ec);
	fs::create_directories(work);

	QtDispatcher d(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(d, inputs);
	vm::RamWatchViewModel watches(d, session);
	qt::QmlViewModel watchesQml(watches);
	std::string error;

	/* two saved lists, each a separator with its own note, and a file that is no list */
	const fs::path one = work / "one.wch", two = work / "Two.WCH", other = work / "notes.txt";
	CHECK(watches.run("addSeparator", {std::string("first")}, error).has_value());
	CHECK(watches.run("save", {one.string()}, error).has_value());
	CHECK(watches.run("newList", {}, error).has_value());
	CHECK(watches.run("addSeparator", {std::string("second")}, error).has_value());
	CHECK(watches.run("addSeparator", {std::string("second again")}, error).has_value());
	CHECK(watches.run("save", {two.string()}, error).has_value());
	std::ofstream(other) << "not a list\n";
	CHECK(watches.run("newList", {}, error).has_value());

	QQmlApplicationEngine engine;
	engine.rootContext()->setContextProperty(QStringLiteral("watches"), &watchesQml);
	engine.loadData(kWindow);
	auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
	CHECK(window != nullptr);
	if (window == nullptr) return 1;
	CHECK(QTest::qWaitForWindowExposed(window));
	QObject *ask = window->findChild<QObject *>(QStringLiteral("askSaveDialog"));
	CHECK(ask != nullptr);
	if (ask == nullptr) return 1;

	/* nothing changed: dropped on, a list opens at once (a name in capitals too), and a file that is no list is left alone */
	dropOn(window, {urlOf(other)});
	QTest::qWait(200);
	CHECK(watches.path.get().empty() && watches.count.get() == 0 && !opened(ask));
	dropOn(window, {urlOf(other), urlOf(two), urlOf(one)});
	CHECK(QTest::qWaitFor([&] { return isFile(watches.path.get(), two); }, 3000));
	CHECK(watches.count.get() == 2 && !opened(ask));
	std::printf("dropped notes.txt, Two.WCH, one.wch: opened %s with %lld rows\n", watches.path.get().c_str(), static_cast<long long>(watches.count.get()));

	/* changed: it asks first; Escape leaves the list as it is */
	CHECK(watches.run("addSeparator", {std::string("unsaved")}, error).has_value());
	CHECK(watches.changed.get() && watches.count.get() == 3);
	dropOn(window, {urlOf(one)});
	CHECK(QTest::qWaitFor([&] { return opened(ask); }, 2000));
	CHECK(isFile(watches.path.get(), two) && watches.count.get() == 3);
	QTest::keyClick(window, Qt::Key_Escape);
	CHECK(QTest::qWaitFor([&] { return !opened(ask); }, 2000));
	QTest::qWait(200);
	CHECK(isFile(watches.path.get(), two) && watches.count.get() == 3 && watches.changed.get());

	/* asked again: No lets the changes go and opens the file */
	dropOn(window, {urlOf(one)});
	CHECK(QTest::qWaitFor([&] { return opened(ask); }, 2000));
	QQuickItem *no = buttonIn(window->contentItem(), QStringLiteral("No"));
	CHECK(no != nullptr);
	if (no != nullptr)
	{
		const QPointF centre = no->mapToScene(QPointF(no->width() / 2, no->height() / 2));
		QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
	}
	CHECK(QTest::qWaitFor([&] { return isFile(watches.path.get(), one); }, 3000));
	CHECK(watches.count.get() == 1 && !watches.changed.get() && !opened(ask));
	std::printf("changed list, dropped one.wch, No: opened %s with %lld row\n", watches.path.get().c_str(), static_cast<long long>(watches.count.get()));

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
