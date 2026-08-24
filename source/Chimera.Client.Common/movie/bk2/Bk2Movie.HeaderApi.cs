using System.Text;

using Chimera.Bizware.Graphics;

namespace Chimera.Client.Common
{
	public partial class Bk2Movie
	{
		private string _syncSettingsJson = "";

		public string SyncSettingsJson
		{
			get => _syncSettingsJson;
			set
			{
				if (_syncSettingsJson != value)
				{
					Changes = true;
					_syncSettingsJson = value;
				}
			}
		}

		public override ulong Rerecords
		{
			set
			{
				if (Header[HeaderKeys.Rerecords] != value.ToString())
				{
					Changes = true;
					Header[HeaderKeys.Rerecords] = value.ToString();
				}
			}
		}

		public virtual bool StartsFromSavestate
		{
			// ReSharper disable SimplifyConditionalTernaryExpression
			get => Header.TryGetValue(HeaderKeys.StartsFromSavestate, out var s) ? bool.Parse(s) : false;
			// ReSharper restore SimplifyConditionalTernaryExpression
			set
			{
				if (value)
				{
					Header[HeaderKeys.StartsFromSavestate] = "True";
				}
				else
				{
					Header.Remove(HeaderKeys.StartsFromSavestate);
				}
			}
		}

		/// <summary>
		/// The bundle this was recorded against, if any: a run that starts from a save
		/// starts from a bundle, because that is what composes a rom with what a core
		/// keeps. Name for a human, id for a machine.
		/// </summary>
		public string BundleName
		{
			get => Header.TryGetValue(HeaderKeys.Bundle, out var s) ? s : "";
			set
			{
				if (string.IsNullOrWhiteSpace(value)) Header.Remove(HeaderKeys.Bundle);
				else Header[HeaderKeys.Bundle] = value;
			}
		}

		public string BundleId
		{
			get => Header.TryGetValue(HeaderKeys.BundleId, out var s) ? s : "";
			set
			{
				if (string.IsNullOrWhiteSpace(value)) Header.Remove(HeaderKeys.BundleId);
				else Header[HeaderKeys.BundleId] = value;
			}
		}

		public bool StartsFromBundle => !string.IsNullOrWhiteSpace(BundleId) || !string.IsNullOrWhiteSpace(BundleName);

		public override string GameName
		{
			set
			{
				if (Header[HeaderKeys.GameName] != value)
				{
					Changes = true;
					Header[HeaderKeys.GameName] = value;
				}
			}
		}

		public override string SystemID
		{
			set
			{
				if (Header[HeaderKeys.Platform] != value)
				{
					Changes = true;
					Header[HeaderKeys.Platform] = value;
				}
			}
		}

		public override string Hash
		{
			set
			{
				if (Header[HeaderKeys.Sha1] != value)
				{
					Changes = true;
					Header[HeaderKeys.Sha1] = value;
				}
			}
		}

		public override string Author
		{
			set
			{
				if (Header[HeaderKeys.Author] != value)
				{
					Changes = true;
					Header[HeaderKeys.Author] = value;
				}
			}
		}

		public override string Core
		{
			set
			{
				if (Header[HeaderKeys.Core] != value)
				{
					Changes = true;
					Header[HeaderKeys.Core] = value;
				}
			}
		}

		public override string BoardName
		{
			set
			{
				if (Header[HeaderKeys.BoardName] != value)
				{
					Changes = true;
					Header[HeaderKeys.BoardName] = value;
				}
			}
		}

		public override string EmulatorVersion
		{
			set
			{
				if (Header[HeaderKeys.EmulatorVersion] != value)
				{
					Changes = true;
					Header[HeaderKeys.EmulatorVersion] = value;
				}
			}
		}

		public override string OriginalEmulatorVersion
		{
			set
			{
				if (Header[HeaderKeys.OriginalEmulatorVersion] != value)
				{
					Changes = true;
					Header[HeaderKeys.OriginalEmulatorVersion] = value;
				}
			}
		}

		public string TextSavestate { get; set; }
		public byte[] BinarySavestate { get; set; }
		public BitmapBuffer SavestateFramebuffer { get; set; }
	}
}
