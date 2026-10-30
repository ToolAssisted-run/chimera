#include "win32_file_dialog.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#endif

namespace chimera::qt::win32 {

#ifdef _WIN32
namespace {

constexpr DWORD kCheckBoxId = 1;

/* a COM pointer, released when it goes */
template <typename T>
struct Com
{
	T *p = nullptr;
	~Com()
	{
		if (p != nullptr) p->Release();
	}
	T *operator->() const { return p; }
	T **put() { return &p; }
};

std::wstring pathOf(IShellItem *item)
{
	std::wstring out;
	PWSTR name = nullptr;
	if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name != nullptr)
	{
		out = name;
		CoTaskMemFree(name);
	}
	return out;
}

} // namespace

DialogAnswer showFileDialog(const DialogRequest &request)
{
	DialogAnswer answer;
	/* the window's thread has COM already (Qt's OLE); this is its own hold */
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	const bool release = SUCCEEDED(com);
	{
		Com<IFileDialog> dialog;
		const HRESULT made = request.save
			? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, reinterpret_cast<void **>(dialog.put()))
			: CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, reinterpret_cast<void **>(dialog.put()));
		if (FAILED(made) || dialog.p == nullptr)
		{
			if (release) CoUninitialize();
			return answer;
		}
		FILEOPENDIALOGOPTIONS options = 0;
		dialog->GetOptions(&options);
		options |= FOS_FORCEFILESYSTEM;
		if (request.folder) options |= FOS_PICKFOLDERS;
		if (request.multiple && !request.save) options |= FOS_ALLOWMULTISELECT;
		if (request.save) options |= FOS_OVERWRITEPROMPT;
		dialog->SetOptions(options);
		if (!request.title.empty()) dialog->SetTitle(request.title.c_str());
		std::vector<COMDLG_FILTERSPEC> specs;
		if (!request.folder)
			for (const auto &f : request.filters) specs.push_back({f.first.c_str(), f.second.c_str()});
		if (!specs.empty()) dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
		if (!request.defaultExtension.empty()) dialog->SetDefaultExtension(request.defaultExtension.c_str());
		if (!request.fileName.empty()) dialog->SetFileName(request.fileName.c_str());
		if (!request.folderPath.empty())
		{
			Com<IShellItem> start;
			if (SUCCEEDED(SHCreateItemFromParsingName(request.folderPath.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void **>(start.put()))))
				dialog->SetFolder(start.p);
		}
		Com<IFileDialogCustomize> customize;
		const bool box = !request.checkBox.empty()
			&& SUCCEEDED(dialog->QueryInterface(IID_IFileDialogCustomize, reinterpret_cast<void **>(customize.put())))
			&& SUCCEEDED(customize->AddCheckButton(kCheckBoxId, request.checkBox.c_str(), request.checkBoxChecked ? TRUE : FALSE));
		answer.checkBoxChecked = request.checkBoxChecked;

		const HRESULT shown = dialog->Show(static_cast<HWND>(request.owner));
		if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) answer.outcome = 1;
		else if (SUCCEEDED(shown))
		{
			if (!request.save)
			{
				Com<IFileOpenDialog> open;
				Com<IShellItemArray> items;
				if (SUCCEEDED(dialog->QueryInterface(IID_IFileOpenDialog, reinterpret_cast<void **>(open.put())))
					&& SUCCEEDED(open->GetResults(items.put())))
				{
					DWORD count = 0;
					items->GetCount(&count);
					for (DWORD i = 0; i < count; i++)
					{
						Com<IShellItem> item;
						if (SUCCEEDED(items->GetItemAt(i, item.put())))
							if (std::wstring path = pathOf(item.p); !path.empty()) answer.paths.push_back(std::move(path));
					}
				}
			}
			else
			{
				Com<IShellItem> item;
				if (SUCCEEDED(dialog->GetResult(item.put())))
					if (std::wstring path = pathOf(item.p); !path.empty()) answer.paths.push_back(std::move(path));
			}
			if (box)
			{
				BOOL ticked = FALSE;
				if (SUCCEEDED(customize->GetCheckButtonState(kCheckBoxId, &ticked))) answer.checkBoxChecked = ticked != FALSE;
			}
			answer.outcome = answer.paths.empty() ? 1 : 0;
		}
	}
	if (release) CoUninitialize();
	return answer;
}
#else
DialogAnswer showFileDialog(const DialogRequest &)
{
	return {};
}
#endif

} // namespace chimera::qt::win32
