#include "chimera/vm/lua_bridge.hpp"

namespace chimera::vm {

/* Chimera's Lua libraries, as a script written for Chimera calls them, over
 * the view models (chimera.<name>): the names, arguments and answers are
 * Chimera's, the work is the view models'. Tables Chimera numbers from 0
 * are numbered from 0 here. What the window cannot answer is said where it
 * is asked. */
const char *LuaBridge::luaLibraries()
{
	return R"LUA(
local C = chimera

local function lines(text)
	local out = {}
	for line in string.gmatch(text or "", "[^\n]+") do out[#out + 1] = line end
	return out
end

-- functions a script registers (event.onframeend, tastudio.onbranchload,
-- ...): each the script's that was running, with an event, a name and an id,
-- as Chimera's; called as that script, and gone when it stops --------------
local registered = {}
local onRemoved = {}
__chimera_registered_version = 0
local function newGuid()
	return string.format("%08x-%04x-4%03x-%04x-%012x", math.random(0, 0xFFFFFFFF), math.random(0, 0xFFFF),
		math.random(0, 0xFFF), 0x8000 | math.random(0, 0x3FFF), math.random(0, 0xFFFFFFFFFFFF))
end
local function registeredChanged() __chimera_registered_version = __chimera_registered_version + 1 end
local function registerNamed(fn, event, name)
	if type(fn) ~= "function" then error("a function is needed, not " .. type(fn), 3) end
	local f = {fn = fn, event = event, name = name ~= nil and tostring(name) or "Anonymous", guid = newGuid(), script = __chimera_script}
	registered[#registered + 1] = f
	registeredChanged()
	return f
end
-- those `which` says, or only the first of them
local function removeRegistered(which, firstOnly)
	local kept, gone = {}, false
	for _, f in ipairs(registered) do
		if which(f) and not (firstOnly and gone) then gone = true else kept[#kept + 1] = f end
	end
	registered = kept
	if gone then
		registeredChanged()
		for _, fn in ipairs(onRemoved) do fn() end
	end
	return gone
end
-- one called as its script; an error is said and the function kept, as Chimera's
local function callNamed(f, ...)
	local was = __chimera_enter(f.script)
	local results = table.pack(pcall(f.fn, ...))
	__chimera_enter(was)
	if not results[1] then
		print("error running function attached by the event " .. f.event .. "\nError message: " .. tostring(results[2]))
		return nil
	end
	return table.unpack(results, 2, results.n)
end
local function callEvent(event, ...)
	local list = {}
	for _, f in ipairs(registered) do if f.event == event then list[#list + 1] = f end end
	for _, f in ipairs(list) do callNamed(f, ...) end
end
-- the bridge's: a frame's functions; the console closing; a script stopped
-- (its OnExit functions called, then all of its gone); whether a script whose
-- code has ended still has something to run
function __chimera_frame_start_events() callEvent("OnFrameStart") end
function __chimera_frame_end_events() callEvent("OnFrameEnd") end
function __chimera_console_closed() callEvent("OnConsoleClose") end
-- a read of the controls the core reported, inside its frame: the functions
-- may read memory, but not wait for a frame
function __chimera_input_poll_events()
	local was = __chimera_no_waits
	__chimera_no_waits = true
	callEvent("OnInputPoll")
	__chimera_no_waits = was
end
function __chimera_script_stopped(id)
	local exits = {}
	for _, f in ipairs(registered) do if f.script == id and f.event == "OnExit" then exits[#exits + 1] = f end end
	for _, f in ipairs(exits) do callNamed(f) end
	removeRegistered(function(f) return f.script == id end)
	if __chimera_forms_stopped then __chimera_forms_stopped(id) end
end
function __chimera_script_keeps(id)
	for _, f in ipairs(registered) do if f.script == id and f.event ~= "OnExit" then return true end end
	return __chimera_forms_open ~= nil and __chimera_forms_open(id)
end
-- the Registered Functions window's: a line each (event, name, id, script);
-- one called or removed by its id; those of a script, or all, removed
function __chimera_registered_stamp() return __chimera_registered_version end
function __chimera_registered_list()
	local out = {}
	for _, f in ipairs(registered) do out[#out + 1] = f.event .. "\t" .. f.name .. "\t" .. f.guid .. "\t" .. f.script end
	return table.concat(out, "\n")
end
function __chimera_registered_call(guid)
	for _, f in ipairs(registered) do if f.guid == guid then callNamed(f) return true end end
	return false
end
function __chimera_registered_remove(guid) return removeRegistered(function(f) return f.guid == guid end) end
function __chimera_registered_clear(script) removeRegistered(function(f) return script == nil or f.script == script end) end
-- fn after every frame
function C.onFrame(fn) return registerNamed(fn, "OnFrameEnd").guid end

-- console ------------------------------------------------------------------
console = {}
-- a value as Chimera's console writes it: a table shallowly, a line a key
-- (numbers first, then names, then the rest), names in quotes
-- a single value as Chimera's console shows it: True and False, 5 for a
-- whole double, 15 significant digits, 1E+21, NaN and Infinity
local function plain(v)
	if type(v) == "boolean" then return v and "True" or "False" end
	if math.type(v) == "float" then
		if v ~= v then return "NaN" end
		if v == math.huge then return "Infinity" end
		if v == -math.huge then return "-Infinity" end
		return (string.format("%.15g", v):gsub("e", "E"))
	end
	return tostring(v)
end
local function shown(v)
	if type(v) ~= "table" then return v == nil and "nil" or plain(v) end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	local function rank(k) return type(k) == "number" and 0 or type(k) == "string" and 1 or type(k) == "boolean" and 2 or 3 end
	table.sort(keys, function(a, b)
		if rank(a) ~= rank(b) then return rank(a) < rank(b) end
		if rank(a) == 0 or rank(a) == 1 then return a < b end
		return tostring(a) < tostring(b)
	end)
	local out = {}
	for _, k in ipairs(keys) do
		local function one(x) return type(x) == "string" and ('"' .. x .. '"') or (type(x) == "table" and "table" or plain(x)) end
		out[#out + 1] = one(k) .. ": " .. one(v[k]) .. "\n"
	end
	return table.concat(out)
end
local function logWith(separator, terminator, ...)
	local n = select("#", ...)
	if n == 0 or (n == 1 and (...) == nil) then __chimera_write("(no return)" .. terminator) return end
	local t = {}
	for i = 1, n do t[i] = shown((select(i, ...))) end
	__chimera_write(table.concat(t, separator) .. terminator)
end
function console.log(...) logWith("\t", "\n", ...) end
function console.writeline(...) logWith("\n", "\n", ...) end
function console.write(...) logWith("", "", ...) end
function console.clear() if C.luaConsole then C.luaConsole.clear() end end
-- print is console.log, as Chimera's
print = console.log
-- what a line typed into the console gave back: printed unless it gave
-- nothing and printed something itself, as Chimera's
function __chimera_console_result(...)
	local n = select("#", ...)
	if n == 0 or (n == 1 and (...) == nil) then
		if __chimera_console_printed then return end
	end
	console.log(...)
end

-- bit (Chimera's, over Lua's own operators) ---------------------------------
bit = {}
-- 32 bits, as Chimera's (unsigned): a shift counts modulo 32
function bit.band(a, b) return a & b & 0xFFFFFFFF end
function bit.bor(a, b) return (a | b) & 0xFFFFFFFF end
function bit.bxor(a, b) return (a ~ b) & 0xFFFFFFFF end
function bit.bnot(a) return ~a & 0xFFFFFFFF end
function bit.lshift(a, n) return ((a & 0xFFFFFFFF) << (n & 31)) & 0xFFFFFFFF end
function bit.rshift(a, n) return (a & 0xFFFFFFFF) >> (n & 31) end
function bit.arshift(a, n) local v = a & 0xFFFFFFFF if v >= 0x80000000 then v = v - 0x100000000 end return v >> n | (v < 0 and ~(0xFFFFFFFF >> n) or 0) end
function bit.rol(a, n) a = a & 0xFFFFFFFF n = n % 32 return ((a << n) | (a >> (32 - n))) & 0xFFFFFFFF end
function bit.ror(a, n) a = a & 0xFFFFFFFF n = n % 32 return ((a >> n) | (a << (32 - n))) & 0xFFFFFFFF end
function bit.check(a, pos) return (a >> pos) & 1 == 1 end
function bit.set(a, pos) return a | (1 << pos) end
function bit.clear(a, pos) return a & ~(1 << pos) end
function bit.byteswap_16(a) return ((a & 0xFF) << 8) | ((a >> 8) & 0xFF) end
function bit.byteswap_32(a) return string.unpack("<I4", string.pack(">I4", a & 0xFFFFFFFF)) end
function bit.byteswap_64(a) return string.unpack("<i8", string.pack(">i8", math.tointeger(a) or 0)) end

-- bizstring: Chimera's string methods: positions, lengths and widths in
-- UTF-16 units, encodings by their usual names (lua_strings.cpp)
bizstring = {}
local function wholeNumber(n) return math.tointeger(n) or math.floor(tonumber(n) + 0.5) end
function bizstring.decode(bytes, encoding) return __chimera_string("decode", bytes, encoding or "utf-8") end
function bizstring.encode(str, encoding) return __chimera_string("encode", str, encoding or "utf-8") end
-- at least two digits, as Chimera's
function bizstring.hex(num)
	local h = string.format("%X", wholeNumber(num))
	if #h == 1 then h = "0" .. h end
	return h
end
-- no leading zeros: "" for 0, as Chimera's
function bizstring.binary(num)
	local n, bits = wholeNumber(num), {}
	for i = 63, 0, -1 do bits[#bits + 1] = (n >> i) & 1 == 1 and "1" or "0" end
	return (string.gsub(table.concat(bits), "^0+", ""))
end
function bizstring.octal(num)
	local o = string.format("%o", wholeNumber(num))
	if #o == 1 then o = "0" .. o end
	return o
end
for _, name in ipairs({"fold", "pad_end", "pad_start", "trim", "replace", "toupper", "tolower", "substring", "remove",
		"contains", "startswith", "endswith", "split"}) do
	bizstring[name] = function(...) return __chimera_string(name, ...) end
end

-- emu ----------------------------------------------------------------------
emu = {}
-- a number written as a whole one: a fraction cut off toward zero (3.5 is
-- 3); Lua's & refused it
local function whole(v)
	if math.type(v) == "float" then return math.tointeger(v >= 0 and math.floor(v) or math.ceil(v)) or 0 end
	return v
end
-- frameadvance: the script waits here and goes on after the next frame;
-- yield: it goes on at the window's next turn, a frame run or not, as
-- Chimera's. The MCP server's run_lua code may not wait (__chimera_no_waits),
-- as Chimera's refuses it
-- (an event callback cannot wait either: it was skipped there, and Chimera says so)
function emu.frameadvance()
	if __chimera_no_waits or not coroutine.isyieldable() then error("emu.frameadvance() is not allowed during any callbacks", 2) end
	coroutine.yield("frame")
end
function emu.yield()
	if __chimera_no_waits or not coroutine.isyieldable() then error("emu.yield() is not allowed during any callbacks", 2) end
	coroutine.yield()
end
-- the machine's own count, now: the frame property follows it a moment later
function emu.framecount() return C.session.frameNow() end
function emu.lagcount() return C.session.lagCount end
function emu.islagged() return C.session.lagFrame end
-- the frame just run was a lag frame or not (true when not said), as its row in TAStudio says
function emu.setislagged(value) C.session.setIsLagged(value == nil or value and true or false) end
function emu.setlagcount(count) C.session.setLagCount(wholeNumber(count)) end
-- the machine's id, as Chimera's (the game's system); "NULL" with none open
function emu.getsystemid()
	local id = C.session.systemId
	return (id == nil or id == "") and "NULL" or id
end
function emu.getcorename() return C.session.coreName end
function emu.getboardname() return "" end
function emu.getdisplaytype() return "" end
function emu.getregisters()
	local out = {}
	for _, line in ipairs(lines(C.registers.registerList())) do
		local name, value = string.match(line, "^(.-)=(%-?%d+)$")
		if name then out[name] = math.tointeger(tonumber(value)) end
	end
	return out
end
-- 0 for a register the machine does not have, as Chimera's
function emu.getregister(name) return emu.getregisters()[name] or 0 end
function emu.setregister(name, value) C.registers.setRegisterNamed(name, value) end
function emu.totalexecutedcycles() C.registers.refresh() return C.registers.cycles end
function emu.limitframerate(on) C.session.unthrottled = not on end
function emu.displayvsync() end
function emu.setrenderplanes() print("SetRenderPlanes is not supported by this build (no core-specific handling available)") end
function emu.disassemble() error("emu.disassemble: the cores here offer no disassembly") end

-- memory and mainmemory ----------------------------------------------------
local function domainList()
	local out = {}
	for _, line in ipairs(lines(C.session.memoryDomains())) do
		local name, size, writable = string.match(line, "^(.-)\t(%d+)\t(%d)$")
		if name then out[#out + 1] = {name = name, size = math.tointeger(tonumber(size)), writable = writable == "1"} end
	end
	return out
end
local current = nil
local function mainDomain() local d = domainList() return d[1] and d[1].name or "" end
-- Chimera's default for memory: the system bus when there is one, else the main memory
local function currentDomain()
	if current then return current end
	for _, d in ipairs(domainList()) do if d.name == "System Bus" then return d.name end end
	return mainDomain()
end
local function makeMemory(domainOf)
	local m = {}
	local function rd(addr, n, domain) return C.session.readMemory(domain or domainOf(), addr, n) end
	local function wr(addr, bytes, domain) C.session.writeMemory(domain or domainOf(), addr, bytes) end
	function m.readbyte(addr, domain) return string.byte(rd(addr, 1, domain)) end
	function m.writebyte(addr, value, domain) wr(addr, string.char(value & 0xFF), domain) end
	function m.readbyterange(addr, length, domain)
		local s, t = rd(addr, length, domain), {}
		for i = 1, #s do t[i - 1] = string.byte(s, i) end
		return t
	end
	function m.read_bytes_as_array(addr, length, domain)
		local s, t = rd(addr, length, domain), {}
		for i = 1, #s do t[i] = string.byte(s, i) end
		return t
	end
	function m.read_bytes_as_dict(addr, length, domain)
		local s, t = rd(addr, length, domain), {}
		for i = 1, #s do t[addr + i - 1] = string.byte(s, i) end
		return t
	end
	function m.read_bytes_as_binary_string(addr, length, domain) return rd(addr, length, domain) end
	-- the SHA-256 of a stretch, upper-case hex, as Chimera's; outside the domain is an error
	function m.hash_region(addr, count, domain)
		local d = domain or domainOf()
		local size = memory.getmemorydomainsize(d)
		if addr < 0 or addr >= size then error("Address " .. addr .. " is outside the bounds of domain " .. d, 2) end
		if addr + count > size then error("Address " .. addr .. " + count " .. count .. " is outside the bounds of domain " .. d, 2) end
		return C.session.sha256(count == 0 and "" or rd(addr, count, d))
	end
	function m.writebyterange(t, domain)
		for addr, value in pairs(t) do wr(addr, string.char(value & 0xFF), domain) end
	end
	function m.write_bytes_as_array(addr, t, domain)
		local b = {}
		for i = 1, #t do b[i] = string.char(t[i] & 0xFF) end
		wr(addr, table.concat(b), domain)
	end
	function m.write_bytes_as_dict(t, domain) m.writebyterange(t, domain) end
	function m.write_bytes_as_binary_string(addr, s, domain) wr(addr, s, domain) end
	-- the sized reads and writes, each sign and order Chimera has
	for _, size in ipairs({1, 2, 3, 4}) do
		for _, signed in ipairs({false, true}) do
			local letter = signed and "i" or "I"
			local stem = (signed and "s" or "u") .. (size * 8)
			if size == 1 then
				m["read_" .. stem] = function(addr, domain) return (string.unpack("<" .. letter .. "1", rd(addr, 1, domain))) end
				m["write_" .. stem] = function(addr, value, domain) wr(addr, string.pack("<I1", whole(value) & 0xFF), domain) end
			else
				for _, order in ipairs({{"_le", "<"}, {"_be", ">"}}) do
					local fmt = order[2] .. letter .. size
					local mask = (1 << (size * 8)) - 1
					m["read_" .. stem .. order[1]] = function(addr, domain) return (string.unpack(fmt, rd(addr, size, domain))) end
					m["write_" .. stem .. order[1]] = function(addr, value, domain)
						wr(addr, string.pack(order[2] .. "I" .. size, whole(value) & mask), domain)
					end
				end
			end
		end
	end
	function m.readfloat(addr, bigendian, domain) return (string.unpack(bigendian and ">f" or "<f", rd(addr, 4, domain))) end
	function m.writefloat(addr, value, bigendian, domain) wr(addr, string.pack(bigendian and ">f" or "<f", value), domain) end
	return m
end
memory = makeMemory(currentDomain)
function memory.getmemorydomainlist()
	local t = {}
	for i, d in ipairs(domainList()) do t[i - 1] = d.name end
	return t
end
function memory.getmemorydomainsize(name)
	local want = (name == nil or name == "") and currentDomain() or name
	for _, d in ipairs(domainList()) do if d.name == want then return d.size end end
	for _, d in ipairs(domainList()) do if d.name == currentDomain() then return d.size end end
	return 0
end
function memory.getcurrentmemorydomain() return currentDomain() end
function memory.getcurrentmemorydomainsize() return memory.getmemorydomainsize() end
function memory.usememorydomain(name)
	for _, d in ipairs(domainList()) do
		if d.name == name then current = name return true end
	end
	return false
end
mainmemory = makeMemory(mainDomain)
function mainmemory.getname() return mainDomain() end
function mainmemory.getcurrentmemorydomainsize() return memory.getmemorydomainsize(mainDomain()) end

-- memorysavestate: the core's own state in memory, by an id ----------------
memorysavestate = {}
function memorysavestate.savecorestate() return C.session.saveCoreState() end
function memorysavestate.loadcorestate(id) C.session.loadCoreState(tostring(id)) end
function memorysavestate.removestate(id) C.session.removeState(tostring(id)) end
function memorysavestate.clearstatesfrommemory() C.session.clearStates() end

-- userdata: values a script keeps for this session, as Chimera's (in memory,
-- shared by every script; a boolean, a number or a string each)
userdata = {}
local bag = {}
function userdata.set(name, value)
	local t = type(value)
	if t ~= "nil" and t ~= "boolean" and t ~= "number" and t ~= "string" then error("Invalid type for userdata", 2) end
	bag[tostring(name)] = value
end
function userdata.get(key) return bag[tostring(key)] end
function userdata.clear() bag = {} end
function userdata.remove(key)
	key = tostring(key)
	if bag[key] == nil then return false end
	bag[key] = nil
	return true
end
function userdata.containskey(key) return bag[tostring(key)] ~= nil end
-- a --userdata pair from the command line: true/false a boolean, a 32-bit
-- whole number or a number as one, else the text, as Chimera reads them
function __chimera_userdata_pair(k, v)
	local n = tonumber(v)
	if v == "true" then bag[k] = true
	elseif v == "false" then bag[k] = false
	elseif math.tointeger(n) and n >= -2147483648 and n <= 2147483647 then bag[k] = math.tointeger(n)
	elseif n ~= nil then bag[k] = n + 0.0
	else bag[k] = v end
end
function userdata.get_keys()
	local keys = {}
	for k in pairs(bag) do keys[#keys + 1] = k end
	table.sort(keys)
	return keys
end

-- game: a game core's properties by name (docs/game-cores.md) -------------
game = {}
local function describeOf(name)
	local text = C.game.describe(tostring(name))
	if text == "" then return nil end
	local d = {}
	for k, v in string.gmatch(text, "([^\t\n]+)\t([^\n]*)") do d[k] = v end
	return d
end
local function bytesTable(s)
	local t = {}
	for i = 1, #s do t[i] = string.byte(s, i) end
	return t
end
local function getOne(name)
	local kind = C.game.kind(name)
	local v = C.game.get(name)
	if C.game.lastError ~= "" then console.log("game.get: " .. C.game.lastError) return nil end
	if kind == "bytes" then return bytesTable(v) end
	return v
end
-- an array by its own name: its elements, from 1 as Lua counts
local function arrayOf(name)
	if string.find(name, "%]%s*$") then return nil end
	local d = describeOf(name)
	if d == nil or tonumber(d.count) == nil or tonumber(d.count) <= 1 then return nil end
	return tonumber(d.first), tonumber(d.count)
end
function game.list()
	local names = {}
	for n in string.gmatch(C.game.list(), "[^\n]+") do names[#names + 1] = n end
	return names
end
function game.get(name)
	name = tostring(name)
	local first, count = arrayOf(name)
	if first ~= nil then
		local t = {}
		for i = 0, count - 1 do t[i + 1] = getOne(name .. "[" .. (first + i) .. "]") end
		return t
	end
	return getOne(name)
end
local function setOne(name, value)
	local ok
	if type(value) == "table" then
		local parts = {}
		for i = 1, #value do parts[i] = string.char(value[i] & 0xFF) end
		ok = C.game.set(name, table.concat(parts), true)
	else
		ok = C.game.set(name, value, false)
	end
	if not ok then console.log("game.set: " .. C.game.lastError) end
	return ok
end
function game.set(name, value)
	name = tostring(name)
	local first, count = arrayOf(name)
	if first ~= nil and C.game.kind(name) ~= "bytes" then
		if type(value) ~= "table" then
			console.log("game.set: \"" .. name .. "\" is an array of " .. count .. ", so it takes a table of them")
			return false
		end
		local all = true
		for i = 0, count - 1 do
			if value[i + 1] ~= nil then all = setOne(name .. "[" .. (first + i) .. "]", value[i + 1]) and all end
		end
		return all
	end
	return setOne(name, value)
end
function game.describe(name)
	local d = describeOf(name)
	if d == nil then return nil end
	for _, k in ipairs({"offset", "size", "count", "first", "stride", "bit", "bits"}) do d[k] = tonumber(d[k]) end
	d.writable = d.writable == "true"
	return d
end

-- tastudio: the project's TAStudio, as Chimera's library has it -----------
tastudio = {}
local function engaged() return C.session.hasProject end
function tastudio.engaged() return engaged() end
function tastudio.getrecording() return engaged() and C.session.recordingMode end
function tastudio.setrecording(on)
	if engaged() and C.session.recordingMode ~= (on == true) then C.session.toggleRecording() end
end
function tastudio.togglerecording() if engaged() then C.session.toggleRecording() end end
function tastudio.islag(frame)
	if not engaged() then return nil end
	local v = C.session.lagAt(frame)
	if v < 0 then return nil end
	return v == 1
end
function tastudio.setlag(frame, value)
	if engaged() then C.session.setLag(frame, value == nil and -1 or (value and 1 or 0)) end
end
function tastudio.hasstate(frame) return engaged() and C.session.hasState(frame) or false end
function tastudio.cleargreenzone() if engaged() then C.session.clearGreenzone() end end
-- a frame, or a marker by its text
function tastudio.setplayback(where)
	if not engaged() then return end
	local frame = where
	if type(where) == "string" then
		frame = nil
		for _, f in ipairs(tastudio.get_frames_with_markers()) do
			if C.pianoRoll.markerAt(f) == where then frame = f break end
		end
		if frame == nil then return end
	end
	frame = math.tointeger(frame) or math.floor(frame)
	if frame >= 0 then C.session.seek(frame) end
end
-- a seek here is the engine's, done before the call returns
function tastudio.getseekframe() return engaged() and C.session.frame or 0 end
-- the frames picked, from 0
function tastudio.getselection()
	local t, first, last = {}, C.pianoRoll.selectFirst, C.pianoRoll.selectLast
	if engaged() and first >= 0 then for f = first, last do t[f - first] = f end end
	return t
end
-- the edits a script queues, applied together (shared by every script, as Chimera's)
local queued = {}
local function indexIn(list, name)
	for i, n in ipairs(lines(list)) do if n == name then return i - 1 end end
	return nil
end
function tastudio.submitinputchange(frame, button, value)
	if engaged() and frame >= 0 then queued[#queued + 1] = {k = "button", frame = frame, name = button, value = value == true} end
end
function tastudio.submitanalogchange(frame, axis, value)
	if engaged() and frame >= 0 then queued[#queued + 1] = {k = "axis", frame = frame, name = axis, value = math.floor(value)} end
end
local function inMovie(frame, n) return engaged() and frame >= 0 and frame < C.pianoRoll.frameCount and n > 0 end
function tastudio.submitinsertframes(frame, n) if inMovie(frame, n) then queued[#queued + 1] = {k = "insert", frame = frame, n = n} end end
function tastudio.submitdeleteframes(frame, n) if inMovie(frame, n) then queued[#queued + 1] = {k = "delete", frame = frame, n = n} end end
function tastudio.submitclearframes(frame, n) if inMovie(frame, n) then queued[#queued + 1] = {k = "clear", frame = frame, n = n} end end
function tastudio.applyinputchanges()
	if #queued == 0 or not engaged() then return end
	local changes = queued
	queued = {}
	-- a script's edits leave recording mode on, as Chimera's
	C.pianoRoll.scriptEditing = true
	local ok, err = pcall(function()
		for _, c in ipairs(changes) do
			if c.k == "button" then
				local b = indexIn(C.session.buttonList(), c.name)
				if b ~= nil then C.pianoRoll.paint(c.frame, c.frame, b, c.value) end
			elseif c.k == "axis" then
				local x = indexIn(C.session.axisList(), c.name)
				if x ~= nil then C.pianoRoll.setAxis(c.frame, x, c.value) end
			elseif c.k == "insert" then
				C.pianoRoll.insertFrames(c.frame, c.n)
			else
				local last = math.min(C.pianoRoll.frameCount, c.frame + c.n) - 1
				if last >= c.frame then
					if c.k == "delete" then C.pianoRoll.deleteFrames(c.frame, last) else C.pianoRoll.clearFrames(c.frame, last) end
				end
			end
		end
	end)
	C.pianoRoll.scriptEditing = false
	if not ok then error(err, 0) end
end
function tastudio.clearinputchanges() queued = {} end
function tastudio.setbranchtext(text, index)
	if not engaged() then return end
	if index ~= nil then C.branches.rename(index, tostring(text)) else C.branches.newBranchText = tostring(text) end
end
local function branchList()
	local list = {}
	for _, line in ipairs(lines(C.branches.list())) do
		local id, frame, text = string.match(line, "^([^\t]*)\t([^\t]*)\t(.*)$")
		if id then list[#list + 1] = {Id = id, Frame = math.tointeger(tonumber(frame)), Text = text} end
	end
	return list
end
-- from 0, as Chimera's
function tastudio.getbranches()
	local t = {}
	if engaged() then for i, b in ipairs(branchList()) do t[i - 1] = b end end
	return t
end
function tastudio.get_branch_index_by_id(id)
	if not engaged() then return nil end
	for i, b in ipairs(branchList()) do if b.Id == tostring(id) then return i - 1 end end
	console.log("not a branch's id: " .. tostring(id))
	return nil
end
function tastudio.getbranchinput(id, frame)
	local t = {}
	local index = engaged() and tastudio.get_branch_index_by_id(id) or nil
	if index == nil then return t end
	local line = C.branches.inputLine(index, frame)
	if line == "" then return t end
	for _, name in ipairs(lines(C.session.buttonList())) do t[name] = false end
	for _, entry in ipairs(lines(C.session.decodeEntry(line))) do
		local name, value = string.match(entry, "^(.-)=(%-?%d+)$")
		if name then t[name] = math.tointeger(tonumber(value)) else t[entry] = true end
	end
	return t
end
function tastudio.loadbranch(index) if engaged() then C.branches.load(index) end end
-- the main movie's markers (a branch's are not asked for here)
function tastudio.getmarker(frame, branch)
	if not engaged() or branch ~= nil then return nil end
	return C.pianoRoll.markerAt(frame)
end
function tastudio.find_marker_on_or_before(frame, branch)
	if not engaged() or branch ~= nil then return nil end
	local f = C.pianoRoll.previousMarker(frame + 1)
	if f < 0 then return nil end
	return f
end
function tastudio.get_frames_with_markers(branch)
	local t = {}
	if engaged() and branch == nil then
		for _, f in ipairs(lines(C.pianoRoll.markerFrames())) do t[#t + 1] = math.tointeger(tonumber(f)) end
	end
	return t
end
function tastudio.removemarker(frame) if engaged() then pcall(C.pianoRoll.removeMarker, frame) end end
function tastudio.setmarker(frame, message) if engaged() then C.pianoRoll.addMarker(frame, message ~= nil and tostring(message) or "") end end
-- functions called when the greenzone is invalidated, a branch is loaded, made
-- or updated, removed; each its name back, as Chimera's
local function register(event, fn, name)
	if not engaged() then return "" end
	return registerNamed(fn, event, name).guid
end
function __chimera_greenzone_invalidated(frame) callEvent("OnGreenzoneInvalidated", frame) end
function __chimera_branch_loaded(index) callEvent("OnBranchLoad", index) end
function __chimera_branch_saved(index) callEvent("OnBranchSave", index) end
function __chimera_branch_removed(index) callEvent("OnBranchRemove", index) end
function tastudio.ongreenzoneinvalidated(fn, name) return register("OnGreenzoneInvalidated", fn, name) end
function tastudio.onbranchload(fn, name) return register("OnBranchLoad", fn, name) end
function tastudio.onbranchsave(fn, name) return register("OnBranchSave", fn, name) end
function tastudio.onbranchremove(fn, name) return register("OnBranchRemove", fn, name) end

-- input: what the person's own devices hold, as Chimera's -----------------
input = {}
function input.get()
	local held = {}
	for name in string.gmatch(C.input.held(), "[^\n]+") do held[name] = true end
	return held
end
function input.getmouse()
	local b = C.input.mouseButtons
	return {X = C.input.mouseX, Y = C.input.mouseY, Left = b & 1 ~= 0, Middle = b & 2 ~= 0, Right = b & 4 ~= 0,
		XButton1 = b & 8 ~= 0, XButton2 = b & 16 ~= 0, Wheel = C.input.mouseWheel}
end
-- the pointer's place; the gamepads' sticks are not followed here
function input.get_pressed_axes() return {["WMouse X"] = C.input.mouseX, ["WMouse Y"] = C.input.mouseY} end

-- joypad -------------------------------------------------------------------
joypad = {}
-- buttons by their names; with a controller's number, the names without its "P<n> "
local function byController(names, controller, value)
	local out = {}
	for _, name in ipairs(names) do
		if controller == nil then out[name] = value
		else
			local short = string.match(name, "^P" .. controller .. " (.+)$")
			if short then out[short] = value end
		end
	end
	return out
end
local function allButtons() return lines(C.session.buttonList()) end
function joypad.get(controller)
	local held, out = {}, {}
	for _, name in ipairs(lines(C.session.heldButtons())) do held[name] = true end
	for name, _ in pairs(byController(allButtons(), controller, false)) do out[name] = false end
	for name, v in pairs(byController(lines(C.session.heldButtons()), controller, true)) do out[name] = v end
	return out
end
joypad.getimmediate = joypad.get
-- what the core was handed on the frame just run, as Chimera's (its
-- ControllerOutput): the engine plays the movie, so the movie's frame
function joypad.getwithmovie(controller)
	local out = {}
	for name, _ in pairs(byController(allButtons(), controller, false)) do out[name] = false end
	for name, v in pairs(byController(lines(C.session.playedButtons()), controller, true)) do out[name] = v end
	return out
end
-- held for the next frame run, as Chimera's joypad.set
-- true or 1 holds a button for the next frame, false or 0 lets it go
-- whoever holds it, as Chimera's (Convert.ToBoolean)
function joypad.set(buttons, controller)
	local held, released = {}, {}
	for name, on in pairs(buttons) do
		local full = controller and ("P" .. controller .. " " .. name) or name
		if type(on) == "string" then on = on:lower() == "true" end
		if on == true or (type(on) == "number" and on ~= 0) then held[#held + 1] = full
		elseif on == false or on == 0 then released[#released + 1] = full end
	end
	C.session.scriptHold(table.concat(held, "\n"))
	C.session.scriptRelease(table.concat(released, "\n"))
end
-- an analog control for the next frame run, as joypad.set is for buttons
function joypad.setaxis(control, value, controller)
	C.session.scriptAxis(controller and ("P" .. controller .. " " .. control) or control, value, false)
end
-- analog controls held until let go (a value of "" lets one go), as Chimera's
function joypad.setanalog(controls, controller)
	for name, value in pairs(controls) do
		local n = controller and ("P" .. controller .. " " .. name) or name
		local v = math.tointeger(tonumber(value))
		if v == nil then C.session.scriptAxis(n, "", true) else C.session.scriptAxis(n, v, true) end
	end
end
-- a movie line's buttons and analog values for the next frame run
function joypad.setfrommnemonicstr(entry) C.session.scriptEntry(tostring(entry)) end

-- movie --------------------------------------------------------------------
movie = {}
function movie.isloaded() return C.session.hasProject end
function movie.filename() return C.session.projectPath end
-- the engine's, now: the piano roll's count follows it a moment later
function movie.length() return C.session.movieLength() end
function movie.mode()
	if not C.session.hasProject then return "INACTIVE" end
	return C.session.recordingMode and "RECORD" or "PLAY"
end
function movie.getrerecordcount() return C.session.rerecords end
function movie.getreadonly() return not C.session.recordingMode end
function movie.setreadonly(on)
	if C.session.hasProject and C.session.recordingMode ~= (on ~= true) then C.session.toggleRecording() end
end
function movie.getfps() return C.session.fps end
-- a name given is the project's file with its kind added, never written over, as Chimera's
function movie.save(path)
	if not C.session.hasProject then return end
	if path == nil or path == "" then C.session.saveProject() return end
	path = path .. ".chimeraProject"
	local there = io.open(path, "rb")
	if there then
		there:close()
		console.log("File " .. path .. " already exists, will not overwrite")
		return
	end
	C.session.saveProject(path)
end
-- the project given, or the open one, from its start; false when there is none
function movie.play_from_start(path)
	local p = (path ~= nil and path ~= "") and path or C.session.projectPath
	if p == nil or p == "" then return false end
	return client.openproject(p)
end
-- saved first unless told not to, as Chimera's movie.stop(saveChanges)
function movie.stop(saveChanges)
	if saveChanges ~= false and C.session.hasProject and C.session.projectPath ~= "" then C.session.saveProject() end
	C.session.stopMovie()
end
-- a project of Chimera's starts from the machine switched on, unless its header says otherwise
function movie.startsfromsavestate()
	if not C.session.hasProject then return false end
	local value = movie.getheader().StartsFromSavestate
	return value ~= nil and (value == "1" or string.lower(value) == "true")
end
function movie.getinputasmnemonic(frame) return C.session.movieLine(math.tointeger(frame) or 0) end
function movie.getrerecordcounting() return C.session.countingRerecords() end
function movie.setrerecordcounting(counting) C.session.setCountingRerecords(counting and true or false) end
function movie.setrerecordcount(count) if C.session.hasProject then C.session.setRerecords(wholeNumber(count)) end end
function movie.getheader()
	local out = {}
	for _, line in ipairs(lines(C.projectDetails.headerEntries())) do
		local key, value = string.match(line, "^([^\t]*)\t(.*)$")
		if key then out[key] = value end
	end
	return out
end
-- numbered from 0, as Chimera's
local function zeroBased(list)
	local out = {}
	for i, v in ipairs(list) do out[i - 1] = v end
	return out
end
function movie.getcomments()
	if not C.session.hasProject then return {} end
	local list = {}
	for line in string.gmatch(C.projectDetails.comments .. "\n", "(.-)\n") do list[#list + 1] = line end
	if list[#list] == "" then list[#list] = nil end
	return zeroBased(list)
end
function movie.getsubtitles()
	if not C.session.hasProject then return {} end
	return zeroBased(lines(C.projectDetails.subtitleLines()))
end
function movie.getinput(frame, controller)
	-- nothing past the movie's end, as Chimera's
	if frame == nil or frame < 0 or frame >= movie.length() then return nil end
	local pressed, values, out = {}, {}, {}
	for _, line in ipairs(lines(C.session.movieInput(frame))) do
		local name, value = string.match(line, "^(.-)=(%-?%d+)$")
		if name then values[name] = math.tointeger(tonumber(value)) else pressed[#pressed + 1] = line end
	end
	for name, _ in pairs(byController(allButtons(), controller, false)) do out[name] = false end
	for name, v in pairs(byController(pressed, controller, true)) do out[name] = v end
	for name, v in pairs(values) do
		local short = controller and string.match(name, "^P" .. controller .. " (.+)$") or name
		if short then out[short] = v end
	end
	return out
end

-- client -------------------------------------------------------------------
client = {}
function client.pause() C.session.pause() end
function client.unpause() C.session.play() end
function client.togglepause() if C.session.running then C.session.pause() else C.session.play() end end
function client.ispaused() return not C.session.running end
function client.isturbo() return C.session.turbo end
-- TAStudio playing to a frame
function client.isseeking() return C.session.seekingTo >= 0 end
function client.is_rewinding() return C.session.rewinding end
function client.speedmode(percent) C.session.speedPercent = percent end
-- at once, as Chimera's: the next line sees frame 0, and the script goes on
function client.reboot_core() C.session.rebootForScript() end
function client.closerom() C.session.close() end
-- at once, as Chimera's: the next line sees the project open, and the script goes on with it
function client.openproject(path) return C.session.openProjectForScript(path) == true end
function client.getversion() return C.help and C.help.commit or "" end
function client.get_approx_framerate() return C.session.fps end
function client.SetSoundOn(on) C.session.soundOn = on end
function client.GetSoundOn() return C.session.soundOn end
function client.displaymessages(on) C.session.showMessages = on end
function client.clearautohold() C.session.clearAutohold() end
-- Chimera's: the program closes (a project with changes asks first, as closing does)
function client.exit() C.toolWindows.exit(0) end
function client.exitCode(code) C.toolWindows.exit(code) end
function client.opentasstudio() C.toolWindows.show("tastudio") end
-- Chimera's Encode Video without the window: nil when it began, else why
-- not; the command's container is the file's (the path as given stands)
function client.encodevideo(path, from, to, command)
	local e = C.encoder
	if command ~= nil then e.command = tostring(command) end
	e.output = tostring(path)
	e.fromFrame = from
	e.toFrame = to
	local ok, err = pcall(e.start)
	if ok then return nil end
	return tostring(err)
end
-- the machine lent to the encode, until it is given back (the session's own word, read at once)
function client.encodingvideo() return C.session.borrowed() end
function client.pause_av() C.encoder.pauseAv(true) end
function client.unpause_av() C.encoder.pauseAv(false) end
-- the name Chimera gives its Lua: scripts tell Chimera's Lua apart by it
function client.get_lua_engine() return "Lua" end
function client.getluafunctionslist() return C.luaConsole.functionNames() end
-- Chimera's scanline filter is gone: 0, and setting it says so where Chimera says it (its standard output)
function client.gettargetscanlineintensity() return 0 end
function client.settargetscanlineintensity(value)
	io.stdout:write("SetTargetScanlineIntensity is a no-op: the scanline display filter was removed from Chimera\n")
	io.stdout:flush()
end
-- the picture the machine draws
local function videoSize() local w, h = string.match(C.session.videoSize(), "^(%d+) (%d+)$") return math.tointeger(tonumber(w)), math.tointeger(tonumber(h)) end
function client.bufferwidth() local w = videoSize() return w end
function client.bufferheight() local _, h = videoSize() return h end
-- the window: its scale for the system open, and where it sits
function client.getwindowsize() return C.display.scaleFor(C.session.systemId) end
function client.setwindowsize(size)
	local n = math.tointeger(tonumber(size))
	if n == nil or n < 1 or n > 10 then print("Invalid window size") return end
	C.display.setScaleFor(C.session.systemId, n)
	C.toolWindows.resizeWindow()
	C.gui.addMessage("Window size set to " .. n .. "x")
end
-- Chimera's show_future: after each frame, frames played ahead (the movie's
-- input) until the function, asked with how many were played, returns true,
-- or maxFrames are; their picture shown and the machine put back. One at a
-- time; unregistered, or its script stopped, none
local futureFn = nil
function __chimera_future(played)
	if futureFn == nil then return true end
	return callNamed(futureFn, played) == true
end
function client.show_future(fn, maxFrames, name)
	local most = math.tointeger(tonumber(maxFrames))
	if most == nil or most < 1 or most > 32767 then
		print("Invalid number of future frames for timeout (" .. tostring(maxFrames) .. "); number must be in 1..=32767.")
		return "00000000-0000-0000-0000-000000000000"
	end
	futureFn = registerNamed(fn, "BeforeFutureFrame", name)
	C.session.setFutureFrames(most)
	return futureFn.guid
end
onRemoved[#onRemoved + 1] = function()
	if futureFn == nil then return end
	for _, f in ipairs(registered) do if f == futureFn then return end end
	futureFn = nil
	C.session.setFutureFrames(0)
end
-- where the window shows the picture, as it last said: the area's size, the
-- picture's place in it, and the part of the machine's picture shown
local function placement()
	local t = {}
	for n in string.gmatch(C.display.placement, "%-?%d+") do t[#t + 1] = math.tointeger(tonumber(n)) end
	return t
end
function client.screenwidth() return placement()[1] or 0 end
function client.screenheight() return placement()[2] or 0 end
-- a point of the machine's picture, where the window shows it
function client.transformPoint(x, y)
	local p = placement()
	if #p < 10 or p[9] == 0 or p[10] == 0 then return {x = x, y = y} end
	return {x = math.floor(p[3] + (x - p[7]) * p[5] / p[9]), y = math.floor(p[4] + (y - p[8]) * p[6] / p[10])}
end
-- the bars beside and above the picture (its game padding among them)
function client.borderwidth() return client.transformPoint(0, 0).x end
function client.borderheight() return client.transformPoint(0, 0).y end
-- black around the picture in its pixels, the surface scripts draw on grown
-- with it; and room left free around everything in the window's
function client.SetGameExtraPadding(left, top, right, bottom) C.display.setPadding("game", left or 0, top or 0, right or 0, bottom or 0) end
function client.SetClientExtraPadding(left, top, right, bottom) C.display.setPadding("client", left or 0, top or 0, right or 0, bottom or 0) end
function client.xpos() return math.tointeger(tonumber(string.match(C.toolWindows.windowPosition(), "^(%-?%d+)"))) end
function client.ypos() return math.tointeger(tonumber(string.match(C.toolWindows.windowPosition(), " (%-?%d+)$"))) end
function client.openhexeditor() C.toolWindows.show("hex") end
function client.openramwatch() C.toolWindows.show("ram-watch") end
function client.openramsearch() C.toolWindows.show("ram-search") end
-- Chimera's tools, by the names its getavailabletools gives them: the
-- window each is, whether the machine open has what it needs (as Chimera
-- asks the core for the tool's services), and what gettool gives a script
-- for it: the window's own object in chimera (owner, 2026-10-26; the Log
-- Window has none)
local toolList = {
	{"tastudio", "tastudio", function() return C.session.isOpen end, "pianoRoll"},
	{"luaconsole", "lua", nil, "luaConsole"},
	{"ramwatch", "ram-watch", function() return C.session.isOpen end, "ramWatch"},
	{"ramsearch", "ram-search", function() return C.session.isOpen end, "ramSearch"},
	{"hexeditor", "hex", function() return C.session.isOpen end, "hexEditor"},
	{"tracelogger", "traceLoggerWindow", function() return C.session.hasTrace end, "traceLogger"},
	{"genericdebugger", "cpuRegistersWindow", function() return C.session.hasRegisters end, "registers"},
	{"macroinputtool", "macroToolWindow", function() return C.session.isOpen end, "macroTool"},
	{"surfaceviewer", "surfaceViewerWindow", function() return C.session.surfaces ~= "[]" end, "surfaceViewer"},
	{"logwindow", "logWindow", nil, nil},
}
local function toolUsable(t) return t[3] == nil or t[3]() end
-- the tools the machine open can use, open or not (as Chimera's), numbered from 0
function client.getavailabletools()
	local out = {}
	for _, t in ipairs(toolList) do if toolUsable(t) then out[#out + 1] = t[1] end end
	return zeroBased(out)
end
-- a tool by name, any case: its window opened and brought up, and its
-- object given back; nil for a name that is no tool, or a tool the machine
-- open cannot use
function client.gettool(name)
	local wanted = string.lower(tostring(name))
	for _, t in ipairs(toolList) do
		if t[1] == wanted then
			if not toolUsable(t) then return nil end
			C.toolWindows.show(t[2])
			return t[4] and C[t[4]] or {}
		end
	end
	return nil
end
-- client.getconfig: the window's settings by the names scripts know them
-- by, each read as it is when read; they cannot be changed through it
-- (owner, 2026-10-26). The list is config_names.cpp's: settings with one
-- value (a switch, a number, a text, a choice by its name), the places of
-- what is drawn over the picture, and Movies' greenzone ones; a name the
-- window has no setting for is nil,
-- and so is McpAllowChanges, as scripts are not given the MCP server's
local function setting(key)
	local vm, name = string.match(key, "^(%w+)%.(%w+)$")
	local model = vm and C[vm]
	if model == nil then return nil end
	return model[name]
end
local readOnly = function(_, key) error("client.getconfig: " .. tostring(key) .. " can be read here, not changed", 2) end
-- read when indexed; pairs walks every name the Qt window has
local function settingsTable(names)
	return setmetatable({}, {
		__index = function(_, key) local read = names[key] return read and read() end,
		__newindex = readOnly,
		__pairs = function(t)
			return function(_, key)
				local name, read = next(names, key)
				if name == nil then return nil end
				return name, read()
			end, t, nil
		end,
	})
end
local anchors = {[0] = "TopLeft", "TopRight", "BottomLeft", "BottomRight"}
-- how each kind is read, from the window's setting (key) and the kind's extra
local readers = {
	plain = function(key) return function() return setting(key) end end,
	-- a choice by its name in Chimera's list, as the window keeps its number
	choice = function(key, extra)
		local names, i = {}, 0
		for name in string.gmatch(extra, "[^|]+") do names[i] = name i = i + 1 end
		return function() return names[setting(key)] end
	end,
	corekind = function(key) return function() return ({emulators = "Emulators", games = "Games"})[setting(key)] end end,
	-- "#AARRGGBB" as Chimera's colour number (a signed 32-bit ARGB)
	colour = function(key)
		return function()
			local n = tonumber(string.sub(setting(key) or "", 2), 16)
			if n == nil then return nil end
			return n >= 0x80000000 and n - 0x100000000 or n
		end
	end,
	megabytes = function(key) return function() local gb = setting(key) return gb and gb * 1024 end end,
	throttle = function(key, extra) return function() return setting(key) == extra end end,
	resize = function(key, extra) return function() return setting(key) and setting(extra) or 0 end end,
	displayfeatures = function(key, extra)
		return function()
			if setting(key) then return 0 end
			return setting(extra) and 1 or 2
		end
	end,
	nullwhenempty = function(key) return function() local v = setting(key) return v ~= "" and v or nil end end,
	-- one of the places in messages' positions, by its name there, as Chimera's {X, Y, Anchor}
	place = function(key, extra)
		return function()
			local one = string.match(setting(key) or "", '"' .. extra .. '"%s*:%s*(%b{})')
			if one == nil then return nil end
			local function field(k) return math.tointeger(tonumber(string.match(one, '"' .. k .. '"%s*:%s*(%-?%d+)'))) end
			return settingsTable({
				X = function() return field("x") end,
				Y = function() return field("y") end,
				Anchor = function() return anchors[field("anchor")] end,
			})
		end
	end,
}
local configNames, groups = {}, {}
for _, line in ipairs(lines(__chimera_config_names())) do
	local name, kind, key, extra = string.match(line, "^([^\t]*)\t([^\t]*)\t([^\t]*)\t(.*)$")
	local read = readers[kind](key, extra)
	-- one inside Movies, as Chimera's Movies.GreenzoneBudgetMb
	local outer, inner = string.match(name, "^(%w+)%.(%w+)$")
	if outer then
		groups[outer] = groups[outer] or {}
		groups[outer][inner] = read
	else
		configNames[name] = read
	end
end
for outer, names in pairs(groups) do configNames[outer] = function() return settingsTable(names) end end
function client.getconfig() return settingsTable(configNames) end
-- Screenshot (to a file when one is named) and Screenshot To Clipboard, with Capture OSD as it is set
function client.screenshot(path) C.toolWindows.screenshot(path == nil and "" or tostring(path)) end
function client.screenshottoclipboard() C.toolWindows.screenshotToClipboard() end
function client.setscreenshotosd(value) C.session.captureOsd = value and true or false end
-- the scripts' thread waits (and the machine with it), as Chimera's
function client.sleep(ms) __chimera_sleep(math.tointeger(tonumber(ms)) or 0, false) end
function client.exactsleep(ms) __chimera_sleep(math.tointeger(tonumber(ms)) or 0, true) end

-- gameinfo -----------------------------------------------------------------
gameinfo = {}
-- the game as Chimera names it: its file's name without the extension, "_"
-- read as a space; "Null" with nothing open, Chimera's empty machine's
function gameinfo.getromname()
	if not C.session.isOpen then return "Null" end
	return (string.gsub(C.session.gameName, "_", " "))
end
function gameinfo.getromhash() return C.session.gameSha1() end
-- Chimera has no game database: a game is not in it, its empty machine is
function gameinfo.indatabase() return not C.session.isOpen end
function gameinfo.getboardtype() return "" end
function gameinfo.getoptions() return {} end
-- Chimera has no game database: a game is "NotInDatabase", and with none
-- open its empty machine's is a good dump
function gameinfo.getstatus() return C.session.isOpen and "NotInDatabase" or "GoodDump" end
function gameinfo.isstatusbad() return false end

-- gui: drawn over the picture, in the machine's pixels; a frame's drawings
-- go when the next frame begins, so a script draws each frame, as Chimera's
gui = {}
local shapes = {}
local foreground, background, textBackground = "#FFFFFFFF", "", ""
local function colour(c, fallback)
	if c == nil then return fallback end
	if type(c) == "number" then return string.format("#%08X", math.tointeger(c) & 0xFFFFFFFF) end
	return tostring(c)
end
local function jsonOf(v)
	if type(v) == "table" then
		if #v > 0 or next(v) == nil then
			local out = {}
			for i = 1, #v do out[i] = jsonOf(v[i]) end
			return "[" .. table.concat(out, ",") .. "]"
		end
		local out = {}
		-- a key written as a string value is (%q wrote Lua escapes, not JSON's)
		for k, x in pairs(v) do out[#out + 1] = jsonOf(tostring(k)) .. ":" .. jsonOf(x) end
		return "{" .. table.concat(out, ",") .. "}"
	elseif type(v) == "number" then
		return math.type(v) == "integer" and tostring(v) or string.format("%.17g", v)
	elseif type(v) == "boolean" then return tostring(v)
	end
	return '"' .. string.gsub(tostring(v), '[%c"\\]', function(c) return string.format("\\u%04x", string.byte(c)) end) .. '"'
end
-- Chimera's two surfaces: "emucore" (the machine's picture with its crop and
-- padding, in its pixels) and "client" (the window's picture area, in its own
-- pixels); a function's surface named last, else gui.use_surface's
local usedSurface = "emucore"
local function surfaceOf(name)
	if name == nil then return usedSurface end
	local n = string.lower(tostring(name))
	if n == "client" or n == "native" then return "client" end
	if n == "emu" or n == "emucore" then return "emucore" end
	error(tostring(name) .. " is not the name of a display surface", 3)
end
local function add(t, surface)
	if surfaceOf(surface) == "client" then t.surface = "client" end
	shapes[#shapes + 1] = t
end
function gui.drawRectangle(x, y, w, h, line, fill, surface)
	add({k = "rect", x = x, y = y, w = w, h = h, line = colour(line, foreground), fill = colour(fill, background)}, surface)
end
function gui.drawBox(x, y, x2, y2, line, fill, surface)
	gui.drawRectangle(math.min(x, x2), math.min(y, y2), math.abs(x2 - x), math.abs(y2 - y), line, fill, surface)
end
function gui.drawEllipse(x, y, w, h, line, fill, surface)
	add({k = "ellipse", x = x, y = y, w = w, h = h, line = colour(line, foreground), fill = colour(fill, background)}, surface)
end
function gui.drawLine(x1, y1, x2, y2, c, surface) add({k = "line", x = x1, y = y1, x2 = x2, y2 = y2, line = colour(c, foreground)}, surface) end
function gui.drawAxis(x, y, size, c, surface) gui.drawLine(x - size, y, x + size, y, c, surface) gui.drawLine(x, y - size, x, y + size, c, surface) end
local function pointsOf(t, dx, dy)
	local out = {}
	for _, pt in ipairs(t) do out[#out + 1] = {pt[1] + (dx or 0), pt[2] + (dy or 0)} end
	return out
end
function gui.drawPolygon(points, x, y, line, fill, surface)
	add({k = "path", points = pointsOf(points, x, y), closed = true, line = colour(line, foreground), fill = colour(fill, background)}, surface)
end
-- four points: the start, two control points, the end
function gui.drawBezier(points, c, surface)
	local p = pointsOf(points)
	if #p == 4 then add({k = "bezier", points = p, line = colour(c, foreground)}, surface) end
end
-- angles in degrees, clockwise from the right, as Chimera's
function gui.drawPie(x, y, w, h, start, sweep, line, fill, surface)
	add({k = "pie", x = x, y = y, w = w, h = h, start = start, sweep = sweep, line = colour(line, foreground), fill = colour(fill, background)}, surface)
end
-- a file's path, a relative one from the script's folder, as Chimera runs a
-- script in its own folder
local function fileOf(path)
	path = tostring(path)
	if path:match("^[/\\]") or path:match("^%a:[/\\]") then return path end
	local folder = __chimera_folder()
	return folder ~= "" and (folder .. "/" .. path) or path
end
local imageEpoch = 0
-- cache false: the file read again as it is now, once it has changed
local function epochFor(file, cache)
	if cache == false then return -__chimera_file_stamp(file) end
	return imageEpoch
end
function gui.drawImage(path, x, y, w, h, cache, surface)
	local file = fileOf(path)
	add({k = "image", file = file, x = x, y = y, w = w or -1, h = h or -1, epoch = epochFor(file, cache)}, surface)
end
function gui.drawIcon(path, x, y, w, h, surface) gui.drawImage(path, x, y, w, h, true, surface) end
function gui.drawImageRegion(path, sx, sy, sw, sh, x, y, w, h, surface)
	add({k = "image", file = fileOf(path), sx = sx, sy = sy, sw = sw, sh = sh, x = x, y = y, w = w or sw, h = h or sh, epoch = imageEpoch}, surface)
end
function gui.drawPixel(x, y, c, surface) add({k = "rect", x = x, y = y, w = 1, h = 1, line = "", fill = colour(c, foreground)}, surface) end
-- Chimera's: its font (monospace unless named) and style, which end of the
-- text is at x and y, and a background drawn as an outline around the letters
function gui.drawString(x, y, text, fore, back, size, family, style, halign, valign, surface)
	add({k = "text", x = x, y = y, text = tostring(text), color = colour(fore, foreground), back = colour(back, textBackground ~= "" and textBackground or background), size = size or 12,
		family = family, style = style, halign = halign, valign = valign, outline = true}, surface)
end
gui.drawText = gui.drawString
function gui.pixelText(x, y, text, fore, back, family, surface)
	add({k = "text", x = x, y = y, text = tostring(text), color = colour(fore, "#FFFFFFFF"), back = colour(back, "#FF000000"), size = 8}, surface)
end
-- in the window's own pixels, from a corner
function gui.text(x, y, text, fore, anchor)
	add({k = "screentext", x = x, y = y, text = tostring(text), color = colour(fore, foreground), anchor = anchor or "topleft"})
end
-- what was drawn on a surface (the one in use unless named); gui.text's stays
function gui.clearGraphics(surface)
	local which = surfaceOf(surface)
	local kept = {}
	for _, s in ipairs(shapes) do
		if s.k == "screentext" or (s.surface or "emucore") ~= which then kept[#kept + 1] = s end
	end
	shapes = kept
end
function gui.cleartext()
	local kept = {}
	for _, s in ipairs(shapes) do if s.k ~= "screentext" then kept[#kept + 1] = s end end
	shapes = kept
end
function gui.addmessage(text) C.gui.addMessage(tostring(text)) end
function gui.defaultForeground(c) foreground = colour(c, "#FFFFFFFF") end
function gui.defaultBackground(c) background = colour(c, "") end
function gui.defaultTextBackground(c) textBackground = colour(c, "") end
function gui.defaultPixelFont() end
function gui.use_surface(name)
	if name == nil then
		print("Surface name cannot be nil. Pass \"emucore\" to `gui.use_surface` to restore the default.")
		return
	end
	usedSurface = surfaceOf(name)
end
-- deprecated, as Chimera's: the surface drawn on from now, cleared unless asked not to
function gui.DrawNew(name, clear)
	usedSurface = surfaceOf(name)
	if clear ~= false then gui.clearGraphics(name) end
end
function gui.DrawFinish() end
-- the images are read again the next time they are drawn
function gui.clearImageCache() imageEpoch = imageEpoch + 1 end
function __chimera_frame_begin() shapes = {} end
function __chimera_frame_end() if C.gui then C.gui.shapes = jsonOf(shapes) end end

-- SQL: a SQLite database, as Chimera's -----------------------------------
SQL = {}
function SQL.createdatabase(name) return C.sql.createDatabase(tostring(name)) end
function SQL.opendatabase(name) return C.sql.openDatabase(tostring(name)) end
function SQL.writecommand(query) return C.sql.writeCommand(tostring(query or "")) end
-- every value keyed "column row", as Chimera's; else what happened
function SQL.readcommand(query)
	local answer = C.sql.readCommand(tostring(query or ""))
	if string.sub(answer, 1, 1) ~= "\30" then return answer end
	local t = {}
	for key, kind, value in string.gmatch(answer, "([^\30\31]*)\31(%a)\31([^\30]*)\30") do
		if kind == "i" then t[key] = math.tointeger(tonumber(value))
		elseif kind == "f" then t[key] = tonumber(value)
		elseif kind ~= "n" then t[key] = value end
	end
	return t
end

-- comm: data shared with other programs through memory-mapped files, as
-- Chimera's (it has no network functions) ------------------------------------
comm = {}
function comm.getluafunctionslist()
	local names = {}
	for name in pairs(comm) do names[#names + 1] = name end
	table.sort(names)
	return table.concat(names, "\n")
end
function comm.mmfSetFilename(name) C.comm.screenshotFile = tostring(name) end
function comm.mmfGetFilename() return C.comm.screenshotFile end
function comm.mmfScreenshot() return C.comm.mmfScreenshot() end
function comm.mmfWrite(name, text) return C.comm.mmfWrite(tostring(name), tostring(text)) end
function comm.mmfWriteBytes(name, bytes)
	local keys, parts = {}, {}
	for k in pairs(bytes) do keys[#keys + 1] = k end
	table.sort(keys)
	for i, k in ipairs(keys) do parts[i] = string.char(bytes[k] & 0xFF) end
	return C.comm.mmfWrite(tostring(name), table.concat(parts))
end
function comm.mmfCopyFromMemory(name, addr, length, domain)
	return C.comm.mmfWrite(tostring(name), C.session.readMemory(domain or currentDomain(), addr, length))
end
function comm.mmfCopyToMemory(name, addr, length, domain)
	C.session.writeMemory(domain or currentDomain(), addr, C.comm.mmfRead(tostring(name), length))
end
function comm.mmfRead(name, size) return C.comm.mmfRead(tostring(name), size) end
-- from 0, as Chimera's
function comm.mmfReadBytes(name, size)
	local s, t = C.comm.mmfRead(tostring(name), size), {}
	for i = 1, #s do t[i - 1] = string.byte(s, i) end
	return t
end

-- forms: a script's own small windows, as Chimera's -------------------------
forms = {}
local clicks, closers, boxes = {}, {}, {}
-- each form's script, and each control's: what they call runs as that
-- script, and a script's forms close when it stops, as Chimera's
local formOwner, owner = {}, {}
local function asOwner(handle, fn)
	local was = __chimera_enter(owner[handle] or 0)
	local ok, e = pcall(fn)
	__chimera_enter(was)
	if not ok then print(e) end
end
-- the window's: a control clicked, a form closed by the person
function __chimera_forms_click(handle)
	asOwner(handle, function() for _, fn in ipairs(clicks[handle] or {}) do fn() end end)
end
function __chimera_forms_closed(handle)
	local fn = closers[handle]
	closers[handle] = nil
	formOwner[handle] = nil
	if fn then asOwner(handle, fn) end
end
function __chimera_forms_open(script)
	for _, s in pairs(formOwner) do if s == script then return true end end
	return false
end
function __chimera_forms_stopped(script)
	local mine = {}
	for f, s in pairs(formOwner) do if s == script then mine[#mine + 1] = f end end
	for _, f in ipairs(mine) do
		closers[f] = nil
		formOwner[f] = nil
		pcall(C.forms.destroy, f)
	end
end
local function itemsText(t)
	local list = {}
	for _, v in pairs(t or {}) do list[#list + 1] = tostring(v) end
	return table.concat(list, "\n")
end
function forms.newform(width, height, title, onClose)
	local f = C.forms.newForm(width, height, title ~= nil and tostring(title) or nil)
	if onClose ~= nil then closers[f] = onClose end
	formOwner[f] = __chimera_script
	owner[f] = __chimera_script
	return f
end
function forms.button(form, caption, onClick, x, y, width, height)
	local b = C.forms.button(form, tostring(caption or ""), x, y, width, height)
	if b ~= 0 and onClick ~= nil then clicks[b] = {onClick} end
	owner[b] = owner[form] or __chimera_script
	return b
end
function forms.checkbox(form, caption, x, y) return C.forms.checkbox(form, tostring(caption or ""), x, y) end
function forms.label(form, caption, x, y, width, height, fixedWidth)
	return C.forms.label(form, tostring(caption or ""), x, y, width, height, fixedWidth == true)
end
function forms.textbox(form, caption, width, height, boxtype, x, y, multiline, fixedWidth, scrollbars)
	return C.forms.textbox(form, caption ~= nil and tostring(caption) or "", width, height, boxtype, x, y, multiline == true, fixedWidth == true, scrollbars)
end
function forms.dropdown(form, items, x, y, width, height) return C.forms.dropdown(form, itemsText(items), x, y, width, height) end
function forms.addclick(handle, fn)
	owner[handle] = owner[handle] or __chimera_script
	clicks[handle] = clicks[handle] or {}
	table.insert(clicks[handle], fn)
end
function forms.clearclicks(handle) clicks[handle] = nil end
-- a form closed by its script runs its onClose too, as any close does in Chimera
function forms.destroy(handle)
	local onClose = closers[handle]
	closers[handle] = nil
	formOwner[handle] = nil
	local gone = C.forms.destroy(handle)
	if onClose then pcall(onClose) end
	return gone
end
function forms.destroyall()
	local all = closers
	closers = {}
	formOwner = {}
	C.forms.destroyAll()
	for _, onClose in pairs(all) do pcall(onClose) end
end
function forms.gettext(handle) return C.forms.getText(handle) end
function forms.settext(handle, caption) C.forms.setText(handle, tostring(caption)) end
function forms.ischecked(handle) return C.forms.isChecked(handle) end
function forms.getproperty(handle, property) return C.forms.getProperty(handle, tostring(property)) end
function forms.setproperty(handle, property, value) C.forms.setProperty(handle, tostring(property), value) end
function forms.setlocation(handle, x, y) C.forms.setLocation(handle, x, y) end
function forms.setsize(handle, width, height) C.forms.setSize(handle, width, height) end
function forms.setdropdownitems(handle, items, alphabetize) C.forms.setDropdownItems(handle, itemsText(items), alphabetize ~= false) end
function forms.createcolor(r, g, b, a) return ((a or 255) & 0xFF) << 24 | (r & 0xFF) << 16 | (g & 0xFF) << 8 | (b & 0xFF) end
-- a picture box: drawn on as gui draws over the picture, shown on refresh
function forms.pictureBox(form, x, y, width, height)
	local box = C.forms.pictureBox(form, x, y, width, height)
	-- Chimera's picture box: black lines, no fill, text on a half-black box
	if box ~= 0 then boxes[box] = {shapes = {}, background = "", fore = "#FF000000", back = "", textBack = "#80000000"} end
	return box
end
local function boxOf(handle)
	local b = boxes[handle]
	if b == nil then error("no picture box " .. tostring(handle)) end
	return b
end
local function put(handle, shape) local b = boxOf(handle) b.shapes[#b.shapes + 1] = shape end
function forms.clear(handle, color)
	local b = boxOf(handle)
	b.shapes = {}
	b.background = colour(color, "#FFFFFFFF")
	C.forms.draw(handle, "[]", b.background)
end
function forms.refresh(handle)
	local b = boxOf(handle)
	C.forms.draw(handle, jsonOf(b.shapes), b.background)
end
function forms.setDefaultForegroundColor(handle, color) boxOf(handle).fore = colour(color, "#FF000000") end
function forms.setDefaultBackgroundColor(handle, color) boxOf(handle).back = colour(color, "") end
function forms.setDefaultTextBackground(handle, color) boxOf(handle).textBack = colour(color, "") end
function forms.drawRectangle(handle, x, y, w, h, line, fill)
	local b = boxOf(handle)
	put(handle, {k = "rect", x = x, y = y, w = w, h = h, line = colour(line, b.fore), fill = colour(fill, b.back)})
end
function forms.drawBox(handle, x, y, x2, y2, line, fill)
	forms.drawRectangle(handle, math.min(x, x2), math.min(y, y2), math.abs(x2 - x), math.abs(y2 - y), line, fill)
end
function forms.drawEllipse(handle, x, y, w, h, line, fill)
	local b = boxOf(handle)
	put(handle, {k = "ellipse", x = x, y = y, w = w, h = h, line = colour(line, b.fore), fill = colour(fill, b.back)})
end
function forms.drawLine(handle, x1, y1, x2, y2, c) put(handle, {k = "line", x = x1, y = y1, x2 = x2, y2 = y2, line = colour(c, boxOf(handle).fore)}) end
function forms.drawAxis(handle, x, y, size, c)
	forms.drawLine(handle, x - size, y, x + size, y, c)
	forms.drawLine(handle, x, y - size, x, y + size, c)
end
function forms.drawPixel(handle, x, y, c) put(handle, {k = "rect", x = x, y = y, w = 1, h = 1, line = "", fill = colour(c, boxOf(handle).fore)}) end
function forms.drawPolygon(handle, points, x, y, line, fill)
	local b = boxOf(handle)
	put(handle, {k = "path", points = pointsOf(points, x, y), closed = true, line = colour(line, b.fore), fill = colour(fill, b.back)})
end
function forms.drawBezier(handle, points, c)
	local p = pointsOf(points)
	if #p == 4 then put(handle, {k = "bezier", points = p, line = colour(c, boxOf(handle).fore)}) end
end
function forms.drawPie(handle, x, y, w, h, start, sweep, line, fill)
	local b = boxOf(handle)
	put(handle, {k = "pie", x = x, y = y, w = w, h = h, start = start, sweep = sweep, line = colour(line, b.fore), fill = colour(fill, b.back)})
end
function forms.drawArc(handle, x, y, w, h, start, sweep, line)
	put(handle, {k = "pie", arc = true, x = x, y = y, w = w, h = h, start = start, sweep = sweep, line = colour(line, boxOf(handle).fore), fill = ""})
end
-- as Chimera's picture box draws text: black unless a colour is given, on a
-- box of the text background; its font, style and alignment as gui's
function forms.drawString(handle, x, y, text, fore, back, size, family, style, halign, valign)
	local b = boxOf(handle)
	put(handle, {k = "text", x = x, y = y, text = tostring(text), color = colour(fore, "#FF000000"), back = colour(back, b.textBack), size = size or 12,
		family = family, style = style, halign = halign, valign = valign})
end
forms.drawText = forms.drawString
function forms.drawImage(handle, path, x, y, w, h)
	put(handle, {k = "image", file = fileOf(path), x = x, y = y, w = w or -1, h = h or -1, epoch = imageEpoch})
end
forms.drawIcon = forms.drawImage
function forms.drawImageRegion(handle, path, sx, sy, sw, sh, x, y, w, h)
	put(handle, {k = "image", file = fileOf(path), sx = sx, sy = sy, sw = sw, sh = sh, x = x, y = y, w = w or sw, h = h or sh, epoch = imageEpoch})
end
function forms.clearImageCache() imageEpoch = imageEpoch + 1 end
function forms.getMouseX(handle) return C.forms.mouseX(handle) end
function forms.getMouseY(handle) return C.forms.mouseY(handle) end
-- a file picked in the window's dialog, waited for: "" when cancelled; the
-- folder the file named is in, else the Lua folder, as Chimera's
function forms.openfile(fileName, initialDirectory, filter)
	local folder = initialDirectory
	if folder == nil and fileName ~= nil then folder = string.match(tostring(fileName), "^(.*)[/\\][^/\\]*$") or "" end
	if folder == nil then folder = C.paths.resolve("Lua") end
	return C.forms.openFile(fileName ~= nil and tostring(fileName) or "", tostring(folder), filter ~= nil and tostring(filter) or "All Files (*.*)|*.*")
end

-- gui.createcanvas: Chimera's LuaCanvas, a window of the script's own holding
-- one picture box the size asked for, "LuaCanvas" until it is named; its
-- functions are called with a dot (canvas.DrawText(...)), as Chimera's are
local function canvasOf(form, box)
	local c = {}
	function c.SetTitle(title) forms.settext(form, title) end
	function c.SetLocation(left, top) forms.setlocation(form, left, top) end
	function c.Clear(color) forms.clear(box, color) end
	function c.Refresh() forms.refresh(box) end
	function c.SetDefaultForegroundColor(color) forms.setDefaultForegroundColor(box, color) end
	function c.SetDefaultBackgroundColor(color) forms.setDefaultBackgroundColor(box, color) end
	function c.SetDefaultTextBackground(color) forms.setDefaultTextBackground(box, color) end
	function c.DrawBezier(points, color) forms.drawBezier(box, points, color) end
	function c.DrawBox(x1, y1, x2, y2, line, background) forms.drawBox(box, x1, y1, x2, y2, line, background) end
	function c.DrawEllipse(ex, ey, w, h, line, background) forms.drawEllipse(box, ex, ey, w, h, line, background) end
	function c.DrawIcon(path, ix, iy, w, h) forms.drawIcon(box, path, ix, iy, w, h) end
	function c.DrawImage(path, ix, iy, w, h, cache) forms.drawImage(box, path, ix, iy, w, h, cache) end
	function c.ClearImageCache() forms.clearImageCache(box) end
	function c.DrawImageRegion(path, sx, sy, sw, sh, dx, dy, dw, dh) forms.drawImageRegion(box, path, sx, sy, sw, sh, dx, dy, dw, dh) end
	function c.DrawLine(x1, y1, x2, y2, color) forms.drawLine(box, x1, y1, x2, y2, color) end
	function c.DrawAxis(ax, ay, size, color) forms.drawAxis(box, ax, ay, size, color) end
	function c.DrawArc(ax, ay, w, h, start, sweep, line) forms.drawArc(box, ax, ay, w, h, start, sweep, line) end
	function c.DrawPie(px, py, w, h, start, sweep, line, background) forms.drawPie(box, px, py, w, h, start, sweep, line, background) end
	function c.DrawPixel(px, py, color) forms.drawPixel(box, px, py, color) end
	function c.DrawPolygon(points, px, py, line, background) forms.drawPolygon(box, points, px, py, line, background) end
	function c.DrawRectangle(rx, ry, w, h, line, background) forms.drawRectangle(box, rx, ry, w, h, line, background) end
	function c.DrawText(tx, ty, message, fore, back, size, family, style, halign, valign)
		forms.drawText(box, tx, ty, message, fore, back, size, family, style, halign, valign)
	end
	c.DrawString = c.DrawText
	function c.GetMouseX() return forms.getMouseX(box) end
	function c.GetMouseY() return forms.getMouseY(box) end
	-- a relative path is in the system's Screenshots folder, as Chimera's
	function c.save_image_to_disk(path)
		path = tostring(path)
		if not (string.match(path, "^/") or string.match(path, "^%a:[/\\]") or string.match(path, "^\\\\")) then
			local folder = C.paths.resolveFor(C.session.systemId, "Screenshots")
			-- no system open: a system's own default, under the base
			if folder == "" then folder = C.paths.absolute("./screenshots", "") end
			path = folder .. "/" .. path
		end
		C.forms.saveImage(box, path)
	end
	return c
end
function gui.createcanvas(width, height, x, y)
	local form = forms.newform(width, height, "LuaCanvas")
	local box = forms.pictureBox(form, 0, 0, width, height)
	if x ~= nil then forms.setlocation(form, x, y or 0) end
	return canvasOf(form, box)
end

-- tastudio's callbacks into the roll's drawing: a cell's colour and text,
-- the first answer of those registered winning, as Chimera's; and a
-- script's own columns. Asked while the roll is drawn.
local function queryRegister(event, fn, name)
	if not C.session.hasProject then return "" end
	local f = registerNamed(fn, event, name)
	C.pianoRoll.scriptQueries = true
	return f.guid
end
local function firstAnswer(event, row, column)
	for _, f in ipairs(registered) do
		if f.event == event then
			local v = callNamed(f, row, column)
			if v ~= nil then return v end
		end
	end
	return nil
end
-- the roll asks no more once the last of them is gone
onRemoved[#onRemoved + 1] = function()
	if not C.pianoRoll then return end
	for _, f in ipairs(registered) do
		if f.event == "OnQueryItemBg" or f.event == "OnQueryItemText" or f.event == "OnQueryItemIcon" then return end
	end
	if C.pianoRoll.scriptQueries then C.pianoRoll.scriptQueries = false end
end
function __chimera_query_bg(row, column)
	local v = firstAnswer("OnQueryItemBg", row, column)
	if v ~= nil then return colour(v, "") end
	return nil
end
function __chimera_query_text(row, column)
	local v = firstAnswer("OnQueryItemText", row, column)
	if v ~= nil then return tostring(v) end
	return nil
end
-- an icon file's path for a cell, read once (clearIconCache reads them again)
function __chimera_query_icon(row, column)
	local v = firstAnswer("OnQueryItemIcon", row, column)
	if v ~= nil then return fileOf(v) end
	return nil
end
function tastudio.onqueryitemicon(fn, name) return queryRegister("OnQueryItemIcon", fn, name) end
function tastudio.clearIconCache() C.pianoRoll.iconEpoch = C.pianoRoll.iconEpoch + 1 end
function tastudio.onqueryitembg(fn, name) return queryRegister("OnQueryItemBg", fn, name) end
function tastudio.onqueryitemtext(fn, name) return queryRegister("OnQueryItemText", fn, name) end
function tastudio.addcolumn(name, text, width)
	if C.session.hasProject then C.pianoRoll.addScriptColumn(tostring(name), tostring(text), width or 30) end
end
-- every script stopped: their own columns go too
function __chimera_scripts_stopped()
	if C.pianoRoll then C.pianoRoll.clearScriptColumns() end
end

-- event --------------------------------------------------------------------
event = {}
-- each gives back the function's id; both run after each frame, as the
-- window learns of it (the start's first)
function event.onframeend(fn, name) return registerNamed(fn, "OnFrameEnd", name).guid end
function event.onframestart(fn, name) return registerNamed(fn, "OnFrameStart", name).guid end
-- after the script that registered it stops
function event.onexit(fn, name) return registerNamed(fn, "OnExit", name).guid end
function event.onconsoleclose(fn, name) return registerNamed(fn, "OnConsoleClose", name).guid end
-- the calling script's own, by id or by name, as Chimera's
function event.unregisterbyid(guid)
	local me = __chimera_script
	return removeRegistered(function(f) return f.script == me and f.guid == tostring(guid) end, true)
end
function event.unregisterbyname(name)
	local me = __chimera_script
	return removeRegistered(function(f) return f.script == me and f.name == tostring(name) end, true)
end
-- the events part way through a frame no core in a sandbox can call: refused
-- with Chimera's words, so that a script can guard them with pcall
local function coreName() return C.session.coreName ~= "" and C.session.coreName or "This core" end
local midFrame = "No Chimera core can: a core runs in a sandbox that Chimera calls once a frame, "
	.. "and the core cannot stop part way through a frame to run a Lua function."
-- each read of the controls, for a core that reports its reads (issue 344), as
-- Chimera's: the core is asked first, and a function is kept only when it will
-- be called; any other core refuses it, with Chimera's words
local inputPollsAsked = false
function event.oninputpoll(fn, name)
	if not C.session.inputPollsReported() then
		error(coreName() .. " does not report its reads of the controls, so it cannot call a Lua function when it reads them. "
			.. "A core runs in a sandbox that Chimera calls once a frame, and only a core that reports its reads stops part way through a frame to call back. "
			.. "Remove the registration, or guard it with pcall.", 2)
	end
	local guid = registerNamed(fn, "OnInputPoll", name).guid
	inputPollsAsked = true
	C.session.setInputPolls(true)
	return guid
end
-- the core asked no more once the last such function is gone
onRemoved[#onRemoved + 1] = function()
	if not inputPollsAsked then return end
	for _, f in ipairs(registered) do if f.event == "OnInputPoll" then return end end
	inputPollsAsked = false
	C.session.setInputPolls(false)
end
local function memoryRefused(execute, anyAddress)
	return function(fn, address)
		local every = anyAddress or address == nil
		error(coreName() .. " cannot call a Lua function on " .. (every and "every " or "a ") .. "memory "
			.. (execute and "execute" or "access") .. ". " .. midFrame .. " Remove the registration, or guard it with pcall.", 2)
	end
end
event.onmemoryexecute = memoryRefused(true, false)
event.on_bus_exec = event.onmemoryexecute
event.onmemoryexecuteany = memoryRefused(true, true)
event.on_bus_exec_any = event.onmemoryexecuteany
event.onmemoryread = memoryRefused(false, false)
event.on_bus_read = event.onmemoryread
event.onmemorywrite = memoryRefused(false, false)
event.on_bus_write = event.onmemorywrite
function event.availableScopes() return {} end
function event.can_use_callback_params(subset) return subset == "memory" end

-- the Lua Functions List's: each library's functions with their parameters'
-- names, library by library, as JSON
function __chimera_functions_list()
	local skip = {string = true, table = true, math = true, io = true, os = true, coroutine = true, debug = true,
		utf8 = true, package = true, _G = true, chimera = true}
	local libs, out = {}, {}
	for name, lib in pairs(_G) do
		if type(name) == "string" and type(lib) == "table" and not skip[name] and not name:find("^__") then libs[#libs + 1] = name end
	end
	table.sort(libs)
	-- a canvas's functions, as Chimera documents them: the library LuaCanvas
	local tables = {LuaCanvas = canvasOf(0, 0)}
	libs[#libs + 1] = "LuaCanvas"
	for _, lib in ipairs(libs) do
		local names = {}
		local t = tables[lib] or _G[lib]
		for k, v in pairs(t) do if type(k) == "string" and type(v) == "function" then names[#names + 1] = k end end
		table.sort(names, function(a, b) return a:lower() < b:lower() end)
		for _, n in ipairs(names) do
			local fn = t[n]
			local info = debug.getinfo(fn, "u")
			local params = {}
			for i = 1, info.nparams do params[#params + 1] = debug.getlocal(fn, i) end
			if info.isvararg then params[#params + 1] = "..." end
			out[#out + 1] = string.format('{"library": "%s", "name": "%s", "params": "%s"}', lib, n, table.concat(params, ", "))
		end
	end
	return "[" .. table.concat(out, ",") .. "]"
end
)LUA";
}

} // namespace chimera::vm
