using System.ComponentModel;

namespace Chimera.WinForms.Controls
{
	/// <inheritdoc cref="Docs.GroupBox"/>
	public class LocSzGroupBoxEx : GroupBoxExBase
	{
		[DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)]
		public new bool AutoSize => base.AutoSize;
	}
}
