-- The on-screen text's size while it does not grow with the window (issue
-- 488), for "osd text size" (osd-text-size.py): the frame counter shown or
-- not, at the size OSD_SIZE names, with Scale OSD text with the window off.
-- The window is shot when its measuring ends.
chimera.display.osdFollowsWindow = false
chimera.display.osdTextSize = tonumber(os.getenv("OSD_SIZE"))
chimera.session.showFrameCounter = os.getenv("OSD_COUNTER") == "1"
-- nothing played: quitting does not ask about the project
chimera.customize.neverAskSave = true
