-- Level B-greenzone of the synthetic witness: TAStudio's Greenzone choice
-- saved with the project and found there again (issue #158).
--
-- The choice - every frame, one in N, off - lives in TAStudio's part of the
-- .chimeraProject (user-decided, 2026-09-28). This script only opens the
-- project in TAStudio and saves it in place; the leg writes the choice into the
-- file between two runs and reads what the second save wrote. A project whose
-- choice was read on opening saves the same choice back; one that was ignored
-- saves "every frame", which is what TAStudio starts every movie on.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   meta=<path for result metadata>

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local metaPath
local function finish(status, detail)
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

if not movie.isloaded() then
	finish("ERROR", "no movie is loaded - did --project fail?")
end
if not tastudio.engaged() then
	client.opentasstudio()
end
if not tastudio.engaged() then
	finish("ERROR", "TAStudio would not open")
end

-- a few turns of the loop, so the piano roll has finished opening the project
for _ = 1, 10 do emu.yield() end

movie.save()
finish("OK", "")
