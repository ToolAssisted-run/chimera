#pragma once
/* What Chimera's own file chooser (dialogs/BuiltinChooser.qml) lists beside
 * its folder, and how it reads a path typed into it: the places a desktop's
 * chooser has - the root, home, the person's folders, the desktop's
 * bookmarks (GTK's and KDE's) and the drives mounted - for when there is
 * no desktop chooser to ask (docs/file-picker.md). */

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace chimera::qt {

namespace places {

/* "file:///home/a/Games Games" lines, as GTK keeps them: a folder each */
QVariantList fromGtkBookmarks(const QString &text);
/* KDE's user-places.xbel: the folders the person added (KDE's own entries,
 * such as Home and Trash, and hidden ones are left out) */
QVariantList fromKdeBookmarks(const QString &xml);
/* the mount table (/proc/self/mounts): the drives a person mounts, under
 * /media, /run/media and /mnt */
QVariantList fromMounts(const QString &table);

} // namespace places

class FilePlaces : public QObject
{
	Q_OBJECT
public:
	explicit FilePlaces(QObject *parent = nullptr);
	/* every place, in order, each {name, url, kind}: kind is "root", "home",
	 * "folder" (the person's Documents, Downloads...), "bookmark" or "drive";
	 * the root and home are named by the window */
	Q_INVOKABLE QVariantList all() const;
	/* a path typed into the chooser, read as the person meant it: "~" is
	 * home, and a relative path is from the folder shown. {url, exists,
	 * isFolder, parentExists} */
	Q_INVOKABLE QVariantMap resolve(const QString &typed, const QUrl &folder) const;
	/* the folder a URL is in */
	Q_INVOKABLE QUrl parentOf(const QUrl &url) const;
	/* a URL as a path to read, as the location box shows it */
	Q_INVOKABLE QString pathOf(const QUrl &url) const;
};

} // namespace chimera::qt
