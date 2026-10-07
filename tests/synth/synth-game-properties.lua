-- Level B-properties of the synthetic witness: a core's game properties, read
-- and written by name (docs/game-cores.md).
--
-- The synth exports a property table (package-box/synth_wbx.c) naming places in
-- its RAM. This script checks the frontend hands it over whole - the names in
-- the core's order, the types it gives - that a property reads the same bytes
-- memory.* reads, and that a property set by name is what the game plays on:
-- the cursor moved by game.set is drawn where it was put.
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

-- the names, in the core's order
local names = game.list()
local want = { "Status", "Cursor.X", "Cursor.Y", "Steps", "Started",
	"Test.Frames", "Test.Balance", "Test.Gravity", "Test.Name", "Test.Wide", "Test.Key", "Test.Score",
	"Test.Row", "Test.Col", "Test.Flag", "Test.Nibble" }
check(#names == #want, "game.list() has " .. #names .. " names, the core exports " .. #want)
for i, name in ipairs(want) do
	check(names[i] == name, "game.list()[" .. i .. "] is " .. tostring(names[i]) .. ", not " .. name)
end

-- The project's movie is five frames, the last four holding Down, and a
-- headless run pauses where it ends - so everything here happens inside it,
-- and nothing assumes where the cursor stands, only that Down never moves it
-- sideways.
emu.frameadvance()
check(game.get("Started") == true, "Started reads " .. tostring(game.get("Started")) .. " once the game has run, not true")
check(game.get("Cursor.X") == memory.readbyte(1, "RAM") and game.get("Cursor.Y") == memory.readbyte(2, "RAM"),
	"the cursor reads (" .. tostring(game.get("Cursor.X")) .. "," .. tostring(game.get("Cursor.Y")) .. "), RAM holds ("
	.. memory.readbyte(1, "RAM") .. "," .. memory.readbyte(2, "RAM") .. ")")
check(game.describe("Status").label == "Playing", "Status is described as " .. tostring(game.describe("Status").label) .. ", not Playing")
check(game.describe("Steps").type == "u32" and game.describe("Steps").size == 4, "Steps is not described as a four-byte u32")

-- a u32 is four bytes, little-endian
game.set("Steps", 0x01020304)
check(memory.readbyte(4, "RAM") == 4 and memory.readbyte(7, "RAM") == 1, "game.set wrote Steps in the wrong byte order")
check(game.get("Steps") == 0x01020304, "Steps reads back " .. tostring(game.get("Steps")))
game.set("Steps", 0)

-- the game plays on what was set: the cursor, put two cells right, is drawn there
local function cellPixel(cx, cy) return memory.readbyte((cy * 8 + 4) * 128 + cx * 8 + 4, "VRAM") end
local x, y = game.get("Cursor.X"), game.get("Cursor.Y")
local cursorInk = cellPixel(x, y)
check(cursorInk ~= cellPixel(x + 2, y), "the cursor's cell and the one it will be moved to look the same ("
	.. tostring(cursorInk) .. "); this test cannot tell them apart")
game.set("Cursor.X", x + 2)
emu.frameadvance()
local nowY = game.get("Cursor.Y")
check(game.get("Cursor.X") == x + 2, "Cursor.X reads " .. tostring(game.get("Cursor.X")) .. " a frame after it was set to " .. (x + 2))
check(cellPixel(x + 2, nowY) == cursorInk, "the game did not draw the cursor where game.set put it")
check(cellPixel(x, nowY) ~= cursorInk and cellPixel(x, y) ~= cursorInk, "the cursor is still drawn in its old column")

-- every other kind of property, through the engine, read back byte by byte
local function bytesAt(address, count)
	local t = {}
	for k = 0, count - 1 do t[#t + 1] = memory.readbyte(address + k, "RAM") end
	return table.concat(t, ",")
end

check(game.set("Test.Frames", -1), "Test.Frames would not take -1")
check(bytesAt(256, 8) == "255,255,255,255,255,255,255,255", "a u64 of all ones is " .. bytesAt(256, 8))
check(game.get("Test.Frames") == -1, "a u64 of all ones reads as " .. tostring(game.get("Test.Frames")) .. ", not the integer with the same bits")
check(game.describe("Test.Frames").label == "18446744073709551615", "a u64 of all ones is shown as " .. tostring(game.describe("Test.Frames").label))

check(game.set("Test.Balance", math.mininteger), "Test.Balance would not take the least 64-bit integer")
check(bytesAt(264, 8) == "0,0,0,0,0,0,0,128", "the least s64 is " .. bytesAt(264, 8))
check(game.get("Test.Balance") == math.mininteger, "the least s64 reads back as " .. tostring(game.get("Test.Balance")))

check(game.set("Test.Gravity", 0.1), "Test.Gravity would not take 0.1")
check(game.get("Test.Gravity") == 0.1, "an f64 of 0.1 reads back as " .. tostring(game.get("Test.Gravity")))
check(bytesAt(272, 8) == "154,153,153,153,153,153,185,63", "an f64 of 0.1 is " .. bytesAt(272, 8))

check(game.set("Test.Name", "Dungeon"), "Test.Name would not take text")
check(bytesAt(280, 8) == "68,117,110,103,101,111,110,0", "\"Dungeon\" is " .. bytesAt(280, 8))
check(game.set("Test.Name", "The Palace Gate") and game.get("Test.Name") == "The Pala", "text longer than its length is not cut at it: " .. tostring(game.get("Test.Name")))
check(game.set("Test.Wide", "Hi") and bytesAt(288, 6) == "72,0,105,0,0,0", "\"Hi\" in utf16le is " .. bytesAt(288, 6))
check(game.get("Test.Wide") == "Hi", "utf16le text reads back as " .. tostring(game.get("Test.Wide")))

check(game.set("Test.Key", { 0xDE, 0xAD, 0xBE, 0xEF }), "Test.Key would not take four bytes")
check(bytesAt(296, 4) == "222,173,190,239", "the bytes are " .. bytesAt(296, 4))
local key = game.get("Test.Key")
check(type(key) == "table" and #key == 4 and key[1] == 0xDE and key[4] == 0xEF, "bytes do not read back as a table of them")
check(game.describe("Test.Key").label == "DE AD BE EF", "bytes are shown as " .. tostring(game.describe("Test.Key").label))
check(not game.set("Test.Key", { 1, 2 }) and bytesAt(296, 4) == "222,173,190,239", "two bytes were taken for four: " .. bytesAt(296, 4))

check(game.set("Test.Score", 0x01020304) and bytesAt(300, 4) == "1,2,3,4", "a big-endian u32 is " .. bytesAt(300, 4))

check(game.set("Test.Row", { 10, -20, 30, -40 }), "an array would not take a table of its elements")
check(game.set("Test.Col[3]", 7), "an element would not be set by its name")
check(bytesAt(304, 16) == "10,0,0,0,236,255,0,0,30,0,0,0,216,255,7,0",
	"two interleaved arrays are " .. bytesAt(304, 16))
local row = game.get("Test.Row")
check(type(row) == "table" and #row == 4 and row[2] == -20 and row[4] == -40, "an array does not read back as a table of its elements")
check(game.get("Test.Row[1]") == -20, "an element read by its name is " .. tostring(game.get("Test.Row[1]")))
check(game.describe("Test.Row").count == 4 and game.describe("Test.Row").stride == 4, "the array is not described as four with a stride of four")
check(game.get("Test.Row[4]") == nil, "an element past the end is not nil")

check(game.set("Test.Nibble", 10) and game.set("Test.Flag", true), "the bit fields would not take their values")
check(memory.readbyte(320, "RAM") == 0xA4, "two bit fields in a byte make " .. memory.readbyte(320, "RAM") .. ", not 164")
check(game.set("Test.Flag", 0) and memory.readbyte(320, "RAM") == 0xA0, "clearing one bit field touched the other")
check(not game.set("Test.Flag", 2), "a one-bit field took 2")

-- a name the core does not have reads as nothing and sets nothing
check(game.get("No Such Property") == nil, "game.get of an unknown name returned something")
check(game.set("No Such Property", 1) == false, "game.set of an unknown name said it set it")
check(game.set("Cursor.Y", 2) == true and game.get("Cursor.Y") == 2, "game.set of a known name did not say it set it")

finish("OK", "")
