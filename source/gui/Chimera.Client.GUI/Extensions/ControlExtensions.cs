#nullable enable

using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Common.CollectionExtensions;
using Chimera.Common.ReflectionExtensions;
using Chimera.Emulation.Common;

using static Chimera.Common.CommctrlImports;

namespace Chimera.Client.GUI
{
	public static class ControlExtensions
	{
		private const int WM_SETREDRAW = 0x000B;

		/// <exception cref="ArgumentException"><typeparamref name="T"/> does not inherit <see cref="Enum"/></exception>
		public static void PopulateFromEnum<T>(this ComboBox box, T enumVal)
			where T : Enum
		{
			box.ReplaceItems(items: typeof(T).GetEnumDescriptions());
			box.SelectedItem = enumVal.GetDescription();
		}

		public static ToolStripMenuItem ToColumnsMenu(this InputRoll inputRoll, Action changeCallback)
		{
			var menu = new ToolStripMenuItem
			{
				Name = "GeneratedColumnsSubMenu",
				Text = "Columns",
			};

			var columns = inputRoll.AllColumns;

			foreach (var column in columns)
			{
				var menuItem = new ToolStripMenuItem
				{
					Name = column.Name,
					Text = $"{column.Text} ({column.Name})",
					Checked = column.Visible,
					CheckOnClick = true,
					Tag = column.Name,
				};

				menuItem.CheckedChanged += (o, ev) =>
				{
					var sender = (ToolStripMenuItem)o;
					columns.Find(c => c.Name == (string)sender.Tag).Visible = sender.Checked;
					columns.ColumnsChanged();
					changeCallback();
					inputRoll.Refresh();
				};

				menu.DropDownItems.Add(menuItem);
			}

			return menu;
		}

		public static Point ChildPointToScreen(this Control control, Control child)
		{
			return control.PointToScreen(new Point(child.Location.X, child.Location.Y));
		}

		public static void FollowMousePointer(this Form form)
		{
			var point = Cursor.Position;
			point.Offset(form.Width / -2, form.Height / -2);
			form.StartPosition = FormStartPosition.Manual;
			form.Location = point;
		}

		public static DialogResult ShowDialogOnScreen(this Form form)
		{
			var topLeft = new Point(
				Math.Max(0, form.Location.X),
				Math.Max(0, form.Location.Y));
			var screen = DrawingExtensions.BoundsOfDisplayContaining(topLeft)
				?? default; //TODO is zeroed the correct fallback value? --yoshi
			var w = screen.Right - form.Bounds.Right;
			var h = screen.Bottom - form.Bounds.Bottom;
			if (h < 0) topLeft.Y += h;
			if (w < 0) topLeft.X += w;
			form.SetDesktopLocation(topLeft.X, topLeft.Y);
			return form.ShowDialog();
		}

		public static Color Add(this Color color, int val)
		{
			var col = color.ToArgb();
			col += val;
			return Color.FromArgb(col);
		}

		/// <remarks>
		/// Due to the way this is written, using it in a foreach (as is done in SNESGraphicsDebugger)
		/// passes <c>Control</c> as the type parameter, meaning only properties on <see cref="Control"/> (and <see cref="Component"/>, etc.)
		/// will be processed. Why is there even a type param at all? I certainly don't know. --yoshi
		/// </remarks>
		public static T Clone<T>(this T controlToClone)
			where T : Control
		{
			PropertyInfo[] controlProperties = typeof(T).GetProperties(BindingFlags.Public | BindingFlags.Instance);

			Type t = controlToClone.GetType();
			var instance = (T) Activator.CreateInstance(t);

			t.GetProperty("AutoSize")?.SetMethod?.Invoke(instance, new object[] {false});

			for (int i = 0; i < 3; i++) // why 3 passes of this? --yoshi
			{
				foreach (var propInfo in controlProperties)
				{
					if (!propInfo.CanWrite)
					{
						continue;
					}

					if (propInfo.Name != "AutoSize" && propInfo.Name != "WindowTarget")
					{
						propInfo.SetValue(instance, propInfo.GetValue(controlToClone, null), null);
					}
				}
			}

			if (controlToClone is RetainedViewportPanel rvpToClone && instance is RetainedViewportPanel rvpCloned)
			{
				rvpCloned.SetBitmap((Bitmap) rvpToClone.GetBitmap().Clone());
			}

			return instance;
		}

