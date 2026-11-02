-- Pause on a pad's right trigger, as issue 466's reporter had it; the
-- project waits paused (Start Paused) until the pad presses it. Run by
-- gamepad-hotkeys.py.
chimera.hotkeys.edit()
chimera.hotkeys.bind("Pause", "X1 RightTrigger")
chimera.hotkeys.save()
-- the frames played are a change to the project: quitting does not ask about it
chimera.customize.neverAskSave = true
