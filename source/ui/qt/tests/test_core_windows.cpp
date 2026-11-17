/* A core's own windows (issue 610): what their locked-down QML engine lets a
 * core's QML reach, and a real core window's QML over its view model.
 *
 *   test_core_windows cases <folder> <case>...   each <folder>/<case>.qml
 *       loaded in a core's engine: what it reports, what it asked Chimera's
 *       file dialog for and what was refused printed (core-windows.py writes
 *       the cases and listens for anything that gets out)
 *   test_core_windows window <Monitor.qml>   the synthetic core's window over
 *       a core window view model whose core is a stand-in here: what it
 *       shows, a change and a command sent to the core
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <deque>
#include <string>

#include "chimera/vm/core_window_vm.hpp"
#include "../core_windows.hpp"
#include "../qml_view_model.hpp"

using namespace chimera;

static int failures = 0;
#define CHECK(c) \
	do \
	{ \
		if (!(c)) \
		{ \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
			failures++; \
		} \
	} while (0)

/* what a case's QML says, a line each */
class Probe : public QObject
{
	Q_OBJECT
public:
	Q_INVOKABLE void report(const QString &what, const QString &result)
	{
		std::printf("REPORT %s :: %s\n", what.toUtf8().constData(), result.toUtf8().constData());
		std::fflush(stdout);
	}
};

static void wait(int ms)
{
	QEventLoop loop;
	QTimer::singleShot(ms, &loop, &QEventLoop::quit);
	loop.exec();
}

static int cases(const QString &folder, const QStringList &names)
{
	for (const QString &name : names)
	{
		qt::CoreQmlEngine engine(folder);
		Probe probe;
		qt::CoreFiles files(name);
		QObject::connect(&files, &qt::CoreFiles::wanted, [name](const QString &purpose, const QString &mode, const QString &title,
			const QStringList &filters, const QString &suggested) {
			std::printf("WANTED %s :: %s|%s|%s|%s|%s\n", name.toUtf8().constData(), purpose.toUtf8().constData(), mode.toUtf8().constData(),
				title.toUtf8().constData(), filters.join(QLatin1Char(',')).toUtf8().constData(), suggested.toUtf8().constData());
			std::fflush(stdout);
		});
		engine.rootContext()->setContextProperty(QStringLiteral("probe"), &probe);
		engine.rootContext()->setContextProperty(QStringLiteral("files"), &files);
		QQmlComponent component(&engine, QUrl::fromLocalFile(QDir(folder).filePath(name + QStringLiteral(".qml"))));
		QObject *made = component.create();
		std::printf("CASE %s created=%d\n", name.toUtf8().constData(), made != nullptr ? 1 : 0);
		if (made == nullptr) std::printf("ERROR %s\n", component.errorString().simplified().toUtf8().constData());
		wait(1500);
		for (const QString &r : engine.refused()) std::printf("REFUSED %s %s\n", name.toUtf8().constData(), r.toUtf8().constData());
		std::fflush(stdout);
		delete made;
	}
	return 0;
}

/* a core of the test's own: answers as the synthetic core's monitor does,
 * and keeps what it was asked */
struct StandInCore
{
	std::deque<std::string> asked;
};

static QQuickItem *child(QQuickItem *root, const char *name)
{
	return root->findChild<QQuickItem *>(QString::fromLatin1(name));
}

/* the view model's work on the view's thread, at once */
class Now : public vm::Dispatcher
{
public:
	void post(std::function<void()> fn) override { fn(); }
};

