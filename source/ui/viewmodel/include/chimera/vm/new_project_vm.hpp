#pragma once
/* New Project: a project made before any machine runs, as Chimera's New
 * Project window makes one. The core is chosen among the installed ones, and
 * for a package of several machines the machine; the game's files go in the
 * slots the package declares (file_slots.json: kinds of file, how many,
 * which slots a choice rules out); the settings that apply are offered at
 * their defaults; the firmware they call for is found by the engine's rules
 * or chosen by hand. What keeps the project from being made is said, a line
 * each, until nothing does. Making it writes the project file and opens it. */

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/tr.hpp"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/suggestion.hpp"
#include "chimera/vm/settings_form.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class NewProjectViewModel : public ViewModel
{
public:
	NewProjectViewModel(Dispatcher &dispatcher, SessionViewModel &session, CoresViewModel &cores);
	~NewProjectViewModel() override;

	/* the installed cores, a line each, and the one chosen */
	Property<std::string> coreNames;
	Property<std::string> core;
	/* Chimera's core list: emulators or game cores ("emulators", "games",
	 * remembered), every package of that kind a line (its version dated, its
	 * systems), and the line chosen (-1: none) */
	Property<std::string> kind{"emulators"};
	Property<std::string> coreLines;
	Property<int64_t> coreLine{-1};
	/* the chosen package's systems, for a package of one machine */
	Property<std::string> systems;
	/* The renderer, asked beside the machine as Chimera asks it: the
	 * machine's renderers a line each, named for what they are ("Software
	 * (Native)", "Hardware (OpenGL)"...; one, unnamed, for a core that
	 * declares none), the one chosen, and what a hardware one costs. */
	Property<std::string> renderers;
	Property<int64_t> renderer{-1};
	Property<std::string> rendererCaveat;
	/* the chosen package's machines, a line each ("" for a package of one),
	 * and the one chosen, by place */
	Property<std::string> machines;
	Property<int64_t> machine{-1};
	Property<std::string> title;
	/* when the machine's clock starts, UTC, as Chimera's wizard writes it
	 * (2017-05-27 12:44:28, the sandbox's own, at first): the same on every
	 * run, so a movie replays; another gives a game that takes its luck from
	 * the date other luck (owner, 2026-10-24, issue 366) */
	Property<std::string> clockStart;
	/* a second as that row shows it, and the second typed there (none when
	 * it is not a time written so, from 1970 to 2200) */
	static std::string clockText(int64_t unixSeconds);
	static std::optional<int64_t> parseClockText(const std::string &text);
	/* what keeps the project from being made, a line each; "" when ready */
	Property<std::string> problems{tr("new_project.choose_core")};
	Property<bool> ready{false};
	Property<std::string> status;
	/* Chimera's wizard, a page at a time: whether the files page and the
	 * firmware page are asked at all (a package with no slots, a machine
	 * needing no firmware, skips them), what keeps the files page from going
	 * on in its words ("Disks: needs at least 1, has 0"), and the firmware
	 * that is required and not provided ("" for none) */
	Property<bool> filesAsked{true};
	Property<std::string> filesComplaint;
	Property<bool> firmwareAsked{false};
	Property<std::string> firmwareMissing;

	/* Importing a movie made elsewhere (the package's movieImport): the
	 * menu item it asks for ("" when the core imports nothing), what the
	 * movie is and the kinds it comes in, the movie picked, the notes the
	 * core's answer had, and whether the project will take its inputs. */
	Property<std::string> importMenu;
	Property<std::string> importMovieLabel;
	Property<std::string> importMovieFilter;
	Property<std::string> importMovie;
	Property<std::string> importNotes;
	Property<bool> imported{false};
	Property<bool> clockWrong{false};
	/* Create makes the project away from the view, since it reads and hashes
	 * every file: whether one is being made, a count that goes up with each
	 * one made (the wizard closes on it, as Chimera's closed once Create had
	 * made the project), and whether the last was refused for its files (the
	 * wizard then shows the files page with the reason, as Chimera's did) */
	Property<bool> making{false};
	Property<int64_t> made{0};
	Property<bool> filesRefused{false};
	/* how the last import went, for Chimera's Import window: why it was not
	 * taken ("" when it was), what the core said of it when it noted
	 * anything (Chimera's message: "<core> read a <format> movie of <n>
	 * frames, and notes: ..."), and a count that goes up with each import
	 * taken, for the window to go on to the wizard */
	Property<std::string> importFailure;
	Property<std::string> importSummary;
	Property<int64_t> importsTaken{0};
	/* Asks the core what a movie amounts to, away from the machine running
	 * (a process holds one machine): package, the movie, the files it reads
	 * (with the slot each goes in, "" for one mounted by name), the import
	 * settings - asked of a project of them (owner, 2026-10-26); done gets the
	 * core's JSON answer or why there is none, on the view's thread. Without
	 * one the engine is asked here, which only works with no machine open. */
	using ImportRunner = std::function<void(const std::string &package, const std::string &movie,
		const std::vector<AskFile> &files, const std::string &settings,
		std::function<void(std::string answer, std::string error)> done)>;
	void setImportRunner(ImportRunner runner) { importRunner_ = std::move(runner); }

	/* What the core suggests for the game chosen, for a core that says it
	 * suggests (its "suggestSettings"): where the values come from, and which
	 * were applied ("" when nothing was asked). Asked once a game, away from
	 * the machine running, as an import is, with the files and settings
	 * chosen so far; the values a game's suggestion set go back when another
	 * game is chosen. The game's own settings come after the package's, and
	 * its own firmware (a package that says "gameFirmware") after the
	 * package's; another game takes them away. */
	Property<std::string> suggestion;
	/* asks the core in a process of its own: the package, the game, the
	 * other files {name, path}, the slot map and the settings so far */
	/* Chimera's compile step (its wizard's last page), for a package that
	 * says "precompile" (RPCS3): the game's code compiled before the project
	 * is made, by compile sessions of the headless runner side by side
	 * (compileRunner: chimera-run), each a share; what they store is the
	 * game's manifest.json in its folder under compileRoot, named by the
	 * game's SHA1, and Create waits until every module it lists is there.
	 * compileAsked: the page applies (such a package, a game chosen);
	 * compileReady: the game's code is all there. */
	Property<bool> compileAsked{false}, compiling{false}, compileReady{false};
	Property<std::string> compileFolder;
	Property<std::string> compileStatus, compileRunner, compileRoot;
	Property<int64_t> compileDone{0}, compileTotal{0};
	/* how much memory one compile session is counted at, in GB: as many run
	 * side by side as the machine can give that much each (Chimera's
	 * CompileSessionGigabytes, issue 259; 8 is what an RPCS3 session needs) */
	Property<int64_t> compileSessionGigabytes{8};
	using SuggestRunner = std::function<void(const std::string &package, const std::string &game,
		const std::vector<std::pair<std::string, std::string>> &files, const std::string &slots, const std::string &settings,
		std::function<void(std::string answer, std::string error)> done)>;
	void setSuggestRunner(SuggestRunner runner) { suggestRunner_ = std::move(runner); }

	/* The MCP server's create_project (issue 355), as Chimera's wizard does
	 * it for an agent (CompleteForAgent): the agent's answers taken through
	 * this form as a person would leave it having changed nothing else, and
	 * the project made and opened. The core's suggestions for the game come
	 * in as they would, then the agent's own settings over them; firmware is
	 * looked for where the form looks, and what is missing is listed rather
	 * than asked for. A core that compiles a game's code first is refused
	 * until that game's code is compiled. */
	struct AgentRequest
	{
		/* the core, by its name or its package's file name */
		std::string core;
		/* the machine, by its id or what it is called; "" for the package's own */
		std::string system;
		/* files by slot, and files with no slot named (each goes in the slot that takes its kind) */
		std::vector<std::pair<std::string, std::string>> files;
		std::vector<std::string> games;
		/* settings by name (a JSON object), over the defaults and suggestions */
		json::Json settings;
		/* firmware files by id, and a folder to look for firmware in besides */
		std::vector<std::pair<std::string, std::string>> firmware;
		std::string firmwareFolder;
	};
	struct AgentOutcome
	{
		/* the project was made and is being opened */
		bool made = false;
		std::string refusal;
		/* when firmware stood in the way: [{id, name, accepted: [{file, size, sha1}]}],
		 * and the folders looked in */
		json::Json missing, searched;
	};
	/* done is told on the view's thread, once */
	void completeForAgent(const AgentRequest &request, std::function<void(AgentOutcome)> done);

private:
	/* completeForAgent after the core's suggestion for the game came in */
	void finishForAgent(const AgentRequest &request, const std::function<void(AgentOutcome)> &done);
	Connection agentWait_;
	/* how a making went, on the view's thread: why the project was not made
	 * ("" when it was) and whether that was for its files; told to whoever
	 * waits for it */
	void madeOrNot(const std::string &why, bool forFiles);
	std::function<void(const std::string &why)> makeTold_;
	struct Picked
	{
		std::string name, path;
	};
	struct Slot
	{
		std::string id, title, help, namePattern, nameHelp;
		int64_t min = 0, max = -1;
		std::vector<std::string> formats;
		bool exposed = true;
		/* shown only when the machine has it: a slot that only settings
		 * open, which the machine chosen closes, is not on the form */
		bool settingGatedOnly = false, machineHas = true;
	};
	struct Need
	{
		std::string id, display, path, how, sha1;
		/* the package's word on it: the file it is, its label and
		 * description, the hash it pins ("" for the person's own dump), and
		 * whether the machine runs without it */
		std::string name, label, description, declSha1;
		bool required = true;
		/* its entry in the package's "firmware" array (FirmwareNeed's) */
		size_t declIndex = 0;
	};

	class SlotRows : public RowModel
	{
	public:
		explicit SlotRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.slots_.size()); }
		int32_t columnCount() const override { return 12; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};
	class ImportFileRows : public RowModel
	{
	public:
		explicit ImportFileRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 6; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};
	class ImportOptionRows : public RowModel
	{
	public:
		explicit ImportOptionRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 3; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};
	class FirmwareRows : public RowModel
	{
	public:
		explicit FirmwareRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.needs_.size()); }
		int32_t columnCount() const override { return 11; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};

	/* the core, by name; the package `preferred` when it is that core's */
	bool chooseCore(const std::string &name, std::string &error, const std::string &preferred = {});
	/* the files as the engine reads a slot map: {"slot": ["name", ...]} */
	std::string slotMap() const;
	const Slot *slot(const std::string &id) const;
	/* the kinds of file a slot takes on the machine chosen: of those a
	 * machine of the package claims, only this machine's (a slot this
	 * machine claims none of is left as declared), as Chimera narrows them */
	std::vector<std::string> formatsFor(const Slot &s) const;
	/* which slots, settings and firmware apply now, and what is missing */
	void evaluate();

	SessionViewModel &session_;
	CoresViewModel &cores_;
	SlotRows slotRows_{*this};
	SettingsForm form_;
	SettingsForm::Rows settingRows_{form_};
	/* the setting rows the views were last told of (issue 417) */
	int64_t settingRowsTold_ = 0;
	SettingsForm::PresetRows presetRows_{form_};
	FirmwareRows firmwareRows_{*this};
	ImportFileRows importFileRows_{*this};
	ImportOptionRows importOptionRows_{*this};
	/* the core's answer, put into the form */
	void applyImport(const std::string &answer);
	const json::Json *importInput(const std::string &option) const;
	ImportRunner importRunner_;
	SuggestRunner suggestRunner_;
	bool suggests_ = false;
	std::string suggestedFor_;
	/* the question asked last, of suggestions and of imports: an answer
	 * carrying another is to a question since withdrawn */
	uint64_t suggestTicket_ = 0, importTicket_ = 0;
	std::vector<std::string> suggestedKeys_;
	/* what the game's suggestion declared of the game's own */
	std::vector<json::Json> gameSettings_;
	json::Json gameFirmware_;
	/* the package's settings, then the game's, into the form */
	void declareSettings();
	/* the package's firmware declarations, then the game's */
	json::Json firmwareDecls() const;
	/* an import or the last project's answers are filling the form: its
	 * settings are the project's own. The core is still asked about that
	 * game - for what the game has of its own, and its note - but what it
	 * would set is not put over them (keptFor_: the game asked so). */
	bool seeding_ = false;
	std::string keptFor_;
	/* The game (and package) the form was seeded with, while what the core
	 * suggests for it may still sit in the settings: another game takes back
	 * every setting that still holds exactly that (Chimera issue 214). */
	std::string seededGame_, seededPackage_;
	void takeBackSeeded(const std::string &answer);
	/* the last game's own settings, by name, and that game: another game
	 * takes their values away, the same game back keeps them */
	std::vector<std::string> ownNames_;
	std::string ownFor_;
	/* asks for the game's suggestion when the game is another than last time */
	void suggestFor(const std::string &game);
	void applySuggestion(const std::string &answer, const std::string &why);
	json::Json import_;
	std::map<std::string, std::vector<std::string>> importPicked_;
	std::map<std::string, bool> importOptions_;
	std::string pendingInput_;

	std::string package_, packageSha1_, version_, slotsText_;
	json::Json cfg_;
	/* the compile step: the modules the game's manifest lists (or the
	 * sessions stored so far), whether each is there, and the run going on */
	struct Module
	{
		std::string name, sha1;
		bool present = false;
	};
	std::vector<Module> modules_;
	class ModuleRows : public RowModel
	{
	public:
		explicit ModuleRows(NewProjectViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.modules_.size()); }
		int32_t columnCount() const override { return 3; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		NewProjectViewModel &vm_;
	};
	ModuleRows moduleRows_{*this};
	struct CompileJob;
	std::shared_ptr<CompileJob> compileJob_;
	/* the game a compile session compiles: the first file picked */
	std::string compileGame() const;
	/* the game's folder of compiled code, "" without a game or a root */
	std::string compileDirOf(const std::string &game) const;
	/* the game's manifest read again, and whether its modules are there */
	void refreshCompile();
	std::string compileSeen_;
	std::vector<Slot> slots_;
	std::vector<std::vector<std::string>> atLeastOneOf_;
	std::map<std::string, std::vector<Picked>> files_;
	std::vector<Need> needs_;
	/* firmware chosen by hand: id -> {path, sha1} */
	std::map<std::string, std::pair<std::string, std::string>> chosenFirmware_;
	/* firmware Clear took away on the firmware page, until the page is shown again */
	std::vector<std::string> clearedFirmware_;
	std::vector<FirmwareFile> firmwareIndex_;
	/* the hashes of files remembered for this core's firmware, read once */
	std::map<std::string, std::string> rememberedSha_;
	Connection coresWatch_, kindWatch_;
	/* the packages the list shows, in its order */
	std::vector<CoresViewModel::Package> shown_;
	void listCores();
	/* the renderer choice, as the machine has it, written into the settings */
	void refreshRenderers();
};

} // namespace chimera::vm
