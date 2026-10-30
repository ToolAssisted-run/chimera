#include "overlay_painter.hpp"

#include <QFont>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace chimera::qt {

namespace {
QFont monospace(int pixels)
{
	QFont f(QStringLiteral("monospace"));
	f.setStyleHint(QFont::Monospace);
	f.setPixelSize(pixels > 0 ? pixels : 12);
	return f;
}

QColor colorOf(const QJsonValue &v)
{
	const QString s = v.toString();
	return s.isEmpty() ? QColor() : QColor::fromString(s);
}

/* an image file read once, again for a new epoch (clearImageCache), as the canvas loads them */
QImage imageOf(const QString &file, int epoch)
{
	/* drawn with cache false: read for this drawing alone */
	if (epoch < 0) return QImage(file);
	static QMap<QString, QImage> cache;
	const QString key = file + QLatin1Char('\n') + QString::number(epoch);
	auto it = cache.find(key);
	if (it == cache.end()) it = cache.insert(key, QImage(file));
	return *it;
}

/* a line one pixel of the image wide, however large the surface's pixels are, as the canvas draws it */
QPen hairline(const QColor &colour)
{
	QPen pen(colour, 1);
	pen.setCosmetic(true);
	return pen;
}

void strokeAndFill(QPainter &p, const QPainterPath &path, const QJsonObject &s)
{
	const QColor fill = colorOf(s.value(QStringLiteral("fill"))), line = colorOf(s.value(QStringLiteral("line")));
	if (fill.isValid()) p.fillPath(path, fill);
	if (line.isValid()) p.strokePath(path, hairline(line));
}
} // namespace

