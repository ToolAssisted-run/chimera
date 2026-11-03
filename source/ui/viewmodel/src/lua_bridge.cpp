#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/tr.hpp"
#include "chimera/vm/config_names.hpp"
#include "chimera/vm/json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <type_traits>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace chimera::vm {

int luaChimeraString(lua_State *L);

namespace {
/* __chimera_sleep(milliseconds, exact): the scripts' thread waits, as
 * Chimera's client.sleep makes its own wait; exact waits the last of it
 * out by the clock, as its exactsleep does */
/* __chimera_config_names(): the names client.getconfig gives the window's
 * settings (config_names.hpp) */
int configNames(lua_State *L)
{
	const std::string text = configNamesText();
	lua_pushlstring(L, text.data(), text.size());
	return 1;
}

/* __chimera_file_stamp(path): a number that changes when the file does (its
 * time and size), 0 when there is none: an image drawn with cache false is
 * read again once it changed */
int fileStamp(lua_State *L)
{
	std::error_code ec;
	const std::filesystem::path p = std::filesystem::u8path(luaL_checkstring(L, 1));
	const auto size = std::filesystem::file_size(p, ec);
	if (ec)
	{
		lua_pushinteger(L, 0);
		return 1;
	}
	const auto when = std::filesystem::last_write_time(p, ec).time_since_epoch().count();
	lua_pushinteger(L, static_cast<lua_Integer>((static_cast<uint64_t>(when) * 31 + size) & 0x3FFFFFFF) + 1);
	return 1;
}

int sleepFor(lua_State *L)
{
	const auto ms = static_cast<int64_t>(luaL_checknumber(L, 1));
	const bool exact = lua_toboolean(L, 2) != 0;
	if (ms <= 0) return 0;
	const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
	if (!exact)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
		return 0;
	}
	while (until - std::chrono::steady_clock::now() > std::chrono::milliseconds(100)) std::this_thread::sleep_for(std::chrono::milliseconds(50));
	while (std::chrono::steady_clock::now() < until) std::this_thread::yield();
	return 0;
}
} // namespace

namespace {
const char *const kBridge = "chimera.bridge";
const char *const kModel = "chimera.viewmodel";

std::string utf8(const std::filesystem::path &p)
{
	const std::u8string u = p.u8string();
	return std::string(reinterpret_cast<const char *>(u.data()), u.size());
}

LuaBridge *bridgeOf(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, kBridge);
	auto *b = static_cast<LuaBridge *>(lua_touserdata(L, -1));
	lua_pop(L, 1);
	return b;
}

std::string quoted(const std::string &s)
{
	std::string out = "\"";
	for (const char c : s)
	{
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if (c == '\n') out += "\\n";
		else if (static_cast<unsigned char>(c) < 0x20)
		{
			char buf[8];
			std::snprintf(buf, sizeof buf, "\\u%04x", c);
			out += buf;
		}
		else out += c;
	}
	return out + "\"";
}
} // namespace

template <class F>
auto LuaBridge::onScripts(F &&fn) const -> decltype(fn())
{
	using R = decltype(fn());
	if (!toScripts) return fn();
	if constexpr (std::is_void_v<R>)
	{
		toScripts([&] { fn(); });
	}
	else
	{
		std::optional<R> answer;
		toScripts([&] { answer.emplace(fn()); });
		return answer ? std::move(*answer) : R{};
	}
}

bool LuaBridge::atWindow(const std::function<void()> &fn) const
{
	if (!toWindow)
	{
		fn();
		return true;
	}
	return toWindow(fn);
}

