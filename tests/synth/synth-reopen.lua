-- Level B-reopen of the synthetic witness: a second project opened on top of a
-- first, in one session.
--
-- This is the shape that used to throw. Booting over a live session ran the old
-- machine's close INSIDE the new one's load, where it took the movie the load
-- had just queued, and RunQueuedMovie fell over on the nothing it was left. The
-- session has to end before the next one begins.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   second=<path of the project to open on top of the running one>
--   outram=<path for the final RAM dump, from the SECOND project>
--   meta=<path for result metadata>

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
		"firstlength=" .. (meta.firstLength or -1),
		"secondlength=" .. (meta.secondLength or -1),
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

-- the script can be resumed before the project on the command line has finished
-- attaching its movie, so give it a few frames to appear rather than racing it
local waited = 0
while not movie.isloaded() and waited < 120 do
	emu.frameadvance()
	waited = waited + 1
end
-- the script can be resumed before the project named on the command line has
-- finished attaching its movie, so give it a few frames to appear rather than
-- racing it
local waited = 0
while not movie.isloaded() and waited < 120 do
	emu.frameadvance()
	waited = waited + 1
end
if not movie.isloaded() then
	finish("ERROR", "no movie is loaded - did the first --project fail?")
end
meta.firstLength = movie.length()

-- TAStudio open, because that is the session: the crash this pins was the old
-- window's close running inside the new project's load.
pcall(function() client.opentasstudio() end)

-- on top of the running one, with nothing closed by hand first
if not client.openproject(job.second) then
	finish("ERROR", "the second project refused to open")
end

if not movie.isloaded() then
	finish("ERROR", "the second project opened but left no movie")
end
meta.secondLength = movie.length()

finish("OK", "")
