#pragma once
/* Windows' own file and folder dialog (the common item dialog, IFileDialog),
 * shown by Chimera itself rather than through Qt for a dialog that carries a
 * tick box, such as Include sub-folders: Qt's Windows dialog takes no
 * controls of a caller's, and IFileDialogCustomize adds one to the dialog
 * Windows draws (docs/file-picker.md). No Qt here, so it builds alone. */

#include <string>
#include <utility>
#include <vector>

namespace chimera::qt::win32 {

struct DialogRequest
{
	bool save = false, folder = false, multiple = false;
	std::wstring title;
	/* the folder to open in, a save's file name, a save's default extension */
	std::wstring folderPath, fileName, defaultExtension;
	/* each a label and its patterns, "*.a;*.b" */
	std::vector<std::pair<std::wstring, std::wstring>> filters;
	/* the tick box's label (none when empty), and whether it starts ticked */
	std::wstring checkBox;
	bool checkBoxChecked = false;
	/* the window it is over (an HWND), or none */
	void *owner = nullptr;
};

struct DialogAnswer
{
	/* 0 chosen, 1 Cancel, 2 the dialog could not be made (Qt's is shown) */
	int outcome = 2;
	std::vector<std::wstring> paths;
	bool checkBoxChecked = false;
};

/* shows the dialog and waits for it, as a modal dialog; on anything but
 * Windows the answer is always 2 */
DialogAnswer showFileDialog(const DialogRequest &request);

} // namespace chimera::qt::win32
