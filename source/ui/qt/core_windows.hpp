#pragma once
/* A core's own windows (issue 610, user-decided 2026-11-17). A core extends
 * Chimera's window with windows of its own: their design is QML in its
 * package ("windows/" and waterbox.config's "windows"), the core itself is
 * their view model (vm::CoreWindowViewModel), and Chimera knows nothing of
 * what any of them is for.
 *
 * The QML runs in Chimera's process, outside the sandbox, so it runs locked
 * down (the owner's choice, 2026-11-17): a file of the computer reaches the core
 * only when the person picks it in Chimera's own dialog (CoreFiles, below), and
 * each core's windows have an engine of their own that loads nothing but the
 * core's own window files and Qt Quick's modules (Controls, Layouts and what
 * they use), and sends nothing
 * anywhere - every request its scripts, an Image or a Loader would
 * make goes to a scheme nothing serves. What is refused is said once each on
 * standard error and kept for the tests. */

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QStringList>
#include <QUrl>

#include <utility>

#include <memory>
#include <vector>

#include "chimera/vm/observable.hpp"

namespace chimera::vm {
class SessionViewModel;
class CoreWindowViewModel;
}
class QQuickWindow;
class QQmlContext;

namespace chimera::qt {

class QmlViewModel;

/* The engine a core's windows run in: its window files are under folder,
 * and nothing else outside Qt Quick's own modules is loaded */
class CoreQmlEngine : public QQmlEngine
{
	Q_OBJECT
public:
	explicit CoreQmlEngine(const QString &folder, QObject *parent = nullptr);
	~CoreQmlEngine() override;
	/* the addresses refused so far, each once */
	QStringList refused() const { return refused_; }

private:
	class Gate;
	class NoNetwork;
	std::unique_ptr<Gate> gate_;
	std::unique_ptr<NoNetwork> noNetwork_;
	QStringList refused_;
};

/* A core's window and the files of the computer (owner, 2026-11-17): any
 * file may be read or written, but only one the person picks in Chimera's own
 * file dialog, and it goes to the core, not to the QML: Chimera opens it and
 * hands it to the core, in its sandbox, for one request ("file"). Neither the
 * core nor its window ever opens a file of the computer on its own. Given to
 * each window's QML as `files` */
class CoreFiles : public QObject
{
	Q_OBJECT
public:
	explicit CoreFiles(QString window, QObject *parent = nullptr) : QObject(parent), window_(std::move(window)) {}
	const QString &window() const { return window_; }

	/* asks Chimera to show its dialog: mode "read" (open a file) or "write"
	 * (save one, name suggested); the file picked is given to the core with
	 * this purpose, and `chosen` says so, with the file's own name */
	Q_INVOKABLE void choose(const QString &purpose, const QString &mode, const QString &title,
		const QStringList &nameFilters = {}, const QString &name = {});

Q_SIGNALS:
	void wanted(const QString &purpose, const QString &mode, const QString &title, const QStringList &nameFilters,
		const QString &name);
	void chosen(const QString &purpose, const QString &name);
	void cancelled(const QString &purpose);

private:
	QString window_;
};

/* The windows of the core open: opened from the Emulator menu, each a window
 * of its own over its view model, closed when the machine goes */
class CoreWindows : public QObject
{
	Q_OBJECT
public:
	CoreWindows(vm::SessionViewModel &session, QObject *parent = nullptr);
	~CoreWindows() override;

	/* opens (or brings up) the open core's window by its id: "" when it is
	 * open, else why not, in a sentence */
	Q_INVOKABLE QString open(const QString &id);
	/* for the tests: the window open by id (null for none), and the engine
	 * of the core open (null before a window opened) */
	QQuickWindow *window(const QString &id) const;
	CoreQmlEngine *engine() const { return engine_.get(); }
	/* the person picked a file in the dialog a window asked for, or did not:
	 * given to the core, or the window told */
	Q_INVOKABLE void fileChosen(const QString &id, const QString &purpose, const QString &mode, const QString &path);
	Q_INVOKABLE void fileCancelled(const QString &id, const QString &purpose);

Q_SIGNALS:
	/* a window asks for a file: the main window shows Chimera's dialog */
	void fileWanted(const QString &id, const QString &purpose, const QString &mode, const QString &title,
		const QStringList &nameFilters, const QString &name);

public:
	/* a refused link, given to no one */
	Q_INVOKABLE void ignoreUrl(const QUrl &) {}

private:
	struct Open
	{
		std::shared_ptr<vm::CoreWindowViewModel> vm;
		QmlViewModel *qml = nullptr;
		CoreFiles *files = nullptr;
		QQmlContext *context = nullptr;
		QPointer<QQuickWindow> window;
	};
	void closeAll();
	/* the core's window files, written out of its package into the cache
	 * once per package; "" with why when they cannot be */
	QString unpack(QString &why);

	vm::SessionViewModel &session_;
	std::unique_ptr<CoreQmlEngine> engine_;
	QString enginePackage_, engineFolder_;
	QQmlPropertyMap *texts_ = nullptr;
	QHash<QString, Open> open_;
	std::vector<vm::Connection> watches_;
};

} // namespace chimera::qt
