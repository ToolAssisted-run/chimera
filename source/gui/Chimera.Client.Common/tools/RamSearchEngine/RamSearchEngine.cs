using System.Collections.Generic;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common.RamSearchEngine
{
	/// <summary>
	/// RAM Search, as the tool sees it. The search itself - the candidates, their
	/// previous values and change counts, the undo history - is the engine's
	/// (<see cref="EngineRamSearch"/>, engine.h): it costs what the memory searched
	/// costs, so a domain of any size can be searched (issue #89). What is left
	/// here is the settings the tool edits and the translation of its enums.
	/// </summary>
	public sealed class RamSearchEngine : IDisposable
	{
		private Compare _compareTo = Compare.Previous;

		private EngineRamSearch? _search;
		private readonly SearchEngineSettings _settings;

		public RamSearchEngine(SearchEngineSettings settings, IMemoryDomains memoryDomains)
		{
			_settings = new SearchEngineSettings(memoryDomains, settings.UseUndoHistory)
			{
				Mode = settings.Mode,
				Domain = settings.Domain,
				Size = settings.Size,
				CheckMisAligned = settings.CheckMisAligned,
				Type = settings.Type,
				BigEndian = settings.BigEndian,
				PreviousType = settings.PreviousType,
			};
		}

		public RamSearchEngine(SearchEngineSettings settings, IMemoryDomains memoryDomains, Compare compareTo, uint? compareValue, uint? differentBy)
			: this(settings, memoryDomains)
		{
			_compareTo = compareTo;
			DifferentBy = differentBy;
			CompareValue = compareValue;
		}

		/// <exception cref="OutOfMemoryException">the host has not the memory for this domain</exception>
		public void Start()
		{
			_search?.Dispose();
			_search = null;
			var search = new EngineRamSearch(_settings.Domain);
			try
			{
				search.SetUndoEnabled(_settings.UseUndoHistory);
				search.Start((int) _settings.Size, _settings.CheckMisAligned, _settings.BigEndian, _settings.IsDetailed());
				search.SetPreviousType((int) _settings.PreviousType);
			}
			catch
			{
				search.Dispose();
				throw;
			}
			_search = search;
		}

		/// <summary>
		/// Exposes the current watch state based on index
		/// </summary>
		public Watch this[long index]
		{
			get
			{
				if (_search is null || !_search.TryGetRow(index, out var row)) throw new ArgumentOutOfRangeException(nameof(index));
				return Watch.GenerateWatch(
					_settings.Domain,
					row.Address,
					_settings.Size,
					_settings.Type,
					_settings.BigEndian,
					"",
					0,
					row.Previous,
					row.ChangeCount);
			}
		}

		/// <summary>The address listed at <paramref name="index"/>, without building a watch for it.</summary>
		public long AddressAt(long index)
			=> _search is not null && _search.TryGetRow(index, out var row) ? row.Address : -1;

		/// <summary>Where <paramref name="address"/> is listed, or -1.</summary>
		public long IndexOf(long address) => _search?.IndexOf(address) ?? -1;

		/// <returns>how many addresses the search removed</returns>
		public long DoSearch()
		{
			if (_search is null) return 0;
			if (_compareTo is Compare.Changes && !_settings.IsDetailed()) throw new InvalidOperationException();
			return _search.Search((int) _compareTo, (int) Operator, Display, RequiredCompareValue, RequiredDifferentBy, (int) _settings.PreviousType);
		}

		public bool Preview(long index)
		{
			if (_search is null) return false;
			if (_compareTo is Compare.Changes && !_settings.IsDetailed()) throw new InvalidOperationException();
			return _search.WouldRemove(index, (int) _compareTo, (int) Operator, Display, RequiredCompareValue, RequiredDifferentBy);
		}

		private int Display => _settings.Type switch
		{
			WatchDisplayType.Signed => 1,
			WatchDisplayType.Float => 2,
			_ => 0,
		};

		private uint RequiredCompareValue
			=> _compareTo is Compare.Previous ? 0 : CompareValue ?? throw new InvalidOperationException();

		private uint RequiredDifferentBy
			=> Operator is ComparisonOperator.DifferentBy ? DifferentBy ?? throw new InvalidOperationException() : 0;

		public long Count => _search?.Count ?? 0;

		public SearchMode Mode => _settings.Mode;

		public MemoryDomain Domain => _settings.Domain;

		/// <exception cref="InvalidOperationException">(from setter) <see cref="Mode"/> is <see cref="SearchMode.Fast"/> and <paramref name="value"/> is not <see cref="Compare.Changes"/></exception>
		public Compare CompareTo
		{
			get => _compareTo;
			set
			{
				if (CanDoCompareType(value))
				{
					_compareTo = value;
				}
				else
				{
					throw new InvalidOperationException();
				}
			}
		}

		public uint? CompareValue { get; set; }

		public ComparisonOperator Operator { get; set; }

		public uint? DifferentBy { get; set; }

		public void Update()
		{
			if (!_settings.IsDetailed()) return;
			_search?.Update((int) _settings.PreviousType);
		}

		public void SetType(WatchDisplayType type) => _settings.Type = type;

		public void SetEndian(bool bigEndian)
		{
			_settings.BigEndian = bigEndian;
			_search?.SetBigEndian(bigEndian);
		}

		/// <exception cref="InvalidOperationException"><see cref="Mode"/> is <see cref="SearchMode.Fast"/> and <paramref name="type"/> is <see cref="PreviousType.LastFrame"/></exception>
		public void SetPreviousType(PreviousType type)
		{
			if (_settings.IsFastMode() && type == PreviousType.LastFrame)
			{
				throw new InvalidOperationException();
			}

			_settings.PreviousType = type;
			_search?.SetPreviousType((int) type);
		}

		public void SetPreviousToCurrent() => _search?.SetPreviousToCurrent();

		public void ClearChangeCounts() => _search?.ClearChangeCounts();

		public bool HasOutOfRangeAddresses => _search is not null && _search.OutOfRangeCount > 0;

		public void RemoveOutOfRangeAddresses() => _search?.RemoveOutOfRange();

		/// <summary>Removes a set of watches; can be undone.</summary>
		public void RemoveSmallWatchRange(IEnumerable<Watch> watches)
			=> _search?.RemoveAddresses(watches.Select(static w => (ulong) w.Address).ToArray(), recordUndo: true);

		/// <summary>Removes the rows listed at <paramref name="indices"/>; can be undone.</summary>
		public void RemoveRange(IEnumerable<int> indices)
			=> _search?.RemoveIndices(indices.Select(static i => (long) i).ToArray());

		public void AddRange(IEnumerable<long> addresses, bool append)
			=> _search?.AddAddresses(addresses.Select(static a => (ulong) a).ToArray(), append);

		public void ConvertTo(WatchSize size)
		{
			_search?.ConvertTo((int) size);
			_settings.Size = size;
		}

		public void Sort(string column, bool reverse)
		{
			int? col = column switch
			{
				WatchList.Address => 0,
				WatchList.Value => 1,
				WatchList.Prev => 2,
				WatchList.ChangesCol => 3,
				WatchList.Diff => 4,
				_ => null,
			};
			if (col is int c) _search?.Sort(c, reverse, Display);
		}

		public bool UndoEnabled
		{
			get => _settings.UseUndoHistory;
			set
			{
				_settings.UseUndoHistory = value;
				_search?.SetUndoEnabled(value);
			}
		}

		public bool CanUndo => UndoEnabled && _search is not null && _search.CanUndo;

		public bool CanRedo => UndoEnabled && _search is not null && _search.CanRedo;

		public void ClearHistory() => _search?.ClearHistory();

		/// <returns>how many addresses came back</returns>
		public long Undo() => _search?.Undo() ?? 0;

		/// <returns>how many addresses went again</returns>
		public long Redo() => -(_search?.Redo() ?? 0);

		private bool CanDoCompareType(Compare compareType)
		{
			return _settings.Mode switch
			{
				SearchMode.Detailed => true,
				SearchMode.Fast => (compareType != Compare.Changes),
				_ => true,
			};
		}

		public void Dispose()
		{
			_search?.Dispose();
			_search = null;
		}
	}
}
