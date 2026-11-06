#include "chimera/vm/forms_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cstdlib>

namespace chimera::vm {

namespace {
std::vector<std::string> lines(const std::string &text)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at < text.size())
	{
		size_t end = text.find('\n', at);
		if (end == std::string::npos) end = text.size();
		out.push_back(text.substr(at, end - at));
		at = end + 1;
	}
	return out;
}

std::string joined(const std::vector<std::string> &items)
{
	std::string out;
	for (const std::string &i : items) out += (out.empty() ? "" : "\n") + i;
	return out;
}

/* a whole number argument that may be left out (nil), else the default */
int64_t optional(const Args &a, size_t i, int64_t fallback)
{
	if (i >= a.size()) return fallback;
	if (const int64_t *v = std::get_if<int64_t>(&a[i])) return *v;
	if (const double *d = std::get_if<double>(&a[i])) return static_cast<int64_t>(*d);
	return fallback;
}

bool optionalBool(const Args &a, size_t i)
{
	if (i >= a.size()) return false;
	if (const bool *b = std::get_if<bool>(&a[i])) return *b;
	return false;
}

std::string optionalText(const Args &a, size_t i)
{
	if (i >= a.size()) return {};
	if (const std::string *s = std::get_if<std::string>(&a[i])) return *s;
	return {};
}
} // namespace

