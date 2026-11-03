-- What the window draws, for "window drawing" (window-drawing.py): two
-- lines of text through gui.drawText and gui.text, every frame; RAM Watch
-- with three watches and RAM Search with a new search, for their lists; then three
-- frames encoded with Capture OSD and the frame counter shown, written as raw pixels to the file
-- CHIMERA_DRAW_VIDEO names, and the picture area's size and the encode's
-- answer to the file CHIMERA_DRAW_META names; then TAStudio with two input
-- rolls. The window goes on playing and drawing until the test's
-- screenshots are taken.
local function draw()
	gui.drawText(4, 4, "MM\nMM", 0xFFFF00FF, 0xFF000000, 12)
	gui.text(200, 40, "WW\nWW", 0xFF00FFFF, "topright")
end
for _ = 1, 10 do
	draw()
	emu.frameadvance()
end
local watch = client.gettool("ramwatch")
for address = 0, 2 do watch.add(memory.getcurrentmemorydomain(), address, 1, 0, false, "") end
client.gettool("ramsearch").newSearch()
chimera.session.showFrameCounter = true
chimera.encoder.captureOsd = true
chimera.encoder.resize = false
chimera.encoder.audioSync = false
local err = client.encodevideo(os.getenv("CHIMERA_DRAW_VIDEO"), 2, 4, "-an -f rawvideo -pix_fmt bgra")
while client.encodingvideo() do
	draw()
	emu.frameadvance()
end
local meta = assert(io.open(os.getenv("CHIMERA_DRAW_META"), "w"))
meta:write(string.format("area=%d %d\nerror=%s\nfinished=1\n", client.screenwidth(), client.screenheight(), err or ""))
meta:close()
-- TAStudio with a second input roll, and a script column so wide that the
-- first roll's columns are wider than the window: painted green, it must not
-- show under the side's boxes (issue 442)
tastudio.addcolumn("wide", "Wide", 1500)
tastudio.onqueryitembg(function(index, column) if column == "wide" then return 0xFF00FF00 end end)
client.gettool("tastudio").newRoll()
-- played frames are changes to the project: quitting does not ask about them
chimera.customize.neverAskSave = true
while true do
	draw()
	emu.frameadvance()
end
