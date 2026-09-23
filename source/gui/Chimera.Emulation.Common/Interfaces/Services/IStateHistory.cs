#nullable enable

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// Where the machine has been along a movie's timeline, kept by the engine.
	///
	/// The whole of it lives in C++ (docs/state-manager.md): what a stored frame
	/// costs, when a frame is thinned or dropped, and how a
	/// frame is reached from the one before it. This interface is a remote
	/// control, and deliberately holds no state of its own - a second copy of
	/// "which frames exist" up here is a second thing to keep true, and it would
	/// go wrong quietly, on the long runs where nobody could reproduce it.
	///
	/// The frame numbers are the CALLER'S. The engine counts frames only for a
	/// movie it owns itself, and the frontend owns its own.
	/// </summary>
	public interface IStateHistory : IEmulatorService
	{
		/// <summary>Bytes to keep in memory; 0 turns it off and drops everything.</summary>
		void Enable(long budgetBytes);

		/// <summary>
		/// At most one frame in this many is kept right behind the playhead (1 to 32).
		/// The history may keep more; it never thins past this.
		/// </summary>
		void MaxNearStride(int stride);

		/// <summary>
		/// How often a frame is stored: 1 every frame (the default), N only the multiples
		/// of N, 0 none. Off, BeforeAdvance and Capture store nothing and cost nothing;
		/// what is stored stays, and Restore still works. Turning it on from off stores
		/// the current frame as a whole state at once.
		/// </summary>
		void SetCapturePeriod(int period);

		/// <summary>Frames it can produce - not the number of stored objects.</summary>
		long Count { get; }

		/// <summary>The greatest frame it can produce at or before this one, or -1.</summary>
		int Nearest(int frame);

		bool Has(int frame);

		/// <summary>
		/// Before the machine moves, every time. A delta is what changed since a
		/// marked moment, so the moment has to be marked first; skipping it is
		/// not wrong, it just makes the next capture a whole state.
		/// </summary>
		void BeforeAdvance();

		/// <summary>After it has moved, with the frame now stood on.</summary>
		void Capture(int frame);

		/// <summary>
		/// Puts the machine on a stored frame, restoring the emulator's own
		/// side-band with it. False when that frame is not one it can produce.
		/// </summary>
		bool RestoreTo(int frame);

		/// <summary>Drops everything after this frame - what an input edit means.</summary>
		void InvalidateAfter(int afterFrame);

		/// <summary>
		/// The history across sessions. Both answer "it did not throw", never
		/// "the states are there": a history of another machine is dropped
		/// rather than refused, because losing it costs replaying and never work.
		/// </summary>
		bool Save(string path, string machineId);

		/// <summary>
		/// The same save, queued on the engine's writer rather than waited for.
		///
		/// Writing the history is the longest thing a project save does - up to
		/// fourteen gigabytes under the default budgets, and TAStudio fires one
		/// every thirty minutes without being asked - and none of it needs the
		/// machine. What the file describes is the history as it stands at this
		/// call; the run carries on while it is written.
		/// </summary>
		bool SaveLater(string path, string machineId);

		/// <summary>Whether a queued save is still being written.</summary>
		bool SavePending { get; }

		/// <summary>
		/// Waits for a queued save. True when it worked, and when there was
		/// nothing queued. This is the barrier, and it belongs wherever the
		/// project is let go of: the file has to be whole by then.
		/// </summary>
		bool SaveWait();

		bool Load(string path, string machineId);

		/// <summary>
		/// Frames to keep reachable whatever the thinning would otherwise do.
		/// What deserves it is this layer's business - a marker somebody wants
		/// to jump to instantly - and nothing the engine could work out itself.
		/// </summary>
		void Pin(int frame, bool pinned);

		void UnpinAll();
	}
}
