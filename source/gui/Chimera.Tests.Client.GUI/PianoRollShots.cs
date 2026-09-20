using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The piano roll, pictured, in every theme.
	///
	/// It is the surface the whole application is about and the one nothing else
	/// here can show: TAStudio itself needs a core, a project and a greenzone to
	/// exist, none of which a frontend test has. So the roll is built directly and
	/// given exactly the row and cell colours TAStudio gives it - the greenzone
	/// shades, the stated and invalidated variants, the lag zone, a marker, the
	/// frame the emulator is on, the analog-edit tint and the alternate-player
	/// stripe - which is what a person needs to look at to say whether a theme
	/// works.
	/// </summary>
	[TestClass]
	public class PianoRollShots
	{
		private enum RowKind { GreenZone, GreenZoneStated, GreenZoneInvalidated, LagZone, LagZoneStated, LagZoneInvalidated, Marker, Current, Untouched }

		private static RowKind KindOf(int row) => (row % 24) switch
		{
			< 4 => RowKind.GreenZoneStated,
			< 8 => RowKind.GreenZone,
			8 or 9 => RowKind.Marker,
			10 => RowKind.Current,
			< 14 => RowKind.LagZone,
			< 17 => RowKind.LagZoneStated,
			< 19 => RowKind.LagZoneInvalidated,
			< 22 => RowKind.GreenZoneInvalidated,
			_ => RowKind.Untouched,
		};

		private static Form BuildRoll()
		{
			Form host = new()
			{
				ClientSize = new(560, 420),
				StartPosition = FormStartPosition.Manual,
				Location = new(0, 0),
				Text = "Piano roll",
			};
			InputRoll roll = new() { Dock = DockStyle.Fill, GridLines = true };
			roll.AllColumns.AddRange(
			[
				new RollColumn("Cursor", 18, ""),
				new RollColumn("Frame", 68, "Frame"),
				new RollColumn("P1 U", 24, "U"),
				new RollColumn("P1 D", 24, "D"),
				new RollColumn("P1 L", 24, "L"),
				new RollColumn("P1 R", 24, "R"),
				new RollColumn("P1 A", 24, "A"),
				new RollColumn("P1 B", 24, "B"),
				new RollColumn("P1 Tilt", 52, "Tilt"),
				new RollColumn("P2 U", 24, "U"),
				new RollColumn("P2 D", 24, "D"),
				new RollColumn("P2 A", 24, "A"),
				new RollColumn("P2 B", 24, "B"),
			]);
			roll.RowCount = 120;

			roll.QueryItemText += static (InputRoll sender, int index, RollColumn column, out string text, ref int x, ref int y) =>
			{
				text = column.Name switch
				{
					"Cursor" => KindOf(index) is RowKind.Current ? ">" : "",
					"Frame" => index.ToString("D6"),
					"P1 Tilt" => index % 7 is 0 ? (index % 200) - 100 + "" : "",
					_ => (index * (column.Name.Length + 3)) % 5 is 0 ? column.Text : "",
				};
			};

			roll.QueryRowBkColor += static (InputRoll sender, int index, ref Color color) =>
			{
				var p = TAStudioPalette.FromTheme(ThemeLibrary.Current);
				color = KindOf(index) switch
				{
					RowKind.Current => p.CurrentFrame_InputLog,
					RowKind.GreenZone => p.GreenZone_InputLog,
					RowKind.GreenZoneStated => p.GreenZone_InputLog_Stated,
					RowKind.GreenZoneInvalidated => p.GreenZone_InputLog_Invalidated,
					RowKind.LagZone => p.LagZone_InputLog,
					RowKind.LagZoneStated => p.LagZone_InputLog_Stated,
					RowKind.LagZoneInvalidated => p.LagZone_InputLog_Invalidated,
					RowKind.Marker => p.GreenZone_InputLog,
					_ => ThemeEngine.Color(ThemeColorRole.TasDefaultRow),
				};
			};

			roll.QueryItemBkColor += static (InputRoll sender, int index, RollColumn column, ref Color color) =>
			{
				var p = TAStudioPalette.FromTheme(ThemeLibrary.Current);
				if (column.Name is "Cursor")
				{
					color = ThemeEngine.Color(ThemeColorRole.TasCursorColumn);
					return;
				}
				if (column.Name is "Frame")
				{
					color = KindOf(index) switch
					{
						RowKind.Marker => index % 48 is 8 ? p.PermanentMarker_FrameCol : p.Marker_FrameCol,
						RowKind.LagZone or RowKind.LagZoneStated or RowKind.LagZoneInvalidated => p.LagZone_FrameCol,
						_ => ThemeEngine.Color(ThemeColorRole.TasFrameColumnWash),
					};
					return;
				}
				if (column.Name is "P1 Tilt" && index % 7 is 0)
				{
					color = p.AnalogEdit_Col;
					return;
				}
				if (column.Name.StartsWith("P2", StringComparison.Ordinal))
				{
					color = ThemeEngine.Color(ThemeColorRole.TasAlternatePlayer);
				}
			};

			host.Controls.Add(roll);
			return host;
		}

		[TestMethod]
		public void PianoRoll()
		{
			if (UiShots.Dir is null) { Assert.Inconclusive("set CHIMERA_UI_SHOTS to write screenshots"); return; }
			using var form = BuildRoll();
			UiShots.Shoot(form, "piano-roll");
		}

		/// <summary>
		/// Not a picture: the roll has to survive being told every theme in turn
		/// while it is on screen and drawing, which is what a live theme change is.
		/// </summary>
		[TestMethod]
		public void TheRollTakesEveryThemeWhileItIsDrawing()
		{
			using var form = BuildRoll();
			form.Show();
			foreach (var name in ThemeLibrary.All.Select(static t => t.Name).ToList())
			{
				ThemeEngine.Apply(form, ThemeLibrary.Select(name));
				form.Refresh();
				Application.DoEvents();
			}
			var roll = (InputRoll) form.Controls[0];
			var dark = ThemeLibrary.Select("Dark");
			ThemeEngine.Apply(form, dark);
			Assert.AreEqual(dark[ThemeColorRole.RollBackground].ToArgb(), roll.BackColor.ToArgb());
			Assert.AreEqual(dark[ThemeColorRole.RollColumnBackground].ToArgb(), roll.ColumnBackColor.ToArgb());
			Assert.AreEqual(dark[ThemeColorRole.RollGridLines].ToArgb(), roll.GridLineColor.ToArgb());
			ThemeEngine.Apply(form, ThemeLibrary.Select("Light"));
		}
	}
}
