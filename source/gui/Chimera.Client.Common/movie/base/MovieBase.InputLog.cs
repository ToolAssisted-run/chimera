using System.IO;

using Chimera.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	public partial class MovieBase
	{
		protected IStringLog Log { get; set; } = StringLogUtil.MakeStringLog();
		public string LogKey { get; set; }

		public virtual void Dispose() => Log.Dispose();

		public void WriteInputLog(TextWriter writer)
		{
			// the engine renders the [Input] block straight from the log's own
			// engine-side storage (see docs/engine-migration.md); the EOL matches
			// what TextWriter.WriteLine wrote here historically
			var engineLog = ((EngineStringLog)Log).Engine;
			engineLog.Key = string.IsNullOrEmpty(LogKey) ? LogEntryGenerator.GenerateLogKey(Session.MovieController.Definition) : LogKey;
			writer.Write(engineLog.Serialize(crlf: writer.NewLine == "\r\n"));
		}

		public string GetInputLogEntry(int frame)
		{
			return frame < FrameCount && frame >= 0
				? Log[frame]
				: LogEntryGenerator.EmptyEntry(Session.MovieController);
		}

		public virtual bool ExtractInputLog(TextReader reader, out string errorMessage)
		{
			errorMessage = "";

			// We are in record mode so replace the movie log with the one from the savestate
			if (Session.Settings.EnableBackupMovies && MakeBackup && Log.Count != 0)
			{
				SaveBackup();
				MakeBackup = false;
			}

			using EngineMovieLog engineLog = new();
			if (!engineLog.Parse(reader.ReadToEnd(), out errorMessage))
			{
				// unlike the parser this replaced, a failed parse leaves the movie as
				// it was rather than half-loaded
				return false;
			}

			// engine-to-engine: the parsed entries become the log's storage wholesale
			var parsedKey = engineLog.Key;
			var target = ((EngineStringLog)Log).Engine;
			var previousKey = target.Key;
			target.AssignFrom(engineLog);
			if (parsedKey is not null)
			{
				LogKey = parsedKey;
			}
			else
			{
				target.Key = previousKey; // assign copied the (absent) key; the movie keeps its own
			}

			// quirk, preserved: text with no "Frame N" line sets this message yet
			// still succeeds, and the state frame is taken as 0
			if (!engineLog.HasStateFrame)
			{
				errorMessage = "Savestate Frame number failed to parse";
			}

			var stateFramei = engineLog.HasStateFrame ? engineLog.StateFrame : 0;

			if (stateFramei.StrictlyBoundedBy(0.RangeTo(Log.Count)))
			{
				if (!Session.Settings.VBAStyleMovieLoadState)
				{
					Truncate(stateFramei);
				}
			}
			else if (stateFramei > Log.Count) // Post movie savestate
			{
				if (!Session.Settings.VBAStyleMovieLoadState)
				{
					Truncate(Log.Count);
				}

				Mode = MovieMode.Finished;
			}

			if (IsCountingRerecords)
			{
				Rerecords++;
			}

			return true;
		}

		public bool CheckTimeLines(TextReader reader, out string errorMessage)
		{
			// This function will compare the movie data to the savestate movie data to see if they match
			errorMessage = "";

			using EngineMovieLog newLog = new();
			if (!newLog.Parse(reader.ReadToEnd(), out errorMessage))
			{
				return false;
			}

			// quirk, preserved: an absent (or literal 0) frame number means the
			// whole savestate input log
			var stateFrame = newLog.HasStateFrame ? newLog.StateFrame : 0;
			if (stateFrame == 0)
			{
				stateFrame = (int)newLog.Count;
			}

			if (stateFrame > newLog.Count)
			{
				errorMessage = $"Savestate has invalid frame number {stateFrame} (expected maximum {newLog.Count})";
				return false;
			}

			if (Log.Count < stateFrame)
			{
				if (this.IsFinished())
				{
					return true;
				}

				errorMessage = $"The savestate is from frame {newLog.Count} which is greater than the current movie length of {Log.Count}";

				return false;
			}

			for (var i = 0; i < stateFrame; i++)
			{
				if (Log[i] != newLog[i])
				{
					errorMessage = $"The savestate input does not match the movie input at frame {(i + 1)}.";

					return false;
				}
			}

			if (stateFrame > newLog.Count) // stateFrame is greater than state input log, so movie finished mode
			{
				if (Mode is MovieMode.Play or MovieMode.Finished)
				{
					Mode = MovieMode.Finished;
					return true;
				}

				return false;
			}

			if (Mode == MovieMode.Finished)
			{
				Mode = MovieMode.Play;
			}

			return true;
		}
	}
}
