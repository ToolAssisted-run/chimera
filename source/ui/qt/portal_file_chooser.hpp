#pragma once
/* The desktop's own file chooser, asked for through the XDG desktop portal
 * (org.freedesktop.portal.FileChooser on the session D-Bus): KDE's, GNOME's,
 * or whichever the desktop is set to, with the person's bookmarks, drives,
 * typed paths and hidden files. Qt is built without D-Bus, so libdbus is
 * loaded when the chooser is first asked for. With no session bus, no
 * portal, no chooser behind it or no libdbus, ask() says no and the window
 * shows its own chooser instead (docs/file-picker.md). */

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <memory>

class QWindow;

namespace chimera::qt {

namespace portal {

/* a filter as the portal takes one: what the person reads, and its globs */
struct Filter
{
	QString label;
	QStringList globs;
};

/* Qt's name filters, "Projects (*.chimeraProject *.json)", as the portal's;
 * a filter with no patterns in brackets is its own pattern list */
QList<Filter> filtersFrom(const QStringList &nameFilters);
/* a file:// URI the portal answers with, as a local path; empty for any other */
QString pathFromUri(const QString &uri);
/* the portal's name for this connection in a request's object path:
 * ":1.42" is "1_42" */
QString requestPathPart(const QString &uniqueName);
/* a saved file's name with the dialog's default suffix, when it has none */
QString withDefaultSuffix(const QString &path, const QString &suffix);

} // namespace portal

class PortalFileChooser : public QObject
{
	Q_OBJECT
public:
	/* how a question ended: a file chosen, Cancel, or the portal could not
	 * show its chooser (the window's own is shown instead) */
	enum Outcome { Chosen = 0, Cancelled = 1, Unavailable = 2 };
	Q_ENUM(Outcome)

	explicit PortalFileChooser(QObject *parent = nullptr);
	~PortalFileChooser() override;

	/* Asks the desktop's chooser, as a modal dialog: Chimera's windows take
	 * no clicks, keys or closing until it answers, and are still drawn. The
	 * request: mode ("open", "openMany", "save" or "folder"), title,
	 * nameFilters, folder (a URL), name (a save's file name), window (the
	 * window it is over), defaultSuffix, and checkBox and checkBoxChecked: a
	 * tick box the chooser carries (a portal "choice" with no options), such
	 * as Include sub-folders. False when the portal cannot be asked: then
	 * nothing is answered. */
	Q_INVOKABLE bool ask(const QVariantMap &request);
	/* the window an object of a window is shown in, or none */
	Q_INVOKABLE QWindow *windowOf(QObject *object) const;

Q_SIGNALS:
	/* the answer to ask(): an Outcome, the files (or the folder) chosen, and
	 * the tick box as it was confirmed: 1 ticked, 0 not, -1 when the chooser
	 * did not carry it (termfilechooser draws none) */
	void answered(int outcome, const QList<QUrl> &files, int checkBox);

private:
	struct Pending;
	void poll();
	void finish(int outcome, const QList<QUrl> &files = {}, int checkBox = -1);
	std::unique_ptr<Pending> pending_;
	QTimer timer_;
};

} // namespace chimera::qt
