-- Level B memory calls of the synthetic witness (issue 496): event.on_bus_exec,
-- on_bus_read and on_bus_write on a core that watches memory.
--
-- The synth names two scopes, "Code" (the instructions it runs) and "RAM",
-- and calls the host part way through the frame at each access the host
-- watches (package-box/synth_wbx.c). gridWalker starts each frame at the
-- instruction at 0, and reads its status byte (RAM 0) twice in every frame.
-- So a function on the instruction at 0 is called once a frame, one on reads
-- of RAM 0 twice, each with the value memory holds and the flags of the
-- access; one on every write is given the value memory holds once the frame
-- ends. A scope the core does not have, or a kind of access a scope does not
-- take, is refused; removed, the functions are called no more, and one
-- registered on the same address at once, before a frame, is (issue 570).
--
-- The synth lets a function change a read's or a write's value (issue 534).
-- gridWalker reads its init flag (RAM 8) once a playing frame and puts the
-- cursor back on its start when it reads 0, writing the flag again. With
-- functions on that read giving back 0, then 1, then nothing, the last
-- number (1) is used: the flag is not written. Without the one giving 1, the
-- 0 is used, and nothing after it undoes it: the flag is written, and the
-- function on that write makes it 7. One is left registered when Chimera
-- closes.
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
local wrongFlags = {}
local ok, execId = pcall(event.on_bus_exec, function(addr, val, flags)
	starts = starts + 1
	if addr ~= 0 or val ~= 0 then startsElsewhere = startsElsewhere + 1 end
	if flags ~= 0x4000 then wrongFlags[#wrongFlags + 1] = "instruction " .. tostring(flags) end
end, 0, "each frame's start", "Code")
check(ok, "event.on_bus_exec was refused: " .. tostring(execId))
local readId
ok, readId = pcall(event.on_bus_read, function(addr, val, flags)
	reads = reads + 1
	if addr ~= 0 or val ~= memory.readbyte(0, "RAM") then readsWrong = readsWrong + 1 end
	-- a read of a byte
	if flags ~= 0x11000 then wrongFlags[#wrongFlags + 1] = "read " .. tostring(flags) end
end, 0, "the status", "RAM")
check(ok, "event.on_bus_read was refused: " .. tostring(readId))
local writeId
local written = {}
ok, writeId = pcall(event.on_bus_write, function(addr, val, flags)
	writes = writes + 1
	written[addr] = val
	-- a byte, or the step counter's four bytes
	local want = addr == 4 and 0x32000 or 0x12000
	if flags ~= want then wrongFlags[#wrongFlags + 1] = "write " .. tostring(flags) .. " at " .. addr end
end, nil, "every write", "RAM")
check(ok, "event.on_bus_write on every address was refused: " .. tostring(writeId))

-- what the core cannot watch is refused, and the script goes on
local refused, why = pcall(event.on_bus_read, function() end, 0, "a read of code", "Code")
check(not refused and tostring(why):find("cannot watch", 1, true) ~= nil, "a read in the Code scope was not refused: " .. tostring(why))
refused, why = pcall(event.on_bus_exec, function() end, 0, "nowhere", "Nowhere")
check(not refused and tostring(why):find("no memory scope", 1, true) ~= nil, "an unknown scope was not refused: " .. tostring(why))

-- The project's movie is five frames and a headless run pauses where it ends,
-- so everything happens inside it.
check(event.can_replace_value("read") and event.can_replace_value("write") and not event.can_replace_value("exec"),
	"the synth's reads and writes were not said to be changeable, or its instructions were")
emu.frameadvance()
local s0, r0 = starts, reads
written = {}
emu.frameadvance()
check(starts == s0 + 1, "a frame ran the instruction at 0 " .. (starts - s0) .. " times by the calls, not once")
check(reads == r0 + 2, "a frame read the status " .. (reads - r0) .. " times by the calls, not twice")
check(startsElsewhere == 0 and readsWrong == 0, startsElsewhere .. " instruction calls and " .. readsWrong
	.. " read calls gave another address or a value memory did not hold")
for addr, val in pairs(written) do
	if memory.readbyte(addr, "RAM") ~= (val & 0xFF) then writesWrong = writesWrong + 1 end
end
check(writes > 0 and writesWrong == 0, writes .. " write calls, " .. writesWrong .. " of the places written not holding the value given once the frame ended")
check(#wrongFlags == 0, "calls had other flags: " .. table.concat(wrongFlags, ", ", 1, math.min(#wrongFlags, 4)))

event.unregisterbyid(execId)
event.unregisterbyid(readId)
event.unregisterbyid(writeId)
-- one put back on the same address at once, before a frame has run, is
-- called: the core still watches it (issue 570)
local againStarts = 0
local againId = event.on_bus_exec(function() againStarts = againStarts + 1 end, 0, "the frame's start again", "Code")
local s1, r1, w1 = starts, reads, writes
emu.frameadvance()
check(starts == s1 and reads == r1 and writes == w1, "the functions were still called after they were removed")
check(againStarts == 1, "a function registered on an address right after one there was removed was called "
	.. againStarts .. " times in a frame, not once")
event.unregisterbyid(againId)

-- the init flag's read given 0, then 1, then nothing: the 1 is used
local flagReads, flagWrites, givenFlag = 0, 0, {}
-- a function called inside the frame may not save a state there
local stateRefused
local zeroId = event.on_bus_read(function(addr, val)
	flagReads = flagReads + 1
	givenFlag[#givenFlag + 1] = val
	local saved, why = pcall(memorysavestate.savecorestate)
	stateRefused = not saved and tostring(why):find("not allowed", 1, true) ~= nil
	return 0
end, 8, "init flag 0", "RAM")
local oneId = event.on_bus_read(function(addr, val)
	givenFlag[#givenFlag + 1] = val
	return 1
end, 8, "init flag 1", "RAM")
local noneId = event.on_bus_read(function(addr, val)
	givenFlag[#givenFlag + 1] = val
end, 8, "init flag left", "RAM")
local sevenId = event.on_bus_write(function(addr, val)
	flagWrites = flagWrites + 1
	return 7
end, 8, "init flag 7", "RAM")
emu.frameadvance()
check(againStarts == 1, "the function put back was still called after it was removed")
check(flagReads == 1, "the init flag was read " .. flagReads .. " times in a frame, not once")
check(stateRefused == true, "a function called inside the frame could save a state there")
check(givenFlag[1] == 1 and givenFlag[2] == 1 and givenFlag[3] == 1, "the functions on one read were not each given the value read: "
	.. tostring(givenFlag[1]) .. ", " .. tostring(givenFlag[2]) .. ", " .. tostring(givenFlag[3]))
check(flagWrites == 0 and memory.readbyte(8, "RAM") == 1, "with the last number given 1, the game still saw 0: the flag was written "
	.. flagWrites .. " times and holds " .. memory.readbyte(8, "RAM"))
-- without the 1: the 0 is used, and the function giving nothing after it leaves it
event.unregisterbyid(oneId)
emu.frameadvance()
check(flagWrites == 1, "with 0 given and nothing after it, the game wrote its flag " .. flagWrites .. " times, not once")
check(memory.readbyte(8, "RAM") == 7, "the flag's write was given 7, and memory holds " .. memory.readbyte(8, "RAM"))
event.unregisterbyid(zeroId)
event.unregisterbyid(noneId)
event.unregisterbyid(sevenId)

-- one more, left registered when the script ends, as most scripts leave
-- theirs: Chimera closes the core before the script, and taking the function
-- off a closed core must not stop Chimera with an error
event.on_bus_exec(function() end, 0, "left registered")
finish("OK", "")
