using System.ComponentModel;
using System.Drawing;

namespace Chimera.WinForms.Controls
{
	/// <inheritdoc cref="Docs.LabelOrLinkLabel"/>
	public class LocLinkLabelEx : LinkLabelExBase
	{
		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new bool AutoSize => base.AutoSize;

		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new Size Size => base.Size;

		public LocLinkLabelEx() => base.AutoSize = true;
	}
}