		/// <summary>
		/// Converts the outdated IEnumerable Controls property to an <see cref="IEnumerable{T}"/> like .NET should have done a long time ago
		/// </summary>
		public static IEnumerable<Control> Controls(this Control control)
			=> control.Controls.Cast<Control>();

		public static IEnumerable<TabPage> TabPages(this TabControl tabControl)
		{
			return tabControl.TabPages.Cast<TabPage>();
		}

		public static Control? InnermostControlAt(this Form form, Point pos, GetChildAtPointSkip flags = GetChildAtPointSkip.None)
		{
			Control? top = form;
			Control? found;
			do
			{
				found = top!.GetChildAtPoint(top.PointToClient(pos), flags);
				top = found;
			} while (found is { HasChildren: true });
			return found;
		}

#pragma warning disable CS0618 // WinForms doesn't use generics ofc
		public static bool InsertAfter(this Control.ControlCollection controls, Control needle, Control insert)
		{
			var insertPoint = controls.IndexOf(needle);
			if (insertPoint < 0) return false;
			controls.Add(insert);
			controls.SetChildIndex(insert, insertPoint + 1);
			return true;
		}

		public static bool InsertAfter(this ToolStripItemCollection items, ToolStripItem needle, ToolStripItem insert)
			=> ((IList) items).InsertAfter(needle, insert: insert);

		public static bool InsertBefore(this Control.ControlCollection controls, Control needle, Control insert)
		{
			var insertPoint = controls.IndexOf(needle);
			if (insertPoint < 0) return false;
			controls.Add(insert);
			controls.SetChildIndex(insert, insertPoint);
			return true;
		}

		public static bool InsertBefore(this ToolStripItemCollection items, ToolStripItem needle, ToolStripItem insert)
			=> ((IList) items).InsertBefore(needle, insert: insert);
#pragma warning restore CS0618

		public static void ReplaceDropDownItems(this ToolStripDropDownItem menu, params ToolStripItem[] items)
		{
			menu.DropDownItems.Clear();
			menu.DropDownItems.AddRange(items);
		}

		public static void ReplaceItems(this ComboBox dropdown, params object[] items)
		{
			dropdown.Items.Clear();
			dropdown.Items.AddRange(items);
		}

		public static void ReplaceItems(this ComboBox dropdown, IEnumerable<object> items)
			=> dropdown.ReplaceItems(items: items.ToArray());

		public static CheckState ToCheckState(this bool? tristate)
			=> tristate switch
			{
				true => CheckState.Checked,
				false => CheckState.Unchecked,
				null => CheckState.Indeterminate,
			};

		public static void SuspendDrawing(this Control control)
		{
			if (!OSTailoredCode.IsUnixHost)
			{
				WmImports.SendMessageW(control.Handle, WM_SETREDRAW, (IntPtr) 0, IntPtr.Zero);
			}
		}

		public static void ResumeDrawing(this Control control)
		{
			if (!OSTailoredCode.IsUnixHost)
			{
				WmImports.SendMessageW(control.Handle, WM_SETREDRAW, (IntPtr) 1, IntPtr.Zero);
			}
		}
	}

	public static class ListViewExtensions
	{
		/// <summary>
		/// Dumps the contents of the ListView into a tab separated list of lines
		/// </summary>
		public static string CopyItemsAsText(this ListView listViewControl)
		{
			var indexes = listViewControl.SelectedIndices;
			if (indexes.Count <= 0)
			{
				return "";
			}

			var sb = new StringBuilder();

			// walk over each selected item and subitem within it to generate a string from it
			foreach (int index in indexes)
			{
				foreach (ListViewItem.ListViewSubItem item in listViewControl.Items[index].SubItems)
				{
					if (!string.IsNullOrWhiteSpace(item.Text))
					{
						sb.Append(item.Text).Append('\t');
					}
				}

				// remove the last tab
				sb.Remove(sb.Length - 1, 1);

				sb.Append("\r\n");
			}

			// remove last newline
			sb.Length -= 2;


			return sb.ToString();
		}

