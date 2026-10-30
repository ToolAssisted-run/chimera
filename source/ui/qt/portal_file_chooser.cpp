#include "portal_file_chooser.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QWindow>

#include <cstdint>
#include <type_traits>
#include <vector>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <dlfcn.h>
#define CHIMERA_PORTAL 1
#endif

namespace chimera::qt {

namespace portal {

QList<Filter> filtersFrom(const QStringList &nameFilters)
{
	static const QRegularExpression bracketed(QStringLiteral("\\(([^()]*)\\)\\s*$"));
	static const QRegularExpression between(QStringLiteral("[\\s;]+"));
	QList<Filter> filters;
	for (const QString &nameFilter : nameFilters)
	{
		const QString label = nameFilter.trimmed();
		const QRegularExpressionMatch match = bracketed.match(label);
		const QString patterns = match.hasMatch() ? match.captured(1) : label;
		Filter filter{label, {}};
		for (const QString &glob : patterns.split(between, Qt::SkipEmptyParts))
			/* "*.*" means every file; as a glob it would miss a name with no dot */
			filter.globs << (glob == QStringLiteral("*.*") ? QStringLiteral("*") : glob);
		if (!filter.label.isEmpty() && !filter.globs.isEmpty()) filters << filter;
	}
	return filters;
}

QString pathFromUri(const QString &uri)
{
	const QUrl url(uri);
	return url.isLocalFile() ? url.toLocalFile() : QString();
}

QString requestPathPart(const QString &uniqueName)
{
	QString part = uniqueName;
	if (part.startsWith(QLatin1Char(':'))) part.remove(0, 1);
	return part.replace(QLatin1Char('.'), QLatin1Char('_'));
}

QString withDefaultSuffix(const QString &path, const QString &suffix)
{
	QString wanted = suffix;
	while (wanted.startsWith(QLatin1Char('.'))) wanted.remove(0, 1);
	if (wanted.isEmpty() || path.isEmpty() || !QFileInfo(path).suffix().isEmpty()) return path;
	return path + QLatin1Char('.') + wanted;
}

} // namespace portal

#ifdef CHIMERA_PORTAL
namespace {

constexpr const char *kService = "org.freedesktop.portal.Desktop";
constexpr const char *kObjectPath = "/org/freedesktop/portal/desktop";
constexpr const char *kInterface = "org.freedesktop.portal.FileChooser";
constexpr const char *kRequest = "org.freedesktop.portal.Request";
constexpr const char *kBus = "org.freedesktop.DBus";
/* a "the chooser could not be shown" answer this soon was never a person's
 * Cancel: the window's own chooser is shown instead (shana answers every
 * Cancel with the same code, so a later one is a Cancel) */
constexpr qint64 kNeverShownMs = 300;
/* the id the one tick box is sent and read back under */
constexpr const char *kCheckBoxId = "checkbox";

constexpr int kTypeByte = 'y', kTypeBoolean = 'b', kTypeUint32 = 'u', kTypeString = 's', kTypeObjectPath = 'o';
constexpr int kTypeArray = 'a', kTypeVariant = 'v', kTypeStruct = 'r', kTypeDictEntry = 'e';

struct DBusConnection;
struct DBusMessage;
/* libdbus' own structs, which the caller allocates: DBusError as libdbus
 * lays it out, and room to spare for a DBusMessageIter (72 bytes on x64) */
struct DBusError
{
	const char *name;
	const char *message;
	unsigned int bits;
	void *padding;
};
struct Iter
{
	alignas(void *) unsigned char bytes[128];
};

/* the parts of libdbus (the reference D-Bus library, on every Linux
 * desktop) the chooser needs. D-Bus is the desktop's local message bus: a
 * socket on this machine, never the network. */
struct Lib
{
	void (*error_init)(DBusError *);
	void (*error_free)(DBusError *);
	unsigned (*error_is_set)(const DBusError *);
	DBusConnection *(*bus_get_private)(int, DBusError *);
	const char *(*bus_get_unique_name)(DBusConnection *);
	void (*bus_add_match)(DBusConnection *, const char *, DBusError *);
	void (*connection_set_exit_on_disconnect)(DBusConnection *, unsigned);
	void (*connection_close)(DBusConnection *);
	void (*connection_unref)(DBusConnection *);
	DBusMessage *(*connection_send_with_reply_and_block)(DBusConnection *, DBusMessage *, int, DBusError *);
	unsigned (*connection_read_write)(DBusConnection *, int);
	DBusMessage *(*connection_pop_message)(DBusConnection *);
	DBusMessage *(*message_new_method_call)(const char *, const char *, const char *, const char *);
	void (*message_unref)(DBusMessage *);
	unsigned (*message_is_signal)(DBusMessage *, const char *, const char *);
	const char *(*message_get_path)(DBusMessage *);
	void (*message_iter_init_append)(DBusMessage *, Iter *);
	unsigned (*message_iter_append_basic)(Iter *, int, const void *);
	unsigned (*message_iter_append_fixed_array)(Iter *, int, const void *, int);
	unsigned (*message_iter_open_container)(Iter *, int, const char *, Iter *);
	unsigned (*message_iter_close_container)(Iter *, Iter *);
	unsigned (*message_iter_init)(DBusMessage *, Iter *);
	int (*message_iter_get_arg_type)(Iter *);
	void (*message_iter_get_basic)(Iter *, void *);
	unsigned (*message_iter_next)(Iter *);
	void (*message_iter_recurse)(Iter *, Iter *);
};

/* libdbus, loaded the first time; null where there is none */
const Lib *lib()
{
	static const Lib *loaded = [] () -> const Lib * {
		void *so = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
		if (so == nullptr) return nullptr;
		static Lib l;
		bool all = true;
		auto take = [&](auto &fn, const char *name) {
			fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(so, name));
			all = all && fn != nullptr;
		};
		take(l.error_init, "dbus_error_init");
		take(l.error_free, "dbus_error_free");
		take(l.error_is_set, "dbus_error_is_set");
		take(l.bus_get_private, "dbus_bus_get_private");
		take(l.bus_get_unique_name, "dbus_bus_get_unique_name");
		take(l.bus_add_match, "dbus_bus_add_match");
		take(l.connection_set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
		take(l.connection_close, "dbus_connection_close");
		take(l.connection_unref, "dbus_connection_unref");
		take(l.connection_send_with_reply_and_block, "dbus_connection_send_with_reply_and_block");
		take(l.connection_read_write, "dbus_connection_read_write");
		take(l.connection_pop_message, "dbus_connection_pop_message");
		take(l.message_new_method_call, "dbus_message_new_method_call");
		take(l.message_unref, "dbus_message_unref");
		take(l.message_is_signal, "dbus_message_is_signal");
		take(l.message_get_path, "dbus_message_get_path");
		take(l.message_iter_init_append, "dbus_message_iter_init_append");
		take(l.message_iter_append_basic, "dbus_message_iter_append_basic");
		take(l.message_iter_append_fixed_array, "dbus_message_iter_append_fixed_array");
		take(l.message_iter_open_container, "dbus_message_iter_open_container");
		take(l.message_iter_close_container, "dbus_message_iter_close_container");
		take(l.message_iter_init, "dbus_message_iter_init");
		take(l.message_iter_get_arg_type, "dbus_message_iter_get_arg_type");
		take(l.message_iter_get_basic, "dbus_message_iter_get_basic");
		take(l.message_iter_next, "dbus_message_iter_next");
		take(l.message_iter_recurse, "dbus_message_iter_recurse");
		return all ? &l : nullptr;
	}();
	return loaded;
}

/* a libdbus error, freed when it goes */
struct Error
{
	DBusError e{};
	Error() { lib()->error_init(&e); }
	~Error() { lib()->error_free(&e); }
	bool set() const { return lib()->error_is_set(&e) != 0; }
};

/* A connection of the chooser's own to the session bus, or null. Private,
 * so closing it touches nothing else, and told not to end the program if
 * the bus goes away, which is libdbus' default. */
DBusConnection *openBus()
{
	if (lib() == nullptr) return nullptr;
	/* with no address libdbus would start a bus of its own; a desktop with
	 * a portal has one, at the address or at the runtime directory's bus */
	const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
	if (qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS") && (runtime.isEmpty() || !QFileInfo::exists(runtime + QStringLiteral("/bus"))))
		return nullptr;
	Error error;
	DBusConnection *connection = lib()->bus_get_private(0 /* the session bus */, &error.e);
	if (error.set() || connection == nullptr) return nullptr;
	lib()->connection_set_exit_on_disconnect(connection, 0);
	return connection;
}

void closeBus(DBusConnection *connection)
{
	if (connection == nullptr) return;
	lib()->connection_close(connection);
	lib()->connection_unref(connection);
}

bool addMatch(DBusConnection *connection, const QString &rule)
{
	Error error;
	lib()->bus_add_match(connection, rule.toUtf8().constData(), &error.e);
	return !error.set();
}

QString text(const char *p)
{
	return p == nullptr ? QString() : QString::fromUtf8(p);
}

/* a string (or an object path) the iterator is at; empty for anything else */
QString stringAt(Iter *iter)
{
	const int type = lib()->message_iter_get_arg_type(iter);
	if (type != kTypeString && type != kTypeObjectPath) return QString();
	const char *value = nullptr;
	lib()->message_iter_get_basic(iter, &value);
	return text(value);
}

/* One method call to the portal. Containers are opened and closed in pairs,
 * as libdbus requires; the strings live as long as the writer. */
class Writer
{
public:
	Writer(const char *iface, const char *method) : message_(lib()->message_new_method_call(kService, kObjectPath, iface, method))
	{
		iters_.push_back(std::make_unique<Iter>());
		lib()->message_iter_init_append(message_, iters_.back().get());
		open_.push_back(iters_.back().get());
	}
	~Writer()
	{
		if (message_ != nullptr) lib()->message_unref(message_);
	}
	Writer(const Writer &) = delete;
	Writer &operator=(const Writer &) = delete;

	void string(const QString &s)
	{
		strings_.push_back(s.toUtf8());
		const char *p = strings_.back().constData();
		lib()->message_iter_append_basic(top(), kTypeString, &p);
	}
	void boolean(bool b)
	{
		const uint32_t value = b ? 1 : 0;
		lib()->message_iter_append_basic(top(), kTypeBoolean, &value);
	}
	void uint32(uint32_t u) { lib()->message_iter_append_basic(top(), kTypeUint32, &u); }
	/* a path as the portal takes a folder: its bytes and a NUL */
	void bytes(const QString &s)
	{
		strings_.push_back(s.toUtf8());
		const char *p = strings_.back().constData();
		open(kTypeArray, "y");
		lib()->message_iter_append_fixed_array(top(), kTypeByte, &p, int(strings_.back().size()) + 1);
		close();
	}
	void open(int type, const char *signature)
	{
		iters_.push_back(std::make_unique<Iter>());
		lib()->message_iter_open_container(top(), type, signature, iters_.back().get());
		open_.push_back(iters_.back().get());
	}
	void close()
	{
		Iter *sub = open_.back();
		open_.pop_back();
		lib()->message_iter_close_container(top(), sub);
	}
	/* one {sv} of the options: a key, and a value of the signature given */
	template <typename Value>
	void entry(const char *key, const char *signature, Value value)
	{
		open(kTypeDictEntry, nullptr);
		string(QString::fromLatin1(key));
		open(kTypeVariant, signature);
		value();
		close();
		close();
	}
	/* a filter, (sa(us)): its label and its globs (pattern kind 0) */
	void filter(const portal::Filter &f)
	{
		open(kTypeStruct, nullptr);
		string(f.label);
		open(kTypeArray, "(us)");
		for (const QString &glob : f.globs)
		{
			open(kTypeStruct, nullptr);
			uint32(0);
			string(glob);
			close();
		}
		close();
		close();
	}
	/* sends the call and waits for its reply; null if it failed */
	DBusMessage *send(DBusConnection *connection, int timeoutMs)
	{
		if (message_ == nullptr) return nullptr;
		Error error;
		DBusMessage *reply = lib()->connection_send_with_reply_and_block(connection, message_, timeoutMs, &error.e);
		if (!error.set()) return reply;
		if (reply != nullptr) lib()->message_unref(reply);
		return nullptr;
	}

private:
	Iter *top() { return open_.back(); }
	DBusMessage *message_;
	std::vector<std::unique_ptr<Iter>> iters_;
	std::vector<Iter *> open_;
	std::vector<QByteArray> strings_;
};

/* the FileChooser interface's version: 0 for none, 3 and later picks folders */
uint32_t portalVersion()
{
	static const uint32_t version = [] () -> uint32_t {
		DBusConnection *connection = openBus();
		if (connection == nullptr) return 0;
		uint32_t found = 0;
		Writer writer("org.freedesktop.DBus.Properties", "Get");
		writer.string(QString::fromLatin1(kInterface));
		writer.string(QStringLiteral("version"));
		if (DBusMessage *reply = writer.send(connection, 5000))
		{
			Iter iter, inner;
			if (lib()->message_iter_init(reply, &iter) && lib()->message_iter_get_arg_type(&iter) == kTypeVariant)
			{
				lib()->message_iter_recurse(&iter, &inner);
				if (lib()->message_iter_get_arg_type(&inner) == kTypeUint32) lib()->message_iter_get_basic(&inner, &found);
			}
			lib()->message_unref(reply);
		}
		closeBus(connection);
		return found;
	}();
	return version;
}

/* The window the chooser belongs to, as the portal names one. Under X11 it
 * is "x11:" and the window in hex. Under Wayland a parent is a handle the
 * compositor gives out (xdg-foreign), which Qt does not ask for; the chooser
 * is given none there, as GNOME's crashed on an X11 parent that was not its
 * own (PR 275), and is modal regardless, since Chimera's windows take no
 * input while it is open. */
QString parentOf(QWindow *window)
{
	if (window == nullptr || QGuiApplication::platformName() != QStringLiteral("xcb")) return QString();
	return QStringLiteral("x11:") + QString::number(quint64(window->winId()), 16);
}

QString responseRule(const QString &path)
{
	return QStringLiteral("type='signal',interface='%1',member='Response',path='%2'").arg(QString::fromLatin1(kRequest), path);
}

/* Throws away what a person does to Chimera's windows while the chooser is
 * open: mouse buttons and wheel, keys, touches, and closing. Movement and
 * drawing still go through. Windows are not disabled: a disabled window
 * that is modal itself kept the focus, and the chooser opened behind it
 * (Plasma and GNOME on Wayland, PR 275). */
class InputBlocker : public QObject
{
public:
	bool eventFilter(QObject *, QEvent *event) override
	{
		switch (event->type())
		{
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		case QEvent::MouseButtonDblClick:
		case QEvent::Wheel:
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		case QEvent::ShortcutOverride:
		case QEvent::Shortcut:
		case QEvent::TouchBegin:
		case QEvent::TouchUpdate:
		case QEvent::TouchEnd:
		case QEvent::TabletPress:
		case QEvent::TabletRelease:
			return true;
		case QEvent::Close:
			event->ignore();
			return true;
		default:
			return false;
		}
	}
};

} // namespace
#endif

struct PortalFileChooser::Pending
{
#ifdef CHIMERA_PORTAL
	DBusConnection *connection = nullptr;
	InputBlocker blocker;
#endif
	QString path;
	QString defaultSuffix;
	bool save = false;
	QPointer<QWindow> window;
	QElapsedTimer asked;
};

PortalFileChooser::PortalFileChooser(QObject *parent) : QObject(parent)
{
	timer_.setInterval(20);
	connect(&timer_, &QTimer::timeout, this, &PortalFileChooser::poll);
}

PortalFileChooser::~PortalFileChooser()
{
#ifdef CHIMERA_PORTAL
	if (pending_)
	{
		qApp->removeEventFilter(&pending_->blocker);
		closeBus(pending_->connection);
	}
#endif
}

QWindow *PortalFileChooser::windowOf(QObject *object) const
{
	for (QObject *o = object; o != nullptr; o = o->parent())
	{
		if (auto *window = qobject_cast<QWindow *>(o)) return window;
		if (auto *item = qobject_cast<QQuickItem *>(o)) return item->window();
	}
	return nullptr;
}

bool PortalFileChooser::ask(const QVariantMap &request)
{
#ifdef CHIMERA_PORTAL
	/* the window's own chooser, asked for by name: for a desktop whose
	 * portal misbehaves, and for the tests */
	if (qEnvironmentVariable("CHIMERA_FILE_CHOOSER") == QStringLiteral("builtin")) return false;
	if (pending_) return false;
	const QString mode = request.value(QStringLiteral("mode")).toString();
	const bool folder = mode == QStringLiteral("folder"), save = mode == QStringLiteral("save");
	/* a folder is picked from version 3 of the interface on */
	if (portalVersion() < (folder ? 3u : 1u)) return false;
	DBusConnection *connection = openBus();
	if (connection == nullptr) return false;
	auto fail = [connection] { closeBus(connection); return false; };

	static int tokens = 0;
	const QString token = QStringLiteral("chimera%1x%2").arg(QCoreApplication::applicationPid()).arg(++tokens);
	/* The answer comes as a signal on a request object whose path is known
	 * from the connection's name and the token, so the match goes in before
	 * the call: a portal that answers at once does not answer to nobody. */
	const QString expected = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
		.arg(portal::requestPathPart(text(lib()->bus_get_unique_name(connection))), token);
	if (!addMatch(connection, responseRule(expected))) return fail();
	/* and if the portal leaves the bus (it crashed, or was stopped), no
	 * answer will come: the window's own chooser is shown instead */
	if (!addMatch(connection, QStringLiteral("type='signal',sender='%1',interface='%1',member='NameOwnerChanged',arg0='%2'").arg(QString::fromLatin1(kBus), QString::fromLatin1(kService))))
		return fail();

	QWindow *window = qobject_cast<QWindow *>(request.value(QStringLiteral("window")).value<QObject *>());
	Writer writer(kInterface, save ? "SaveFile" : "OpenFile");
	writer.string(parentOf(window));
	writer.string(request.value(QStringLiteral("title")).toString());
	writer.open(kTypeArray, "{sv}");
	writer.entry("handle_token", "s", [&] { writer.string(token); });
	writer.entry("modal", "b", [&] { writer.boolean(true); });
	if (!save)
	{
		writer.entry("multiple", "b", [&] { writer.boolean(mode == QStringLiteral("openMany")); });
		if (folder) writer.entry("directory", "b", [&] { writer.boolean(true); });
	}
	const QList<portal::Filter> filters = folder ? QList<portal::Filter>() : portal::filtersFrom(request.value(QStringLiteral("nameFilters")).toStringList());
	if (!filters.isEmpty())
	{
		writer.entry("filters", "a(sa(us))", [&] {
			writer.open(kTypeArray, "(sa(us))");
			for (const portal::Filter &f : filters) writer.filter(f);
			writer.close();
		});
		writer.entry("current_filter", "(sa(us))", [&] { writer.filter(filters.first()); });
	}
	const QString name = request.value(QStringLiteral("name")).toString();
	if (save && !name.isEmpty()) writer.entry("current_name", "s", [&] { writer.string(QFileInfo(name).fileName()); });
	const QString start = request.value(QStringLiteral("folder")).toUrl().toLocalFile();
	if (!start.isEmpty() && QFileInfo(start).isDir()) writer.entry("current_folder", "ay", [&] { writer.bytes(QDir::cleanPath(start)); });
	/* a choice with no options is a tick box; its value is "true" or "false" */
	const QString checkBox = request.value(QStringLiteral("checkBox")).toString();
	if (!checkBox.isEmpty())
		writer.entry("choices", "a(ssa(ss)s)", [&] {
			writer.open(kTypeArray, "(ssa(ss)s)");
			writer.open(kTypeStruct, nullptr);
			writer.string(QString::fromLatin1(kCheckBoxId));
			writer.string(checkBox);
			writer.open(kTypeArray, "(ss)");
			writer.close();
			writer.string(request.value(QStringLiteral("checkBoxChecked")).toBool() ? QStringLiteral("true") : QStringLiteral("false"));
			writer.close();
			writer.close();
		});
	writer.close();

	DBusMessage *reply = writer.send(connection, 5000);
	if (reply == nullptr) return fail();
	QString path;
	Iter iter;
	if (lib()->message_iter_init(reply, &iter) && lib()->message_iter_get_arg_type(&iter) == kTypeObjectPath) path = stringAt(&iter);
	lib()->message_unref(reply);
	if (path.isEmpty()) return fail();
	/* an older portal names the request object itself */
	if (path != expected && !addMatch(connection, responseRule(path))) return fail();

	pending_ = std::make_unique<Pending>();
	pending_->connection = connection;
	pending_->path = path;
	pending_->save = save;
	pending_->defaultSuffix = request.value(QStringLiteral("defaultSuffix")).toString();
	pending_->window = window;
	pending_->asked.start();
	qApp->installEventFilter(&pending_->blocker);
	timer_.start();
	return true;
#else
	Q_UNUSED(request);
	return false;
#endif
}

void PortalFileChooser::poll()
{
#ifdef CHIMERA_PORTAL
	if (!pending_) return;
	DBusConnection *connection = pending_->connection;
	/* the bus went away: no answer will come */
	if (!lib()->connection_read_write(connection, 0)) { finish(Unavailable); return; }
	while (DBusMessage *message = lib()->connection_pop_message(connection))
	{
		struct Unref { DBusMessage *m; ~Unref() { lib()->message_unref(m); } } unref{message};
		if (lib()->message_is_signal(message, kBus, "NameOwnerChanged"))
		{
			/* (s name, s old_owner, s new_owner): the portal's name lost its
			 * owner; one the call started gains one, which is not that */
			Iter iter;
			if (!lib()->message_iter_init(message, &iter) || stringAt(&iter) != QString::fromLatin1(kService)) continue;
			if (!lib()->message_iter_next(&iter) || !lib()->message_iter_next(&iter)) continue;
			if (stringAt(&iter).isEmpty()) { finish(Unavailable); return; }
			continue;
		}
		if (!lib()->message_is_signal(message, kRequest, "Response") || text(lib()->message_get_path(message)) != pending_->path) continue;
		/* (u response, a{sv} results): 0 chosen, 1 Cancel, 2 ended some other way */
		Iter iter;
		uint32_t code = 2;
		if (lib()->message_iter_init(message, &iter) && lib()->message_iter_get_arg_type(&iter) == kTypeUint32) lib()->message_iter_get_basic(&iter, &code);
		if (code == 1) { finish(Cancelled); return; }
		if (code != 0)
		{
			finish(pending_->asked.elapsed() < kNeverShownMs ? Unavailable : Cancelled);
			return;
		}
		QList<QUrl> files;
		int checked = -1;
		if (lib()->message_iter_next(&iter) && lib()->message_iter_get_arg_type(&iter) == kTypeArray)
		{
			Iter entries, entry, value, item, pair;
			lib()->message_iter_recurse(&iter, &entries);
			while (lib()->message_iter_get_arg_type(&entries) == kTypeDictEntry)
			{
				lib()->message_iter_recurse(&entries, &entry);
				const QString key = stringAt(&entry);
				lib()->message_iter_next(&entry);
				lib()->message_iter_recurse(&entry, &value); /* into the variant */
				if (key == QStringLiteral("uris") && lib()->message_iter_get_arg_type(&value) == kTypeArray)
				{
					lib()->message_iter_recurse(&value, &item);
					while (lib()->message_iter_get_arg_type(&item) == kTypeString)
					{
						QString local = portal::pathFromUri(stringAt(&item));
						if (pending_->save) local = portal::withDefaultSuffix(local, pending_->defaultSuffix);
						if (!local.isEmpty()) files << QUrl::fromLocalFile(local);
						lib()->message_iter_next(&item);
					}
				}
				/* the choices as confirmed, a(ss): an id and its value */
				else if (key == QStringLiteral("choices") && lib()->message_iter_get_arg_type(&value) == kTypeArray)
				{
					lib()->message_iter_recurse(&value, &item);
					while (lib()->message_iter_get_arg_type(&item) == kTypeStruct)
					{
						lib()->message_iter_recurse(&item, &pair);
						const QString id = stringAt(&pair);
						lib()->message_iter_next(&pair);
						if (id == QString::fromLatin1(kCheckBoxId)) checked = stringAt(&pair) == QStringLiteral("true") ? 1 : 0;
						lib()->message_iter_next(&item);
					}
				}
				lib()->message_iter_next(&entries);
			}
		}
		finish(files.isEmpty() ? Cancelled : Chosen, files, checked);
		return;
	}
#endif
}

void PortalFileChooser::finish(int outcome, const QList<QUrl> &files, int checkBox)
{
	timer_.stop();
	if (!pending_) return;
	const std::unique_ptr<Pending> done = std::move(pending_);
#ifdef CHIMERA_PORTAL
	qApp->removeEventFilter(&done->blocker);
	closeBus(done->connection);
#endif
	if (done->window) done->window->requestActivate();
	Q_EMIT answered(outcome, files, checkBox);
}

} // namespace chimera::qt
