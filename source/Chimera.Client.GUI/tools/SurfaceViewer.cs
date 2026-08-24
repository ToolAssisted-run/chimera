using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Shows the auxiliary picture surfaces a core renders for itself
	/// (<see cref="ICoreSurfaces"/>) - nametables, pattern tables, sprite lists,
	/// tilemaps, whatever the core decided is worth looking at.
	///
	/// This window knows nothing about any of those things. It asks the core how
	/// many surfaces there are, how big each one is, and blits what it gets back;
	/// the picking-apart of PPU memory happens inside the core, where the knowledge
	/// already lives. That is the whole reason this one window replaces the pile of
	/// per-system viewers a frontend would otherwise need.
	///
	/// The UI is built in code rather than by the designer, because it's small and
	/// entirely uniform - there is nothing here to lay out per core.
	/// </summary>
	public sealed class SurfaceViewer : ToolFormBase, IToolFormAutoConfig
	{
		[RequiredService]
		private ICoreSurfaces Surfaces { get; set; }

		public static Icon ToolIcon
			=> Properties.Resources.MonitorIcon;

		protected override string WindowTitleStatic => "Surface Viewer";

		private readonly ComboBox _surfacePicker;
		private readonly ComboBox _zoomPicker;
		private readonly Panel _scrollPanel;
		private readonly PictureBox _canvas;
		private readonly ToolStripStatusLabel _status;

		private Bitmap _bmp;
		private int _index;
		private int _width, _height;
		private int _zoom = 1;

		public SurfaceViewer()
		{
			SuspendLayout();
			Icon = ToolIcon;
			Size = new(560, 480);

			MenuStrip menu = new();
			ToolStripMenuItem file = new("&File");
			file.DropDownItems.Add("&Copy to Clipboard", null, (_, _) => CopyToClipboard());
			file.DropDownItems.Add("&Save as PNG...", null, (_, _) => SaveAsPng());
			menu.Items.Add(file);
			// (the "Settings" menu is added by ToolManager for IToolFormAutoConfig)
			MainMenuStrip = menu;

			_surfacePicker = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = UIHelper.ScaleX(180) };
			_surfacePicker.SelectedIndexChanged += (_, _) => SelectSurface(_surfacePicker.SelectedIndex);
			_zoomPicker = new() { DropDownStyle = ComboBoxStyle.DropDownList, Width = UIHelper.ScaleX(60) };
			_zoomPicker.Items.AddRange([ "1x", "2x", "3x", "4x" ]);
			_zoomPicker.SelectedIndex = 0;
			_zoomPicker.SelectedIndexChanged += (_, _) =>
			{
				_zoom = _zoomPicker.SelectedIndex + 1;
				ResizeCanvas();
			};

			ToolStrip bar = new()
			{
				GripStyle = ToolStripGripStyle.Hidden,
				Items =
				{
					new ToolStripLabel("Surface:"),
					new ToolStripControlHost(_surfacePicker),
					new ToolStripLabel("Zoom:"),
					new ToolStripControlHost(_zoomPicker),
				},
			};

			_canvas = new() { BackColor = Color.Black, Location = new(0, 0) };
			_canvas.Paint += Canvas_Paint;
			_scrollPanel = new() { AutoScroll = true, Dock = DockStyle.Fill, BackColor = Color.Black };
			_scrollPanel.Controls.Add(_canvas);

			_status = new() { Spring = true, TextAlign = ContentAlignment.MiddleLeft };
			StatusStrip statusBar = new() { Items = { _status } };

			Controls.Add(_scrollPanel);
			Controls.Add(bar);
			Controls.Add(statusBar);
			Controls.Add(menu);
			ResumeLayout();
		}

		public override void Restart()
		{
			_surfacePicker.Items.Clear();
			foreach (var name in Surfaces.SurfaceNames) _surfacePicker.Items.Add(name);
			if (_surfacePicker.Items.Count is 0) return;
			_surfacePicker.SelectedIndex = _index < _surfacePicker.Items.Count ? _index : 0;
			SelectSurface(_surfacePicker.SelectedIndex);
		}

		private void SelectSurface(int index)
		{
			if (index < 0 || index >= Surfaces.SurfaceNames.Count) return;
			_index = index;
			Surfaces.GetSurfaceSize(index, out _width, out _height);
			_bmp?.Dispose();
			_bmp = new(_width, _height, PixelFormat.Format32bppArgb);
			_status.Text = $"{Surfaces.SurfaceNames[index]} - {_width}x{_height}";
			ResizeCanvas();
			RenderCurrent();
		}

		private void ResizeCanvas()
		{
			_canvas.Size = new(_width * _zoom, _height * _zoom);
			_canvas.Invalidate();
		}

		/// <summary>Pulls one freshly rendered frame of the selected surface into the bitmap.</summary>
		private void RenderCurrent()
		{
			if (_bmp is null) return;
			var pixels = Surfaces.RenderSurface(_index);
			var locked = _bmp.LockBits(new(0, 0, _width, _height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
			Marshal.Copy(pixels, 0, locked.Scan0, _width * _height);
			_bmp.UnlockBits(locked);
			_canvas.Invalidate();
		}

		private void Canvas_Paint(object sender, PaintEventArgs e)
		{
			if (_bmp is null) return;
			// nearest-neighbour: these are pixel-art surfaces, and a blurred nametable
			// is useless for the thing people open this window to do
			e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
			e.Graphics.PixelOffsetMode = PixelOffsetMode.Half;
			e.Graphics.DrawImage(_bmp, new Rectangle(0, 0, _width * _zoom, _height * _zoom));
		}

		private void CopyToClipboard()
		{
			if (_bmp is not null) Clipboard.SetImage(_bmp);
		}

		private void SaveAsPng()
		{
			if (_bmp is null) return;
			var name = new string(Surfaces.SurfaceNames[_index].Where(static c => char.IsLetterOrDigit(c)).ToArray());
			var path = this.ShowFileSaveDialog(
				initDir: Config!.PathEntries.ScreenshotAbsolutePathFor(Game.System),
				fileExt: ".png",
				filter: new FilesystemFilterSet(FilesystemFilter.PNGs),
				initFileName: $"{Game.FilesystemSafeName()}-{name}.png");
			if (path is not null) _bmp.Save(path, ImageFormat.Png);
		}

		protected override void UpdateAfter() => RenderCurrent();

		protected override void GeneralUpdate() => RenderCurrent();

		protected override void Dispose(bool disposing)
		{
			if (disposing) _bmp?.Dispose();
			base.Dispose(disposing);
		}
	}
}
