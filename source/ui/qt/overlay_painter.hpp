#pragma once
/* What is drawn over the picture, painted into a picture for Encode Video's
 * Capture Lua and Capture OSD (Chimera's): the shapes scripts draw with the
 * gui library, as Shapes.js paints them on the window's canvas; and lines of
 * text placed over them (the counters and messages, as the window lays them
 * out). Capture Lua paints into the machine's own picture, Capture OSD into
 * the picture area as the window shows it. */

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>

#include <vector>

namespace chimera::qt {

struct OverlayText
{
	QString text;
	int x = 0, y = 0;
	QColor color;
};

/* where the shapes go in the image. Left as it is, the image is the
 * machine's picture: the surface scripts draw on is the whole of it, and
 * what is drawn on the window's own (client) surface is left out. For the
 * picture area as the window shows it: where the surface is in it, how
 * large its pixels are there, and how large gui.text is (the counters'
 * scale) */
struct ShapesPlace
{
	QRectF surface;
	double scaleX = 1, scaleY = 1, textScale = 1;
	bool pictureArea = false;
};

/* the gui overlay's shapes (its JSON list) */
void paintShapes(QImage &image, const QString &shapes, const ShapesPlace &place = {});
/* lines of monospace text with a black outline, as the window's counters, 12 pixels at 1x */
void paintTexts(QImage &image, const std::vector<OverlayText> &texts, int pixelSize = 12);

} // namespace chimera::qt
