#include "core_windows.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>

#include <QtQml/qtqmlglobal.h>
/* A Qt whose QML reaches the network needs the manager below, which refuses
 * every request; one built without Qt's network module (Chimera's releases
 * for Linux and Windows) gives QML no way to reach anything */
#if QT_CONFIG(qml_network)
#ifndef CHIMERA_QML_REFUSER
#error "this Qt's QML reaches the network: build with Qt's Network module, so a core's window refuses every request (source/ui/meson.build)"
#endif
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QQmlNetworkAccessManagerFactory>
#endif

#include <algorithm>
#include <cstdio>
#include <utility>

#include "chimera/engine.h"
#include "chimera/vm/core_window_vm.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/tr.hpp"
#include "qml_view_model.hpp"

namespace chimera::qt {

namespace {

/* the scheme an address refused is given: nothing serves it, so a request
 * made to it fails at once and goes nowhere */
const QString kRefused = QStringLiteral("chimera-refused");

/* Qt Quick's modules a core's window may use: Qt Quick itself, its
 * Controls (and the styles and templates they are made of) and Layouts,
 * QtQml's own, and Chimera's style of the Controls. Not Dialogs, not
 * LocalStorage, not Qt.labs or QtCore (files, settings, folders) */
bool allowedModule(const QString &path)
{
	static const QStringList allowed = {
		QStringLiteral("QtQuick/qmldir"), QStringLiteral("QtQuick/Controls/"), QStringLiteral("QtQuick/Templates/"),
		QStringLiteral("QtQuick/Layouts/"), QStringLiteral("QtQuick/Window/"), QStringLiteral("QtQuick/NativeStyle/"),
		QStringLiteral("QtQml/"), QStringLiteral("ChimeraStyle/"),
	};
	for (const QString &a : allowed)
		if (path.startsWith(a)) return true;
	return false;
}

} // namespace

/* What a core's QML loads: its own window files, and the modules above */
class CoreQmlEngine::Gate : public QQmlAbstractUrlInterceptor
{
public:
	Gate(CoreQmlEngine &engine, QString folder) : engine_(engine), folder_(std::move(folder))
	{
		/* where Qt keeps its modules: where Qt says, and every folder the
		 * engine looks in for modules (a program with no qt.conf, as on
		 * Windows, finds them beside it, lib/qml, where Qt does not say) */
		QStringList roots = engine.importPathList();
		roots << QLibraryInfo::path(QLibraryInfo::QmlImportsPath);
		for (const QString &r : roots)
		{
			const QString real = QDir(r).canonicalPath();
			if (!real.isEmpty() && !sameOrUnder(real + QLatin1Char('/'), folder_)) moduleRoots_ << real + QLatin1Char('/');
		}
	}
	QUrl intercept(const QUrl &url, DataType type) override
	{
		if (allowed(url)) return url;
		const QString said = url.toString();
		/* Qt looks for a module in places that are not there (versioned
		 * folders, the program's own): refused without a word; what is said
		 * and kept is something there, or an address */
		const bool there = url.isLocalFile() ? QFileInfo::exists(url.toLocalFile())
			: url.scheme() == QLatin1String("qrc") ? QFileInfo::exists(QLatin1Char(':') + url.path()) : true;
		if (there && !engine_.refused_.contains(said))
		{
			engine_.refused_.push_back(said);
			std::fprintf(stderr, "core window: %s is not one of the core's window files, and was not loaded\n", said.toUtf8().constData());
		}
		/* QML, a script or a module's qmldir: a file of the core's folder that
		 * is not there (sent to a scheme instead, an import from an address
		 * was asked for again and again, for ever). Anything else - a
		 * picture, a font, a link for the browser - to the scheme nothing serves */
		if (type != UrlString) return QUrl::fromLocalFile(folder_ + QStringLiteral(".refused-by-chimera"));
		QUrl to;
		to.setScheme(kRefused);
		to.setPath(QStringLiteral("refused"));
		return to;
	}

private:
	bool allowed(const QUrl &url) const
	{
		if (url.scheme() == kRefused) return true;
		if (url.isLocalFile())
		{
			const QString path = QFileInfo(url.toLocalFile()).absoluteFilePath();
			const QString clean = QDir::cleanPath(path);
			if (sameOrUnder(clean, folder_)) return true;
			/* Qt's own modules, where Qt keeps them */
			const QString real = QFileInfo(clean).canonicalFilePath();
			if (!real.isEmpty())
				for (const QString &root : moduleRoots_)
					if (sameOrUnder(real, root)) return allowedModule(real.mid(root.size()));
			return false;
		}
		/* Qt's and Chimera's modules built into the program */
		if (url.scheme() == QLatin1String("qrc"))
		{
			const QString p = url.path();
			for (const QLatin1String prefix : {QLatin1String("/qt-project.org/imports/"), QLatin1String("/qt/qml/")})
				if (p.startsWith(prefix)) return allowedModule(p.mid(prefix.size()));
		}
		return false;
	}
	/* a path under a folder (ending in /): letters' case as the system has it */
	static bool sameOrUnder(const QString &path, const QString &folder)
	{
#ifdef _WIN32
		return path.startsWith(folder, Qt::CaseInsensitive);
#else
		return path.startsWith(folder);
#endif
	}
	CoreQmlEngine &engine_;
	QString folder_;
	QStringList moduleRoots_;
};

/* Every request the engine would make - an XMLHttpRequest, a picture or a
 * file by address - made to the refused scheme instead: nothing is sent and
 * nothing read (issue 610). This is the one network class in Chimera's
 * window, and it exists to refuse (tools/check-no-network.sh) */
#if QT_CONFIG(qml_network)
class CoreQmlEngine::NoNetwork : public QQmlNetworkAccessManagerFactory
{
	class Refusing : public QNetworkAccessManager
	{
	public:
		using QNetworkAccessManager::QNetworkAccessManager;

