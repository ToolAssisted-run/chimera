#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QUrl>

#include <functional>
#include <string>

#include "chimera/vm/picture.hpp"
#include "chimera/vm/tr.hpp"

namespace chimera::qt {

/* The machine's picture as a file, for QML: a bug report's game.png
 * (Chimera's Report an Issue puts the game as it is now in the zip), and
 * a screenshot where it is asked for (Chimera's Screenshot As). */
class PictureFiles : public QObject
{
	Q_OBJECT
public:
	explicit PictureFiles(vm::Picture &picture, QObject *parent = nullptr) : QObject(parent), picture_(picture) {}
	/* written where asked; "" when there is no picture or it could not be */
	Q_INVOKABLE QString savePng(const QString &path) const
	{
		vm::Picture::Frame f;
		if (!picture_.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0) return {};
		const QImage image(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32);
		return image.save(path, "PNG") ? path : QString();
	}
	/* the picture where it is asked for: a JPEG when the name ends ".jpg",
	 * as Chimera's, else a PNG; "" when there is no picture or it could not
	 * be written. Either is said (say), as the Screenshot key says it */
	Q_INVOKABLE QString save(const QUrl &file) const
	{
		const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
		vm::Picture::Frame f;
		if (path.isEmpty() || !picture_.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0)
		{
			if (say) say(vm::tr("screenshot.no_picture"));
			return {};
		}
		QDir().mkpath(QFileInfo(path).absolutePath());
		const QImage image(reinterpret_cast<const uchar *>(f.pixels.data()), f.width, f.height, f.width * 4, QImage::Format_RGB32);
		const bool jpeg = path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive);
		const bool written = image.save(path, jpeg ? "JPG" : "PNG");
		if (say) say(written ? vm::tr("screenshot.saved", {QFileInfo(path).fileName().toStdString()}) : vm::tr("screenshot.not_written", {path.toStdString()}));
		return written ? path : QString();
	}
	/* a picture the window grabbed (the picture area with what is over it) */
	Q_INVOKABLE QString saveGrab(const QUrl &file, const QVariant &grabbed) const
	{
		const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
		const QImage image = grabbed.value<QImage>().convertToFormat(QImage::Format_RGB32);
		if (path.isEmpty() || image.isNull()) return {};
		QDir().mkpath(QFileInfo(path).absolutePath());
		const bool written = image.save(path, path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive) ? "JPG" : "PNG");
		if (say) say(written ? vm::tr("screenshot.saved", {QFileInfo(path).fileName().toStdString()}) : vm::tr("screenshot.not_written", {path.toStdString()}));
		return written ? path : QString();
	}
	std::function<void(const std::string &)> say;
	/* where screenshots go: CHIMERA_SCREENSHOTS, else the Paths window's
	 * Screenshots folder (folderFor), else Screenshots beside the program */
	static std::function<QString()> &folderFor()
	{
		static std::function<QString()> f;
		return f;
	}
	static QString screenshotFolder()
	{
		const QString folder = qEnvironmentVariable("CHIMERA_SCREENSHOTS");
		if (!folder.isEmpty()) return folder;
		if (folderFor()) return folderFor()();
		return QCoreApplication::applicationDirPath() + QStringLiteral("/Screenshots");
	}
	/* a folder as a dialog's address; a file's path from its URL */
	Q_INVOKABLE QUrl folderUrl(const QString &path) const { return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path); }
	Q_INVOKABLE bool exists(const QString &path) const { return !path.isEmpty() && QFileInfo::exists(path); }
	Q_INVOKABLE QString localPath(const QUrl &file) const { return file.isLocalFile() ? file.toLocalFile() : file.toString(); }
	/* a screenshot's name as Chimera's: the game, then the date and time
	 * ("walker.2026-10-23 11.35.46"), in the screenshot folder, no ending */
	static QString screenshotBase(QString game)
	{
		for (const QChar c : QStringLiteral("<>:\"/\\|?*")) game.replace(c, QLatin1Char('_'));
		if (game.isEmpty()) game = QStringLiteral("screenshot");
		return screenshotFolder() + QLatin1Char('/') + game + QLatin1Char('.') +
			QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
	}
	/* Screenshot As's first answer, as a file address for the save dialog;
	 * the folder is made so the dialog can open in it */
	Q_INVOKABLE QUrl screenshotSuggestion(const QString &game) const
	{
		QDir().mkpath(screenshotFolder());
		return QUrl::fromLocalFile(screenshotBase(game) + QStringLiteral(".png"));
	}
	/* the game open, as the window names it ("" for none) */
	static std::function<QString()> &gameFor()
	{
		static std::function<QString()> f;
		return f;
	}
	/* a save dialog's first answer: the game's name with the dialog's
	 * ending, in the folder it opens in, as Chimera's tools name their files
	 * (issue 510); nothing when no game is open */
	Q_INVOKABLE QUrl gameFile(const QUrl &folder, const QString &ending) const
	{
		QString game = gameFor() ? gameFor()() : QString();
		for (const QChar c : QStringLiteral("<>:\"/\\|?*")) game.replace(c, QLatin1Char('_'));
		if (game.trimmed().isEmpty()) return {};
		const QString name = game + ending;
		return folder.isLocalFile() ? QUrl::fromLocalFile(QDir(folder.toLocalFile()).filePath(name)) : QUrl::fromLocalFile(name);
	}
	/* a file in the system's place for passing files, for one written and read straight away */
	Q_INVOKABLE QString temporaryFile(const QString &name) const
	{
		return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
	}

private:
	vm::Picture &picture_;
};

} // namespace chimera::qt
