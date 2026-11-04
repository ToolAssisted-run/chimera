-- TAStudio's branch list while branches are made and updated, for "tastudio
-- branches" (run with CHIMERA_QT_STRICT_QML). Each branch made or updated
-- makes the list again, and its rows were drawn once with no frame yet: the
-- colour of each was asked of the piano roll with nothing for a frame, and
-- "argument 1 cannot be nothing" was on the screen twice a branch (issue
-- 483). The window says such a call on standard error and, strict, quits with
-- 1 for it; the script quits with 0 when its branches were made, 1 when not.
client.opentasstudio()
-- frames run, so the list's rows have greenzone colours to ask for
for _ = 1, 60 do emu.frameadvance() end
local function made(n)
	for _ = 1, 600 do
		if chimera.branches.made >= n then return true end
		emu.frameadvance()
	end
	return false
end
chimera.branches.create("right")
local ok = made(1)
for _ = 1, 10 do emu.frameadvance() end
chimera.branches.create("")
ok = ok and made(2)
for _ = 1, 10 do emu.frameadvance() end
chimera.branches.saveSlot(0)
ok = ok and made(3)
-- the window draws the list as it is now before the run ends
for _ = 1, 10 do emu.frameadvance() end
io.stderr:write(string.format("tastudio branches: %d made, %d listed\n", chimera.branches.made, chimera.branches.count))
-- the frames played are a change to the project: quitting does not ask about it
chimera.customize.neverAskSave = true
chimera.toolWindows.exit(ok and chimera.branches.count == 2 and 0 or 1)
