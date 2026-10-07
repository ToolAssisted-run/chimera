-- Level B-tastudio of the synthetic witness: a script's input reaching the
-- movie while TAStudio EXTENDS it past its end.
--
-- Every other frontend leg drives a movie that already has the frames it
-- plays. This one asks for the frames that do not exist yet: TAStudio is told
-- to seek past the end of the log, so each frame out there has to be authored
-- as it is reached, and what it is authored FROM is the whole question
-- (issue #95). A seek turns recording off for its duration so the rows it
-- passes over are replayed rather than typed over - out past the end there are
-- no rows to protect, and the frames were being written from the autoholds
-- alone, which threw away everything a hand or a script was pressing.
--
-- Driving TAStudio unattended: it owns playback, so the emulator is told where
-- to go (setplayback) and then kept running. client.unpause() has to be inside
-- the loop with emu.yield() - NOT emu.frameadvance() - because the piano roll
-- pauses again on its own the moment a seek ends, and a script parked in
-- frameadvance is never resumed to notice (the unpause() doc says exactly
-- this). joypad.set() lands before the frame it is meant for either way: a
-- yielding script is resumed between the run loop's controller pass and the
-- frame itself.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   meta=<path for result metadata>
--   outram=<path for the final RAM dump>
--   extend=<frames to add past the end of the log>
--   button=<button to hold while they are added>
--   press=1     hold that button from the script every frame (0: press nothing)
--   record=1    ask TAStudio for recording mode first (0: leave it read-only)

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
		"length=" .. (meta.length or -1),
		"frames=" .. (meta.frames or -1),
		"recorded=" .. (meta.recorded or -1),
		"machine=" .. (meta.machine or -1),
		"recording=" .. tostring(meta.recording),
	}
	if meta.metaPath then
		writeAll(meta.metaPath, table.concat(lines, "\n") .. "\n")
	end
	client.exit()
end

local jobPath = os.getenv("CHIMERA_JOB")
if jobPath == nil then error("CHIMERA_JOB env var not set") end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
meta.metaPath = job.meta

local extend = tonumber(job.extend or "") or 20
local button = job.button or "P1 Down"
local press = job.press == "1"
local record = job.record == "1"

if not movie.isloaded() then
	finish("ERROR", "no movie is loaded - did --project fail?")
end
if not tastudio.engaged() then
	client.opentasstudio()
end
if not tastudio.engaged() then
	finish("ERROR", "TAStudio would not open")
end
pcall(function() client.speedmode(6400) end)

local length = movie.length()
meta.length = length
if length <= 0 then
	finish("ERROR", "the project has no frames")
end

if record then
	pcall(function() tastudio.setrecording(true) end)
	if not tastudio.getrecording() then
		finish("ERROR", "TAStudio would not enter recording mode")
	end
end

-- past the end: every frame from `length` on has to be invented as it is reached
local target = length + extend
pcall(function() tastudio.setplayback(target) end)
pcall(function() client.unpause() end)

-- joypad.getwithmovie() is the controller the core is handed, read just after
-- the frame that was handed it: the machine's own pad readout, and the half of
-- the report that a log entry alone cannot answer
local spins = 0
local seen = 0
local was = emu.framecount()
while emu.framecount() < target and spins < 20 * extend + 200 do
	if press then joypad.set({ [button] = true }) end
	client.unpause()
	emu.yield()
	local now = emu.framecount()
	if now > was then
		-- `was` is the frame that just ran, so only count once the seek has left
		-- the rows the project already had: those come from the log and hold
		-- whatever the project recorded, which is not what is being asked here
		if was >= length then
			local out = joypad.getwithmovie()
			if out ~= nil and out[button] then seen = seen + 1 end
		end
		was = now
	end
	spins = spins + 1
end
meta.machine = seen
meta.recording = tastudio.getrecording()
if emu.framecount() < target then
	finish("ERROR", string.format("stalled at frame %d, wanted %d (%d spins)",
		emu.framecount(), target, spins))
end

-- what the movie kept: the log is what a reopened project would replay, so it
-- is the movie's own answer to "was that press authored?"
local recorded = 0
for f = length, target - 1 do
	local entry = movie.getinput(f)
	if entry ~= nil and entry[button] then recorded = recorded + 1 end
end
meta.frames = target - length
meta.recorded = recorded

-- and what the MACHINE did with them: the caller compares this dump against
-- the same run with nothing pressed, so a log that reads right but never
-- reached the core cannot pass
local bytes = memory.read_bytes_as_array(0, 4096, "RAM")
local chunks = {}
for i = 1, #bytes do chunks[i] = string.char(bytes[i]) end
writeAll(job.outram, table.concat(chunks))

finish("OK", "")
