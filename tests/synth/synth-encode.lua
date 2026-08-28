-- Level B-encode of the synthetic witness: Chimera encodes part of a real run
-- into a real video file, through the same job Encode Video runs.
--
-- What it is here to catch is everything that used to be assembled by hand and
-- could therefore be assembled wrongly: a writer that never opened, a range off
-- by one at either end, a file never closed, and an emulator left somewhere
-- else when it was over.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   out=<path for the video>
--   meta=<path for result metadata>
--   from=<first movie frame>
--   to=<last movie frame>
--   command=<the middle of the ffmpeg command line>

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
		"before=" .. (meta.before or -1),
		"after=" .. (meta.after or -1),
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

if not movie.isloaded() then
	finish("ERROR", "no movie is loaded - did --project fail?")
end

pcall(function() client.invisibleemulation(true) end)

-- Where the person was. An encode borrows the emulator and has to give it back.
meta.before = emu.framecount()

local err = client.encodevideo(job.out, tonumber(job.from), tonumber(job.to), job.command)
if err ~= nil then
	finish("ERROR", err)
end

-- The run loop does the encoding; this only keeps turning the crank until it
-- says it is finished. The guard is so a broken encode fails the gate rather
-- than hanging it.
local guard = 0
while client.encodingvideo() and guard < 100000 do
	emu.frameadvance()
	guard = guard + 1
end

meta.after = emu.framecount()
if client.encodingvideo() then
	finish("ERROR", "the encode never finished")
end

finish("OK", "")