FormsViewModel::FormsViewModel(Dispatcher &dispatcher) : ViewModel("forms", dispatcher)
{
	rows("forms", "a script's forms: handle, title (text), width, height, ...", formRows_);
	rows("controls", "every form's controls: handle, form, kind, text, x, y, width, height, checked, items, selected, ...", controlRows_);
	/* a form: at Chimera's own size when none is given */
	command("newForm", "a form, its size inside its frame and its title; its handle", {"width", "height", "title"},
		[this](const Args &a, std::string &error) -> Value {
			Item f;
			f.kind = "form";
			f.width = optional(a, 0, 284);
			f.height = optional(a, 1, 261);
			f.text = optionalText(a, 2);
			if (f.text.empty()) f.text = tr("forms.default_title");
			return add(f, error);
		});
	/* a control on a form: at its place and size, Chimera's own when none is given */
	auto control = [this](const std::string &kind, int64_t form, const std::string &text, int64_t x, int64_t y, int64_t w, int64_t h,
					   std::string &error) -> Value {
		Item c;
		c.kind = kind;
		c.form = form;
		c.text = text;
		c.x = x;
		c.y = y;
		c.width = w;
		c.height = h;
		return add(c, error);
	};
	command("button", "a button: form, caption, x, y, width, height; its handle", {"form", "caption", "x", "y", "width", "height"},
		[this, control](const Args &a, std::string &error) -> Value {
			return control("button", optional(a, 0, 0), optionalText(a, 1), optional(a, 2, 0), optional(a, 3, 0), optional(a, 4, 75),
				optional(a, 5, 23), error);
		});
	command("checkbox", "a check box: form, caption, x, y; its handle", {"form", "caption", "x", "y"},
		[this, control](const Args &a, std::string &error) -> Value {
			return control("checkbox", optional(a, 0, 0), optionalText(a, 1), optional(a, 2, 0), optional(a, 3, 0), 104, 24, error);
		});
	command("label", "a label: form, caption, x, y, width, height, fixedWidth; its handle",
		{"form", "caption", "x", "y", "width", "height", "fixedWidth"}, [this, control](const Args &a, std::string &error) -> Value {
			const Value h = control("label", optional(a, 0, 0), optionalText(a, 1), optional(a, 2, 0), optional(a, 3, 0), optional(a, 4, 100),
				optional(a, 5, 23), error);
			if (Item *item = find(optional({h}, 0, 0)))
			{
				item->fixedWidth = optionalBool(a, 6);
				changed(*item);
			}
			return h;
		});
	command("textbox", "a text box: form, caption, width, height, boxtype (HEX, SIGNED, UNSIGNED), x, y, multiline, fixedWidth, "
		"scrollbars; its handle", {"form", "caption", "width", "height", "boxtype", "x", "y", "multiline", "fixedWidth", "scrollbars"},
		[this, control](const Args &a, std::string &error) -> Value {
			const Value h = control("textbox", optional(a, 0, 0), optionalText(a, 1), optional(a, 5, 0), optional(a, 6, 0), optional(a, 2, 100),
				optional(a, 3, 20), error);
			if (Item *item = find(optional({h}, 0, 0)))
			{
				item->boxType = optionalText(a, 4);
				item->multiline = optionalBool(a, 7);
				item->fixedWidth = optionalBool(a, 8);
				item->scrollBars = optionalText(a, 9);
				changed(*item);
			}
			return h;
		});
	command("dropdown", "a dropdown: form, its items (a line each), x, y, width, height; its handle",
		{"form", "items", "x", "y", "width", "height"}, [this, control](const Args &a, std::string &error) -> Value {
			const Value h = control("dropdown", optional(a, 0, 0), "", optional(a, 2, 0), optional(a, 3, 0), optional(a, 4, 121),
				optional(a, 5, 21), error);
			if (Item *item = find(optional({h}, 0, 0)))
			{
				item->items = lines(optionalText(a, 1));
				std::sort(item->items.begin(), item->items.end());
				item->selected = item->items.empty() ? -1 : 0;
				item->text = item->items.empty() ? "" : item->items.front();
				changed(*item);
			}
			return h;
		});
	command("pictureBox", "a picture box to draw on: form, x, y, width, height; its handle", {"form", "x", "y", "width", "height"},
		[this, control](const Args &a, std::string &error) -> Value {
			return control("picturebox", optional(a, 0, 0), "", optional(a, 1, 0), optional(a, 2, 0), optional(a, 3, 100),
				optional(a, 4, 50), error);
		});
	command("destroy", "closes a form (and its controls) by its handle; whether there was one", {"handle"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			if (!arg(a, 0, h, error)) return {};
			const bool there = std::any_of(forms_.begin(), forms_.end(), [h](const Item &f) { return f.handle == h; });
			if (there) removeForm(h);
			return there;
		});
	command("openFile", "Chimera's forms.openfile: the window's file dialog, waited for; the file picked, \"\" when "
		"cancelled", {"fileName", "folder", "filter"}, [this](const Args &a, std::string &error) -> Value {
			std::string name, folder, filter;
			if (!arg(a, 0, name, error) || !arg(a, 1, folder, error) || !arg(a, 2, filter, error)) return {};
			return openFile ? openFile(name, folder, nameFilters(filter)) : std::string();
		});
	command("destroyAll", "closes every form", {}, [this](const Args &, std::string &) -> Value {
		while (!forms_.empty()) removeForm(forms_.front().handle);
		return {};
	});
	command("getText", "a form's title or a control's text (a dropdown's item chosen)", {"handle"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			if (!arg(a, 0, h, error)) return {};
			const Item *item = find(h);
			return item != nullptr ? item->text : std::string();
		});
	command("setText", "a form's title or a control's text", {"handle", "text"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0;
		std::string text;
		if (!arg(a, 0, h, error) || !arg(a, 1, text, error)) return {};
		if (Item *item = find(h))
		{
			item->text = text;
			changed(*item);
		}
		return {};
	});
	command("isChecked", "whether a check box is ticked", {"handle"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0;
		if (!arg(a, 0, h, error)) return {};
		const Item *item = find(h);
		return item != nullptr && item->checked;
	});
	command("setChecked", "a check box ticked or not (the window says)", {"handle", "checked"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0;
		bool c = false;
		if (!arg(a, 0, h, error) || !arg(a, 1, c, error)) return {};
		if (Item *item = find(h))
		{
			item->checked = c;
			changed(*item);
		}
		return {};
	});
	command("choose", "a dropdown's item chosen, by its place (the window says)", {"handle", "index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0, i = 0;
			if (!arg(a, 0, h, error) || !arg(a, 1, i, error)) return {};
			if (Item *item = find(h); item != nullptr && i >= 0 && i < static_cast<int64_t>(item->items.size()))
			{
				item->selected = i;
				item->text = item->items[static_cast<size_t>(i)];
				changed(*item);
			}
			return {};
		});
	command("setDropdownItems", "a dropdown's items, a line each, sorted unless told not to", {"handle", "items", "alphabetize"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			std::string text;
			if (!arg(a, 0, h, error) || !arg(a, 1, text, error)) return {};
			const bool sorted = a.size() < 3 || !std::holds_alternative<bool>(a[2]) || std::get<bool>(a[2]);
			if (Item *item = find(h))
			{
				item->items = lines(text);
				if (sorted) std::sort(item->items.begin(), item->items.end());
				item->selected = item->items.empty() ? -1 : 0;
				item->text = item->items.empty() ? "" : item->items.front();
				changed(*item);
			}
			return {};
		});
	command("setLocation", "a control's or form's place", {"handle", "x", "y"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0, x = 0, y = 0;
		if (!arg(a, 0, h, error) || !arg(a, 1, x, error) || !arg(a, 2, y, error)) return {};
		if (Item *item = find(h))
		{
			item->x = x;
			item->y = y;
			changed(*item);
		}
		return {};
	});
	command("setSize", "a control's or form's size", {"handle", "width", "height"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0, w = 0, ht = 0;
		if (!arg(a, 0, h, error) || !arg(a, 1, w, error) || !arg(a, 2, ht, error)) return {};
		if (Item *item = find(h))
		{
			item->width = w;
			item->height = ht;
			changed(*item);
		}
		return {};
	});
	/* the property names scripts use, those these controls have */
	command("getProperty", "a property's value as text: Text, Left, Top, Width, Height, Visible, Enabled, Checked, SelectedItem, "
		"SelectedIndex", {"handle", "property"}, [this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			std::string p;
			if (!arg(a, 0, h, error) || !arg(a, 1, p, error)) return {};
			const Item *item = find(h);
			if (item == nullptr) return std::string();
			if (p == "Text" || p == "SelectedItem") return item->text;
			if (p == "Left") return std::to_string(item->x);
			if (p == "Top") return std::to_string(item->y);
			if (p == "Width") return std::to_string(item->width);
			if (p == "Height") return std::to_string(item->height);
			if (p == "Visible") return std::string(item->visible ? "True" : "False");
			if (p == "Enabled") return std::string(item->enabled ? "True" : "False");
			if (p == "Checked") return std::string(item->checked ? "True" : "False");
			if (p == "SelectedIndex") return std::to_string(item->selected);
			error = tr("forms.no_property", {p});
			return {};
		});
	command("setProperty", "a property set from text: Text, Left, Top, Width, Height, Visible, Enabled, Checked, SelectedIndex",
		{"handle", "property", "value"}, [this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			std::string p, v;
			if (!arg(a, 0, h, error) || !arg(a, 1, p, error)) return {};
			if (a.size() > 2)
			{
				if (const std::string *s = std::get_if<std::string>(&a[2])) v = *s;
				else if (const int64_t *i = std::get_if<int64_t>(&a[2])) v = std::to_string(*i);
				else if (const double *d = std::get_if<double>(&a[2])) v = std::to_string(static_cast<int64_t>(*d));
				else if (const bool *b = std::get_if<bool>(&a[2])) v = *b ? "True" : "False";
			}
			Item *item = find(h);
			if (item == nullptr) return {};
			auto number = [&] { return static_cast<int64_t>(std::atoll(v.c_str())); };
			auto yes = [&] { return v == "True" || v == "true" || v == "1"; };
			if (p == "Text") item->text = v;
			else if (p == "Left") item->x = number();
			else if (p == "Top") item->y = number();
			else if (p == "Width") item->width = number();
			else if (p == "Height") item->height = number();
			else if (p == "Visible") item->visible = yes();
			else if (p == "Enabled") item->enabled = yes();
			else if (p == "Checked") item->checked = yes();
			else if (p == "SelectedIndex" && number() >= 0 && number() < static_cast<int64_t>(item->items.size()))
			{
				item->selected = number();
				item->text = item->items[static_cast<size_t>(item->selected)];
			}
			else
			{
				error = tr("forms.no_property", {p});
				return {};
			}
			changed(*item);
			return {};
		});
	command("saveImage", "a picture box's picture saved to a PNG file by the window, as a canvas's save_image_to_disk does",
		{"handle", "path"}, [this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			std::string path;
			if (!arg(a, 0, h, error) || !arg(a, 1, path, error)) return {};
			Item *item = find(h);
			if (item == nullptr || item->kind != "picturebox")
			{
				error = tr("forms.no_picture_box", {std::to_string(h)});
				return {};
			}
			item->saveTo = path;
			item->saves++;
			changed(*item);
			return {};
		});
	command("draw", "what a picture box shows (the gui library's shapes, JSON) and its colour behind them", {"handle", "shapes", "background"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			std::string shapes, background;
			if (!arg(a, 0, h, error) || !arg(a, 1, shapes, error) || !arg(a, 2, background, error)) return {};
			if (Item *item = find(h))
			{
				item->shapes = shapes;
				if (!background.empty()) item->background = background;
				changed(*item);
			}
			return {};
		});
	command("pointer", "where the pointer is over a picture box (the window says)", {"handle", "x", "y"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0, x = 0, y = 0;
			if (!arg(a, 0, h, error) || !arg(a, 1, x, error) || !arg(a, 2, y, error)) return {};
			if (Item *item = find(h))
			{
				item->mouseX = x;
				item->mouseY = y;
			}
			return {};
		});
	command("mouseX", "the pointer across a picture box, last seen", {"handle"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0;
		if (!arg(a, 0, h, error)) return {};
		const Item *item = find(h);
		return item != nullptr ? item->mouseX : int64_t{0};
	});
	command("mouseY", "the pointer down a picture box, last seen", {"handle"}, [this](const Args &a, std::string &error) -> Value {
		int64_t h = 0;
		if (!arg(a, 0, h, error)) return {};
		const Item *item = find(h);
		return item != nullptr ? item->mouseY : int64_t{0};
	});
	command("click", "a control clicked (the window says): the script's click functions run", {"handle"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			if (!arg(a, 0, h, error)) return {};
			if (find(h) != nullptr) clicked.emit(h);
			return {};
		});
	command("closed", "a form closed by the person (the window says): its close function runs, and it goes", {"handle"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t h = 0;
			if (!arg(a, 0, h, error)) return {};
			if (std::any_of(forms_.begin(), forms_.end(), [h](const Item &f) { return f.handle == h; }))
			{
				closed.emit(h);
				removeForm(h);
			}
			return {};
		});
}