	protected:
		QNetworkReply *createRequest(Operation op, const QNetworkRequest &request, QIODevice *data) override
		{
			if (request.url().scheme() != kRefused)
				std::fprintf(stderr, "core window: a request to %s was refused (a core's window reaches nothing)\n",
					request.url().toString().toUtf8().constData());
			QUrl to;
			to.setScheme(kRefused);
			to.setPath(QStringLiteral("refused"));
			return QNetworkAccessManager::createRequest(op, QNetworkRequest(to), data);
		}
	};

public:
	QNetworkAccessManager *create(QObject *parent) override { return new Refusing(parent); }
};
#else
/* nothing to refuse: this Qt's QML has no network */
class CoreQmlEngine::NoNetwork
{
};
#endif

CoreQmlEngine::CoreQmlEngine(const QString &folder, QObject *parent)
	: QQmlEngine(parent)
{
	const QString root = QDir::cleanPath(QFileInfo(folder).absoluteFilePath()) + QLatin1Char('/');
	gate_ = std::make_unique<Gate>(*this, root);
	noNetwork_ = std::make_unique<NoNetwork>();
	addUrlInterceptor(gate_.get());
#if QT_CONFIG(qml_network)
	setNetworkAccessManagerFactory(noNetwork_.get());
#endif
	/* no file of its own anywhere */
	setOfflineStoragePath(QString());
}

CoreQmlEngine::~CoreQmlEngine()
{
	removeUrlInterceptor(gate_.get());
}

void CoreFiles::choose(const QString &purpose, const QString &mode, const QString &title, const QStringList &nameFilters,
	const QString &name)
{
	/* a suggested name is a name, never a place */
	const QString own = QFileInfo(QString(name).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
	const QString as = mode == QLatin1String("write") || mode == QLatin1String("log") ? mode : QStringLiteral("read");
	Q_EMIT wanted(purpose, as, title, nameFilters, own);
}

CoreWindows::CoreWindows(vm::SessionViewModel &session, QObject *parent) : QObject(parent), session_(session)
{
	watches_.push_back(session_.coreWindowsClosed.connect([this] { closeAll(); }));
	/* a link a core's window refused is handed to no program of the system */
	QDesktopServices::setUrlHandler(kRefused, this, "ignoreUrl");
}

CoreWindows::~CoreWindows()
{
	QDesktopServices::unsetUrlHandler(kRefused);
	closeAll();
}

void CoreWindows::closeAll()
{
	/* taken out first: a window going says it is no longer shown */
	const QHash<QString, Open> all = std::exchange(open_, {});
	for (const Open &o : all)
	{
		if (o.window)
		{
			o.window->disconnect(this);
			delete o.window.data();
		}
		delete o.context;
		delete o.qml;
		delete o.files;
	}
}

void CoreWindows::fileChosen(const QString &id, const QString &purpose, const QString &mode, const QString &path)
{
	const auto it = open_.find(id);
	if (it == open_.end() || !it->vm) return;
	if (mode == QLatin1String("log")) it->vm->keepFile(purpose.toStdString(), path.toStdString());
	else
		it->vm->giveFile(purpose.toStdString(), mode == QLatin1String("write") ? vm::CoreWindowViewModel::Write : vm::CoreWindowViewModel::Read,
			path.toStdString());
	if (it->files) Q_EMIT it->files->chosen(purpose, QFileInfo(path).fileName());
}

void CoreWindows::fileCancelled(const QString &id, const QString &purpose)
{
	const auto it = open_.find(id);
	if (it != open_.end() && it->files) Q_EMIT it->files->cancelled(purpose);
}

QQuickWindow *CoreWindows::window(const QString &id) const
{
	const auto it = open_.constFind(id);
	return it != open_.constEnd() ? it->window.data() : nullptr;
}

bool unpackWindowFiles(ce_package *pk, const QString &folder, QString &why)
{
	QDir(folder).removeRecursively();
	const QString prefix = QStringLiteral("windows/");
	const QString names = QString::fromUtf8(ce_package_entry_names(pk, prefix.toUtf8().constData()));
	for (const QString &name : names.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
	{
		/* the folder is the package's "windows/" folder: a window's declared
		 * file "windows/kit/SaveData.qml" is <folder>/kit/SaveData.qml (the
		 * window is opened from there). Written only inside the folder: a name
		 * climbing out is not a window file */
		const QString to = QDir::cleanPath(QDir(folder).filePath(name.mid(prefix.size())));
		if (!to.startsWith(QDir::cleanPath(folder) + QLatin1Char('/'))) continue;
		uint64_t len = 0;
		const uint8_t *d = ce_package_entry(pk, name.toUtf8().constData(), &len);
		if (d == nullptr) continue;
		QDir().mkpath(QFileInfo(to).absolutePath());
		QFile f(to);
		if (!f.open(QIODevice::WriteOnly) || f.write(reinterpret_cast<const char *>(d), static_cast<qint64>(len)) != static_cast<qint64>(len))
		{
			why = f.errorString();
			return false;
		}
	}
	return true;
}

QString CoreWindows::unpack(QString &why)
{
	const std::string package = session_.packagePath();
	const char *perr = nullptr;
	ce_package *pk = ce_package_open(package.c_str(), &perr);
	if (pk == nullptr)
	{
		why = QString::fromUtf8(perr != nullptr ? perr : "");
		return {};
	}
	const char *sha = ce_package_sha1(pk);
	const QString base = QString::fromStdString(session_.cacheFolder.get());
	const QString folder = QDir(base.isEmpty() ? QDir::tempPath() : base)
		.filePath(QStringLiteral("core-windows/") + QString::fromUtf8(sha != nullptr ? sha : "folder"));
	if (!unpackWindowFiles(pk, folder, why))
	{
		ce_package_free(pk);
		return {};
	}
	/* the package's texts in the language in use, over its English, for the window's labels */
	if (texts_ == nullptr) texts_ = new QQmlPropertyMap(this);
	for (const QString &key : texts_->keys()) texts_->clear(key);
	for (const char *code : {"en", ce_language_in_use()})
	{
		if (code == nullptr) continue;
		uint64_t len = 0;
		const uint8_t *d = ce_package_entry(pk, (std::string("languages/") + code + ".json").c_str(), &len);
		if (d == nullptr) continue;
		const QJsonObject table = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char *>(d), static_cast<qsizetype>(len)))
									  .object().value(QStringLiteral("texts")).toObject();
		for (auto t = table.begin(); t != table.end(); ++t)
			texts_->insert(t.key(), t.value().isObject() ? t.value().toObject().value(QStringLiteral("other")) : t.value());
	}
	ce_package_free(pk);
	return folder;
}

QString CoreWindows::open(const QString &id)
{
	if (auto it = open_.find(id); it != open_.end() && it->window)
	{
		it->window->show();
		it->window->raise();
		it->window->requestActivate();
		return {};
	}
	/* what the package declares for it: its item's words, its QML */
	QString menu = id, qmlFile;
	{
		vm::json::Json list;
		std::string err;
		if (vm::json::parse(session_.coreWindows.get(), list, err))
			for (const vm::json::Json &w : list.items)
				if (const vm::json::Json *i = w.find("id"); i != nullptr && QString::fromStdString(i->text) == id)
				{
					if (const vm::json::Json *m = w.find("menu")) menu = QString::fromStdString(m->text);
					if (const vm::json::Json *q = w.find("qml")) qmlFile = QString::fromStdString(q->text);
				}
	}
	const QString title = QString::fromStdString(session_.coreName.get()) + QStringLiteral(": ") + menu;
	auto refused = [&](const QString &why) {
		return QString::fromStdString(vm::tr("core_window.cannot_open", {title.toStdString(), why.toStdString()}));
	};
	if (!qmlFile.startsWith(QStringLiteral("windows/")))
		return QString::fromStdString(vm::tr("core_window.not_declared", {id.toStdString()}));
	std::string error;
	auto vm = session_.openCoreWindow(id.toStdString(), error);
	if (vm == nullptr) return refused(QString::fromStdString(error));
	/* the engine of this package's windows, made when its first window opens */
	const QString package = QString::fromStdString(session_.packagePath());
	if (!engine_ || enginePackage_ != package)
	{
		closeAll();
		QString why;
		const QString folder = unpack(why);
		if (folder.isEmpty()) return refused(why);
		engine_ = std::make_unique<CoreQmlEngine>(folder);
		enginePackage_ = package;
		engineFolder_ = folder;
	}
	const QString folder = engineFolder_;
	Open o;
	o.vm = vm;
	o.qml = new QmlViewModel(*vm, this);
	o.files = new CoreFiles(id, this);
	connect(o.files, &CoreFiles::wanted, this, [this, id](const QString &purpose, const QString &mode, const QString &title,
		const QStringList &nameFilters, const QString &name) { Q_EMIT fileWanted(id, purpose, mode, title, nameFilters, name); });
	connect(o.files, &CoreFiles::released, this, [this, id](const QString &purpose) {
		if (const auto it = open_.find(id); it != open_.end() && it->vm) it->vm->releaseFile(purpose.toStdString());
	});
	o.context = new QQmlContext(engine_->rootContext(), this);
	o.context->setContextProperty(QStringLiteral("core"), o.qml);
	o.context->setContextProperty(QStringLiteral("texts"), texts_);
	o.context->setContextProperty(QStringLiteral("files"), o.files);
	QQmlComponent component(engine_.get(), QUrl::fromLocalFile(QDir(folder).filePath(qmlFile.mid(QStringLiteral("windows/").size()))));
	QObject *made = component.create(o.context);
	auto *item = qobject_cast<QQuickItem *>(made);
	if (item == nullptr)
	{
		delete made;
		delete o.context;
		delete o.qml;
		delete o.files;
		session_.closeCoreWindow(id.toStdString());
		return refused(component.isError() ? component.errorString().trimmed() : qmlFile);
	}
	auto *window = new QQuickWindow;
	window->setTitle(title);
	item->setParent(window);
	item->setParentItem(window->contentItem());
	window->resize(std::max<int>(200, static_cast<int>(item->implicitWidth())), std::max<int>(120, static_cast<int>(item->implicitHeight())));
	item->setSize(window->size());
	connect(window, &QQuickWindow::widthChanged, item, [item, window] { item->setWidth(window->width()); });
	connect(window, &QQuickWindow::heightChanged, item, [item, window] { item->setHeight(window->height()); });
	/* the window's colours, Chimera's */
	window->setColor(QGuiApplication::palette().color(QPalette::Window));
	/* closed by the person: the core is told, and the view model goes */
	connect(window, &QQuickWindow::visibleChanged, this, [this, id, window] {
		if (window->isVisible()) return;
		auto it = open_.find(id);
		if (it == open_.end() || it->window != window) return;
		session_.closeCoreWindow(id.toStdString());
		QQmlContext *context = it->context;
		QmlViewModel *qml = it->qml;
		CoreFiles *files = it->files;
		open_.erase(it);
		window->deleteLater();
		context->deleteLater();
		qml->deleteLater();
		files->deleteLater();
	});
	o.window = window;
	open_.insert(id, o);
	window->show();
	return {};
}

} // namespace chimera::qt