void paintShapes(QImage &image, const QString &shapes, const ShapesPlace &place)
{
	const QJsonArray list = QJsonDocument::fromJson(shapes.toUtf8()).array();
	if (list.isEmpty()) return;
	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	for (const QJsonValue &value : list)
	{
		const QJsonObject s = value.toObject();
		const QString k = s.value(QStringLiteral("k")).toString();
		const bool screen = k == QLatin1String("screentext");
		/* the client surface is the window's picture area's own pixels: only in a picture of it */
		const bool client = s.value(QStringLiteral("surface")).toString() == QLatin1String("client");
		if (client && !place.pictureArea) continue;
		auto num = [&s](const char *name) { return s.value(QLatin1String(name)).toDouble(); };
		const double x = num("x"), y = num("y"), w = num("w"), h = num("h");
		p.save();
		/* the surface's pixels, from its top left, as the window's canvas places them */
		double sx = 1, sy = 1;
		if (place.pictureArea && !client && !screen)
		{
			sx = place.scaleX;
			sy = place.scaleY;
			p.translate(place.surface.topLeft());
			p.scale(sx, sy);
		}
		if (k == QLatin1String("rect"))
		{
			const QColor fill = colorOf(s.value(QStringLiteral("fill"))), line = colorOf(s.value(QStringLiteral("line")));
			if (fill.isValid()) p.fillRect(QRectF(x, y, w, h), fill);
			if (line.isValid())
			{
				p.setPen(hairline(line));
				p.drawRect(QRectF(x + 0.5 / sx, y + 0.5 / sy, w, h));
			}
		}
		else if (k == QLatin1String("ellipse"))
		{
			QPainterPath path;
			path.addEllipse(QRectF(x, y, w, h));
			strokeAndFill(p, path, s);
		}
		else if (k == QLatin1String("line"))
		{
			p.setPen(hairline(colorOf(s.value(QStringLiteral("line")))));
			p.drawLine(QPointF(x + 0.5, y + 0.5), QPointF(num("x2") + 0.5, num("y2") + 0.5));
		}
		else if (k == QLatin1String("path") || k == QLatin1String("bezier"))
		{
			const QJsonArray points = s.value(QStringLiteral("points")).toArray();
			auto at = [&points](int i) { return QPointF(points[i].toArray()[0].toDouble() + 0.5, points[i].toArray()[1].toDouble() + 0.5); };
			if (!points.isEmpty())
			{
				QPainterPath path(at(0));
				if (k == QLatin1String("bezier") && points.size() >= 4)
					path.cubicTo(at(1), at(2), at(3));
				else
					for (int i = 1; i < points.size(); i++) path.lineTo(at(i));
				if (s.value(QStringLiteral("closed")).toBool()) path.closeSubpath();
				strokeAndFill(p, path, s);
			}
		}
		else if (k == QLatin1String("pie"))
		{
			/* the canvas turns clockwise from three o'clock; Qt counts the other way */
			const QRectF box(x, y, w, h);
			const double start = num("start"), sweep = num("sweep");
			QPainterPath path;
			if (!s.value(QStringLiteral("arc")).toBool()) path.moveTo(box.center());
			path.arcMoveTo(box, -start);
			path.arcTo(box, -start, -sweep);
			if (!s.value(QStringLiteral("arc")).toBool())
			{
				path.lineTo(box.center());
				path.closeSubpath();
			}
			strokeAndFill(p, path, s);
		}
		else if (k == QLatin1String("image"))
		{
			const QImage img = imageOf(s.value(QStringLiteral("file")).toString(), s.value(QStringLiteral("epoch")).toInt());
			if (!img.isNull())
			{
				if (s.contains(QStringLiteral("sw")))
					p.drawImage(QRectF(x, y, w, h), img, QRectF(num("sx"), num("sy"), num("sw"), num("sh")));
				else if (w >= 0 && h >= 0 && s.contains(QStringLiteral("w")))
					p.drawImage(QRectF(x, y, w, h), img);
				else
					p.drawImage(QPointF(x, y), img);
			}
		}
		else if (k == QLatin1String("text") || k == QLatin1String("screentext"))
		{
			/* gui.text is the counters' size, 12 pixels at 1x */
			const double k12 = screen ? place.textScale : 1;
			const int size = screen ? static_cast<int>(std::lround(12 * k12)) : static_cast<int>(num("size"));
			/* the font and style the script named, as Shapes.js draws them */
			QFont font = monospace(size);
			const QString family = s.value(QStringLiteral("family")).toString();
			if (!screen && !family.isEmpty()) font.setFamily(family);
			const QString style = s.value(QStringLiteral("style")).toString().toLower();
			font.setBold(style == QLatin1String("bold"));
			font.setItalic(style == QLatin1String("italic"));
			font.setUnderline(style == QLatin1String("underline"));
			font.setStrikeOut(style == QLatin1String("strikethrough"));
			/* a line a row, each from the text's left, as Chimera's */
			const QStringList lines = s.value(QStringLiteral("text")).toString().remove(QLatin1Char('\r')).split(QLatin1Char('\n'));
			const QFontMetrics metrics(font);
			int tw = 0;
			for (const QString &line : lines) tw = std::max(tw, metrics.horizontalAdvance(line));
			const double lineHeight = screen ? 14 * k12 : std::ceil(size * 1.15);
			const double th = lineHeight * static_cast<double>(lines.size());
			double tx = x, ty = y;
			if (!screen)
			{
				const QString ha = s.value(QStringLiteral("halign")).toString().toLower(), va = s.value(QStringLiteral("valign")).toString().toLower();
				if (ha == QLatin1String("center") || ha == QLatin1String("middle")) tx -= tw / 2;
				else if (ha == QLatin1String("right")) tx -= tw;
				if (va == QLatin1String("center") || va == QLatin1String("middle")) ty -= std::floor(th / 2);
				else if (va == QLatin1String("bottom")) ty -= th;
			}
			else
			{
				/* from a corner of the picture, as the window places it in its own */
				const QString a = s.value(QStringLiteral("anchor")).toString();
				const bool right = a == QLatin1String("topright") || a == QLatin1String("bottomright") || a == QLatin1String("1") || a == QLatin1String("3");
				const bool bottom = a == QLatin1String("bottomleft") || a == QLatin1String("bottomright") || a == QLatin1String("2") || a == QLatin1String("3");
				tx = right ? image.width() - x * k12 - tw : x * k12;
				ty = bottom ? image.height() - (y + 12) * k12 - (th - lineHeight) : y * k12;
			}
			const QColor back = colorOf(s.value(QStringLiteral("back")));
			p.setFont(font);
			auto each = [&](double dx, double dy) {
				for (qsizetype i = 0; i < lines.size(); i++)
					p.drawText(QPointF(tx + dx, ty + dy + static_cast<double>(i) * lineHeight + metrics.ascent()), lines[i]);
			};
			if (!screen && back.isValid())
			{
				/* a picture box's background is a box; the picture's an outline around the letters */
				if (s.value(QStringLiteral("outline")).toBool())
				{
					p.setPen(back);
					for (int dx = -1; dx <= 1; dx++)
						for (int dy = -1; dy <= 1; dy++) each(dx, dy);
				}
				else
					p.fillRect(QRectF(tx, ty, tw, th), back);
			}
			p.setPen(colorOf(s.value(QStringLiteral("color"))));
			each(0, 0);
		}
		p.restore();
	}
}

void paintTexts(QImage &image, const std::vector<OverlayText> &texts, int pixelSize)
{
	if (texts.empty()) return;
	QPainter p(&image);
	const QFont font = monospace(pixelSize);
	p.setFont(font);
	const int ascent = QFontMetrics(font).ascent();
	for (const OverlayText &t : texts)
	{
		const QPointF at(t.x, t.y + ascent);
		/* the outline Qt Quick's Text draws, a pixel of black all round */
		p.setPen(Qt::black);
		for (int dx = -1; dx <= 1; dx++)
			for (int dy = -1; dy <= 1; dy++)
				if (dx != 0 || dy != 0) p.drawText(at + QPointF(dx, dy), t.text);
		p.setPen(t.color);
		p.drawText(at, t.text);
	}
}

} // namespace chimera::qt
