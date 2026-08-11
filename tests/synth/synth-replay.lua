-- Level B of the synthetic witness: replay a synth movie through EmuHawk's
-- frontend input pipeline and dump the final RAM and VRAM (framebuffer) domains.
--
-- Job description is read from the file named by the MINIHAWK_JOB env var:
--   movie=<path to movie txt ("|UDLRABST|" per frame)>
--   outram=<path for the 4096-byte final RAM dump>
--   outvram=<path for the 15360-byte final framebuffer dump>
--   meta=<path for result metadata>
--   mode=simple|rerecord

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local meta = {}
local function finish(status, detail)
	local lines = {
		"status=" .. status,
		"detail=" .. (detail or ""),
		"frames=" .. (meta.frames or 0),
		"startframe=" .. (meta.startframe or -1),
	}
	if meta.metaPath then
		writeAll(meta.metaPath, table.concat(lines, "\n") .. "\n")
	end
	client.exit()
end

local jobPath = os.getenv("MINIHAWK_JOB")
if jobPath == nil then error("MINIHAWK_JOB env var not set") end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
meta.metaPath = job.meta

if emu.getsystemid() ~= "Synth" then
	finish("ERROR", "wrong system id: " .. tostring(emu.getsystemid()))
end

local frames = {}
do
	local f = assert(io.open(job.movie, "rb"))
	for line in f:lines() do
		line = line:gsub("\r$", "")
		if line:sub(1, 1) == "|" then frames[#frames + 1] = line end
	end
	f:close()
end

-- clean power-on (EmuHawk emulates one frame during rom load)
if emu.framecount() > 0 then
	client.reboot_core()
	if emu.framecount() > 0 then emu.frameadvance() end
end
meta.startframe = emu.framecount()
if meta.startframe ~= 0 then
	finish("ERROR", "could not reach clean frame 0 (framecount=" .. meta.startframe .. ")")
end

pcall(function() client.speedmode(6400) end)
pcall(function() client.invisibleemulation(true) end)

-- mnemonic order matches the pad bitmask: U D L R A B Select(S) Start(T)
local names = { "Up", "Down", "Left", "Right", "A", "B", "Select", "Start" }
local rerecord = (job.mode == "rerecord")
local stateId = nil
if rerecord then stateId = memorysavestate.savecorestate() end

for i = 1, #frames do
	if rerecord then memorysavestate.loadcorestate(stateId) end
	local buttons = {}
	for b = 1, 8 do
		local c = frames[i]:sub(1 + b, 1 + b)
		buttons["P1 " .. names[b]] = (c ~= "." and c ~= "" and c ~= "|")
	end
	joypad.set(buttons)
	emu.frameadvance()
	if rerecord then
		memorysavestate.removestate(stateId)
		stateId = memorysavestate.savecorestate()
	end
end

meta.frames = emu.framecount()

local function dumpDomain(domain, size, path)
	local bytes = memory.read_bytes_as_array(0, size, domain)
	local chunks = {}
	for i = 1, #bytes do chunks[i] = string.char(bytes[i]) end
	writeAll(path, table.concat(chunks))
end
dumpDomain("RAM", 4096, job.outram)
dumpDomain("VRAM", 15360, job.outvram)

finish("OK", "")