LuaBridge::LuaBridge() : L_(luaL_newstate())
{
	luaL_openlibs(L_);
	lua_pushlightuserdata(L_, this);
	lua_setfield(L_, LUA_REGISTRYINDEX, kBridge);

	/* a view model in Lua: a userdata holding its pointer, read and written by name */
	luaL_newmetatable(L_, kModel);
	lua_pushcfunction(L_, &LuaBridge::index);
	lua_setfield(L_, -2, "__index");
	lua_pushcfunction(L_, &LuaBridge::newIndex);
	lua_setfield(L_, -2, "__newindex");
	lua_pop(L_, 1);

	std::error_code ec;
	home_ = utf8(std::filesystem::current_path(ec));

	lua_newtable(L_);
	lua_setglobal(L_, "chimera");
	lua_pushcfunction(L_, &LuaBridge::write);
	lua_setglobal(L_, "__chimera_write");
	lua_pushcfunction(L_, &LuaBridge::enter);
	lua_setglobal(L_, "__chimera_enter");
	lua_pushcfunction(L_, &LuaBridge::folder);
	lua_setglobal(L_, "__chimera_folder");
	/* bizstring's work (lua_strings.cpp) */
	lua_pushcfunction(L_, &luaChimeraString);
	lua_setglobal(L_, "__chimera_string");
	lua_pushcfunction(L_, &sleepFor);
	lua_setglobal(L_, "__chimera_sleep");
	lua_pushcfunction(L_, &fileStamp);
	lua_setglobal(L_, "__chimera_file_stamp");
	lua_pushcfunction(L_, &configNames);
	lua_setglobal(L_, "__chimera_config_names");
	lua_pushinteger(L_, 0);
	lua_setglobal(L_, "__chimera_script");
	/* Chimera's libraries, a layer of Lua over the view models */
	const char *libraries = luaLibraries();
	if (luaL_loadbuffer(L_, libraries, std::char_traits<char>::length(libraries), "@chimera-libraries") != LUA_OK ||
		lua_pcall(L_, 0, 0, 0) != LUA_OK)
	{
		std::fprintf(stderr, "lua: the libraries did not load: %s\n", lua_tostring(L_, -1) != nullptr ? lua_tostring(L_, -1) : "?");
		lua_pop(L_, 1);
	}
}

LuaBridge::~LuaBridge() { lua_close(L_); }

void LuaBridge::add(ViewModel &vm)
{
	models_.push_back(&vm);
	lua_getglobal(L_, "chimera");
	*static_cast<ViewModel **>(lua_newuserdatauv(L_, sizeof(ViewModel *), 0)) = &vm;
	luaL_setmetatable(L_, kModel);
	lua_setfield(L_, -2, vm.name().c_str());
	lua_pop(L_, 1);
}

void LuaBridge::push(lua_State *L, const Value &v)
{
	if (const bool *b = std::get_if<bool>(&v)) lua_pushboolean(L, *b);
	else if (const int64_t *i = std::get_if<int64_t>(&v)) lua_pushinteger(L, static_cast<lua_Integer>(*i));
	else if (const double *d = std::get_if<double>(&v)) lua_pushnumber(L, *d);
	else if (const std::string *s = std::get_if<std::string>(&v)) lua_pushlstring(L, s->data(), s->size());
	else lua_pushnil(L);
}

bool LuaBridge::read(lua_State *L, int at, Value &out)
{
	switch (lua_type(L, at))
	{
	case LUA_TNIL: out = std::monostate{}; return true;
	case LUA_TBOOLEAN: out = lua_toboolean(L, at) != 0; return true;
	case LUA_TNUMBER:
		if (lua_isinteger(L, at)) out = static_cast<int64_t>(lua_tointeger(L, at));
		else out = static_cast<double>(lua_tonumber(L, at));
		return true;
	case LUA_TSTRING:
	{
		size_t n = 0;
		const char *s = lua_tolstring(L, at, &n);
		out = std::string(s, n);
		return true;
	}
	default: return false;
	}
}

/* Lua reports an error by jumping out of the C function (longjmp), past any
 * C++ object's destructor. So each function below does its C++ work in an
 * inner block, leaves the message on the Lua stack, and raises it only once
 * that block has ended. */

