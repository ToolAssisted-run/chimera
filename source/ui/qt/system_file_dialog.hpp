#pragma once
/* Windows' own file dialog for the window's QML (dialogs/PickerBase.qml),
 * for a dialog that carries a tick box: Qt's takes none of a caller's
 * (win32_file_dialog.cpp). Elsewhere it is not available, and Qt's dialog
 * or the desktop's chooser is shown (docs/file-picker.md). */

#include <QObject>
#include <QVariantMap>

namespace chimera::qt {

class SystemFileDialog : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool available READ available CONSTANT)
public:
	explicit SystemFileDialog(QObject *parent = nullptr);
	bool available() const;
	/* Shows the dialog and waits for it. The request as the portal takes it
	 * (portal_file_chooser.hpp): mode, title, nameFilters, folder, name,
	 * window, defaultSuffix, checkBox, checkBoxChecked. The answer: outcome
	 * (0 chosen, 1 Cancel, 2 none could be shown), files, checkBoxChecked. */
	Q_INVOKABLE QVariantMap ask(const QVariantMap &request) const;
};

} // namespace chimera::qt
