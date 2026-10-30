#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>

#include <chrono>
#include <ctime>
#include <utility>

#include "chimera/engine.h"

namespace chimera::vm {

namespace {
std::string now()
{
	const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &t);
#else
	localtime_r(&t, &local);
#endif
	char buf[32];
	std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
	return buf;
}
} // namespace

BranchesViewModel::BranchesViewModel(Dispatcher &dispatcher, InputLog &inputs, SessionViewModel &session, PianoRollViewModel &roll)
	: ViewModel("branches", dispatcher), inputs_(inputs), session_(session), roll_(roll)
{
	expose("count", "how many branches the movie has", count);
	expose("status", "what the branches last had to say", status);
	expose("made", "branches the machine has made, their states kept or not", made);
	expose("loads", "branches loaded", loads);
	expose("loadedFromState", "whether the last branch loaded came from its state, not a replay", loadedFromState);
	rows("branches", "one row a branch: name, frame, when it was made, how many frames it has, its state file's name (\"\" for none)", rows_);
	expose("selected", "the branch picked in the list, -1 for none", selected, true);
	expose("undoText", "what undo would take back (\"Undo Branch Load\"...), \"\" for nothing", undoText);
	expose("loadOnDoubleClick", "a double click on a branch loads it", loadOnDoubleClick, true);
	expose("previewSide", "the longest side a branch's picture is shown at, 64 to 4096", previewSide, true);
	expose("previewing", "the branch whose picture is shown, -1 for none", previewing);
	expose("previewWidth", "the width to show the branch's picture at", previewWidth);
	expose("previewHeight", "the height to show the branch's picture at", previewHeight);
	picture("preview", "the picture of the branch the pointer is over", preview, previewChanged);
	command("preview", "shows a branch's picture (-1: none), at no side longer than previewSide", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = -1;
			if (!arg(a, 0, i, error)) return {};
			if (i < 0 || i >= static_cast<int64_t>(branches_.size()) || !branches_[static_cast<size_t>(i)].picture ||
				branches_[static_cast<size_t>(i)].picture->width <= 0)
			{
				previewing.set(-1);
				return false;
			}
			const Picture::Frame &f = *branches_[static_cast<size_t>(i)].picture;
			/* its shape as shown, then shrunk to fit, never grown */
			const double height = f.height, width = f.height * f.aspect();
			const double side = static_cast<double>(std::clamp<int64_t>(previewSide.get(), 64, 4096));
			const double scale = std::min(1.0, side / std::max(width, height));
			previewWidth.set(static_cast<int64_t>(width * scale + 0.5));
			previewHeight.set(static_cast<int64_t>(height * scale + 0.5));
			preview.publish(f.pixels.data(), f.width, f.height, f.shownWidth, f.shownHeight);
			previewing.set(i);
			previewChanged.emit();
			return true;
		});
	previewSideWatch_ = previewSide.changed.connect([this] {
		if (previewSide.get() < 64) previewSide.set(64);
		else if (previewSide.get() > 4096) previewSide.set(4096);
	});
	expose("oldControlScheme", "loading a branch only puts the machine on its frame, the movie left as it is",
		oldControlScheme, true);
	command("jumpTo", "puts the machine on a branch's frame, the movie left as it is", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!index(a, i, error)) return {};
			std::string e;
			session_.run("seek", {branches_[i].frame}, e);
			return {};
		}, &session_.isOpen);
	command("undo", "takes back the last branch load, update, text edit or removal", {}, [this](const Args &, std::string &error) -> Value {
		switch (undo_)
		{
		case Undo::None:
			error = tr("branches.nothing_to_undo");
			return {};
		case Undo::Load:
		{
			/* the run as it was before the load, back */
			const SessionViewModel::Branch b = backup_;
			undo_ = Undo::None;
			undoText.set("");
			session_.loadBranch(b, [this, b](bool, const std::string &) {
				session_.forgetBranch(b);
				status.set(tr("branches.load_canceled"));
			});
			roll_.replaceAll(b.markers);
			return {};
		}
		case Undo::Update:
		case Undo::Text:
			for (size_t i = 0; i < branches_.size(); i++)
				if (branches_[i].id == backup_.id || (undo_ == Undo::Update && static_cast<int64_t>(i) == backupAt_))
				{
					const SessionViewModel::Branch replaced = branches_[i];
					branches_[i] = backup_;
					if (undo_ == Undo::Update && replaced.id != backup_.id) session_.forgetBranch(replaced);
					status.set(tr(undo_ == Undo::Update ? "branches.update_canceled" : "branches.text_edit_canceled"));
					break;
				}
			break;
		case Undo::Remove:
		{
			const int64_t at = std::min<int64_t>(backupAt_, static_cast<int64_t>(branches_.size()));
			branches_.insert(branches_.begin() + at, backup_);
			/* the branch loaded is the one it was: it moved down with the removal */
			if (current.get() >= at) current.set(current.get() + 1);
			previewing.set(-1);
			status.set(tr("branches.removal_canceled"));
			break;
		}
		}
		undo_ = Undo::None;
		undoText.set("");
		changed();
		return {};
	});

	expose("newBranchText", "the text the next branch made is given when none is", newBranchText, true);
	command("list", "every branch, a line each: its id, frame and text, tab apart", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		for (const SessionViewModel::Branch &b : branches_)
			out += std::to_string(b.id) + "\t" + std::to_string(b.frame) + "\t" + b.name + "\n";
		return out;
	});
	command("inputLine", "a branch's movie line for a frame; \"\" past its end", {"index", "frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0, f = 0;
			if (!arg(a, 0, i, error) || !arg(a, 1, f, error)) return {};
			if (i < 0 || i >= static_cast<int64_t>(branches_.size()) || f < 0) return std::string();
			/* the frames' lines are those starting with |, in order */
			int64_t at = 0;
			size_t pos = 0;
			const std::string &text = branches_[static_cast<size_t>(i)].text;
			while (pos < text.size())
			{
				size_t end = text.find('\n', pos);
				if (end == std::string::npos) end = text.size();
				std::string line = text.substr(pos, end - pos);
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (!line.empty() && line[0] == '|' && at++ == f) return line;
				pos = end + 1;
			}
			return std::string();
		});
	command("create", "keeps the movie as it is now, and the frame the machine is on, as a branch", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!a.empty() && !arg(a, 0, name, error)) return {};
			/* a script's text for the next one, used once */
			if (name.empty() && !newBranchText.get().empty())
			{
				name = newBranchText.get();
				newBranchText.set("");
			}
			return keepRun(-1, name);
		}, &session_.isOpen);
	command("saveSlot", "puts the run as it stands into the branch at this place, over what it held (a new branch "
		"when there is none there): Chimera's numbered branch slots", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (i < 0)
			{
				error = tr("branches.no_branch", {std::to_string(i)});
				return {};
			}
			return keepRun(i, "");
		}, &session_.isOpen);
	command("load", "puts a branch's movie back, and the machine on its frame", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!index(a, i, error)) return {};
			if (oldControlScheme.get())
			{
				loadWarning_.clear();
				std::string e;
				session_.run("seek", {branches_[i].frame}, e);
				return {};
			}
			const SessionViewModel::Branch &b = branches_[i];
			current.set(static_cast<int64_t>(i));
			/* its state when it has one, else the engine takes the branch's
			 * movie, keeps the states before the first frame where the two
			 * differ, and replays from there */
			status.set(tr("branches.loading", {b.name}));
			/* the run as it is now, kept so Undo can put it back */
			keepLoadBackup();
			session_.loadBranch(b, [this, name = b.name, i](bool fromState, const std::string &why) {
				status.set(tr(fromState ? "branches.loaded_from_state" : "branches.loaded_replayed", {name}) +
					(why.empty() ? "" : " " + tr("branches.state_not_loaded", {why})) + std::exchange(loadWarning_, std::string()));
				loadedFromState.set(fromState);
				loads.set(loads.get() + 1);
				branchLoaded.emit(static_cast<int64_t>(i));
			});
			roll_.replaceAll(b.markers);
			return {};
		}, &session_.isOpen);
	expose("current", "the current branch: the one last loaded, made or updated (-1 none)", current);
	command("loadWithWarning", "a person loads a branch (a button, a menu, a double click, a hotkey): as load, and when "
		"the current branch is another and the movie holds inputs or markers it does not, the message after the load "
		"says so; Undo Branch Load brings them back (owner, 2026-10-28, issue 395). A script loads with load", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			size_t i = 0;
			if (!index(a, i, error)) return {};
			const int64_t was = current.get();
			if (was >= 0 && was < static_cast<int64_t>(branches_.size()) && was != static_cast<int64_t>(i) &&
				differsFrom(static_cast<size_t>(was)))
				loadWarning_ = tr("branches.load_warning", {std::to_string(was + 1)});
			run("load", {static_cast<int64_t>(i)}, error);
			return {};
		}, &session_.isOpen);
	command("remove", "forgets a branch", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, i, error)) return {};
		status.set(tr("branches.removed", {branches_[i].name}));
		const SessionViewModel::Branch gone = branches_[i];
		branches_.erase(branches_.begin() + static_cast<long>(i));
		if (current.get() == static_cast<int64_t>(i)) current.set(-1);
		else if (current.get() > static_cast<int64_t>(i)) current.set(current.get() - 1);
		previewing.set(-1);
		/* kept, its state file too, for Undo */
		keepBackup(Undo::Remove, gone, static_cast<int64_t>(i));
		if (selected.get() >= static_cast<int64_t>(branches_.size())) selected.set(static_cast<int64_t>(branches_.size()) - 1);
		changed();
		branchRemoved.emit(static_cast<int64_t>(i));
		return {};
	});
	command("swap", "Chimera's branch dragged by its number onto another: the two change places, the branch loaded "
		"staying the one loaded", {"from", "to"}, [this](const Args &a, std::string &error) -> Value {
		size_t from = 0, to = 0;
		if (!index(a, from, error)) return {};
		Args second(a.begin() + 1, a.end());
		if (!index(second, to, error)) return {};
		if (from == to) return {};
		std::swap(branches_[from], branches_[to]);
		const int64_t f = static_cast<int64_t>(from), t = static_cast<int64_t>(to);
		if (current.get() == f) current.set(t);
		else if (current.get() == t) current.set(f);
		if (previewing.get() == f) previewing.set(t);
		else if (previewing.get() == t) previewing.set(f);
		selected.set(t);
		changed();
		return {};
	});
	command("rename", "names a branch again", {"index", "name"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		std::string name;
		if (!index(a, i, error) || !arg(a, 1, name, error)) return {};
		keepBackup(Undo::Text, branches_[i], static_cast<int64_t>(i));
		branches_[i].name = name;
		changed();
		return {};
	});

	loadedWatch_ = session_.branchesLoaded.connect([this](const std::vector<SessionViewModel::Branch> &loaded) {
		/* another project's branches: nothing of this one's to undo, show or ask */
		dropBackup();
		selected.set(-1);
		previewing.set(-1);
		loadWarning_.clear();
		branches_ = loaded;
		current.set(-1);
		for (SessionViewModel::Branch &b : branches_) b.id = ++lastId_;
		count.set(static_cast<int64_t>(branches_.size()));
		rows_.reset.emit();
	});
}