int LuaBridge::index(lua_State *L)
{
	ViewModel *vm = *static_cast<ViewModel **>(luaL_checkudata(L, 1, kModel));
	const char *k = luaL_checkstring(L, 2);
	bool failed = false;
	{
		const std::string key = k;
		const LuaBridge *b = bridgeOf(L);
		const ViewModel::PropertyInfo *property = nullptr;
		for (const auto &p : vm->properties())
			if (p.name == key) property = &p;
		if (property != nullptr)
		{
			/* the machine's own answer where it has one, else the window's */
			Value v;
			if (property->scriptSide && b->toWindow)
				v = property->scriptSide();
			else if (!b->atWindow([&] { v = property->get(); }))
			{
				lua_pushstring(L, tr("lua.window_closing", {vm->name() + "." + key}).c_str());
				failed = true;
			}
			if (!failed) push(L, v);
		}
		else
		{
			bool command = false;
			for (const auto &c : vm->commands()) command = command || c.name == key;
			if (command)
			{
				/* the command as a function, holding its view model and name */
				lua_pushlightuserdata(L, vm);
				lua_pushstring(L, key.c_str());
				lua_pushcclosure(L, &LuaBridge::call, 2);
			}
			else
			{
				lua_pushstring(L, tr("lua.no_member", {vm->name(), key}).c_str());
				failed = true;
			}
		}
	}
	return failed ? lua_error(L) : 1;
}

int LuaBridge::newIndex(lua_State *L)
{
	ViewModel *vm = *static_cast<ViewModel **>(luaL_checkudata(L, 1, kModel));
	const char *k = luaL_checkstring(L, 2);
	bool failed = false;
	{
		const std::string key = k;
		Value v;
		std::string error;
		if (!read(L, 3, v)) error = tr("lua.cannot_be", {vm->name() + "." + key, luaL_typename(L, 3)});
		else if (!bridgeOf(L)->atWindow([&] { vm->set(key, v, error); })) error = tr("lua.window_closing", {vm->name() + "." + key});
		if (!error.empty())
		{
			lua_pushstring(L, error.c_str());
			failed = true;
		}
	}
	return failed ? lua_error(L) : 0;
}

int LuaBridge::call(lua_State *L)
{
	auto *vm = static_cast<ViewModel *>(lua_touserdata(L, lua_upvalueindex(1)));
	const int argc = lua_gettop(L);
	bool failed = false;
	{
		const std::string name = lua_tostring(L, lua_upvalueindex(2));
		Args args;
		std::string error;
		for (int i = 1; i <= argc && error.empty(); i++)
		{
			Value v;
			if (!read(L, i, v))
				error = tr("lua.argument_cannot_be", {vm->name() + "." + name, std::to_string(i), luaL_typename(L, i)});
			args.push_back(std::move(v));
		}
		std::optional<Value> result;
		const LuaBridge *b = bridgeOf(L);
		const ViewModel::CommandInfo *command = nullptr;
		for (const auto &c : vm->commands())
			if (c.name == name) command = &c;
		if (!error.empty())
		{
		}
		else if (command != nullptr && command->scriptSide && b->toWindow)
		{
			/* the machine's own work, done here on its thread */
			Value v = command->run(args, error);
			if (error.empty()) result = std::move(v);
		}
		else if (!b->atWindow([&] { result = vm->run(name, args, error); }))
			error = tr("lua.window_closing", {vm->name() + "." + name});
		if (result) push(L, *result);
		else
		{
			lua_pushstring(L, error.c_str());
			failed = true;
		}
	}
	return failed ? lua_error(L) : 1;
}

/* text as it is written, line ends and all (console.write) */
int LuaBridge::write(lua_State *L)
{
	size_t n = 0;
	const char *s = luaL_checklstring(L, 1, &n);
	bridgeOf(L)->writeText(std::string(s, n));
	return 0;
}

/* __chimera_enter(script): the libraries call a script's function as that script */
int LuaBridge::enter(lua_State *L)
{
	const auto id = static_cast<int64_t>(luaL_checkinteger(L, 1));
	lua_pushinteger(L, static_cast<lua_Integer>(bridgeOf(L)->enterScript(id)));
	return 1;
}

/* __chimera_folder(): the folder of the script whose code runs, where its relative paths start */
int LuaBridge::folder(lua_State *L)
{
	LuaBridge *b = bridgeOf(L);
	const Script *s = b->find(b->current_);
	const std::string f = s != nullptr && !s->directory.empty() ? s->directory : b->home_;
	lua_pushlstring(L, f.data(), f.size());
	return 1;
}

void LuaBridge::log(const std::string &line) { writeText(line + "\n"); }

