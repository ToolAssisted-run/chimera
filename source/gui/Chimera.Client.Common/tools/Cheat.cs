using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public class Cheat
	{
		public enum CompareType
		{
			None,
			Equal,
			GreaterThan,
			GreaterThanOrEqual,
			LessThan,
			LessThanOrEqual,
			NotEqual,
		}

		private readonly Watch _watch;
		private int? _compare;
		private int _val;
		private bool _enabled;

		/// <summary>
		/// What a freeze of a game property holds (docs/game-cores.md): the value as text the
		/// engine takes back exactly, since no int can hold 64 bits, a double or a string.
		/// </summary>
		private readonly string _propertyValue;

		public Cheat(Watch watch, int value, int? compare = null, bool enabled = true, CompareType comparisonType = CompareType.None)
		{
			_enabled = enabled;
			_watch = watch;
			_compare = compare;
			_val = value;
			ComparisonType = comparisonType;
			if (watch is PropertyWatch property) _propertyValue = property.RawText;

			Pulse();
		}

		public Cheat(Cheat cheat)
		{
			if (cheat.IsSeparator)
			{
				_enabled = false;
				_watch = SeparatorWatch.Instance;
				_compare = null;
			}
			else if (cheat._watch is PropertyWatch property)
			{
				// a property's watch is the engine's view of it; the copy holds the same value
				_enabled = cheat.Enabled;
				_watch = property;
				_propertyValue = cheat._propertyValue;
			}
			else
			{
				_enabled = cheat.Enabled;
				_watch = Watch.GenerateWatch(
					cheat.Domain,
					cheat.Address ?? 0,
					cheat.Size,
					cheat.Type,
					cheat.BigEndian ?? false,
					cheat.Name);
				_compare = cheat.Compare;
				_val = cheat.Value ?? 0;

				Pulse();
			}
		}

		public delegate void CheatEventHandler(object sender);
		public event CheatEventHandler Changed;

		public static Cheat Separator => new Cheat(SeparatorWatch.Instance, 0, null, false);

		public bool IsSeparator => _watch.IsSeparator;

		public bool Enabled => !IsSeparator && _enabled;

		public long? Address => _watch.Address;

		public int? Value => IsSeparator ? null : _val;

		public bool? BigEndian => IsSeparator ? null : _watch.BigEndian;

		public int? Compare => _compare.HasValue && !IsSeparator ? _compare : null;

		public MemoryDomain Domain
		{
			get =>	_watch.Domain;
			set => _watch.Domain = value;
		}

		public WatchSize Size => _watch.Size;

		public char SizeAsChar => _watch.SizeAsChar;

		public WatchDisplayType Type => _watch.Type;

		public char TypeAsChar => _watch.TypeAsChar;

		public string Name => IsSeparator ? "" : _watch.Notes;

		public string AddressStr => _watch.AddressString;

		public string ValueStr =>
			_watch is PropertyWatch ? _propertyValue :
			_watch.Size switch
				{
					WatchSize.Byte => ((ByteWatch) _watch).FormatValue((byte)_val),
					WatchSize.Word => ((WordWatch) _watch).FormatValue((ushort)_val),
					WatchSize.DWord => ((DWordWatch) _watch).FormatValue((uint)_val),
					WatchSize.Separator => "",
					_ => string.Empty,
				};

		public string CompareStr
		{
			get
			{
				if (_compare.HasValue)
				{
					return _watch.Size switch
					{
						WatchSize.Byte => ((ByteWatch) _watch).FormatValue((byte)_compare.Value),
						WatchSize.Word => ((WordWatch) _watch).FormatValue((ushort)_compare.Value),
						WatchSize.DWord => ((DWordWatch) _watch).FormatValue((uint)_compare.Value),
						WatchSize.Separator => "",
						_ => string.Empty,
					};
				}

				return "";
			}
		}

		public CompareType ComparisonType { get; }

		public void Enable(bool handleChange = true)
		{
			if (!IsSeparator)
			{
				var wasEnabled = _enabled;
				_enabled = true;
				if (!wasEnabled && handleChange)
				{
					Changes();
				}
			}
		}

		public void Disable(bool handleChange = true)
		{
			if (!IsSeparator)
			{
				var wasEnabled = _enabled;
				_enabled = false;
				if (wasEnabled && handleChange)
				{
					Changes();
				}
			}
		}

		public void Toggle(bool handleChange = true)
		{
			if (!IsSeparator)
			{
				_enabled = !_enabled;
				if (handleChange)
				{
					Changes();
				}
			}
		}

		public void Pulse()
		{
			if (!IsSeparator && _enabled)
			{
				if (_watch is PropertyWatch property)
				{
					property.Poke(_propertyValue);
					return;
				}

				if (ShouldPoke())
				{
					switch (_watch.Size)
					{
						case WatchSize.Byte:
							_watch.Poke(((ByteWatch)_watch).FormatValue((byte)_val));
							break;
						case WatchSize.Word:
							_watch.Poke(((WordWatch)_watch).FormatValue((ushort)_val));
							break;
						case WatchSize.DWord:
							_watch.Poke(((DWordWatch)_watch).FormatValue((uint)_val));
							break;
					}
				}

				// A core that takes cheats itself (a memory domain that answers SendCheatToCore) is handed
				// the cheat with its compare option directly. Only works for byte cheats currently
				if (_watch.Size == WatchSize.Byte && _watch.Domain.Name == "System Bus")
				{
					if (Compare.HasValue)
					{
						_watch.Domain.SendCheatToCore((int)Address.Value, (byte)Value, Compare.Value, (int)ComparisonType);
					}
					else
					{
						_watch.Domain.SendCheatToCore((int)Address.Value, (byte)Value, -1, 0);
					}
				}
			}
		}

		// Returns true if compare value exists, and the comparison type criteria matches
		private bool ShouldPoke()
		{
			if (_compare.HasValue)
			{
				return ComparisonType switch
				{
					CompareType.GreaterThan => _compare.Value > _watch.Value,
					CompareType.GreaterThanOrEqual => _compare.Value >= _watch.Value,
					CompareType.LessThan => _compare.Value < _watch.Value,
					CompareType.LessThanOrEqual => _compare.Value <= _watch.Value,
					CompareType.NotEqual => _compare.Value != _watch.Value,
					_ => _compare.Value == _watch.Value,
				};
			}

			return true;
		}

		public bool Contains(long addr)
		{
			switch (_watch.Size)
			{
				default:
				case WatchSize.Separator:
					return false;
				case WatchSize.Byte:
					return _watch.Address == addr;
				case WatchSize.Word:
					return addr == _watch.Address || addr == _watch.Address + 1;
				case WatchSize.DWord:
					return addr >= _watch.Address && addr <= _watch.Address + 3;
				case WatchSize.Property:
					return addr >= _watch.Address && addr < _watch.Address + _watch.ByteSize;
			}
		}

		/// <summary>
		/// Finds a frozen game property again in a core that was reloaded, by name; false when
		/// that core no longer has it. Any other freeze is left as it is.
		/// </summary>
		public bool Rebind(IGameProperties properties)
			=> _watch is not PropertyWatch property || property.Rebind(properties);

		public void PokeValue(int val)
		{
			if (!IsSeparator)
			{
				_val = val;
			}
		}

		public void Increment()
		{
			if (!IsSeparator)
			{
				_val++;
				if (_val > _watch.MaxValue)
				{
					_val = 0;
				}

				Pulse();
				Changes();
			}
		}

		public void Decrement()
		{
			if (!IsSeparator)
			{
				_val--;
				if ((uint)_val > _watch.MaxValue)
				{
					_val = (int)_watch.MaxValue;
				}

				Pulse();
				Changes();
			}
		}

		public void SetType(WatchDisplayType type)
		{
			if (_watch.IsDisplayTypeAvailable(type))
			{
				_watch.Type = type;
				Changes();
			}
		}

		private void Changes()
		{
			Changed?.Invoke(this);
		}

		public override bool Equals(object obj)
		{
			if (obj is Watch watch)
			{
				return Domain == watch.Domain && Address == watch.Address;
			}

			if (obj is Cheat cheat)
			{
				return Domain == cheat.Domain && Address == cheat.Address;
			}

			return base.Equals(obj);
		}

		public override int GetHashCode()
		{
			return Domain.GetHashCode() + (int)(Address ?? 0);
		}

		public static bool operator ==(Cheat a, Cheat b)
		{
			// If one is null, but not both, return false.
			if (a is null || b is null)
			{
				return false;
			}

			return a.Domain == b.Domain && a.Address == b.Address;
		}

		public static bool operator !=(Cheat a, Cheat b)
		{
			return !(a == b);
		}

		public static bool operator ==(Cheat a, Watch b)
		{
			// If one is null, but not both, return false.
			if (a is null || b is null)
			{
				return false;
			}

			return a.Domain == b.Domain && a.Address == b.Address;
		}

		public static bool operator !=(Cheat a, Watch b)
		{
			return !(a == b);
		}
	}
}
