using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Windows.Forms;

namespace Chimera.WinForms.Controls
{
	/// <summary>
	/// This class adds on to the functionality provided in <see cref="MenuStrip"/>.
	/// </summary>
	public class MenuStripEx : MenuStrip
	{
		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new Size Size => base.Size;

		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new Point Location => new Point(0, 0);

		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new string Text
			=> base.Text;

		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new string Name
			=> base.Name;

		/// <summary>each right-aligned item's own left margin, before it was widened</summary>
		private readonly Dictionary<ToolStripItem, int> _ownLeftMargin = new();

		private bool _pushingRight;

		/// <summary>
		/// A flowing menu (<see cref="ToolStripLayoutStyle.Flow"/>, which wraps onto more
		/// rows in a narrow window instead of hiding items behind a chevron) ignores
		/// <see cref="ToolStripItem.Alignment"/>. So an item marked
		/// <see cref="ToolStripItemAlignment.Right"/> is pushed to the right edge of the row
		/// it lands on here, by widening its left margin by the room left over - worked out
		/// again on every layout, so it follows the window's width. Meant for the last item.
		/// </summary>
		protected override void OnLayout(LayoutEventArgs e)
		{
			base.OnLayout(e);
			if (_pushingRight || LayoutStyle is not ToolStripLayoutStyle.Flow) return;
			_pushingRight = true;
			try
			{
				foreach (ToolStripItem item in Items)
				{
					if (item.Alignment is not ToolStripItemAlignment.Right) continue;
					if (!_ownLeftMargin.TryGetValue(item, out int own)) _ownLeftMargin[item] = own = item.Margin.Left;
					if (!item.Available) continue;
					Padding m = item.Margin;
					if (m.Left != own)
					{
						item.Margin = new Padding(own, m.Top, m.Right, m.Bottom);
						base.OnLayout(e);
					}
					int row = item.Bounds.Top;
					// a pixel short of the edge, so the wider margin never wraps it onto a row of its own
					int room = DisplayRectangle.Right - item.Bounds.Right - m.Right - 1;
					if (room <= 0) continue;
					item.Margin = new Padding(own + room, m.Top, m.Right, m.Bottom);
					base.OnLayout(e);
					if (item.Bounds.Top != row)
					{
						item.Margin = new Padding(own, m.Top, m.Right, m.Bottom);
						base.OnLayout(e);
					}
				}
			}
			finally
			{
				_pushingRight = false;
			}
		}

		protected override void WndProc(ref Message m)
		{
			base.WndProc(ref m);
			if (m.Msg == NativeConstants.WM_MOUSEACTIVATE
				&& m.Result == (IntPtr)NativeConstants.MA_ACTIVATEANDEAT)
			{
				m.Result = (IntPtr)NativeConstants.MA_ACTIVATE;
			}
		}
	}
}
