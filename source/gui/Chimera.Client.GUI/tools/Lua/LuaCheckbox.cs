using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	public class LuaCheckbox : CheckBox
	{
		private void DoLuaClick(object sender, EventArgs e)
		{
			var parent = Parent as LuaWinform;
			parent?.DoLuaEvent(Handle);
		}

		protected override void OnClick(EventArgs e)
		{
			base.OnClick(e);
			DoLuaClick(this, e);
		}
	}
}