void LuaBridge::writeText(const std::string &text)
{
	written_++;
	if (onWrite)
	{
		atWindow([&] { onWrite(text); });
		return;
	}
	partial_ += text;
	size_t at = 0;
	std::vector<std::string> lines;
	for (size_t nl = partial_.find('\n'); nl != std::string::npos; nl = partial_.find('\n', at))
	{
		lines.push_back(partial_.substr(at, nl - at));
		at = nl + 1;
	}
	partial_.erase(0, at);
	if (onLog && !lines.empty())
		atWindow([&] {
			for (const std::string &line : lines) onLog(line);
		});
}

int64_t LuaBridge::enterScript(int64_t id)
{
	const int64_t was = current_;
	if (id == was) return was;
	current_ = id;
	lua_pushinteger(L_, static_cast<lua_Integer>(id));
	lua_setglobal(L_, "__chimera_script");
	/* the working folder is the script's while its code runs, as Chimera's
	 * sandbox makes it, so that its dofile, require and io.open find files
	 * beside it */
	const Script *s = find(id);
	const std::filesystem::path folder = std::filesystem::u8path(s != nullptr && !s->directory.empty() ? s->directory : home_);
	std::error_code ec;
	if (!folder.empty() && std::filesystem::current_path(ec) != folder) std::filesystem::current_path(folder, ec);
	return was;
}

LuaBridge::Script *LuaBridge::find(int64_t id)
{
	for (Script &s : scripts_)
		if (s.id == id) return &s;
	return nullptr;
}

int64_t LuaBridge::start(const std::string &path, std::string &error)
{
	return onScripts([&] { return startHere(path, error); });
}

