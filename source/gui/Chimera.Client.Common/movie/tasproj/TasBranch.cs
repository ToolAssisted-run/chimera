using System.Collections.Generic;
using System.IO;
using System.Linq;

using Newtonsoft.Json;

using Chimera.Display;
using Chimera.Common.IOExtensions;

namespace Chimera.Client.Common
{
	public class TasBranch
	{
		internal struct ForSerialization
		{
			public readonly int Frame;

			public readonly DateTime TimeStamp;

			public readonly Guid UniqueIdentifier;

			public ForSerialization(int frame, DateTime timeStamp, Guid uniqueIdentifier)
			{
				Frame = frame;
				TimeStamp = timeStamp;
				UniqueIdentifier = uniqueIdentifier;
			}
		}

		public int Frame { get; set; }
		/// <summary>
		/// The machine at this branch, as the NAME of a state file in the movie's branch-state
		/// directory (<see cref="TasMovie.BranchStatePath"/>); null when the branch has only its
		/// input, and is reached by replaying to its frame. It used to be the state itself, as a
		/// byte array - which cannot be more than 2 GiB, and a PS3's state is (issue #84). The
		/// engine streams the machine to the file and back, so it never has to fit anywhere.
		/// </summary>
		public string StateFile { get; set; }
		public IStringLog InputLog { get; set; }
		public BitmapBuffer CoreFrameBuffer { get; set; }
		public BitmapBuffer OSDFrameBuffer { get; set; }
		public IMovieChangeLog ChangeLog { get; set; }
		public DateTime TimeStamp { get; set; }
		public TasMovieMarkerList Markers { get; set; }
		public Guid Uuid { get; set; }
		public string UserText { get; set; }

		internal ForSerialization ForSerial => new(Frame, TimeStamp, Uuid);

		public TasBranch Clone() => (TasBranch)MemberwiseClone();
	}

	public interface ITasBranchCollection : IList<TasBranch>
	{
		int Current { get; set; }
		string NewBranchText { get; set; }

		void Swap(int b1, int b2);
		void Replace(TasBranch old, TasBranch newBranch);

	}

	public class TasBranchCollection : List<TasBranch>, ITasBranchCollection
	{
		private readonly ITasMovie _movie;

		public TasBranchCollection(ITasMovie movie)
		{
			_movie = movie;
		}

		public int Current { get; set; } = -1;
		public string NewBranchText { get; set; } = "";

		public void Swap(int b1, int b2)
		{
			var branch = this[b1];

			if (b2 >= Count)
			{
				b2 = Count - 1;
			}

			Remove(branch);
			Insert(b2, branch);
			_movie.FlagChanges();
		}

		public void Replace(TasBranch old, TasBranch newBranch)
		{
			int index = IndexOf(old);
			newBranch.Uuid = old.Uuid;
			if (newBranch.UserText.Length is 0) newBranch.UserText = old.UserText;
			this[index] = newBranch;
			if (!_movie.IsReserved(old.Frame))
				_movie.States?.Pin(old.Frame, false);

			_movie.FlagChanges();
		}

		public new TasBranch this[int index]
		{
			get => index >= Count || index < 0
				? null
				: base [index];
			set => base[index] = value;
		}

		public new void Add(TasBranch item)
		{
			if (item is null) throw new ArgumentNullException(paramName: nameof(item));

			if (item.Uuid == Guid.Empty)
			{
#pragma warning disable RS0030 // this is to ensure no collisions
				item.Uuid = Guid.NewGuid();
#pragma warning restore RS0030
			}

			base.Add(item);
			_movie.FlagChanges();
		}

		public new bool Remove(TasBranch item)
		{
			var result = base.Remove(item);
			if (result)
			{
				if (!_movie.IsReserved(item!.Frame))
					_movie.States?.Pin(item.Frame, false);

				_movie.FlagChanges();
			}

			return result;
		}
	}

	public static class TasBranchExtensions
	{
		public static int IndexOfFrame(this IList<TasBranch> list, int frame)
		{
			// intentionally not using linq here because this is called many times per frame
			int index = -1;
			var timeStamp = DateTime.MinValue;
			for (int i = 0; i < list.Count; i++)
			{
				if (list[i].Frame == frame && list[i].TimeStamp > timeStamp)
				{
					index = i;
					timeStamp = list[i].TimeStamp;
				}
			}

			return index;
		}

		// TODO: stop relying on the index value of a branch
		public static int IndexOfHash(this IList<TasBranch> list, Guid uuid)
		{
			var branch = list.SingleOrDefault(b => b.Uuid == uuid);
			return branch == null
				? -1
				: list.IndexOf(branch);
		}
	}
}
