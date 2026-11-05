-- Level B memory calls of the synthetic witness (issue 496): event.on_bus_exec,
-- on_bus_read and on_bus_write on a core that watches memory.
--
-- The synth names two scopes, "Code" (the instructions it runs) and "RAM",
-- and calls the host part way through the frame at each access the host
-- watches (package-box/synth_wbx.c). gridWalker starts each frame at the
-- instruction at 0, and reads its status byte (RAM 0) twice in every frame.
-- So a function on the instruction at 0 is called once a frame, one on reads
-- of RAM 0 twice, each with the value memory holds; one on every write sees
-- the value written already in memory. A scope the core does not have, or a
-- kind of access a scope does not take, is refused; removed, the functions
-- are called no more. One is left registered when Chimera closes.
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

local scopes = event.availableScopes()
check(scopes[0] == "Code" and scopes[1] == "RAM" and scopes[2] == nil,
	"the scopes were not Code and RAM: " .. tostring(scopes[0]) .. ", " .. tostring(scopes[1]) .. ", " .. tostring(scopes[2]))

local starts, startsElsewhere = 0, 0
local reads, readsWrong = 0, 0
local writes, writesWrong = 0, 0
local ok, execId = pcall(event.on_bus_exec, function(addr, val)
	starts = starts + 1
	if addr ~= 0 or val ~= 0 then startsElsewhere = startsElsewhere + 1 end
end, 0, "each frame's start", "Code")
check(ok, "event.on_bus_exec was refused: " .. tostring(execId))
local readId
ok, readId = pcall(event.on_bus_read, function(addr, val)
	reads = reads + 1
	if addr ~= 0 or val ~= memory.readbyte(0, "RAM") then readsWrong = readsWrong + 1 end
end, 0, "the status", "RAM")
check(ok, "event.on_bus_read was refused: " .. tostring(readId))
local writeId
ok, writeId = pcall(event.on_bus_write, function(addr, val)
	writes = writes + 1
	if memory.readbyte(addr, "RAM") ~= (val & 0xFF) then writesWrong = writesWrong + 1 end
end, nil, "every write", "RAM")
check(ok, "event.on_bus_write on every address was refused: " .. tostring(writeId))

-- what the core cannot watch is refused, and the script goes on
local refused, why = pcall(event.on_bus_read, function() end, 0, "a read of code", "Code")
check(not refused and tostring(why):find("cannot watch", 1, true) ~= nil, "a read in the Code scope was not refused: " .. tostring(why))
refused, why = pcall(event.on_bus_exec, function() end, 0, "nowhere", "Nowhere")
check(not refused and tostring(why):find("no memory scope", 1, true) ~= nil, "an unknown scope was not refused: " .. tostring(why))

-- The project's movie is five frames and a headless run pauses where it ends,
-- so everything happens inside it.
emu.frameadvance()
local s0, r0 = starts, reads
emu.frameadvance()
check(starts == s0 + 1, "a frame ran the instruction at 0 " .. (starts - s0) .. " times by the calls, not once")
check(reads == r0 + 2, "a frame read the status " .. (reads - r0) .. " times by the calls, not twice")
check(startsElsewhere == 0 and readsWrong == 0, startsElsewhere .. " instruction calls and " .. readsWrong
	.. " read calls gave another address or a value memory did not hold")
check(writes > 0 and writesWrong == 0, writes .. " write calls, " .. writesWrong .. " of them with a value memory did not hold")

event.unregisterbyid(execId)
event.unregisterbyid(readId)
event.unregisterbyid(writeId)
local s1, r1, w1 = starts, reads, writes
emu.frameadvance()
check(starts == s1 and reads == r1 and writes == w1, "the functions were still called after they were removed")

-- one more, left registered when the script ends, as most scripts leave
-- theirs: Chimera closes the core before the script, and taking the function
-- off a closed core must not stop Chimera with an error
event.on_bus_exec(function() end, 0, "left registered")
finish("OK", "")
