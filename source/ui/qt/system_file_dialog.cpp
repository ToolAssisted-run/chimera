#include "system_file_dialog.hpp"

#include "portal_file_chooser.hpp"
#include "win32_file_dialog.hpp"

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QWindow>

namespace chimera::qt {

SystemFileDialog::SystemFileDialog(QObject *parent) : QObject(parent) {}

bool SystemFileDialog::available() const
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

QVariantMap SystemFileDialog::ask(const QVariantMap &request) const
{
	const QString mode = request.value(QStringLiteral("mode")).toString();
	win32::DialogRequest r;
	r.save = mode == QStringLiteral("save");
	r.folder = mode == QStringLiteral("folder");
	r.multiple = mode == QStringLiteral("openMany");
	r.title = request.value(QStringLiteral("title")).toString().toStdWString();
	const QString folder = request.value(QStringLiteral("folder")).toUrl().toLocalFile();
	if (!folder.isEmpty() && QFileInfo(folder).isDir()) r.folderPath = QDir::toNativeSeparators(QDir::cleanPath(folder)).toStdWString();
	r.fileName = QFileInfo(request.value(QStringLiteral("name")).toString()).fileName().toStdWString();
	QString suffix = request.value(QStringLiteral("defaultSuffix")).toString();
	while (suffix.startsWith(QLatin1Char('.'))) suffix.remove(0, 1);
	r.defaultExtension = suffix.toStdWString();
	for (const portal::Filter &f : portal::filtersFrom(request.value(QStringLiteral("nameFilters")).toStringList()))
		r.filters.emplace_back(f.label.toStdWString(), f.globs.join(QLatin1Char(';')).toStdWString());
	r.checkBox = request.value(QStringLiteral("checkBox")).toString().toStdWString();
	r.checkBoxChecked = request.value(QStringLiteral("checkBoxChecked")).toBool();
	if (auto *window = qobject_cast<QWindow *>(request.value(QStringLiteral("window")).value<QObject *>()))
		r.owner = reinterpret_cast<void *>(window->winId());

	const win32::DialogAnswer a = win32::showFileDialog(r);
	QList<QUrl> files;
	for (const std::wstring &path : a.paths) files << QUrl::fromLocalFile(QString::fromStdWString(path));
	return {
		{QStringLiteral("outcome"), a.outcome},
		{QStringLiteral("files"), QVariant::fromValue(files)},
		{QStringLiteral("checkBoxChecked"), a.checkBoxChecked},
	};
}

} // namespace chimera::qt