int64_t BranchesViewModel::keepRun(int64_t slot, std::string name)
{
	/* listed at once; the machine says its frame and movie, and keeps its
	 * state, when it gets to it */
	SessionViewModel::Branch b;
	b.time = now();
	b.frame = session_.frame.get();
	b.text = inputs_.text();
	b.frames = inputs_.size();
	b.id = ++lastId_;
	for (const auto &[f, m] : roll_.markers()) b.markers.push_back(m);
	int64_t at = 0;
	if (slot >= 0 && slot < static_cast<int64_t>(branches_.size()))
	{
		/* the slot keeps its name; what it held is kept for Undo, its state
		 * file too, until Undo cannot want it */
		const SessionViewModel::Branch old = branches_[static_cast<size_t>(slot)];
		b.name = name.empty() ? old.name : name;
		branches_[static_cast<size_t>(slot)] = b;
		at = slot;
		keepBackup(Undo::Update, old, slot);
		changed();
	}
	else
	{
		b.name = name.empty() ? tr("branches.default_name", {std::to_string(branches_.size() + 1)}) : name;
		branches_.push_back(b);
		at = static_cast<int64_t>(branches_.size()) - 1;
		changed();
	}
	current.set(at);
	status.set(tr("new_project.making_at", {b.name}));
	session_.makeBranch(b.name, b.time, [this, id = b.id](SessionViewModel::Branch done, const std::string &why) {
		for (SessionViewModel::Branch &x : branches_)
			if (x.id == id)
			{
				x.frame = done.frame;
				x.text = std::move(done.text);
				x.frames = done.frames;
				x.state = done.state;
				x.picture = done.picture;
				changed();
				status.set(!why.empty() ? tr("branches.made_no_state", {x.name, std::to_string(x.frame), why})
						   : x.state.empty() ? tr("branches.made", {x.name, std::to_string(x.frame)})
											 : tr("branches.made_with_state", {x.name, std::to_string(x.frame)}));
				branchSaved.emit(static_cast<int64_t>(&x - branches_.data()));
				break;
			}
		made.set(made.get() + 1);
	});
	return at;
}