FormsViewModel::Item *FormsViewModel::find(int64_t handle)
{
	for (std::vector<Item> *list : {&forms_, &controls_})
		for (Item &i : *list)
			if (i.handle == handle) return &i;
	return nullptr;
}

int64_t FormsViewModel::add(Item item, std::string &error)
{
	if (item.kind != "form" && std::none_of(forms_.begin(), forms_.end(), [&](const Item &f) { return f.handle == item.form; }))
	{
		/* as Chimera's: no form, no control (handle 0) */
		(void)error;
		return 0;
	}
	item.handle = next_++;
	const bool form = item.kind == "form";
	std::vector<Item> &list = form ? forms_ : controls_;
	list.push_back(std::move(item));
	(form ? formRows_ : controlRows_).appended.emit(static_cast<int64_t>(list.size()) - 1, static_cast<int64_t>(list.size()) - 1);
	return list.back().handle;
}

void FormsViewModel::changed(const Item &item)
{
	const bool form = item.kind == "form";
	const std::vector<Item> &list = form ? forms_ : controls_;
	for (size_t i = 0; i < list.size(); i++)
		if (list[i].handle == item.handle) (form ? formRows_ : controlRows_).changed.emit(static_cast<int64_t>(i), static_cast<int64_t>(i));
}

void FormsViewModel::removeForm(int64_t handle)
{
	std::erase_if(controls_, [handle](const Item &c) { return c.form == handle; });
	std::erase_if(forms_, [handle](const Item &f) { return f.handle == handle; });
	controlRows_.reset.emit();
	formRows_.reset.emit();
}

