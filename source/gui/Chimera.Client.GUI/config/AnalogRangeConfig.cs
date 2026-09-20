using System.Drawing;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	public partial class AnalogRangeConfig : Panel, IThemedControl
	{
		private Pen _axisPen = Pens.Black;

		private Pen _limitPen = Pens.Cyan;

		private Brush _fieldBrush = SystemBrushes.Control;

		private Brush _dotBrush = Brushes.White;

		/// <inheritdoc/>
		public void ApplyTheme(Theme theme)
		{
			Release(_axisPen);
			Release(_limitPen);
			Release(_fieldBrush);
			Release(_dotBrush);
			BackColor = theme[ThemeColorRole.AnalogRangeBackground];
			_axisPen = new Pen(theme[ThemeColorRole.AnalogRangeAxis]);
			_limitPen = new Pen(theme[ThemeColorRole.AnalogRangeLimit]);
			_fieldBrush = new SolidBrush(theme[ThemeColorRole.AnalogRangeField]);
			_dotBrush = new SolidBrush(theme[ThemeColorRole.AnalogRangeDot]);
			Refresh();
		}

		/// <summary>The system's own pens and brushes are shared and must not be disposed.</summary>
		private static void Release(object drawing)
		{
			if (drawing is Pen p && p != Pens.Black && p != Pens.Cyan) p.Dispose();
			else if (drawing is Brush b && b != SystemBrushes.Control && b != Brushes.White) b.Dispose();
		}

		private const int ScaleFactor = 4;
		private const int _3DPadding = 5;

		private int _maxX = 127;
		private int _maxY = 127;
		private bool _radial;

		public int MaxX
		{
			get => _maxX;
			set
			{
				_maxX = value;
				Refresh();
				Changed();
			}
		}

		public int MaxY
		{
			get => _maxY;
			set
			{
				_maxY = value;
				Refresh();
				Changed();
			}
		}

		public bool Radial
		{
			get => _radial;
			set
			{
				_radial = value;
				Refresh();
				Changed();
			}
		}

		private int ScaledX => MaxX / ScaleFactor;

		private int ScaledY => MaxY / ScaleFactor;

		private Point TopLeft
		{
			get
			{
				var centerX = Size.Width / 2;
				var centerY = Size.Height / 2;

				return new Point(centerX - ScaledX, centerY - ScaledY);
			}
		}

		public AnalogRangeConfig()
		{
			MaxX = 127;
			MaxY = 127;
			Size = new Size(65, 65);
			SetStyle(ControlStyles.AllPaintingInWmPaint, true);
			SetStyle(ControlStyles.UserPaint, true);
			SetStyle(ControlStyles.OptimizedDoubleBuffer, true);
			SetStyle(ControlStyles.SupportsTransparentBackColor, true);
			SetStyle(ControlStyles.Opaque, true);
			BackColor = Color.Gray;
			BorderStyle = BorderStyle.Fixed3D;

			InitializeComponent();
		}

		protected override void OnPaint(PaintEventArgs e)
		{
			e.Graphics.FillRectangle(_fieldBrush, 0, 0, Width, Height);
			e.Graphics.FillEllipse(_dotBrush, 0, 0, Width - _3DPadding, Height - _3DPadding);
			e.Graphics.DrawEllipse(_axisPen, 0, 0, Width - _3DPadding, Height - _3DPadding);

			if (Radial)
			{
				e.Graphics.DrawEllipse(
					_limitPen,
					TopLeft.X,
					TopLeft.Y,
					ScaledX * 2 - 4,
					ScaledY * 2 - 4);
			}
			else
			{
				e.Graphics.DrawRectangle(
					_limitPen,
					TopLeft.X,
					TopLeft.Y,
					ScaledX * 2 - 3,
					ScaledY * 2 - 3);
			}

			base.OnPaint(e);
		}

		private bool _isDragging;

		protected override void OnMouseDown(MouseEventArgs e)
		{
			if (e.Button == MouseButtons.Left)
			{
				_isDragging = true;
				DoDrag(e.X, e.Y);
			}
			else if (e.Button == MouseButtons.Right)
			{
				Radial = !Radial;
			}

			base.OnMouseDown(e);
		}

		protected override void OnMouseUp(MouseEventArgs e)
		{
			if (e.Button == MouseButtons.Left)
			{
				_isDragging = false;
			}

			base.OnMouseUp(e);
		}

		private void DoDrag(int x, int y)
		{
			if (_isDragging)
			{
				var centerX = Size.Width / 2;
				var centerY = Size.Height / 2;

				var offsetX = Math.Abs(centerX - x) * ScaleFactor;
				var offsetY = Math.Abs(centerY - y) * ScaleFactor;

				MaxX = Math.Min(offsetX, sbyte.MaxValue);
				MaxY = Math.Min(offsetY, sbyte.MaxValue);
			}
		}

		protected override void OnMouseMove(MouseEventArgs e)
		{
			DoDrag(e.X, e.Y);
			base.OnMouseMove(e);
		}

		public Action ChangeCallback { get; set; }

		private void Changed()
		{
			ChangeCallback?.Invoke();
		}
	}
}