int64_t LuaBridge::startHere(const std::string &path, std::string &error)
{
	std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
	if (!in)
	{
		error = tr("lua.cannot_access", {path});
		return 0;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	const std::filesystem::path p = std::filesystem::absolute(std::filesystem::u8path(path));
	return startCodeHere(ss.str(), utf8(p.filename()), utf8(p.parent_path()), error);
}

int64_t LuaBridge::startCode(const std::string &code, const std::string &chunkName, const std::string &directory, std::string &error)
{
	return onScripts([&] { return startCodeHere(code, chunkName, directory, error); });
}

int64_t LuaBridge::startCodeHere(const std::string &code, const std::string &chunkName, const std::string &directory, std::string &error)
{
	if (luaL_loadbuffer(L_, code.data(), code.size(), ("@" + chunkName).c_str()) != LUA_OK)
	{
		error = lua_tostring(L_, -1) != nullptr ? lua_tostring(L_, -1) : tr("lua.unknown_error");
		lua_pop(L_, 1);
		return 0;
	}
	/* a coroutine of its own: where it yields, it waits for the next frame */
	lua_State *co = lua_newthread(L_);
	lua_insert(L_, -2);
	lua_xmove(L_, co, 1);
	Script s;
	s.id = nextId_++;
	s.directory = directory;
	s.thread = luaL_ref(L_, LUA_REGISTRYINDEX);
	scripts_.push_back(s);
	const int64_t id = s.id;
	const bool ok = resume(id, error);
	published();
	if (!ok)
	{
		stopHere(id);
		return 0;
	}
	const Script *now = find(id);
	if (now != nullptr && (now->stopAfter || (now->thread == LUA_NOREF && !keeps(id)))) stopHere(id);
	return id;
}

bool LuaBridge::resume(int64_t id, std::string &error)
{
	Script *s = find(id);
	if (s == nullptr || s->thread == LUA_NOREF) return true;
	lua_rawgeti(L_, LUA_REGISTRYINDEX, s->thread);
	lua_State *co = lua_tothread(L_, -1);
	const int64_t was = enterScript(id);
	const int64_t outer = resuming_;
	resuming_ = id;
	int results = 0;
	const int status = co != nullptr ? lua_resume(co, L_, 0, &results) : LUA_ERRRUN;
	resuming_ = outer;
	enterScript(was);
	bool ok = true;
	/* what it waits for: emu.frameadvance yields "frame" */
	const bool frameWait = status == LUA_YIELD && results > 0 && lua_type(co, -results) == LUA_TSTRING &&
		std::string_view(lua_tostring(co, -results)) == "frame";
	if (status == LUA_YIELD || status == LUA_OK)
		lua_pop(co, results);
	else
	{
		error = co != nullptr && lua_tostring(co, -1) != nullptr ? lua_tostring(co, -1) : tr("lua.unknown_error");
		ok = false;
	}
	lua_pop(L_, 1);
	/* the script may have been let go while it ran (by its own call) */
	s = find(id);
	if (s != nullptr) s->frameWait = frameWait;
	if (s != nullptr && status != LUA_YIELD)
	{
		luaL_unref(L_, LUA_REGISTRYINDEX, s->thread);
		s->thread = LUA_NOREF;
	}
	return ok;
}

bool LuaBridge::keeps(int64_t id) { return askHere("__chimera_script_keeps", {Value(id)}) == "true"; }

void LuaBridge::published()
{
	/* what a script drew as it started is shown at once */
	callHook("__chimera_frame_end");
}

void LuaBridge::stop(int64_t id)
{
	onScripts([&] { stopHere(id); });
}

void LuaBridge::stopHere(int64_t id)
{
	Script *s = find(id);
	if (s == nullptr) return;
	/* already being stopped (an OnExit function of its own asking again) */
	if (s->state == State::Stopped) return;
	if (resuming_ == id)
	{
		/* its own code asked: it stops once that code waits or ends */
		s->stopAfter = true;
		return;
	}
	s->state = State::Stopped;
	/* its OnExit functions, then all it registered and its forms, as Chimera's */
	askHere("__chimera_script_stopped", {Value(id)});
	s = find(id);
	if (s == nullptr) return;
	if (s->thread != LUA_NOREF) luaL_unref(L_, LUA_REGISTRYINDEX, s->thread);
	scripts_.erase(scripts_.begin() + (s - scripts_.data()));
	if (onStopped) atWindow([&] { onStopped(id); });
}

void LuaBridge::setPaused(int64_t id, bool paused)
{
	onScripts([&] {
		if (Script *s = find(id)) s->state = paused ? State::Paused : State::Running;
	});
}

LuaBridge::State LuaBridge::state(int64_t id) const
{
	return onScripts([&] {
		for (const Script &s : scripts_)
			if (s.id == id) return s.state;
		return State::Stopped;
	});
}

void LuaBridge::stopAll()
{
	onScripts([&] { stopAllHere(); });
}

void LuaBridge::stopAllHere()
{
	std::vector<int64_t> ids;
	for (const Script &s : scripts_) ids.push_back(s.id);
	for (const int64_t id : ids) stopHere(id);
	/* what the scripts left with the libraries goes with them */
	callHook("__chimera_scripts_stopped");
}

bool LuaBridge::running() const
{
	return onScripts([&] { return !scripts_.empty(); });
}

LuaBridge::Seen LuaBridge::seen()
{
	return onScripts([&] {
		Seen s;
		s.running = !scripts_.empty();
		for (const Script &script : scripts_) s.states[script.id] = script.state;
		s.stamp = askHere("__chimera_registered_stamp", {});
		return s;
	});
}

void LuaBridge::consoleClosed()
{
	onScripts([&] {
		callHook("__chimera_console_closed");
		stopAllHere();
	});
}

void LuaBridge::execute(const std::string &command)
{
	onScripts([&] { executeHere(command); });
}

void LuaBridge::executeHere(const std::string &command)
{
	if (command.find("emu.frameadvance(") != std::string::npos)
	{
		log("emu.frameadvance() can not be called from the console");
		return;
	}
	const int64_t was = enterScript(0);
	const int base = lua_gettop(L_);
	/* an expression's values; else a statement, as Chimera's console reads it */
	if (luaL_loadbuffer(L_, ("return " + command).data(), command.size() + 7, "=input") != LUA_OK)
	{
		lua_pop(L_, 1);
		if (luaL_loadbuffer(L_, command.data(), command.size(), "=input") != LUA_OK)
		{
			log(lua_tostring(L_, -1) != nullptr ? lua_tostring(L_, -1) : "unknown error");
			lua_settop(L_, base);
			enterScript(was);
			return;
		}
	}
	const uint64_t before = written_;
	if (lua_pcall(L_, 0, LUA_MULTRET, 0) != LUA_OK)
		log(lua_tostring(L_, -1) != nullptr ? lua_tostring(L_, -1) : "unknown error");
	else
	{
		/* what it gave back, printed as console.log prints it */
		const int n = lua_gettop(L_) - base;
		lua_pushboolean(L_, written_ != before);
		lua_setglobal(L_, "__chimera_console_printed");
		lua_getglobal(L_, "__chimera_console_result");
		if (lua_isfunction(L_, -1))
		{
			lua_insert(L_, base + 1);
			if (lua_pcall(L_, n, 0, 0) != LUA_OK) log(lua_tostring(L_, -1) != nullptr ? lua_tostring(L_, -1) : "?");
		}
	}
	lua_settop(L_, base);
	enterScript(was);
}

namespace {
/* the most tables within tables a value is followed into, as Chimera's */
constexpr int kLuaDepth = 8;

/* the value at `at` as JSON, as Chimera's run_lua says it */
json::Json luaValue(lua_State *L, int at, int depth)
{
	at = lua_absindex(L, at);
	switch (lua_type(L, at))
	{
	case LUA_TNIL:
		return {};
	case LUA_TBOOLEAN:
		return json::Json::boolean_(lua_toboolean(L, at) != 0);
	case LUA_TNUMBER:
	{
		if (lua_isinteger(L, at)) return json::Json::integer(static_cast<long long>(lua_tointeger(L, at)));
		const double d = lua_tonumber(L, at);
		/* a whole one is said as one */
		if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 9e15) return json::Json::integer(static_cast<long long>(d));
		if (!std::isfinite(d)) return json::Json::string(lua_tostring(L, at));
		return json::Json::number(d);
	}
	case LUA_TSTRING:
	{
		size_t n = 0;
		const char *s = lua_tolstring(L, at, &n);
		return json::Json::string(std::string(s, n));
	}
	case LUA_TFUNCTION:
		return json::Json::string("(a function)");
	case LUA_TTABLE:
	{
		if (depth >= kLuaDepth) return json::Json::string(tr("lua.too_deep"));
		/* every key, by its text: Lua keeps a table's keys in no order, and an answer should read the same each time */
		std::vector<std::pair<std::string, json::Json>> fields;
		std::vector<long long> whole;
		bool list = true;
		lua_pushnil(L);
		while (lua_next(L, at) != 0)
		{
			long long k = 0;
			if (lua_type(L, -2) == LUA_TNUMBER)
			{
				if (lua_isinteger(L, -2))
					k = static_cast<long long>(lua_tointeger(L, -2));
				else if (const double d = lua_tonumber(L, -2); d == std::floor(d) && std::fabs(d) < 9e15)
					k = static_cast<long long>(d);
			}
			if (k < 1) list = false;
			whole.push_back(k);
			std::string name;
			if (lua_type(L, -2) == LUA_TNUMBER || lua_type(L, -2) == LUA_TSTRING)
			{
				lua_pushvalue(L, -2);
				size_t n = 0;
				const char *s = lua_tolstring(L, -1, &n);
				name.assign(s, n);
				lua_pop(L, 1);
			}
			else if (lua_type(L, -2) == LUA_TBOOLEAN)
				name = lua_toboolean(L, -2) != 0 ? "true" : "false";
			else
				name = luaL_typename(L, -2);
			fields.emplace_back(std::move(name), luaValue(L, -1, depth + 1));
			lua_pop(L, 1);
		}
		json::Json out;
		if (list && !fields.empty())
		{
			std::vector<size_t> order(fields.size());
			for (size_t i = 0; i < order.size(); i++) order[i] = i;
			std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return whole[a] < whole[b]; });
			for (size_t i = 0; i < order.size() && list; i++) list = whole[order[i]] == static_cast<long long>(i + 1);
			if (list)
			{
				out.kind = json::Json::Kind::Array;
				for (size_t i : order) out.items.push_back(std::move(fields[i].second));
				return out;
			}
		}
		std::stable_sort(fields.begin(), fields.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
		out.kind = json::Json::Kind::Object;
		out.members = std::move(fields);
		return out;
	}
	default:
	{
		size_t n = 0;
		const char *s = luaL_tolstring(L, at, &n);
		json::Json out = json::Json::string(std::string(s, n));
		lua_pop(L, 1);
		return out;
	}
	}
}
} // namespace