		/// <exception cref="Win32Exception">unmanaged call failed</exception>
		public static void SetSortIcon(this ListView listViewControl, int columnIndex, SortOrder order)
		{
			if (OSTailoredCode.IsUnixHost)
			{
				return;
			}

			var columnHeader = WmImports.SendMessageW(listViewControl.Handle, LVM_GETHEADER, IntPtr.Zero, IntPtr.Zero);
			for (int columnNumber = 0, l = listViewControl.Columns.Count; columnNumber < l; columnNumber++)
			{
				var columnPtr = new IntPtr(columnNumber);
				var item = new HDITEMW { mask = HDITEMW.Mask.Format };
				if (SendMessageW(columnHeader, HDM_GETITEMW, columnPtr, ref item) == IntPtr.Zero)
				{
					throw new Win32Exception();
				}

				if (columnNumber != columnIndex || order == SortOrder.None)
				{
					item.fmt &= ~HDITEMW.Format.SortDown & ~HDITEMW.Format.SortUp;
				}
				// ReSharper disable once SwitchStatementMissingSomeEnumCasesNoDefault
				else switch (order)
				{
					case SortOrder.Ascending:
						item.fmt &= ~HDITEMW.Format.SortDown;
						item.fmt |= HDITEMW.Format.SortUp;
						break;
					case SortOrder.Descending:
						item.fmt &= ~HDITEMW.Format.SortUp;
						item.fmt |= HDITEMW.Format.SortDown;
						break;
				}

				if (SendMessageW(columnHeader, HDM_SETITEMW, columnPtr, ref item) == IntPtr.Zero)
				{
					throw new Win32Exception();
				}
			}
		}

		public static bool IsOk(this DialogResult dialogResult)
		{
			return dialogResult == DialogResult.OK;
		}

		/// <summary>
		/// Sets the desired effect if data is present, else None
		/// </summary>
		public static void Set(this DragEventArgs e, DragDropEffects effect)
		{
			e.Effect = e.Data.GetDataPresent(DataFormats.FileDrop)
				? effect
				: DragDropEffects.None;
		}

		public static Bitmap ToBitMap(this Control control)
		{
			var b = new Bitmap(control.Width, control.Height);
			var rect = new Rectangle(new Point(0, 0), control.Size);
			control.DrawToBitmap(b, rect);
			return b;
		}

		public static void ToClipBoard(this Bitmap bitmap)
		{
			using var img = bitmap;
			Clipboard.SetImage(img);
		}

		public static void SaveAsFile(this Bitmap bitmap, IGameInfo game, string suffix, string systemId, PathEntryCollection paths, IDialogParent parent)
		{
			var result = parent.ShowFileSaveDialog(
				discardCWDChange: true,
				filter: FilesystemFilterSet.Screenshots,
				initDir: paths.ScreenshotAbsolutePathFor(systemId),
				initFileName: $"{game.FilesystemSafeName()}-{suffix}");
			if (result is null) return;
			FileInfo file = new(result);
			string extension = file.Extension.ToUpperInvariant();
			ImageFormat i = extension switch
			{
				".BMP" => ImageFormat.Bmp,
				_ => ImageFormat.Png,
			};
			bitmap.Save(file.FullName, i);
		}

		public static void SetDistanceOrDefault(this SplitContainer splitter, int distance, int defaultDistance)
		{
			if (distance > 0)
			{
				try
				{
					splitter.SplitterDistance = distance;
				}
				catch (Exception)
				{
					splitter.SplitterDistance = defaultDistance;
				}
			}
		}

		public static bool IsPressed(this KeyEventArgs e, Keys key)
			=> !e.Alt && !e.Control && !e.Shift && e.KeyCode == key;

		public static bool IsShift(this KeyEventArgs e, Keys key)
			=> !e.Alt && !e.Control && e.Shift && e.KeyCode == key;

		public static bool IsCtrl(this KeyEventArgs e, Keys key)
			=> !e.Alt && e.Control && !e.Shift && e.KeyCode == key;

		public static bool IsAlt(this KeyEventArgs e, Keys key)
			=> e.Alt && !e.Control && !e.Shift && e.KeyCode == key;

		public static bool IsCtrlShift(this KeyEventArgs e, Keys key)
			=> !e.Alt && e.Control && e.Shift && e.KeyCode == key;

