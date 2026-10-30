#include "file_places.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>

namespace chimera::qt {

namespace places {

namespace {

QVariantMap place(const QString &name, const QString &path, const char *kind)
{
	return {{QStringLiteral("name"), name}, {QStringLiteral("url"), QUrl::fromLocalFile(path)}, {QStringLiteral("kind"), QString::fromLatin1(kind)}};
}

/* the mount table writes a space, tab, newline and backslash as octal */
QString unescapeMount(const QString &field)
{
	QString out;
	for (int i = 0; i < field.size(); i++)
	{
		if (field[i] == QLatin1Char('\\') && i + 3 < field.size())
		{
			bool ok = false;
			const int code = field.mid(i + 1, 3).toInt(&ok, 8);
			if (ok)
			{
				out += QChar(code);
				i += 3;
				continue;
			}
		}
		out += field[i];
	}
	return out;
}

} // namespace

QVariantList fromGtkBookmarks(const QString &text)
{
	QVariantList list;
	for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
	{
		const QString trimmed = line.trimmed();
		const int space = trimmed.indexOf(QLatin1Char(' '));
		const QUrl url(space < 0 ? trimmed : trimmed.left(space));
		if (!url.isLocalFile()) continue;
		const QString path = QDir::cleanPath(url.toLocalFile());
		const QString label = space < 0 ? QString() : trimmed.mid(space + 1).trimmed();
		list << place(label.isEmpty() ? QFileInfo(path).fileName() : label, path, "bookmark");
	}
	return list;
}

QVariantList fromKdeBookmarks(const QString &xml)
{
	QVariantList list;
	QXmlStreamReader reader(xml);
	QString href, title;
	bool hidden = false, system = false, inBookmark = false;
	while (!reader.atEnd())
	{
		reader.readNext();
		const QStringView name = reader.name();
		if (reader.isStartElement())
		{
			if (name == QStringLiteral("bookmark"))
			{
				inBookmark = true;
				href = reader.attributes().value(QStringLiteral("href")).toString();
				title.clear();
				hidden = system = false;
			}
			else if (inBookmark && name == QStringLiteral("title")) title = reader.readElementText();
			else if (inBookmark && name == QStringLiteral("IsHidden")) hidden = reader.readElementText() == QStringLiteral("true");
			else if (inBookmark && name == QStringLiteral("isSystemItem")) system = reader.readElementText() == QStringLiteral("true");
		}
		else if (reader.isEndElement() && name == QStringLiteral("bookmark"))
		{
			inBookmark = false;
			const QUrl url(href);
			if (hidden || system || !url.isLocalFile()) continue;
			const QString path = QDir::cleanPath(url.toLocalFile());
			list << place(title.isEmpty() ? QFileInfo(path).fileName() : title, path, "bookmark");
		}
	}
	return list;
}

QVariantList fromMounts(const QString &table)
{
	QVariantList list;
	for (const QString &line : table.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
	{
		const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
		if (fields.size() < 2) continue;
		const QString at = unescapeMount(fields[1]);
		if (!at.startsWith(QStringLiteral("/media/")) && !at.startsWith(QStringLiteral("/run/media/")) && !at.startsWith(QStringLiteral("/mnt/")))
			continue;
		list << place(QFileInfo(at).fileName(), at, "drive");
	}
	return list;
}

} // namespace places

FilePlaces::FilePlaces(QObject *parent) : QObject(parent) {}

QVariantList FilePlaces::all() const
{
	QVariantList list;
	QSet<QString> seen;
	auto add = [&](const QVariant &v) {
		const QVariantMap p = v.toMap();
		const QString path = p.value(QStringLiteral("url")).toUrl().toLocalFile();
		if (path.isEmpty() || seen.contains(path) || !QFileInfo(path).isDir()) return;
		seen.insert(path);
		list << p;
	};
	const QString home = QDir::homePath();
#ifdef _WIN32
	for (const QFileInfo &drive : QDir::drives())
		add(QVariantMap{{QStringLiteral("name"), QDir::toNativeSeparators(drive.absolutePath())}, {QStringLiteral("url"), QUrl::fromLocalFile(drive.absolutePath())}, {QStringLiteral("kind"), QStringLiteral("drive")}});
#else
	add(QVariantMap{{QStringLiteral("name"), QStringLiteral("/")}, {QStringLiteral("url"), QUrl::fromLocalFile(QStringLiteral("/"))}, {QStringLiteral("kind"), QStringLiteral("root")}});
#endif
	add(QVariantMap{{QStringLiteral("name"), QFileInfo(home).fileName()}, {QStringLiteral("url"), QUrl::fromLocalFile(home)}, {QStringLiteral("kind"), QStringLiteral("home")}});
	for (const auto location : {QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation,
		QStandardPaths::MusicLocation, QStandardPaths::PicturesLocation, QStandardPaths::MoviesLocation})
	{
		const QString path = QStandardPaths::writableLocation(location);
		if (path.isEmpty() || QDir::cleanPath(path) == QDir::cleanPath(home)) continue;
		add(QVariantMap{{QStringLiteral("name"), QFileInfo(path).fileName()}, {QStringLiteral("url"), QUrl::fromLocalFile(path)}, {QStringLiteral("kind"), QStringLiteral("folder")}});
	}
	auto read = [](const QString &path) {
		QFile f(path);
		return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
	};
	const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
	for (const QVariant &v : places::fromGtkBookmarks(read(config + QStringLiteral("/gtk-3.0/bookmarks")))) add(v);
	for (const QVariant &v : places::fromKdeBookmarks(read(data + QStringLiteral("/user-places.xbel")))) add(v);
#ifdef __linux__
	for (const QVariant &v : places::fromMounts(read(QStringLiteral("/proc/self/mounts")))) add(v);
#endif
	return list;
}

QVariantMap FilePlaces::resolve(const QString &typed, const QUrl &folder) const
{
	QString text = typed.trimmed();
	QString path;
	const QUrl asUrl(text);
	if (asUrl.isLocalFile()) path = asUrl.toLocalFile();
	else
	{
		if (text == QStringLiteral("~")) text = QDir::homePath();
		else if (text.startsWith(QStringLiteral("~/"))) text = QDir::homePath() + text.mid(1);
		path = QDir::isAbsolutePath(text) ? text : QDir(folder.toLocalFile()).filePath(text);
	}
	path = QDir::cleanPath(path);
	const QFileInfo info(path);
	return {
		{QStringLiteral("url"), QUrl::fromLocalFile(path)},
		{QStringLiteral("exists"), info.exists()},
		{QStringLiteral("isFolder"), info.isDir()},
		{QStringLiteral("parentExists"), QFileInfo(info.absolutePath()).isDir()},
	};
}

QUrl FilePlaces::parentOf(const QUrl &url) const
{
	const QString path = QDir::cleanPath(url.toLocalFile());
	if (path.isEmpty()) return url;
	QDir dir(path);
	if (!dir.cdUp()) return url;
	return QUrl::fromLocalFile(dir.absolutePath());
}

QString FilePlaces::pathOf(const QUrl &url) const
{
	return QDir::toNativeSeparators(url.toLocalFile());
}

} // namespace chimera::qt