bool LuaBridge::runForAgent(const std::string &code, const std::string &directory, std::string &results, std::string &error)
{
	return onScripts([&] { return runForAgentHere(code, directory, results, error); });
}

bool LuaBridge::runForAgentHere(const std::string &code, const std::string &directory, std::string &results, std::string &error)
{
	/* the code as a Lua long string, whose brackets it does not contain; whether it is
	 * an expression is asked of Lua's own load, as Chimera asks */
	std::string level;
	while (code.find("]" + level + "]") != std::string::npos) level += '=';
	/* a line end before the closing brackets: code ending in ']' would close them early */
	const std::string chunk = "local code = [" + level + "[\n" + code + "\n]" + level + "]\n"
		"local run, failed = load('return ' .. code, '=input')\n"
		"if not run then run, failed = load(code, '=input') end\n"
		"if not run then error(failed, 0) end\n"
		"__chimera_run_results = table.pack(run())\n";
	lua_pushboolean(L_, 1);
	lua_setglobal(L_, "__chimera_no_waits");
	const int64_t id = startCodeHere(chunk, "run_lua", directory, error);
	lua_pushnil(L_);
	lua_setglobal(L_, "__chimera_no_waits");
	/* waited all the same (coroutine.yield): nothing would go on with it */
	if (const Script *s = id != 0 ? find(id) : nullptr; s != nullptr && s->thread != LUA_NOREF)
	{
		stopHere(id);
		lua_pushnil(L_);
		lua_setglobal(L_, "__chimera_run_results");
		error = tr("lua.waited_for_frame");
		return false;
	}
	json::Json out;
	out.kind = json::Json::Kind::Array;
	const int base = lua_gettop(L_);
	lua_getglobal(L_, "__chimera_run_results");
	if (id != 0 && lua_istable(L_, -1))
	{
		lua_getfield(L_, -1, "n");
		const auto n = static_cast<int64_t>(lua_tointeger(L_, -1));
		lua_pop(L_, 1);
		for (int64_t i = 1; i <= n; i++)
		{
			lua_rawgeti(L_, -1, static_cast<lua_Integer>(i));
			out.items.push_back(luaValue(L_, -1, 0));
			lua_pop(L_, 1);
		}
	}
	lua_settop(L_, base);
	lua_pushnil(L_);
	lua_setglobal(L_, "__chimera_run_results");
	if (id == 0)
	{
		if (error.empty()) error = tr("lua.unknown_error");
		return false;
	}
	results = json::write(out);
	return true;
}

