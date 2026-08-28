using System.Collections;
using System.Collections.Generic;
using System.Linq;

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// The frozen addresses of the running machine: a value held at an address for
	/// as long as the freeze is on. It belongs to the session, not to a file - a
	/// freeze is something you set while looking at RAM (RAM Watch, RAM Search, the
	/// Hex Editor), and it lasts as long as the machine it was set on.
	/// </summary>
	public class CheatCollection : ICollection<Cheat>
	{
		private readonly IDialogParent _dialogParent;

		private readonly List<Cheat> _cheatList = new();

		public CheatCollection(IDialogParent dialogParent)
			=> _dialogParent = dialogParent;

		public delegate void CheatListEventHandler(object sender, CheatListEventArgs e);
		public event CheatListEventHandler Changed;

		public int Count => _cheatList.Count;

		public bool AnyActive
			=> _cheatList.Exists(static c => c.Enabled);

		public bool IsReadOnly => false;

		public Cheat this[int index] => _cheatList[index];

		public Cheat this[MemoryDomain domain, long address]
			=> _cheatList.Find(cheat => cheat.Domain == domain && cheat.Address == address);

		public IEnumerator<Cheat> GetEnumerator() => _cheatList.GetEnumerator();

		IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

		public void Pulse()
		{
			_cheatList.ForEach(cheat => cheat.Pulse());
		}

		/// <exception cref="ArgumentNullException"><paramref name="cheat"/> is null</exception>
		public void Add(Cheat cheat)
		{
			if (cheat is null) throw new ArgumentNullException(paramName: nameof(cheat));

			if (cheat.IsSeparator)
			{
				_cheatList.Add(cheat);
			}
			else
			{
				cheat.Changed += CheatChanged;
				if (Contains(cheat))
				{
					_cheatList.Remove(this.FirstOrDefault(c => c.Domain == cheat.Domain && c.Address == cheat.Address));
				}

				_cheatList.Add(cheat);
			}

			Touched();
		}

		public void AddRange(IEnumerable<Cheat> cheats)
		{
			var toAdd = cheats.Where(c => !_cheatList.Contains(c)).ToList();
			if (toAdd.Count is 0) return;
			const int WARN_WHEN_ADDING_MORE_THAN = 200;
			if (toAdd.Count > WARN_WHEN_ADDING_MORE_THAN && !_dialogParent.ModalMessageBox2($"Freezing {toAdd.Count} addresses at once is probably a bad idea. Do it anyway?")) return;
			_cheatList.AddRange(toAdd);
			Touched();
		}

		public bool Remove(Cheat cheat)
		{
			if (!_cheatList.Remove(cheat)) return false;
			Touched();
			return true;
		}

		public bool Contains(Cheat cheat)
			=> _cheatList.Exists(c => c == cheat);

		public void CopyTo(Cheat[] array, int arrayIndex)
		{
			_cheatList.CopyTo(array, arrayIndex);
		}

		public void RemoveRange(IEnumerable<Cheat> cheats)
		{
			foreach (var cheat in cheats.ToList()) // enumerate passed IEnumerable because it may depend on the value of _cheatList
			{
				_cheatList.Remove(cheat);
			}

			Touched();
		}

		public void RemoveRange(IEnumerable<Watch> watches)
		{
			_cheatList.RemoveAll(cheat => watches.Any(w => w == cheat));
			Touched();
		}

		public void Clear()
		{
			_cheatList.Clear();
			Touched();
		}

		public void DisableAll()
		{
			_cheatList.ForEach(c => c.Disable(false));
			Touched();
		}

		public bool IsActive(MemoryDomain domain, long address)
			=> _cheatList.Exists(cheat => !cheat.IsSeparator && cheat.Enabled && cheat.Domain == domain && cheat.Contains(address));

		/// <summary>
		/// Re-points the freezes at the domains of the machine that is now running,
		/// dropping any whose domain that machine does not have.
		/// </summary>
		public void UpdateDomains(IMemoryDomains domains)
		{
			for (int i = _cheatList.Count - 1; i >= 0; i--)
			{
				var cheat = _cheatList[i];
				if (cheat.IsSeparator) continue;

				var newDomain = domains[cheat.Domain.Name];
				if (newDomain is not null)
				{
					cheat.Domain = newDomain;
				}
				else
				{
					_cheatList.RemoveAt(i);
					Touched();
				}
			}
		}

		/// <summary>The list changed, so everything showing frozen addresses is stale.</summary>
		private void Touched()
			=> CheatChanged(Cheat.Separator); // a dummy: no single cheat invoked this change

		private void CheatChanged(object sender)
			=> Changed?.Invoke(this, new CheatListEventArgs(sender as Cheat));

		public class CheatListEventArgs : EventArgs
		{
			public CheatListEventArgs(Cheat c)
			{
				Cheat = c;
			}

			public Cheat Cheat { get; }
		}
	}
}
