#include "chimera/vm/media_maker_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <filesystem>

#include "chimera/engine.h"

namespace chimera::vm {

namespace {
struct Shape
{
	/* the label and the help are keys in the language tables */
	const char *label, *extension, *help;
};
/* the shapes, in the order the box offers them (Chimera's words) */
const Shape kShapes[] = {
	{"media.zip", ".zip", "media.zip_help"},
	{"media.iso", ".iso", "media.iso_help"},
	{"media.floppy", ".img", "media.floppy_help"},
};

struct Progress
{
	MediaMakerViewModel *vm;
	Dispatcher *dispatcher;
	std::atomic<bool> *cancel;
	/* the view model's later(): nothing posted runs once it is gone */
	std::function<void(std::function<void()>)> post;
};
} // namespace

MediaMakerViewModel::MediaMakerViewModel(Dispatcher &dispatcher, RecipeFinder recipes)
	: ViewModel("mediaMaker", dispatcher), recipes_(std::move(recipes))
{
	formatHelp.set(tr(kShapes[0].help));
	expose("folder", "the folder to pack", folder);
	expose("output", "the file written", output, true);
	expose("format", "0 a zip stored, 1 an ISO 9660 + Joliet disc, 2 a FAT12 floppy", format, true);
	expose("formatHelp", "what the shape chosen is for", formatHelp);
	expose("recognised", "what an installed core's recipe recognised the folder as, \"\" for none", recognised);
	expose("running", "the folder is being packed", running);
	expose("current", "the file being written, and how many of how many", current);
	expose("progress", "how far the pack has got, 0 to 1", progress);
	expose("result", "what came of the last pack", result);
	expose("sha1", "the SHA-1 of the file written last, what a project records", sha1);
	command("shapes", "the shapes offered, a line each", {}, [](const Args &, std::string &) -> Value {
		std::string text;
		for (const Shape &s : kShapes) text += (text.empty() ? "" : "\n") + tr(s.label);
		return text;
	});
	command("setFolder", "the folder to pack; the file is suggested beside it, named after it", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			while (path.size() > 1 && (path.back() == '/' || path.back() == '\\')) path.pop_back();
			folder.set(path);
			packQuestion.set("");
			/* beside the folder, named after it: never inside what is packed */
			if (output.get().empty())
			{
				const std::filesystem::path f(path);
				output.set((f.parent_path() / (f.filename().string() + kShapes[format.get()].extension)).string());
			}
			recognise();
			return {};
		});
	formatWatch_ = format.changed.connect([this] {
		if (format.get() < 0 || format.get() > 2)
		{
			format.set(0);
			return;
		}
		formatHelp.set(tr(kShapes[format.get()].help));
		packQuestion.set("");
		/* the suggested name kept in step with the shape */
		if (!output.get().empty() && !folder.get().empty())
			output.set(std::filesystem::path(output.get()).replace_extension(kShapes[format.get()].extension).string());
		recognise();
	});
	command("setOutput", "the file to write", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		output.set(path);
		packQuestion.set("");
		return {};
	});
	expose("packQuestion", "Make found files that look like a packed game in the folder (issue 637): the question, with their "
		"names; \"\" while none is asked. answerPack answers it", packQuestion);
	command("make", "packs the folder into the file, the recognised recipe's way when there is one. When the folder holds files "
		"that look like a disc image or an archive made earlier, nothing is written and packQuestion asks first: answerPack",
		{}, [this](const Args &, std::string &error) -> Value {
			begin(false, error);
			return {};
		});
	command("answerPack", "answers packQuestion: true packs the folder as it is, false writes nothing", {"go"},
		[this](const Args &a, std::string &error) -> Value {
			bool go = false;
			if (!arg(a, 0, go, error)) return {};
			if (packQuestion.get().empty())
			{
				error = tr("media.no_question");
				return {};
			}
			packQuestion.set("");
			if (go) begin(true, error);
			return {};
		});
	command("cancel", "stops the pack; what it had written is removed", {}, [this](const Args &, std::string &) -> Value {
		cancel_.store(true);
		if (running.get()) current.set(tr("media.stopping"));
		return {};
	});
}

MediaMakerViewModel::~MediaMakerViewModel()
{
	cancel_.store(true);
	if (worker_.joinable()) worker_.join();
}