void LuaBridge::callHook(const char *name)
{
	lua_getglobal(L_, name);
	if (lua_isfunction(L_, -1))
	{
		if (lua_pcall(L_, 0, 0, 0) != LUA_OK)
		{
			log(std::string(name) + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "?"));
			lua_pop(L_, 1);
		}
	}
	else
		lua_pop(L_, 1);
}

void LuaBridge::callHook(const char *name, int64_t arg)
{
	onScripts([&] { askHere(name, {Value(arg)}); });
}

void LuaBridge::notify(const char *name, int64_t arg)
{
	if (!toScriptsLater)
	{
		callHook(name, arg);
		return;
	}
	toScriptsLater([this, name, arg] { askHere(name, {Value(arg)}); });
}

std::string LuaBridge::ask(const char *name, const std::vector<Value> &args)
{
	return onScripts([&] { return askHere(name, args); });
}

std::string LuaBridge::askHere(const char *name, const std::vector<Value> &args)
{
	lua_getglobal(L_, name);
	if (!lua_isfunction(L_, -1))
	{
		lua_pop(L_, 1);
		return {};
	}
	for (const Value &v : args) push(L_, v);
	if (lua_pcall(L_, static_cast<int>(args.size()), 1, 0) != LUA_OK)
	{
		log(std::string(name) + ": " + (lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "?"));
		lua_pop(L_, 1);
		return {};
	}
	std::string out;
	if (lua_type(L_, -1) == LUA_TSTRING || lua_type(L_, -1) == LUA_TNUMBER)
	{
		size_t n = 0;
		const char *s = lua_tolstring(L_, -1, &n);
		out.assign(s, n);
	}
	else if (lua_type(L_, -1) == LUA_TBOOLEAN && lua_toboolean(L_, -1))
		out = "true";
	lua_pop(L_, 1);
	return out;
}

