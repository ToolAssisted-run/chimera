/* New Project's compile page while the sessions run (issue 618): the bar says
 * it is busy before the first module is done, and the list of modules follows
 * the newest one. The window over the view model the program uses, the
 * synthetic core's package that asks for compiling, and a stand-in for the
 * compile session that says what it does when the test lets it.
 *   test-compile-page <package> <game> <work folder> */
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

QQuickItem *findNamed(QQuickItem *item, const QString &name)
{
	if (item->objectName() == name) return item;
	for (QQuickItem *child : item->childItems())
		if (QQuickItem *found = findNamed(child, name)) return found;
	return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
#ifdef _WIN32
	std::printf("SKIP the compile page: the stand-in session is a shell script\n");
	return 0;
#else
	QGuiApplication app(argc, argv);
	if (argc < 4)
	{
		std::fprintf(stderr, "usage: test-compile-page <package> <game> <work folder>\n");
		return 2;
	}
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	const fs::path package = argv[1], game = argv[2], work = argv[3];
	std::error_code ec;
	fs::remove_all(work, ec);
	fs::create_directories(work / "Cores");
	fs::copy_file(package, work / "Cores" / "synth-compile.chimeraCore");
	fs::copy_file(game, work / "walker.testrom");

	/* a compile session that waits for the test twice: before it has finished
	 * a module, and before it ends, with eighty modules stored in between */
	const fs::path script = work / "session.sh";
	std::ofstream(script) << "#!/bin/sh\n"
		"out=; while [ $# -gt 0 ]; do case \"$1\" in --cache-dir) out=$2; shift;; esac; shift; done\n"
		"mkdir -p \"$out/synth\"\n"
		"echo 'Precompiled 0/1 modules'\n"
		"while [ ! -e '" << (work / "go").string() << "' ]; do sleep 0.1; done\n"
		"for i in $(seq 1 80); do printf 'm%s' $i > \"$out/synth/module$i.bin\"; "
		"echo \"[cache] stored synth/module$i.bin $(sha1sum \"$out/synth/module$i.bin\" | cut -d' ' -f1)\"; "
		"echo \"Precompiled $i/80 modules\"; done\n"
		"while [ ! -e '" << (work / "end").string() << "' ]; do sleep 0.1; done\n";
	fs::permissions(script, fs::perms::owner_all);

	QtDispatcher d(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(d, inputs);
	vm::CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (work / "Cores").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	vm::NewProjectViewModel wizard(d, session, installed);
	wizard.compileRunner.set(script.string());
	wizard.compileRoot.set((work / "compiled").string());
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
	window->resize(900, 600);
	CHECK(QTest::qWaitForWindowExposed(window));

	CHECK(wizard.run("chooseCore", {std::string("Synth Compile")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), (work / "walker.testrom").string()}, error).has_value());
	CHECK(wizard.compileAsked.get());
	QMetaObject::invokeMethod(window, "showPage", Q_ARG(QVariant, 4));
	QQuickItem *list = nullptr, *bar = nullptr;
	CHECK(QTest::qWaitFor([&] {
		list = findNamed(window->contentItem(), QStringLiteral("compiledModules"));
		bar = findNamed(window->contentItem(), QStringLiteral("compileBar"));
		return list != nullptr && list->isVisible() && bar != nullptr;
	}));
	if (list == nullptr || bar == nullptr)
	{
		std::fprintf(stderr, "  list %p (visible %d), bar %p, page %d, compileAsked %d\n", static_cast<void *>(list), list != nullptr && list->isVisible(),
			static_cast<void *>(bar), window->property("page").toInt(), wizard.compileAsked.get() ? 1 : 0);
		return 1;
	}

	/* the session has said "0 of 1" and no module is done: the bar is busy, not stuck */
	CHECK(wizard.run("compile", {}, error).has_value());
	CHECK(QTest::qWaitFor([&] { return wizard.compiling.get() && wizard.compileTotal.get() == 1; }, 20000));
	CHECK(bar->property("indeterminate").toBool());

	/* eighty modules arrive, more than the list shows: it is at the newest */
	std::ofstream(work / "go") << "go";
	CHECK(QTest::qWaitFor([&] { return wizard.compileDone.get() == 80; }, 60000));
	CHECK(wizard.compiling.get());
	CHECK(QTest::qWaitFor([&] { return list->property("count").toInt() == 80 && list->property("atYEnd").toBool(); }, 10000));
	CHECK(list->property("contentHeight").toDouble() > list->height());
	CHECK(!bar->property("indeterminate").toBool());
	if (!list->property("atYEnd").toBool())
		std::fprintf(stderr, "  list: count %d, contentY %.0f, content %.0f, shown %.0f\n", list->property("count").toInt(),
			list->property("contentY").toDouble(), list->property("contentHeight").toDouble(), list->height());

	std::ofstream(work / "end") << "end";
	CHECK(QTest::qWaitFor([&] { return !wizard.compiling.get(); }, 30000));
	std::printf("%d checks, %d failed\n", checks, failures);
	fs::remove_all(work, ec);
	return failures == 0 ? 0 : 1;
#endif
}