void MediaMakerViewModel::begin(bool confirmed, std::string &error)
{
	Dispatcher &dispatcher = dispatcher_;
	if (running.get())
	{
		error = tr("media.under_way");
		return;
	}
	if (folder.get().empty() || output.get().empty())
	{
		error = tr("media.choose_first");
		return;
	}
	if (worker_.joinable()) worker_.join();
	packQuestion.set("");
	cancel_.store(false);
	running.set(true);
	result.set("");
	current.set("");
	progress.set(0.0);
	const std::string from = folder.get(), to = output.get(), recipe = recipeJson_;
	const int32_t shape = static_cast<int32_t>(format.get());
	worker_ = std::thread([this, &dispatcher, from, to, recipe, shape, confirmed] {
		/* first, unless the person has said to go on: the files that look like an
		 * earlier image, scanned here so that a big folder does not hold the window */
		if (!confirmed)
		{
			const std::string found = ce_media_earlier_images(from.c_str());
			if (!found.empty())
			{
				later([this, found] {
					running.set(false);
					packQuestion.set(questionText(found));
				});
				return;
			}
		}
		Progress p{this, &dispatcher, &cancel_, [this](std::function<void()> fn) { later(std::move(fn)); }};
		auto onProgress = [](const char *file, uint64_t done, uint64_t total, uint64_t filesDone, uint64_t filesTotal, void *user) -> int32_t {
			auto *pr = static_cast<Progress *>(user);
			const std::string name = file != nullptr ? file : "";
			const double share = total > 0 ? static_cast<double>(done) / static_cast<double>(total) : 0.0;
			MediaMakerViewModel *vm = pr->vm;
			pr->post([vm, name, share, filesDone, filesTotal] {
				vm->current.set(tr("media.progress", {name, std::to_string(filesDone), std::to_string(filesTotal)}));
				vm->progress.set(share);
			});
			return pr->cancel->load() ? 0 : 1;
		};
		const int32_t ok = recipe.empty() ? ce_media_make(from.c_str(), to.c_str(), shape, onProgress, &p)
										  : ce_media_make_with(from.c_str(), to.c_str(), recipe.c_str(), onProgress, &p);
		const std::string sha = ok == 1 && ce_media_last_sha1() != nullptr ? ce_media_last_sha1() : "";
		const char *why = ce_media_last_error();
		const std::string error = why != nullptr ? why : "";
		std::error_code ec;
		const auto size = ok == 1 ? std::filesystem::file_size(to, ec) : 0;
		later([this, ok, sha, error, to, size] {
			running.set(false);
			current.set("");
			sha1.set(sha);
			if (ok == 1)
			{
				progress.set(1.0);
				/* the size as Chimera's N0 writes it: 1,474,560 */
				std::string grouped = std::to_string(size);
				for (int at = static_cast<int>(grouped.size()) - 3; at > 0; at -= 3) grouped.insert(static_cast<size_t>(at), ",");
				result.set(tr("media.written", {to, sha, grouped}));
			}
			else
				result.set(error == "cancelled" ? tr("media.cancelled") : error);
		});
	});
}

std::string MediaMakerViewModel::questionText(const std::string &lines)
{
	/* the first names, and how many more */
	constexpr size_t kShown = 5;
	std::string shown;
	size_t count = 0, at = 0;
	while (at <= lines.size())
	{
		size_t end = lines.find('\n', at);
		if (end == std::string::npos) end = lines.size();
		if (end > at && ++count <= kShown) shown += (shown.empty() ? "" : "\n") + lines.substr(at, end - at);
		at = end + 1;
	}
	if (count > kShown) shown += "\n" + tr("media.earlier_images_more", {std::to_string(count - kShown)});
	return tr("media.earlier_images_question", {shown});
}

void MediaMakerViewModel::recognise()
{
	/* the first installed core's recipe that writes this shape and is for this folder */
	recipeJson_.clear();
	recognised.set("");
	if (folder.get().empty() || !recipes_) return;
	for (const Recipe &r : recipes_())
	{
		if (ce_media_recipe_format(r.json.c_str()) != format.get()) continue;
		if (ce_media_recipe_applies(r.json.c_str(), folder.get().c_str()) != 1) continue;
		recipeJson_ = r.json;
		recognised.set(tr("media.recognised", {r.label, r.core}));
		return;
	}
}

} // namespace chimera::vm