std::string LuaBridge::query(const char *name, int64_t row, const std::string &column)
{
	return onScripts([&] { return askHere(name, {Value(row), Value(column)}); });
}

void LuaBridge::query(std::vector<Cell> &cells)
{
	static const char *const hooks[] = {"__chimera_query_bg", "__chimera_query_text", "__chimera_query_icon"};
	onScripts([&] {
		/* the kinds some script answers: b its colour, t its text, i its icon */
		const std::string kinds = askHere("__chimera_query_kinds", {});
		for (Cell &cell : cells)
		{
			const int kind = std::clamp(cell.kind, 0, 2);
			cell.answer = kinds.find("bti"[kind]) == std::string::npos ? std::string() : askHere(hooks[kind], {Value(cell.row), Value(cell.column)});
		}
	});
}

void LuaBridge::frameStart()
{
	onScripts([&] {
		/* the libraries' frame: what the gui drew last frame goes, and what is drawn now is shown at its end */
		callHook("__chimera_frame_begin");
		begun_ = true;
		callHook("__chimera_frame_start_events");
	});
}

void LuaBridge::frameDone()
{
	onScripts([&] { frameDoneHere(); });
}

void LuaBridge::frameDoneHere()
{
	/* the machine moved without a frame of its own (or the caller has no
	 * before): the frame's start too, as it always was */
	if (!begun_)
	{
		callHook("__chimera_frame_begin");
		callHook("__chimera_frame_start_events");
	}
	begun_ = false;
	callHook("__chimera_frame_end_events");
	resumeRunning(true);
	callHook("__chimera_frame_end");
}

void LuaBridge::inputPoll()
{
	onScripts([&] { callHook("__chimera_input_poll_events"); });
}

bool LuaBridge::yieldTurn()
{
	return onScripts([&] { return resumeRunning(false); });
}

bool LuaBridge::resumeRunning(bool frames)
{
	/* the running scripts go on, each to its next wait (a list taken first:
	 * one that starts or stops another does not change it under this loop) */
	std::vector<int64_t> ids;
	for (const Script &s : scripts_)
		if (s.state == State::Running && (frames || (s.thread != LUA_NOREF && !s.frameWait))) ids.push_back(s.id);
	bool stopped = false;
	for (const int64_t id : ids)
	{
		const Script *s = find(id);
		if (s == nullptr || s->state != State::Running) continue;
		std::string error;
		if (!resume(id, error))
		{
			log("script stopped: " + error);
			stopHere(id);
			stopped = true;
			continue;
		}
		s = find(id);
		if (s != nullptr && (s->stopAfter || (s->thread == LUA_NOREF && !keeps(id))))
		{
			stopHere(id);
			stopped = true;
		}
	}
	return stopped;
}

std::string LuaBridge::describe() const
{
	std::string out = "[";
	for (size_t m = 0; m < models_.size(); m++)
	{
		const ViewModel &vm = *models_[m];
		out += m ? ",\n" : "\n";
		out += " {\"name\": " + quoted(vm.name()) + ", \"properties\": [";
		for (size_t i = 0; i < vm.properties().size(); i++)
		{
			const auto &p = vm.properties()[i];
			out += (i ? ", " : "") + std::string("{\"name\": ") + quoted(p.name) + ", \"help\": " + quoted(p.help) +
				", \"settable\": " + (p.set ? "true" : "false") + "}";
		}
		out += "], \"commands\": [";
		for (size_t i = 0; i < vm.commands().size(); i++)
		{
			const auto &c = vm.commands()[i];
			out += (i ? ", " : "") + std::string("{\"name\": ") + quoted(c.name) + ", \"help\": " + quoted(c.help) + ", \"params\": [";
			for (size_t k = 0; k < c.params.size(); k++) out += (k ? ", " : "") + quoted(c.params[k]);
			out += "]}";
		}
		out += "]}";
	}
	return out + "\n]\n";
}

} // namespace chimera::vm
