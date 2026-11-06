-- Level B branch loads of the synthetic witness (issue 534): userdata kept
-- with a branch and event.onloadstate.
--
-- A branch made keeps the scripts' userdata as it is then; loading the
-- branch puts it back, every value with its own type, and only then calls the
-- event.onloadstate functions, with the branch's name. Those functions may
-- not wait for a frame. A branch load that only moves the machine (TAStudio's
-- old control scheme) and a core state loaded by memorysavestate call none.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   meta=<path for result metadata>

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

-- the first result is the one kept: client.exit() only asks, and the script
-- runs on until the frontend stops it
local metaPath
local finished = false
local function finish(status, detail)
	if finished then return end
	finished = true
	if metaPath then
		writeAll(metaPath, "status=" .. status .. "\ndetail=" .. (detail or "") .. "\n")
	end
	client.exit()
end

local jobPath = os.getenv("CHIMERA_JOB")
if jobPath == nil then error("CHIMERA_JOB env var not set") end
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k == "meta" then metaPath = v end
end

local function check(ok, detail)
	if not ok then finish("FAIL", detail) end
end
-- the window's work done between frames: a branch made or loaded
local function waitFor(what, done)
	for _ = 1, 2000 do
		if done() then return end
		emu.yield()
	end
	finish("FAIL", "timed out waiting for " .. what)
end

-- one of each kind a value may be, and text that needs escaping
local text = "a b%=\n\t\"x\"\xC3\xA9"
userdata.clear()
userdata.set("hits", 5)
userdata.set("big", math.mininteger)
userdata.set("third", 1 / 3)
userdata.set("whole float", 2.0)
userdata.set("tiny", -0.0)
userdata.set("text", text)
userdata.set("empty", "")
userdata.set("flag", true)
userdata.set("off", false)
userdata.set("a name with spaces & %", "kept")

emu.frameadvance()
emu.frameadvance()
local madeAt = emu.framecount()
chimera.branches.create("kept")
waitFor("the branch to be made", function() return chimera.branches.made >= 1 end)

-- the userdata changes after the branch
userdata.set("hits", 9)
userdata.set("later", "x")
userdata.remove("flag")

local seen = {}
local waitRefused
event.onloadstate(function(name)
	seen[#seen + 1] = {name = name, hits = userdata.get("hits"), later = userdata.get("later"), frame = emu.framecount()}
	waitRefused = not pcall(emu.frameadvance)
end, "reset")

emu.frameadvance()
chimera.branches.load(0)
waitFor("the branch to be loaded", function() return chimera.branches.loads >= 1 and #seen >= 1 end)

check(#seen == 1, "event.onloadstate was called " .. #seen .. " times for one branch load")
check(seen[1].name == "kept", "event.onloadstate was given the name " .. tostring(seen[1].name) .. ", not kept")
check(seen[1].frame == madeAt, "event.onloadstate was called on frame " .. tostring(seen[1].frame) .. ", not the branch's " .. madeAt)
check(seen[1].hits == 5 and seen[1].later == nil, "event.onloadstate saw the userdata before it was put back: hits "
	.. tostring(seen[1].hits) .. ", later " .. tostring(seen[1].later))
check(waitRefused == true, "a function called for a load could wait for a frame")

local function same(key, want)
	local got = userdata.get(key)
	local ok = got == want and math.type(got) == math.type(want)
	-- -0.0 equals 0.0: its sign is told by dividing by it
	if ok and want == 0 and math.type(want) == "float" then ok = 1 / got == 1 / want end
	check(ok, "userdata \"" .. key .. "\" came back as " .. tostring(got) .. " (" .. tostring(math.type(got) or type(got)) .. "), not "
		.. tostring(want) .. " (" .. tostring(math.type(want) or type(want)) .. ")")
end
same("hits", 5)
same("big", math.mininteger)
same("third", 1 / 3)
same("whole float", 2.0)
same("tiny", -0.0)
same("text", text)
same("empty", "")
same("flag", true)
same("off", false)
same("a name with spaces & %", "kept")
check(userdata.get("later") == nil, "a value set after the branch was made is still there once it is loaded")
check(#userdata.get_keys() == 10, #userdata.get_keys() .. " values are kept, not 10")

-- moving the machine is no load: TAStudio's old control scheme, and a core
-- state. A branch load pauses the machine, as Chimera's: it plays again to run
-- a frame
local loadsBefore = chimera.branches.loads
chimera.branches.oldControlScheme = true
client.unpause()
emu.frameadvance()
chimera.branches.load(0)
for _ = 1, 50 do emu.yield() end
chimera.branches.oldControlScheme = false
local id = memorysavestate.savecorestate()
memorysavestate.loadcorestate(id)
memorysavestate.removestate(id)
for _ = 1, 10 do emu.yield() end
check(#seen == 1, "event.onloadstate was called for a branch load that only moved the machine, or for a core state: "
	.. #seen .. " calls")
check(chimera.branches.loads == loadsBefore, "the old control scheme's load was counted as a branch load")

finish("OK", "")
