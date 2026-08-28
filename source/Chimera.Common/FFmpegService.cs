using System.Threading;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.IO;
using System.Threading.Tasks;
using Chimera.Common.PathExtensions;
using Chimera.Common.StringExtensions;

namespace Chimera.Common
{
	public static class FFmpegService
	{
		/// <summary>
		/// ffmpeg ships in the bundle, beside the other native neighbours
		/// (tools/fetch-ffmpeg.sh puts it there). Chimera used to name a version, a
		/// URL and a checksum and ask the person to go and fetch it the first time
		/// they wanted a video; there is nothing to ask any more, and nothing to
		/// pin, because the build and the binary arrive together.
		/// </summary>
		public static string FFmpegPath => Path.Combine(PathUtils.DataDirectoryPath, "dll", OSTailoredCode.IsUnixHost ? "ffmpeg" : "ffmpeg.exe");

		/// <summary>What to say when it is not there, which now means a broken install.</summary>
		public static string MissingMessage
			=> $"ffmpeg is missing from this build of Chimera. It should be at {FFmpegPath}.";

		public class AudioQueryResult
		{
			public bool IsAudio;
		}

		private static string[] Escape(IEnumerable<string> args)
			=> args.Select(static s => s.ContainsOrdinal(' ') ? $"\"{s}\"" : s).ToArray();

		//note: accepts . or : in the stream stream/substream separator in the stream ID format, since that changed at some point in FFMPEG history
		//if someone has a better idea how to make the determination of whether an audio stream is available, I'm all ears
		private static readonly Regex rxHasAudio = new Regex(@"Stream \#(\d*(\.|\:)\d*)\: Audio", RegexOptions.Compiled);
		public static AudioQueryResult QueryAudio(string path)
		{
			var ret = new AudioQueryResult();
			string stdout = Run("-i", path).Text;
			ret.IsAudio = rxHasAudio.Matches(stdout).Count > 0;
			return ret;
		}

		/// <summary>
		/// Whether the shipped ffmpeg is there and runs. No version is demanded:
		/// the one that answers is the one this build shipped with, and a person
		/// who has put their own there in its place has said what they want.
		/// </summary>
		public static bool QueryServiceAvailable()
		{
			try
			{
				return File.Exists(FFmpegPath) && Run("-version").Text.ContainsOrdinal("ffmpeg version");
			}
			catch
			{
				return false;
			}
		}

		public struct RunResults
		{
			public string Text;
			public int ExitCode;
		}

		public static RunResults Run(params string[] args)
		{
			args = Escape(args);
			StringBuilder sbCmdline = new StringBuilder();
			for (int i = 0; i < args.Length; i++)
			{
				sbCmdline.Append(args[i]);
				if (i != args.Length - 1) sbCmdline.Append(' ');
			}

			ProcessStartInfo oInfo = new ProcessStartInfo(FFmpegPath, sbCmdline.ToString())
			{
				UseShellExecute = false,
				CreateNoWindow = true,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
			};

			Process proc = new Process();
			proc.StartInfo = oInfo;
			Mutex m = new Mutex();

			var outputBuilder = new StringBuilder();
			var outputCloseEvent = new TaskCompletionSource<bool>();
			var errorCloseEvent = new TaskCompletionSource<bool>();

			proc.OutputDataReceived += (s, e) =>
			{
				if (e.Data == null)
				{
					outputCloseEvent.SetResult(true);
				}
				else
				{
					m.WaitOne();
					outputBuilder.Append(e.Data);
					m.ReleaseMutex();
				}
			};

			proc.ErrorDataReceived += (s, e) =>
			{
				if (e.Data == null)
				{
					errorCloseEvent.SetResult(true);
				}
				else
				{
					m.WaitOne();
					outputBuilder.Append(e.Data);
					m.ReleaseMutex();
				}
			};

			proc.Start();
			proc.BeginOutputReadLine();
			proc.BeginErrorReadLine();
			proc.WaitForExit();
			string resultText = "";
			m.WaitOne();
			resultText = outputBuilder.ToString();
			m.ReleaseMutex();

			return new RunResults
			{
				ExitCode = proc.ExitCode,
				Text = resultText,
			};
		}

		/// <exception cref="InvalidOperationException">FFmpeg exited with non-zero exit code or produced no output</exception>
		public static byte[] DecodeAudio(string path)
		{
			string tempfile = Path.GetTempFileName();
			try
			{
				var runResults = Run("-i", path, "-xerror", "-f", "wav", "-ar", "44100", "-ac", "2", "-acodec", "pcm_s16le", "-y", tempfile);
				if (runResults.ExitCode != 0)
					throw new InvalidOperationException($"Failure running ffmpeg for audio decode. here was its output:\r\n{runResults.Text}");
				byte[] ret = File.ReadAllBytes(tempfile);
				if (ret.Length == 0)
					throw new InvalidOperationException($"Failure running ffmpeg for audio decode. here was its output:\r\n{runResults.Text}");
				return ret;
			}
			finally
			{
				File.Delete(tempfile);
			}
		}
	}
}
