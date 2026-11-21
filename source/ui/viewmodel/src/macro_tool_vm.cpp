#include "chimera/vm/macro_tool_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {
std::vector<std::string> split(const std::string &text, char by)
{
	std::vector<std::string> out;
	std::stringstream in(text);
	for (std::string one; std::getline(in, one, by);)
		if (!one.empty()) out.push_back(one);
	return out;
}
} // namespace

MacroToolViewModel::MacroToolViewModel(Dispatcher &dispatcher, PianoRollViewModel &roll, SessionViewModel &session)
	: ViewModel("macroTool", dispatcher), roll_(roll), session_(session)
{
	expose("picked", "the zone picked, by its place", picked, true);
	expose("movieColumns", "the movie's columns, as [name, ...]", movieColumns);
	expose("unsavedCount", "zones set and not saved", unsavedCount);
	expose("status", "what the macro tool last did", status);
	rows("zones", "one row a zone: name, start, length, overlay, replace, columns (\"\" all), saved", rows_);
	command("setZone", "a zone of the frames start to end (both in the movie), named \"Zone N\"", {"start", "end"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t start = 0, end = 0;
			if (!arg(a, 0, start, error) || !arg(a, 1, end, error)) return {};
			if (start > end) std::swap(start, end);
			if (start < 0 || end >= roll_.frameCount.get())
			{
				error = tr("macro.frames_inside");
				return {};
			}
			Zone z;
			/* a name no zone has: removing one would otherwise give two the same */
			for (size_t n = zones_.size();; n++)
			{
				z.name = tr("macro.zone_name", {std::to_string(n)});
				if (std::none_of(zones_.begin(), zones_.end(), [&z](const Zone &o) { return o.name == z.name; })) break;
			}
			z.start = start;
			z.length = end - start + 1;
			z.saved = false;
			zones_.push_back(std::move(z));
			picked.set(static_cast<int64_t>(zones_.size()) - 1);
			changed();
			return picked.get();
		});
	command("rename", "a zone's name", {"zone", "name"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		std::string name;
		if (!zoneOf(a, i, error) || !arg(a, 1, name, error)) return {};
		zones_[i].name = name;
		changed();
		return {};
	});
	command("setOverlay", "a zone pressed over what is there (true), or written as it is", {"zone", "on"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			bool on = false;
			if (!zoneOf(a, i, error) || !arg(a, 1, on, error)) return {};
			zones_[i].overlay = on;
			changed();
			return {};
		});
	command("setReplace", "a zone over the frames there (true), or put in before them", {"zone", "on"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			bool on = false;
			if (!zoneOf(a, i, error) || !arg(a, 1, on, error)) return {};
			zones_[i].replace = on;
			changed();
			return {};
		});
	command("setColumns", "the columns a zone carries, \"P1 Up|P1 A\" (\"\" all of the movie's)", {"zone", "columns"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			std::string list;
			if (!zoneOf(a, i, error) || !arg(a, 1, list, error)) return {};
			if (!zones_[i].fileLines.empty())
			{
				error = tr("macro.own_columns");
				return {};
			}
			json::Json movie;
			std::string err;
			json::parse(movieColumns.get(), movie, err);
			std::vector<std::string> cols = split(list, '|');
			for (const std::string &c : cols)
				if (std::none_of(movie.items.begin(), movie.items.end(), [&c](const json::Json &m) { return m.text == c; }))
				{
					error = tr("macro.not_a_column", {c});
					return {};
				}
			zones_[i].columns = cols;
			changed();
			return {};
		});
	command("place", "a zone placed from this frame on, one step of the undo history", {"zone", "frame"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			int64_t at = 0;
			if (!zoneOf(a, i, error) || !arg(a, 1, at, error)) return {};
			const std::string text = textOf(zones_[i], error);
			if (text.empty()) return {};
			std::vector<std::string> lines;
			std::stringstream in(text);
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				lines.push_back(line);
			}
			if (!roll_.placeMacroLines(at, std::move(lines), zones_[i].name, error)) return {};
			status.set(tr("macro.placed", {zones_[i].name, std::to_string(at)}));
			return {};
		});
	command("saveAs", "a zone written as a .chimeraMacro file Chimera reads", {"zone", "path"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			std::string path;
			if (!zoneOf(a, i, error) || !arg(a, 1, path, error)) return {};
			const std::string text = textOf(zones_[i], error);
			if (text.empty()) return {};
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << text;
			if (!out)
			{
				error = tr("session.could_not_write", {path});
				return {};
			}
			zones_[i].saved = true;
			changed();
			roll_.addRecentMacro(path);
			status.set(tr("macro.saved", {zones_[i].name, path}));
			return {};
		});
	command("load", "a .chimeraMacro file as a zone, named for the file; its columns must be the movie's", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			std::ifstream in(path, std::ios::binary);
			if (!in)
			{
				error = tr("new_project.cannot_read", {path});
				return {};
			}
			std::vector<std::string> lines;
			for (std::string line; std::getline(in, line);)
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				lines.push_back(line);
			}
			while (!lines.empty() && lines.back().empty()) lines.pop_back();
			if (lines.size() < 5)
			{
				error = tr("piano_roll.not_macro", {path});
				return {};
			}
			json::Json movie;
			std::string err;
			json::parse(movieColumns.get(), movie, err);
			for (const std::string &k : split(lines[0], '|'))
				if (std::none_of(movie.items.begin(), movie.items.end(), [&k](const json::Json &m) { return m.text == k; }))
				{
					error = tr("macro.incompatible", {lines[1], lines[2]});
					return {};
				}
			Zone z;
			z.name = std::filesystem::path(path).stem().string();
			const size_t comma = lines[3].find(',');
			auto truth = [](std::string t) {
				for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return t == "true";
			};
			z.overlay = truth(lines[3].substr(0, comma));
			z.replace = comma == std::string::npos || truth(lines[3].substr(comma + 1));
			z.columns = split(lines[0], '|');
			z.length = static_cast<int64_t>(lines.size()) - 4;
			z.fileLines = lines;
			zones_.push_back(std::move(z));
			picked.set(static_cast<int64_t>(zones_.size()) - 1);
			changed();
			roll_.addRecentMacro(path);
			return picked.get();
		});
	command("remove", "a zone let go (the entire movie stays)", {"zone"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!zoneOf(a, i, error)) return {};
		if (zones_[i].whole)
		{
			error = tr("macro.whole_movie_zone");
			return {};
		}
		zones_.erase(zones_.begin() + static_cast<std::ptrdiff_t>(i));
		/* the zone picked stays picked: it moved up if it came after */
		if (picked.get() > static_cast<int64_t>(i)) picked.set(picked.get() - 1);
		picked.set(std::min<int64_t>(picked.get(), static_cast<int64_t>(zones_.size()) - 1));
		changed();
		return {};
	});

	/* another machine: its zones start again, the entire movie first and as
	 * long as the movie is - another project's zones are frames of another
	 * movie (Chimera's Macro Tool clears them as it restarts) */
	auto restart = [this] {
		zones_.clear();
		picked.set(0);
		Zone whole;
		whole.name = tr("macro.entire_movie");
		whole.whole = true;
		whole.length = roll_.frameCount.get();
		zones_.insert(zones_.begin(), std::move(whole));
		readColumns();
		changed();
	};
	/* the movie's columns are the movie's: read again as it changes */
	lengthWatch_ = roll_.frameCount.changed.connect([this] {
		readColumns();
		/* the zone of the whole movie is as long as the movie: its row alone changes */
		if (!zones_.empty() && zones_[0].whole)
		{
			zones_[0].length = roll_.frameCount.get();
			rows_.changed.emit(0, 0);
		}
	});
	opensWatch_ = session_.opens.changed.connect(restart);
	restart();
}

