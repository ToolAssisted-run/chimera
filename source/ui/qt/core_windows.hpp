#pragma once
/* A core's own windows (issue 610, user-decided 2026-11-17). A core extends
 * Chimera's window with windows of its own: their design is QML in its
 * package ("windows/" and waterbox.config's "windows"), the core itself is
 * their view model (vm::CoreWindowViewModel), and Chimera knows nothing of
 * what any of them is for.
 *
 * The QML runs in Chimera's process, outside the sandbox, so it runs locked
 * down (the owner's choice, 2026-11-17): it reads any file and writes only in
 * its core's output folder (CoreFiles, below), and each core's windows have an
 * engine of their own that loads code from nothing but the core's own window
 * files and Qt Quick's modules (Controls, Layouts and what they use), and sends nothing
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

/* A core's window and the files of the computer (owner, 2026-11-17): it
 * reads any file, and writes only inside its core's own output folder (Paths:
 * "Core output", ./output at first, a folder a core named after it, made at
 * the first write). Given to its QML as `files`. A name to write is under that
 * folder: an absolute one, one climbing out with "..", or one through a link
 * that leads outside is refused, and nothing is written. A relative name to
 * read is under it too; an absolute one is anywhere */
class CoreFiles : public QObject
{
	Q_OBJECT
	Q_PROPERTY(QString outputFolder READ outputFolder CONSTANT)
	Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
	explicit CoreFiles(QString outputFolder, QObject *parent = nullptr);
	QString outputFolder() const { return output_; }
	/* why the last call failed, "" when it did not */
	QString error() const { return error_; }

	/* a file's text (UTF-8), "" with error said when it cannot be read */
	Q_INVOKABLE QString read(const QString &path);
	/* a file's bytes, as an ArrayBuffer */
	Q_INVOKABLE QByteArray readBytes(const QString &path);
	Q_INVOKABLE bool exists(const QString &path);
	/* the names in a folder, folders first, each ending in "/" */
	Q_INVOKABLE QStringList list(const QString &folder);
	/* written under the output folder, its folders made; false with error said */
	Q_INVOKABLE bool write(const QString &name, const QString &text);
	Q_INVOKABLE bool writeBytes(const QString &name, const QByteArray &bytes);
	Q_INVOKABLE bool append(const QString &name, const QString &text);

Q_SIGNALS:
	void errorChanged();

private:
	QString readable(const QString &path) const;
	/* where a name to write is, "" when it is not under the output folder */
	QString writable(const QString &name);
	bool put(const QString &name, const QByteArray &bytes, bool add);
	void say(const QString &why);
	QString output_, error_;
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
	/* where every core's output folder is (Paths: "Core output"); a core's is
	 * a folder inside it named after the core */
	void setOutputBase(const QString &folder) { outputBase_ = folder; }
	/* a refused link, given to no one */
	Q_INVOKABLE void ignoreUrl(const QUrl &) {}

private:
	struct Open
	{
		std::shared_ptr<vm::CoreWindowViewModel> vm;
		QmlViewModel *qml = nullptr;
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
	QString outputBase_;
	CoreFiles *files_ = nullptr;
	QHash<QString, Open> open_;
	std::vector<vm::Connection> watches_;
};

} // namespace chimera::qt