		/// <summary>
		/// The grid's own description pane holds two lines and then gives up, and
		/// a core's setting is described in a paragraph - so the pane showed
		/// "Size the state pool to this machine and this system when a movie loads: a
		/// quarter of the RAM that is actually free (never less..." and the rest was
		/// only readable as a tooltip, one line wide enough to cross the screen
		/// (issue #41). This puts a read-only text box under the grid instead: it
		/// wraps, it SCROLLS, and it is filled from whatever row is selected. The
		/// grid's own pane and its tooltip are turned off, so there is one place a
		/// description is read and it is a place that can hold one.
		/// </summary>
		/// <param name="lines">how tall the box is, in lines of the grid's font</param>
		public static TextBox UseScrollableDescription(this PropertyGrid grid, int lines = 5)
		{
			grid.HelpVisible = false;
			SuppressGridTooltip(grid);

			var gap = UIHelper.ScaleY(4);
			var height = (grid.Font.Height * lines) + UIHelper.ScaleY(8);
			TextBox box = new()
			{
				Anchor = (grid.Anchor & ~AnchorStyles.Top) | AnchorStyles.Bottom,
				BackColor = SystemColors.Window,
				BorderStyle = BorderStyle.FixedSingle,
				Location = new Point(grid.Left, grid.Bottom - height),
				Multiline = true,
				ReadOnly = true,
				ScrollBars = ScrollBars.Vertical,
				Size = new Size(grid.Width, height),
				TabStop = false,
				WordWrap = true,
			};
			grid.Height -= height + gap;
			grid.Parent?.Controls.Add(box);
			box.BringToFront();

			void Show(GridItem item)
			{
				var pd = item?.PropertyDescriptor;
				box.Text = pd is null
					? ""
					: string.IsNullOrEmpty(pd.Description)
						? pd.DisplayName
						: pd.DisplayName + Environment.NewLine + Environment.NewLine + pd.Description;
				box.Select(0, 0);
			}
			grid.SelectedGridItemChanged += (_, e) => Show(e.NewSelection);
			// and when the grid is given something else to show: the first row of a
			// fresh object is selected without the item-changed event being raised
			// on every runtime, which left the box empty until something was clicked
			grid.SelectedObjectsChanged += (_, _) => Show(grid.SelectedGridItem);
			Show(grid.SelectedGridItem);
			return box;
		}

		/// <summary>
		/// The hover tooltip the grid shows for a row repeats the description as a
		/// single line, which for a paragraph is a line wider than the screen. There
		/// is no property for it, so this reaches for the view's tooltip and turns
		/// it off; a runtime that keeps it somewhere else simply keeps its tooltip,
		/// and the description box above is what people read either way.
		/// </summary>
		private static void SuppressGridTooltip(PropertyGrid grid)
		{
			try
			{
				foreach (var field in grid.GetType().GetFields(BindingFlags.Instance | BindingFlags.NonPublic))
				{
					if (field.GetValue(grid) is not Control view) continue;
					foreach (var inner in view.GetType().GetFields(BindingFlags.Instance | BindingFlags.NonPublic))
					{
						if (inner.GetValue(view) is ToolTip tip) tip.Active = false;
					}
				}
			}
			catch
			{
				// a tooltip that will not be found is a tooltip, not a failure
			}
		}

		/// <summary>
		/// Changes the description height area to match the rows needed for the largest description in the list
		/// </summary>
		public static void AdjustDescriptionHeightToFit(this PropertyGrid grid)
		{
			try
			{
				int maxLength = 0;
				string desc = "";

				foreach (PropertyDescriptor property in TypeDescriptor.GetProperties(grid.SelectedObject))
				{
					var s = property.Description;
					if (s != null && s.Length > maxLength)
					{
						maxLength = s.Length;
						desc = s;
					}
				}

				foreach (Control control in grid.Controls)
				{
					if (control.GetType().Name == "DocComment")
					{
						var fi = control.GetType().GetField("userSized", BindingFlags.Instance | BindingFlags.NonPublic);
						fi?.SetValue(control, true);
						using var label = new Label();
						var maxSize = new Size(grid.Width - 9, 999999);
						control.Height = label.Height + TextRenderer.MeasureText(desc, control.Font, maxSize, TextFormatFlags.WordBreak).Height;
						return;
					}
				}
			}
			catch
			{
				// Eat it
			}
		}

		public static void EnableCategoriesIfUsed(this PropertyGrid grid)
		{
			foreach (PropertyDescriptor property in TypeDescriptor.GetProperties(grid.SelectedObject))
			{
				if (property.Category is not null)
				{
					grid.PropertySort = PropertySort.Categorized;
					return;
				}
			}
		}
	}
}
