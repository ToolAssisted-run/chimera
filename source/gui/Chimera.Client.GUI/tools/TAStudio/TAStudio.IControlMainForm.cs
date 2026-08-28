namespace Chimera.Client.GUI
{
	public partial class TAStudio : IControlMainform
	{
		private bool _suppressAskSave;


		public bool WantsToControlReadOnly => true;

		public void ToggleReadOnly()
		{
			TastudioToggleReadOnly();
		}

		public bool WantsToControlStopMovie { get; private set; }

		public void StopMovie(bool suppressSave)
		{
			Activate();
			_suppressAskSave = suppressSave;
			StartNewTasMovie();
			_suppressAskSave = false;
		}

		public bool WantsToControlRewind { get; private set; } = true;

		public void CaptureRewind()
		{
			// Do nothing, Tastudio handles this just fine
		}

		public bool Rewind()
		{
			int rewindStep = MainForm.IsFastForwarding ? Settings.RewindStepFast : Settings.RewindStep;
			int frame = Emulator.Frame;
			WheelSeek(rewindStep);
			// we need a frame advance if a state was loaded (frame has changed)
			// and also we are seeking (not already at the target frame)
			return Emulator.Frame != frame && SeekingTo != -1;
		}

		public bool WantsToControlRestartMovie { get; }

		public bool RestartMovie()
		{
			if (!AskSaveChanges()) return false;
			var success = StartNewMovieWrapper(CurrentTasMovie, isNew: false);
			RefreshDialog();
			return success;
		}

		public bool WantsToControlReboot => false;
		public void RebootCore() => throw new NotSupportedException("This should never be called");

		public bool WantsToBypassMovieEndAction => true;
	}
}
