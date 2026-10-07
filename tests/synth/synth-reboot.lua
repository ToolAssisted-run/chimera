-- Reboot Core inside a project (issue #196): the project must still be there.
--
-- The script plays the project's movie part of the way, reboots the core, and
-- then looks at what it is left holding: the same movie (its length and its
-- file), a machine back at power-on, and - with the piano roll open - the
-- marker it set before the reboot. It then plays the movie to its end and
-- dumps RAM, which must be the golden every other level lands on: the inputs
-- the rebooted machine was fed are the project's own.
--
-- A reboot used to be a rom reload like any other: with the piano roll open
-- TAStudio replaced the movie with a blank default.chimeraProject, and with
-- it closed the movie was saved, stopped and disposed.
--
-- Job description is read from the file named by the CHIMERA_JOB env var:
--   outram=<path for the final RAM dump>
--   meta=<path for result metadata>
--   tastudio=<1 to open the piano roll first>
--   rebootat=<frame to reboot on>

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local jobPath = os.getenv("CHIMERA_JOB")
if jobPath == nil then error("CHIMERA_JOB env var not set") end
local job = {}
for line in io.lines(jobPath) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end

local function finish(status, detail)
	writeAll(job.meta, "status=" .. status .. "\ndetail=" .. (detail or "") .. "\n")
	client.exit()
end

if not movie.isloaded() then finish("ERROR", "no movie is loaded") end
if job.tastudio == "1" then
	if not tastudio.engaged() then client.opentasstudio() end
	if not tastudio.engaged() then finish("ERROR", "the piano roll did not open") end
end

-- TAStudio owns playback while it is engaged: it is told where to go
local function playTo(target)
	if tastudio.engaged() then
		tastudio.setplayback(target)
		client.unpause()
		while emu.framecount() < target do emu.yield() end
		client.pause()
	else
		while emu.framecount() < target do emu.frameadvance() end
	end
end

pcall(function() client.speedmode(6400) end)
local length, file = movie.length(), movie.filename()
if tastudio.engaged() then tastudio.setmarker(7, "kept") end
playTo(tonumber(job.rebootat))

client.reboot_core()
-- said out loud: a leg whose script never got this far must not pass for it
io.stderr:write("[synth-reboot] rebooted at frame " .. job.rebootat .. "\n")

if movie.length() ~= length then
	finish("FAIL", "the movie is " .. tostring(movie.length()) .. " frames after the reboot, not " .. length)
end
if movie.filename() ~= file then
	finish("FAIL", "the movie is " .. tostring(movie.filename()) .. " after the reboot, not the project")
end
if emu.framecount() ~= 0 then
	finish("FAIL", "the machine is at frame " .. emu.framecount() .. " after the reboot, not at power-on")
end
if job.tastudio == "1" then
	if not tastudio.engaged() then finish("FAIL", "the piano roll let go of the project") end
	if tastudio.getmarker(7) ~= "kept" then finish("FAIL", "the marker set before the reboot is gone") end
end

playTo(length)

local bytes = memory.read_bytes_as_array(0, 4096, "RAM")
local chunks = {}
for i = 1, #bytes do chunks[i] = string.char(bytes[i]) end
writeAll(job.outram, table.concat(chunks))
finish("OK", "")