bool BranchesViewModel::differsFrom(size_t branch) const
{
	/* the input log, as the engine compares two: identical is -1 */
	ce_movie_log *now = ce_movie_log_new(), *kept = ce_movie_log_new();
	const std::string text = inputs_.text();
	const std::string &theirs = branches_[branch].text;
	ce_movie_log_parse(now, text.data(), text.size());
	ce_movie_log_parse(kept, theirs.data(), theirs.size());
	const bool inputs = ce_movie_log_divergent_point(now, kept) >= 0;
	ce_movie_log_free(now);
	ce_movie_log_free(kept);
	if (inputs) return true;
	/* then the person's markers, by frame and text */
	std::vector<std::pair<int64_t, std::string>> mine, its;
	for (const auto &[frame, m] : roll_.markers()) mine.emplace_back(frame, m.text);
	for (const Marker &m : branches_[branch].markers) its.emplace_back(m.frame, m.text);
	std::sort(mine.begin(), mine.end());
	std::sort(its.begin(), its.end());
	return mine != its;
}

bool BranchesViewModel::listed(uint64_t id) const
{
	for (const SessionViewModel::Branch &b : branches_)
		if (b.id == id) return true;
	return false;
}

void BranchesViewModel::dropBackup()
{
	/* a branch kept only for Undo goes, its state file with it */
	if (undo_ != Undo::None && undo_ != Undo::Text && !listed(backup_.id)) session_.forgetBranch(backup_);
	undo_ = Undo::None;
	undoText.set("");
}