static int window(const QString &qmlFile)
{
	Now dispatcher;
	StandInCore core;
	const std::string description =
		"{\"properties\":[{\"name\":\"pc\"},{\"name\":\"registers\"},{\"name\":\"held\"},{\"name\":\"heldCount\"},"
		"{\"name\":\"stopAt\",\"writable\":true},{\"name\":\"lastFile\"}],\"commands\":[{\"name\":\"continue\"},{\"name\":\"step\"}],"
		"\"lists\":[{\"name\":\"ram\",\"columns\":[\"address\",\"value\"]}]}";
	std::string givenPath;
	vm::CoreWindowViewModel model("monitor", description, dispatcher,
		[&core](std::string request, std::function<void(const std::string &)> answered) {
			core.asked.push_back(request);
			answered("{}");
		},
		[&core, &givenPath](std::string request, std::string path, int, std::function<void(const std::string &)> answered) {
			core.asked.push_back(request);
			givenPath = path;
			answered("{}");
		});
	CHECK(model.problem().empty());
	model.show("{\"properties\":{\"pc\":5,\"registers\":\"R0=1 R1=2\",\"held\":false,\"heldCount\":0,\"stopAt\":-1},"
			   "\"lists\":{\"ram\":[[\"00\",\"7F\"],[\"01\",\"80\"]]}}");
	qt::QmlViewModel qml(model);
	QQmlPropertyMap texts;
	texts.insert(QStringLiteral("window.monitor.held"), QStringLiteral("The frame is held"));
	texts.insert(QStringLiteral("window.monitor.running"), QStringLiteral("Running"));
	texts.insert(QStringLiteral("window.monitor.stop_at"), QStringLiteral("Stop at instruction"));
	texts.insert(QStringLiteral("window.monitor.step"), QStringLiteral("Step"));
	texts.insert(QStringLiteral("window.monitor.continue"), QStringLiteral("Continue"));
	texts.insert(QStringLiteral("window.monitor.load"), QStringLiteral("Load..."));
	texts.insert(QStringLiteral("window.monitor.save"), QStringLiteral("Save..."));
	qt::CoreFiles files(QStringLiteral("monitor"));
	QStringList wanted;
	QObject::connect(&files, &qt::CoreFiles::wanted, [&wanted](const QString &purpose, const QString &mode, const QString &title,
		const QStringList &, const QString &name) { wanted = {purpose, mode, title, name}; });
	/* the window's files where a package's are unpacked, and only those */
	QTemporaryDir dir;
	QFile::copy(qmlFile, dir.filePath(QStringLiteral("Monitor.qml")));
	qt::CoreQmlEngine engine(dir.path());
	QQmlContext context(engine.rootContext());
	context.setContextProperty(QStringLiteral("core"), &qml);
	context.setContextProperty(QStringLiteral("texts"), &texts);
	context.setContextProperty(QStringLiteral("files"), &files);
	QQmlComponent component(&engine, QUrl::fromLocalFile(dir.filePath(QStringLiteral("Monitor.qml"))));
	QObject *made = component.create(&context);
	if (made == nullptr) std::printf("ERROR %s\n", component.errorString().simplified().toUtf8().constData());
	auto *root = qobject_cast<QQuickItem *>(made);
	CHECK(root != nullptr);
	if (root == nullptr) return 1;
	/* nothing it needed was refused: Qt Quick, its Controls and Layouts load */
	CHECK(engine.refused().isEmpty());
	for (const QString &r : engine.refused()) std::printf("refused: %s\n", r.toUtf8().constData());
	/* what the core answered, shown */
	CHECK(child(root, "pc") && child(root, "pc")->property("text").toString() == QStringLiteral("PC 5"));
	CHECK(child(root, "registers") && child(root, "registers")->property("text").toString() == QStringLiteral("R0=1 R1=2"));
	CHECK(child(root, "state") && child(root, "state")->property("text").toString() == QStringLiteral("Running"));
	CHECK(child(root, "step") && !child(root, "step")->property("enabled").toBool());
	CHECK(child(root, "ram") && child(root, "ram")->property("count").toInt() == 2);
	/* held, as the core says next: the buttons come on */
	model.show("{\"properties\":{\"pc\":0,\"held\":true,\"heldCount\":1}}");
	CHECK(child(root, "state")->property("text").toString() == QStringLiteral("The frame is held"));
	CHECK(child(root, "step")->property("enabled").toBool());
	/* Step pressed: the core is asked to run it */
	QMetaObject::invokeMethod(child(root, "step"), "clicked");
	CHECK(!core.asked.empty() && core.asked.back() == "{\"op\":\"run\",\"window\":\"monitor\",\"command\":\"step\",\"args\":[]}");
	/* a stop place typed: the core is asked to set it */
	QQuickItem *field = child(root, "stopAt");
	CHECK(field != nullptr);
	if (field != nullptr)
	{
		field->setProperty("text", QStringLiteral("12"));
		QMetaObject::invokeMethod(field, "editingFinished");
		CHECK(!core.asked.empty() && core.asked.back() == "{\"op\":\"set\",\"window\":\"monitor\",\"name\":\"stopAt\",\"value\":12}");
	}
	/* Save...: Chimera's dialog asked for, a name suggested; the file the
	 * person picks goes to the core, which is told its name and nothing more */
	QMetaObject::invokeMethod(child(root, "save"), "clicked");
	CHECK(wanted == QStringList({QStringLiteral("save"), QStringLiteral("write"), QStringLiteral("Save..."), QStringLiteral("registers.txt")}));
	model.giveFile("save", vm::CoreWindowViewModel::Write, "/somewhere/else/regs.txt");
	CHECK(givenPath == "/somewhere/else/regs.txt");
	CHECK(!core.asked.empty() && core.asked.back() ==
		"{\"op\":\"file\",\"window\":\"monitor\",\"purpose\":\"save\",\"mode\":\"write\",\"file\":\"window-file\",\"name\":\"regs.txt\"}");
	QMetaObject::invokeMethod(child(root, "load"), "clicked");
	CHECK(wanted.size() == 4 && wanted[0] == QStringLiteral("load") && wanted[1] == QStringLiteral("read"));
	for (const std::string &a : core.asked) std::printf("asked: %s\n", a.c_str());
	delete made;
	return 0;
}

int main(int argc, char **argv)
{
	qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
	QGuiApplication app(argc, argv);
	const QStringList args = app.arguments();
	if (args.size() >= 3 && args[1] == QStringLiteral("cases")) return cases(args[2], args.mid(3));
	if (args.size() >= 3 && args[1] == QStringLiteral("window"))
	{
		window(args[2]);
		std::printf("%d failed\n", failures);
		return failures == 0 ? 0 : 1;
	}
	std::fprintf(stderr, "usage: test_core_windows cases <folder> <case>... | window <Monitor.qml>\n");
	return 2;
}

#include "test_core_windows.moc"