void MacroToolViewModel::readColumns()
{
	json::Json cols;
	cols.kind = json::Json::Kind::Array;
	for (const std::string &c : roll_.macroKeys()) cols.items.push_back(json::Json::string(c));
	const std::string text = json::write(cols);
	if (text != movieColumns.get()) movieColumns.set(text);
}

bool MacroToolViewModel::zoneOf(const Args &a, size_t &i, std::string &error) const
{
	int64_t z = 0;
	if (!arg(a, 0, z, error)) return false;
	if (z < 0 || z >= static_cast<int64_t>(zones_.size()))
	{
		error = tr("macro.no_zone", {std::to_string(z)});
		return false;
	}
	i = static_cast<size_t>(z);
	return true;
}

std::string MacroToolViewModel::textOf(const Zone &z, std::string &error) const
{
	if (!z.fileLines.empty())
	{
		/* the file's lines, with this zone's Overlay and Replace */
		std::vector<std::string> lines = z.fileLines;
		lines[3] = std::string(z.overlay ? "True" : "False") + "," + (z.replace ? "True" : "False");
		std::string text;
		for (const std::string &l : lines) text += l + "\n";
		return text;
	}
	if (z.length <= 0)
	{
		error = tr("macro.zone_empty");
		return {};
	}
	return roll_.macroText(z.start, z.start + z.length - 1, z.columns, z.overlay, z.replace, error);
}

void MacroToolViewModel::changed()
{
	int64_t unsaved = 0;
	for (const Zone &z : zones_) unsaved += z.saved ? 0 : 1;
	unsavedCount.set(unsaved);
	rows_.reset.emit();
}

std::string MacroToolViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "start", "length", "overlay", "replace", "columns", "saved"};
	return column >= 0 && column < 7 ? names[column] : "";
}

Value MacroToolViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Zone &z = vm_.zones_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return z.name;
	case 1: return z.start;
	case 2: return z.length;
	case 3: return z.overlay;
	case 4: return z.replace;
	case 5:
	{
		std::string c;
		for (const std::string &k : z.columns) c += (c.empty() ? "" : "|") + k;
		return c;
	}
	case 6: return z.saved;
	default: return {};
	}
}

} // namespace chimera::vm