int64_t FormsViewModel::Rows::rowCount() const
{
	return static_cast<int64_t>((forms_ ? vm_.forms_ : vm_.controls_).size());
}

const FormsViewModel::Item &FormsViewModel::Rows::at(int64_t row) const
{
	return (forms_ ? vm_.forms_ : vm_.controls_)[static_cast<size_t>(row)];
}

std::vector<std::string> FormsViewModel::nameFilters(const std::string &filter)
{
	std::vector<std::string> parts;
	size_t at = 0;
	for (size_t bar = filter.find('|'); ; bar = filter.find('|', at))
	{
		parts.push_back(filter.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
		if (bar == std::string::npos) break;
		at = bar + 1;
	}
	std::vector<std::string> out;
	for (size_t i = 0; i + 1 < parts.size(); i += 2)
	{
		/* the words without their own list of kinds, which a file dialog would read instead */
		std::string words = parts[i];
		if (const size_t open = words.rfind(" ("); open != std::string::npos && !words.empty() && words.back() == ')') words.erase(open);
		std::string kinds;
		size_t k = 0;
		const std::string &list = parts[i + 1];
		for (size_t semi = list.find(';'); ; semi = list.find(';', k))
		{
			std::string kind = list.substr(k, semi == std::string::npos ? std::string::npos : semi - k);
			/* "*.*" is every file, as Windows reads it */
			if (kind == "*.*") kind = "*";
			if (!kind.empty()) kinds += (kinds.empty() ? "" : " ") + kind;
			if (semi == std::string::npos) break;
			k = semi + 1;
		}
		out.push_back(words + " (" + (kinds.empty() ? std::string("*") : kinds) + ")");
	}
	if (out.empty()) out.push_back(tr("forms.all_files"));
	return out;
}

std::string FormsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"handle", "form", "kind", "text", "x", "y", "width", "height", "checked", "items", "selected",
		"multiline", "fixedWidth", "visible", "enabled", "shapes", "background", "boxType", "scrollBars", "saveTo", "saves"};
	return column >= 0 && column < 21 ? names[column] : "";
}

Value FormsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Item &i = at(row);
	switch (column)
	{
	case 0: return i.handle;
	case 1: return i.form;
	case 2: return i.kind;
	case 3: return i.text;
	case 4: return i.x;
	case 5: return i.y;
	case 6: return i.width;
	case 7: return i.height;
	case 8: return i.checked;
	case 9: return joined(i.items);
	case 10: return i.selected;
	case 11: return i.multiline;
	case 12: return i.fixedWidth;
	case 13: return i.visible;
	case 14: return i.enabled;
	case 15: return i.shapes;
	case 16: return i.background;
	case 17: return i.boxType;
	case 18: return i.scrollBars;
	case 19: return i.saveTo;
	case 20: return i.saves;
	default: return {};
	}
}

} // namespace chimera::vm
