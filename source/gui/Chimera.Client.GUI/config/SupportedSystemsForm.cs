#nullable enable

using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Core Manager &gt; Systems...: every system the listed cores run, with the
	/// cores that run it, and a box that narrows the list as it is typed into
	/// (#172). A system whose cores are none of them installed is shown dimmed,
	/// as the Core Manager shows a core that is not installed.
	/// </summary>
	public sealed class SupportedSystemsForm : FormBase
	{
		private readonly IReadOnlyList<SupportedSystems.Entry> _all;
		private readonly TextBox _filter;
		private readonly ListView _list;
		private readonly Label _count;

		protected override string WindowTitleStatic => "Supported Systems";

		public SupportedSystemsForm(IReadOnlyList<SupportedSystems.Entry> systems)
		{
			_all = systems;
			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(720), UIHelper.ScaleY(520));
			MinimumSize = new(UIHelper.ScaleX(480), UIHelper.ScaleY(300));
			StartPosition = FormStartPosition.CenterParent;
			MinimizeBox = false;
			var margin = UIHelper.ScaleX(10);

			Label filterLabel = new()
			{
				AutoSize = true,
				Location = new(margin, UIHelper.ScaleY(14)),
				Text = "Filter:",
			};
			_filter = new TextBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin + UIHelper.ScaleX(46), UIHelper.ScaleY(10)),
				Width = ClientSize.Width - (2 * margin) - UIHelper.ScaleX(46) - UIHelper.ScaleX(110),
			};
			_filter.TextChanged += (_, _) => Fill();
			_count = new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				AutoSize = false,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(100), UIHelper.ScaleY(14)),
				Size = new(UIHelper.ScaleX(100), UIHelper.ScaleY(18)),
				TextAlign = System.Drawing.ContentAlignment.TopRight,
			};
			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(margin, UIHelper.ScaleY(40)),
				MultiSelect = false,
				Size = new(ClientSize.Width - (2 * margin), ClientSize.Height - UIHelper.ScaleY(40) - UIHelper.ScaleY(46)),
				View = View.Details,
			};
			_list.Columns.Add("System", UIHelper.ScaleX(260));
			_list.Columns.Add("Id", UIHelper.ScaleX(110));
			_list.Columns.Add("Cores", UIHelper.ScaleX(300));
			Button close = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), ClientSize.Height - UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Close",
			};
			Controls.AddRange([ filterLabel, _filter, _count, _list, close ]);
			AcceptButton = close;
			CancelButton = close;
			ResumeLayout();
			Fill();
		}

		/// <summary>The rows shown, as "name | id | cores", for tests.</summary>
		public IReadOnlyList<string> ShownRows
			=> _list.Items.Cast<ListViewItem>().Select(static i => string.Join(" | ", i.SubItems.Cast<ListViewItem.ListViewSubItem>().Select(static s => s.Text))).ToList();

		public string CountText => _count.Text;

		public void SetFilter(string text) => _filter.Text = text;

		private void Fill()
		{
			var shown = _all.Where(e => SupportedSystems.Matches(e, _filter.Text)).ToList();
			_list.BeginUpdate();
			_list.Items.Clear();
			foreach (var entry in shown)
			{
				ListViewItem item = new(entry.Name);
				item.SubItems.Add(entry.Id);
				item.SubItems.Add(string.Join(", ", entry.Cores));
				if (!entry.Installed) item.ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText);
				_list.Items.Add(item);
			}
			_list.EndUpdate();
			_count.Text = shown.Count == _all.Count ? $"{_all.Count} systems" : $"{shown.Count} of {_all.Count}";
		}
	}
}
