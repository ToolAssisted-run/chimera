using System.Collections;
using System.Collections.Generic;
using System.Text;

using BizHawk.Emulation.Common.Engine;

namespace BizHawk.Client.Common
{
	public static class StringLogUtil
	{
		/// <summary>The one implementation: the log's storage is the engine's (see docs/engine-migration.md).</summary>
		public static IStringLog MakeStringLog() => new EngineStringLog();

		public static int? DivergentPoint(this IStringLog currentLog, IStringLog newLog)
		{
			if (currentLog is EngineStringLog a && newLog is EngineStringLog b)
			{
				var divergence = a.Engine.DivergentPoint(b.Engine);
				return divergence is null ? null : checked((int)divergence.Value);
			}

			// a generic walk for mixed implementations, which nothing creates today
			int max = Math.Min(currentLog.Count, newLog.Count);
			for (int i = 0; i < max; i++)
			{
				if (newLog[i] != currentLog[i]) return i;
			}
			return newLog.Count != currentLog.Count ? max : null;
		}

		public static string ToInputLog(this IStringLog log)
		{
			var sb = new StringBuilder();
			foreach (var record in log)
			{
				sb.AppendLine(record);
			}

			return sb.ToString();
		}
	}

	public interface IStringLog : IDisposable, IEnumerable<string>
	{
		void RemoveAt(int index);
		int Count { get; }
		void Clear();
		void Add(string str);
		string this[int index] { get; set; }
		void Insert(int index, string val);
		void InsertRange(int index, IEnumerable<string> collection);
		void AddRange(IEnumerable<string> collection);
		void RemoveRange(int index, int count);
		IStringLog Clone();
		void CopyTo(int index, string[] array, int arrayIndex, int count);
	}

	/// <summary>
	/// An input log whose storage is the engine's <c>ce_movie_log</c> - the same
	/// object the movie file I/O and (in time) the running session's movie use,
	/// so there is exactly one copy of the data and one implementation of its
	/// semantics. The list-shaped surface exists for the frontend's editors.
	/// </summary>
	public sealed class EngineStringLog : IStringLog
	{
		public EngineMovieLog Engine { get; } = new();

		public int Count => checked((int)Engine.Count);

		public string this[int index]
		{
			get => Engine[index];
			set => Engine.Set(index, value);
		}

		public void Add(string str) => Engine.Add(str);

		public void AddRange(IEnumerable<string> collection)
		{
			foreach (var item in collection) Engine.Add(item);
		}

		public void Insert(int index, string val) => Engine.Insert(index, val);

		public void InsertRange(int index, IEnumerable<string> collection)
		{
			foreach (var item in collection) Engine.Insert(index++, item);
		}

		public void RemoveAt(int index) => Engine.RemoveRange(index, 1);

		public void RemoveRange(int index, int count) => Engine.RemoveRange(index, count);

		public void Clear() => Engine.Clear();

		public IStringLog Clone()
		{
			EngineStringLog clone = new();
			clone.Engine.AssignFrom(Engine);
			return clone;
		}

		public void CopyTo(int index, string[] array, int arrayIndex, int count)
		{
			for (int i = 0; i < count; i++)
			{
				array[arrayIndex + i] = Engine[index + i];
			}
		}

		public IEnumerator<string> GetEnumerator()
		{
			for (long i = 0; i < Engine.Count; i++)
			{
				yield return Engine[i];
			}
		}

		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		public void Dispose() => Engine.Dispose();
	}
}
