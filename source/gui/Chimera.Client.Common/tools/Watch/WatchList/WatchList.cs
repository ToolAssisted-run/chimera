using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// This class hold a collection <see cref="Watch"/>
	/// Different memory domain can be mixed
	/// </summary>
	public sealed partial class WatchList
		: IList<Watch>
	{
		public const string Address = "AddressColumn";
		public const string Value = "ValueColumn";
		public const string Prev = "PrevColumn";
		public const string ChangesCol = "ChangesColumn";
		public const string Diff = "DiffColumn";
		public const string Type = "TypeColumn";
		public const string Domain = "DomainColumn";
		public const string Notes = "NotesColumn";
		public const string OnScreen = "OnScreenColumn";

		/// <summary>
		/// Starts the line, after the watches, that lists which of them are drawn on the screen: their
		/// places in the file, 0-based and comma-separated. A build without the flag skips it, as it
		/// skips any line without a watch's five tabs.
		/// </summary>
		private const string OnScreenLine = "OnScreen\t";

		private static readonly Dictionary<string, IComparer<Watch>> WatchComparers;

		private readonly List<Watch> _watchList = new List<Watch>(0);
		private readonly string _systemId;
		private IMemoryDomains _memoryDomains;

		/// <summary>
		/// The core's game properties (docs/game-cores.md), which a property watch in a file is
		/// found in by name; null for a core without any, where such a line is skipped.
		/// </summary>
		public IGameProperties GameProperties { get; set; }

		/// <summary>
		/// Static constructor for the <see cref="WatchList"/> class.
		/// </summary>
		static WatchList()
		{
			// Initialize mapping of columns to comparer for sorting.
			WatchComparers = new Dictionary<string, IComparer<Watch>>
			{
				[Address] = new WatchAddressComparer(),
				[Value] = new WatchValueComparer(),
				[Prev] = new WatchPreviousValueComparer(),
				[ChangesCol] = new WatchChangeCountComparer(),
				[Diff] = new WatchValueDifferenceComparer(),
				[Type] = new WatchFullDisplayTypeComparer(),
				[Domain] = new WatchDomainComparer(),
				[Notes] = new WatchNoteComparer(),
				[OnScreen] = new WatchOnScreenComparer(),
			};
		}

		/// <summary>
		/// Initializes a new instance of the <see cref="WatchList"/> class
		/// that will contains a set of <see cref="Watch"/>
		/// </summary>
		/// <param name="core">All available memory domains</param>
		/// <param name="systemId">System identifier (NES, SNES, ...)</param>
		public WatchList(IMemoryDomains core, string systemId)
		{
			_memoryDomains = core;
			_systemId = systemId;
		}

		/// <summary>
		/// Adds a <see cref="Watch"/> into the current collection
		/// </summary>
		/// <param name="watch"><see cref="Watch"/> to add</param>
		public void Add(Watch watch)
		{
			_watchList.Add(watch);
			Changes = true;
		}

		/// <summary>
		/// Removes all item from the current collection
		/// Clear also the file name
		/// </summary>
		public void Clear()
		{
			_watchList.Clear();
			Changes = false;
			CurrentFileName = "";
		}

		/// <summary>
		/// Determines if the current <see cref="WatchList"/> contains the
		/// specified <see cref="Watch"/>
		/// </summary>
		/// <param name="watch">The object to</param>
		public bool Contains(Watch watch)
		{
			return _watchList.Contains(watch);
		}

		/// <summary>
		/// Copies the elements of the current <see cref="WatchList"/>
		/// into an <see cref="Array"/> starting at a particular <see cref="Array"/> index
		/// </summary>
		/// <param name="array">The one-dimension <see cref="Array"/> that will serve as destination to copy</param>
		/// <param name="arrayIndex">Zero-based index where the copy should starts</param>
		public void CopyTo(Watch[] array, int arrayIndex)
		{
			_watchList.CopyTo(array, arrayIndex);
		}

		/// <summary>
		/// Removes the first of specified <see cref="Watch"/>
		/// </summary>
		/// <param name="watch"><see cref="Watch"/> to remove</param>
		/// <returns>True if <see cref="Watch"/> successfully removed; otherwise, false</returns>
		public bool Remove(Watch watch)
		{
			bool result = _watchList.Remove(watch);
			if (result)
			{
				Changes = true;
			}

			return result;
		}

		/// <summary>
		/// Determines the zero-base position of the specified <see cref="Watch"/>
		/// into the <see cref="WatchList"/>
		/// </summary>
		/// <param name="watch"><see cref="Watch"/> to look for</param>
		/// <returns>Zero-base position if <see cref="Watch"/> has been found; otherwise -1</returns>
		public int IndexOf(Watch watch)
		{
			return _watchList.IndexOf(watch);
		}

		/// <summary>
		/// Insert a <see cref="Watch"/> at the specified index
		/// </summary>
		/// <param name="index">The zero-base index where the <see cref="Watch"/> should be inserted</param>
		/// <param name="watch"><see cref="Watch"/> to insert</param>
		public void Insert(int index, Watch watch)
		{
			_watchList.Insert(index, watch);
			Changes = true;
		}

		/// <param name="index">
		/// <c>0</c> to prepend, <see cref="Count"/> to append, anything in-between to insert there
		/// (the first elem of <paramref name="collection"/> will end up at <paramref name="index"/>)
		/// </param>
		public void InsertRange(int index, IEnumerable<Watch> collection)
		{
#if NET6_0_OR_GREATER
			if (collection.TryGetNonEnumeratedCount(out var n) && n is 0) return;
#else
			if (collection is ICollection<Watch> hasCount && hasCount.Count is 0) return;
#endif
			_watchList.InsertRange(index, collection);
			Changes = true;
		}

		/// <summary>
		/// Removes item at the specified index
		/// </summary>
		/// <param name="index">Zero-based index of the <see cref="Watch"/> to remove</param>
		public void RemoveAt(int index)
		{
			_watchList.RemoveAt(index);
			Changes = true;
		}

		/// <summary>
		/// Returns an enumerator that iterates through the collection
		/// </summary>
		/// <returns>An <see cref="IEnumerator{T}"/> for the current collection</returns>
		public IEnumerator<Watch> GetEnumerator()
		{
			return _watchList.GetEnumerator();
		}

		/// <summary>
		/// Returns an enumerator that iterates through the collection
		/// </summary>
		/// <returns>An <see cref="IEnumerator"/> for the current collection</returns>
		IEnumerator IEnumerable.GetEnumerator()
		{
			return GetEnumerator();
		}

		/// <summary>
		/// Add an existing collection of <see cref="Watch"/> into the current one
		/// </summary>
		/// <param name="watches"><see cref="IEnumerable{Watch}"/> of watch to merge</param>
		public void AddRange(IEnumerable<Watch> watches)
		{
			foreach(var watch in watches)
			{
				_watchList.Add(watch);
			}
			Changes = true;
		}

		/// <summary>
		/// Clears change count of all <see cref="Watch"/> in the collection
		/// </summary>
		public void ClearChangeCounts()
		{
			foreach(var watch in _watchList) watch.ClearChangeCount();
		}

		/// <summary>
		/// Sort the current list based on one of the column constants.
		/// </summary>
		/// <param name="column">The column to sort by.</param>
		/// <param name="reverse">Defines the order of the sort. Ascending (true) or descending (false)</param>
		public void OrderWatches(string column, bool reverse)
		{
			var separatorIndices = new List<int>();
			for (var i = 0; i < _watchList.Count; i++)
			{
				if (_watchList[i].IsSeparator)
				{
					separatorIndices.Add(i);
				}
			}
			separatorIndices.Add(_watchList.Count);

			// Sort "blocks" of addresses between separators.
			int startIndex = 0;
			foreach (int index in separatorIndices)
			{
				_watchList.Sort(startIndex, index - startIndex, WatchComparers[column]);
				if (reverse)
				{
					_watchList.Reverse(startIndex, index - startIndex);
				}
				startIndex = index + 1;
			}

			Changes = true;
		}

		/// <summary>
		/// Sets WatchList's domain list to a new one
		/// <see cref="Watch"/> domain will also be refreshed
		/// </summary>
		/// <param name="core">New domains</param>
		public void RefreshDomains(IMemoryDomains core, PreviousType previousType)
		{
			_memoryDomains = core;
			// a property is found again by name in the core now running, and a watch on
			// one it no longer has goes
			_watchList.RemoveAll(w => w is PropertyWatch property && !property.Rebind(GameProperties));
			foreach(var watch in _watchList)
			{
				if (watch.IsSeparator)
				{
					return;
				}

				watch.Domain = core[watch.Domain.Name];
				watch.ResetPrevious();
				watch.Update(previousType);
				watch.ClearChangeCount();
			}
		}

		/// <summary>
		/// Updates all <see cref="Watch"/> in the current collection
		/// </summary>
		public void UpdateValues(PreviousType previousType)
		{
			foreach(var watch in _watchList)
			{
				watch.Update(previousType);
			}
		}

		/// <summary>
		/// Gets the number of elements contained in this <see cref="WatchList"/>
		/// </summary>
		public int Count => _watchList.Count;

		/// <summary>
		/// <see cref="WatchList"/> is always read-write
		/// so this value will be always false
		/// </summary>
		public bool IsReadOnly => false;

		/// <summary>
		/// Gets or sets element at the specified index
		/// </summary>
		/// <param name="index">The zero based index of the element you want to get or set</param>
		/// <returns><see cref="Watch"/> at the specified index</returns>
		public Watch this[int index]
		{
			get => _watchList[index];
			set => _watchList[index] = value;
		}

		/// <summary>
		/// Gets or sets a value indicating whether the collection has changed or not
		/// </summary>
		public bool Changes { get; set; }

		/// <summary>
		/// Gets or sets current <see cref="WatchList"/>'s filename
		/// </summary>
		public string CurrentFileName { get; set; }

		/// <summary>
		/// Gets the number of <see cref="Watch"/> that are not <see cref="SeparatorWatch"/>
		/// </summary>
		public int WatchCount => _watchList.Count(watch => !watch.IsSeparator);

		/// <summary>
		/// The watches ticked On Screen, in the list's order: what the OSD draws, one under the other
		/// </summary>
		public IEnumerable<Watch> OnScreenWatches => _watchList.Where(static watch => watch.OnScreen && !watch.IsSeparator);

		public bool Load(string path, bool append)
		{
			var result = LoadFile(path, append);

			if (result)
			{
				if (append)
				{
					Changes = true;
				}
				else
				{
					CurrentFileName = path;
					Changes = false;
				}
			}

			return result;
		}

		public void Reload()
		{
			if (!string.IsNullOrWhiteSpace(CurrentFileName))
			{
				LoadFile(CurrentFileName, append: false);
				Changes = false;
			}
		}

		public FileWriteResult Save()
		{
			if (string.IsNullOrWhiteSpace(CurrentFileName))
			{
				return new();
			}

			var sb = new StringBuilder();
			sb.Append("SystemID ").AppendLine(_systemId);

			var onScreen = new List<int>();
			for (var i = 0; i < _watchList.Count; i++)
			{
				sb.AppendLine(_watchList[i].ToString());
				if (_watchList[i].OnScreen) onScreen.Add(i);
			}

			if (onScreen.Count is not 0) sb.Append(OnScreenLine).AppendLine(string.Join(",", onScreen));

			FileWriteResult result = FileWriter.Write(CurrentFileName, (fs) =>
			{
				using var sw = new StreamWriter(fs);
				sw.WriteLine(sb.ToString());
			});

			if (!result.IsError) Changes = false;
			return result;
		}

		public FileWriteResult SaveAs(FileInfo file)
		{
			Debug.Assert(file != null, "Cannot save as without a file name.");

			CurrentFileName = file.FullName;
			return Save();
		}

		private bool LoadFile(string path, bool append)
		{
			var file = new FileInfo(path);
			if (!file.Exists)
			{
				return false;
			}

			var isOurWatchFormat = true; // Hack to support .wch files from other emulators
			using var sr = file.OpenText();
			string line;
			var read = new List<Watch>(); // the file's watch lines in order, null for one that gave no watch
			string onScreen = null;

			if (!append)
			{
				Clear();
			}

			while ((line = sr.ReadLine()) != null)
			{
				// .wch files from other emulators start with a number representing the number of watch, that line can be discarded here
				// Any properly formatted line couldn't possibly be this short anyway, this also takes care of any garbage lines that might be in a file
				if (line.Length < 5)
				{
					isOurWatchFormat = false;
					continue;
				}

				if (line.StartsWithOrdinal("Domain")) isOurWatchFormat = true;
				// is there a step missing here? --yoshi
				if (line.StartsWithOrdinal("SystemID")) continue;
				if (line.StartsWithOrdinal(OnScreenLine))
				{
					onScreen = line.Substring(OnScreenLine.Length);
					continue;
				}

				var numColumns = line.Count(c => c == '\t');
				int startIndex;
				if (numColumns == 5)
				{
					read.Add(null);
					// If 5, then this is a post 1.0.5 .wch file
					if (isOurWatchFormat)
					{
						// Do nothing here
					}
					else
					{
						startIndex = line.IndexOf('\t') + 1;
						line = line.Substring(startIndex: startIndex); // 5 digit value representing the watch position number
					}
				}
				else
				{
					continue;   // If not 4, something is wrong with this line, ignore it
				}

				// Temporary, rename if kept
				int addr;
				var memDomain = _memoryDomains.MainMemory;

				var temp = line.SubstringBefore('\t');
				try
				{
					addr = int.Parse(temp, NumberStyles.HexNumber);
				}
				catch
				{
					continue;
				}

				startIndex = line.IndexOf('\t') + 1;
				line = line.Substring(startIndex: startIndex); // Type
				var size = Watch.SizeFromChar(line[0]);

				startIndex = line.IndexOf('\t') + 1;
				line = line.Substring(startIndex: startIndex); // Signed
				var type = Watch.DisplayTypeFromChar(line[0]);

				startIndex = line.IndexOf('\t') + 1;
				line = line.Substring(startIndex: startIndex); // Endian
				try
				{
					startIndex = short.Parse(line[0].ToString());
				}
				catch
				{
					continue;
				}

				var bigEndian = startIndex != 0;

				if (isOurWatchFormat)
				{
					startIndex = line.IndexOf('\t') + 1;
					line = line.Substring(startIndex: startIndex); // Domain
					temp = line.SubstringBefore('\t');
					memDomain = size == WatchSize.Separator ? null : _memoryDomains[temp] ?? _memoryDomains.MainMemory;
				}

				startIndex = line.IndexOf('\t') + 1;
				var notes = line.Substring(startIndex: startIndex);

				if (size is WatchSize.Property)
				{
					// known by its name, never its offset: a core may move it between versions
					if (PropertyWatch.Find(GameProperties, _memoryDomains, notes) is { } found) _watchList.Add(read[^1] = found);
					continue;
				}

				_watchList.Add(
					read[^1] = Watch.GenerateWatch(
						memDomain,
						addr,
						size,
						type,
						bigEndian,
						notes));
			}

			// a place names a line of this file, so a watch that gave nothing (a property the core no
			// longer has) moves no other watch's flag
			foreach (var place in onScreen?.Split(',') ?? [ ])
			{
				if (int.TryParse(place.Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out var i)
					&& i < read.Count && read[i] is { IsSeparator: false } watch)
				{
					watch.OnScreen = true;
				}
			}

			CurrentFileName = path;
			Changes = append;

			return true;
		}
	}
}