void BranchesViewModel::keepBackup(Undo kind, SessionViewModel::Branch b, int64_t at)
{
	dropBackup();
	undo_ = kind;
	backup_ = std::move(b);
	backupAt_ = at;
	undoText.set(tr(kind == Undo::Load ? "branches.undo_load" : kind == Undo::Update ? "branches.undo_update"
					: kind == Undo::Text ? "branches.undo_text_edit" : "branches.undo_removal"));
}

void BranchesViewModel::keepLoadBackup()
{
	/* as a branch is made, but in no list: the movie, markers and frame, and
	 * the machine's state when it gets to it */
	SessionViewModel::Branch b;
	b.time = now();
	b.name = tr("branches.before_the_load");
	b.frame = session_.frame.get();
	b.text = inputs_.text();
	b.frames = inputs_.size();
	b.id = ++lastId_;
	for (const auto &[f, m] : roll_.markers()) b.markers.push_back(m);
	keepBackup(Undo::Load, b, -1);
	session_.makeBranch(b.name, b.time, [this, id = b.id](SessionViewModel::Branch done, const std::string &) {
		if (undo_ == Undo::Load && backup_.id == id)
		{
			backup_.frame = done.frame;
			backup_.text = std::move(done.text);
			backup_.frames = done.frames;
			backup_.state = done.state;
			backup_.picture = done.picture;
		}
		else
			/* no longer wanted by the time it was made */
			session_.forgetBranch(done);
	});
}

bool BranchesViewModel::index(const Args &a, size_t &out, std::string &error) const
{
	int64_t i = 0;
	if (!arg(a, 0, i, error)) return false;
	if (i < 0 || i >= static_cast<int64_t>(branches_.size()))
	{
		error = tr("branches.no_branch", {std::to_string(i)});
		return false;
	}
	out = static_cast<size_t>(i);
	return true;
}

void BranchesViewModel::changed()
{
	count.set(static_cast<int64_t>(branches_.size()));
	session_.setBranches(branches_);
	session_.markChanged();
	rows_.reset.emit();
}

std::string BranchesViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "frame", "time", "frames", "state"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value BranchesViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const SessionViewModel::Branch &b = vm_.branches_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return b.name;
	case 1: return b.frame;
	case 2: return b.time;
	case 3: return b.frames;
	case 4: return b.state;
	default: return {};
	}
}

} // namespace chimera::vm
