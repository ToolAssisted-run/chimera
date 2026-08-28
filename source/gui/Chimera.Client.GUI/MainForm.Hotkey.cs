using System.Linq;

using Chimera.Emulation.Common;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		private bool CheckHotkey(string trigger)
		{
			// avoid conflict with regular hotkeys
			if (Tools.IsLoaded<TAStudio>() && Tools.TAStudio.AxisEditingMode)
			{
				switch (trigger)
				{
					default:
						return false;

					case "Analog Increment":
						Tools.TAStudio.AnalogIncrementByOne();
						break;
					case "Analog Decrement":
						Tools.TAStudio.AnalogDecrementByOne();
						break;
					case "Analog Incr. by 10":
						Tools.TAStudio.AnalogIncrementByTen();
						break;
					case "Analog Decr. by 10":
						Tools.TAStudio.AnalogDecrementByTen();
						break;
					case "Analog Maximum":
						Tools.TAStudio.AnalogMax();
						break;
					case "Analog Minimum":
						Tools.TAStudio.AnalogMin();
						break;
				}

				return true;
			}

			switch (trigger)
			{
				default:
					return false;

				// Hotkeys handled elsewhere, via the hotkey controller
				case "Autohold":
				case "Autofire":
				case "Frame Advance":
				case "Turbo":
				case "Rewind":
				case "Fast Forward":
					break;

				// General
				case "Pause":
					TogglePause();
					break;
				case "Frame Inch":
					//special! allow this key to get handled as Frame Advance, too
					FrameInch = true;
					break;
				case "Toggle Throttle":
					ToggleUnthrottled();
					break;
				case "Clear Autohold":
					ClearAutohold();
					break;
				case "Screenshot":
					TakeScreenshot();
					break;
				case "Screen Raw to Clipboard":
					// Ctrl+C clash. any tool that has such acc must check this.
					// maybe check if mainform has focus instead?
					if (!(Tools.IsLoaded<TAStudio>() && Tools.Get<TAStudio>().ContainsFocus)) TakeScreenshotToClipboard();
					break;
				case "Screen Client to Clipboard":
					TakeScreenshotClientToClipboard();
					break;
				case "Full Screen":
					ToggleFullscreen();
					break;
				case "Close ROM":
					LoadNullRom();
					break;
				case "Display FPS":
					ToggleFps();
					break;
				case "Frame Counter":
					ToggleFrameCounter();
					break;
				case "Lag Counter":
					if (Emulator.CanPollInput()) ToggleLagCounter();
					break;
				case "Input Display":
					ToggleInputDisplay();
					break;
				case "Toggle BG Input":
					ToggleBackgroundInput();
					break;
				case "Toggle Menu":
					ShowMenuContextMenuItem_Click(this, EventArgs.Empty);
					break;
				case "Volume Up":
					VolumeUp();
					break;
				case "Volume Down":
					VolumeDown();
					break;
				case "Toggle Sound":
					ToggleSound();
					break;
				case "Exit Program":
					ScheduleShutdown();
					break;
				case "Encode Video":
					EncodeVideoDialog();
					break;
				case "Larger Window":
					IncreaseWindowSize();
					break;
				case "Smaller Window":
					DecreaseWindowSize();
					break;
				case "Increase Speed":
					IncreaseSpeed();
					break;
				case "Reset Speed":
					ResetSpeed();
					break;
				case "Decrease Speed":
					DecreaseSpeed();
					break;
				case "Reboot Core":
					RebootCore();
					break;
				case "Toggle Skip Lag Frame":
					Config.SkipLagFrame = !Config.SkipLagFrame;
					AddOnScreenMessage($"Skip Lag Frames toggled {(Config.SkipLagFrame ? "On" : "Off")}");
					break;
				case "Toggle Key Priority":
					ToggleKeyPriority();
					break;
				case "Toggle Messages":
					DisplayMessagesMenuItem_Click(this, EventArgs.Empty);
					break;
				case "Toggle Display Nothing":
					// TODO: account for 1 when implemented
					Config.DispSpeedupFeatures = Config.DispSpeedupFeatures == 0 ? 2 : 0;
					break;
				case "Accept Background Input":
					ToggleBackgroundInput();
					break;
				case "Capture Mouse":
					ToggleCaptureMouse();
					break;
				case "Toggle Stay on Top":
					ToggleStayOnTop();
					break;



				// Movie
				case "Toggle read-only":
					ToggleReadOnly();
					break;
				case "Stop Movie":
					StopMovie();
					break;
				case "Play from beginning":
					_ = RestartMovie();
					break;
				case "Save Movie":
					SaveMovie();
					break;

				// Tools
				case "RAM Watch":
					RamWatchMenuItem_Click(this, EventArgs.Empty);
					break;
				case "RAM Search":
					RamSearchMenuItem_Click(this, EventArgs.Empty);
					break;
				case "Hex Editor":
					HexEditorMenuItem_Click(this, EventArgs.Empty);
					break;
				case "Lua Console":
					OpenLuaConsole();
					break;
				case "Toggle Last Lua Script":
					if (Tools.IsLoaded<LuaConsole>())
					{
						Tools.LuaConsole.ToggleLastLuaScript();
					}
					break;
				case "Toggle All Freezes":
					var frozen = CheatList.Where(static c => !c.IsSeparator).ToList();
					if (frozen.Count is 0) break;
					var firstWasEnabled = frozen[0].Enabled;
					var kind = frozen.TrueForAll(c => c.Enabled == firstWasEnabled)
						? firstWasEnabled
							? "off"
							: "on"
						: "mixed";
					foreach (var x in frozen) x.Toggle();
					AddOnScreenMessage($"Freezes toggled ({kind})");
					break;

				// RAM Search
				case "Do Search":
					if (!Tools.IsLoaded<RamSearch>()) return false;
					Tools.RamSearch.DoSearch();
					break;
				case "New Search":
					if (!Tools.IsLoaded<RamSearch>()) return false;
					Tools.RamSearch.NewSearch();
					break;
				case "Previous Compare To":
					if (!Tools.IsLoaded<RamSearch>()) return false;
					Tools.RamSearch.NextCompareTo(reverse: true);
					break;
				case "Next Compare To":
					if (!Tools.IsLoaded<RamSearch>()) return false;
					Tools.RamSearch.NextCompareTo();
					break;
				case "Previous Operator":
					if (!Tools.IsLoaded<RamSearch>()) return false;
					Tools.RamSearch.NextOperator(reverse: true);
					break;
				case "Next Operator":
					if (!Tools.IsLoaded<RamSearch>()) return false;
					Tools.RamSearch.NextOperator();
					break;

				// TAStudio
				case "Add Branch":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.AddBranchExternal();
					break;
				case "Delete Branch":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.RemoveBranchExternal();
					break;
				case "Show Cursor":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SetVisibleFrame();
					Tools.TAStudio.RefreshDialog();
					break;
				case "Select Current Frame":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SelectCurrentFrame();
					break;
				case "Seek To Selected Frame":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SeekToSelectedFrame();
					break;
				case "Seek To...":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SeekToUserSpecifiedFrame();
					break;
				case "Toggle Follow Cursor":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					var playbackBox = Tools.TAStudio.TasPlaybackBox;
					playbackBox.FollowCursor = !playbackBox.FollowCursor;
					break;
				case "Toggle Auto-Restore":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					var playbackBox1 = Tools.TAStudio.TasPlaybackBox;
					playbackBox1.AutoRestore = !playbackBox1.AutoRestore;
					break;
				case "Seek To Green Arrow":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.RestorePosition();
					break;
				case "Toggle Turbo Seek":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					var playbackBox2 = Tools.TAStudio.TasPlaybackBox;
					playbackBox2.TurboSeek = !playbackBox2.TurboSeek;
					break;
				case "Undo":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.UndoExternal();
					break;
				case "Redo":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.RedoExternal();
					break;
				case "Seek To Prev Marker":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.GoToPreviousMarker();
					break;
				case "Seek To Next Marker":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.GoToNextMarker();
					break;
				case "Cancel Seek":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.StopSeeking();
					break;
				case "Set Marker":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SetMarker();
					break;
				case "Delete Marker":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.RemoveMarker();
					break;
				case "Sel. bet. Markers":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SelectBetweenMarkersExternal();
					break;
				case "Select All":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.SelectAllExternal();
					break;
				case "Reselect Clip.":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.ReselectClipboardExternal();
					break;
				case "Clear Frames":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.ClearFramesExternal();
					break;
				case "Insert Frame":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.InsertFrameExternal();
					break;
				case "Insert # Frames":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.InsertNumFramesExternal();
					break;
				case "Delete Frames":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.DeleteFramesExternal();
					break;
				case "Clone Frames":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.CloneFramesExternal();
					break;
				case "Clone # Times":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.CloneFramesXTimesExternal();
					break;
				case "Paste Insert":
					if (!Tools.IsLoaded<TAStudio>()) return false;
					Tools.TAStudio.PasteInsertExternal();
					break;

			}

			return true;
		}
	}
}


