-- Level B-movie of the synthetic witness: Chimera plays a real .bk2 through
-- the ACTUAL movie pipeline (MovieSession, not scripted joypad), and this
-- script only advances frames and dumps RAM/VRAM once the movie's entries are
-- exhausted. The dumps must match the same goldens every other level uses.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   outram=<path for the final RAM dump>
--   outvram=<path for the final framebuffer dump>
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
		"frames=" .. (meta.frames or 0),
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
	finish("ERROR", "no movie is loaded - did --movie fail?")
end
local length = movie.length()
if length <= 0 then
	finish("ERROR", "movie has no frames")
end

pcall(function() client.speedmode(6400) end)
pcall(function() client.invisibleemulation(true) end)

-- the movie machinery supplies every frame's input; we only turn the crank
while emu.framecount() < length do
	emu.frameadvance()
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
