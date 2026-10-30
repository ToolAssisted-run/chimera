#include "picture_item.hpp"

#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <algorithm>
#include <cstring>

#include "qml_view_model.hpp"

namespace chimera::qt {

PictureItem::PictureItem(QQuickItem *parent) : QQuickItem(parent)
{
	setFlag(ItemHasContents, true);
}

QObject *PictureItem::source() const { return source_.data(); }

void PictureItem::setSource(QObject *source)
{
	QmlPicture *p = qobject_cast<QmlPicture *>(source);
	if (p == source_) return;
	disconnect(watch_);
	source_ = p;
	if (p != nullptr) watch_ = connect(p, &QmlPicture::changed, this, &QQuickItem::update);
	Q_EMIT sourceChanged();
	update();
}

void PictureItem::setSmoothScaling(bool on)
{
	if (on == smooth_) return;
	smooth_ = on;
	Q_EMIT smoothScalingChanged();
	update();
}

void PictureItem::setFit(const QVariantMap &fit)
{
	if (fit == fitMap_) return;
	fitMap_ = fit;
	auto number = [&](const char *name, double otherwise) {
		const auto it = fit.find(QString::fromLatin1(name));
		return it == fit.end() ? otherwise : it->toDouble();
	};
	vm::DisplayFit f;
	f.aspectMode = static_cast<int>(number("aspectMode", 1));
	f.customWidth = static_cast<int>(number("customWidth", 0));
	f.customHeight = static_cast<int>(number("customHeight", 0));
	f.ratioX = number("ratioX", 4);
	f.ratioY = number("ratioY", 3);
	f.letterbox = number("letterbox", 1) != 0;
	f.integerScale = number("integerScale", 0) != 0;
	f.cropLeft = static_cast<int>(number("cropLeft", 0));
	f.cropTop = static_cast<int>(number("cropTop", 0));
	f.cropRight = static_cast<int>(number("cropRight", 0));
	f.cropBottom = static_cast<int>(number("cropBottom", 0));
	f.padLeft = static_cast<int>(number("padLeft", 0));
	f.padTop = static_cast<int>(number("padTop", 0));
	f.padRight = static_cast<int>(number("padRight", 0));
	f.padBottom = static_cast<int>(number("padBottom", 0));
	f.clientLeft = static_cast<int>(number("clientLeft", 0));
	f.clientTop = static_cast<int>(number("clientTop", 0));
	f.clientRight = static_cast<int>(number("clientRight", 0));
	f.clientBottom = static_cast<int>(number("clientBottom", 0));
	f.prescale = static_cast<int>(number("prescale", 1));
	f.autoPrescale = number("autoPrescale", 0) != 0;
	fit_ = f;
	fitNew_ = true;
	Q_EMIT fitChanged();
	update();
}

void PictureItem::placedAt(QRectF shown, QRectF source, QRectF surface)
{
	if (shown == shownRect_ && source == source_rect_ && surface == surfaceRect_) return;
	shownRect_ = shown;
	source_rect_ = source;
	surfaceRect_ = surface;
	Q_EMIT placementChanged();
}

void PictureItem::sizeSeen(int width, int height, int frameWidth, int frameHeight)
{
	if (width == pictureWidth_ && height == pictureHeight_ && frameWidth == frameWidth_ && frameHeight == frameHeight_) return;
	pictureWidth_ = width;
	pictureHeight_ = height;
	frameWidth_ = frameWidth;
	frameHeight_ = frameHeight;
	Q_EMIT pictureSizeChanged();
}

PictureItem::Drawn PictureItem::drawnAs(const uint32_t *pixels, int width, int height, double shownWidth, double shownHeight) const
{
	Drawn d;
	const int areaW = std::max(1, qRound(this->width())), areaH = std::max(1, qRound(this->height()));
	d.image = QImage(areaW, areaH, QImage::Format_RGB32);
	d.image.fill(Qt::black);
	if (pixels == nullptr || width <= 0 || height <= 0) return d;
	/* as updatePaintNode draws it: grown by whole steps, then placed, sharp or smoothed */
	int stepX = 1, stepY = 1;
	fit_.prescaleSteps(width, height, shownWidth, shownHeight, areaW, areaH, smooth_, kLargestSide, stepX, stepY);
	QImage frame(reinterpret_cast<const uchar *>(pixels), width, height, width * 4, QImage::Format_RGB32);
	if (stepX != 1 || stepY != 1) frame = frame.scaled(width * stepX, height * stepY, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	const vm::DisplayFit::Rect from = fit_.source(width, height);
	const vm::DisplayFit::Rect to = fit_.place(width, height, shownWidth, shownHeight, areaW, areaH);
	const vm::DisplayFit::Rect all = fit_.surface(width, height, shownWidth, shownHeight, areaW, areaH);
	QPainter p(&d.image);
	p.setRenderHint(QPainter::SmoothPixmapTransform, smooth_);
	p.drawImage(QRectF(to.x, to.y, to.w, to.h), frame, QRectF(from.x * stepX, from.y * stepY, from.w * stepX, from.h * stepY));
	d.surface = QRectF(all.x, all.y, all.w, all.h);
	d.scaleX = from.w > 0 ? to.w / from.w : 1;
	d.scaleY = from.h > 0 ? to.h / from.h : 1;
	return d;
}

QSGNode *PictureItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
	auto *node = static_cast<QSGSimpleTextureNode *>(old);
	const bool fresh = source_ != nullptr && source_->picture().takeIfNewer(frame_.serial, frame_);
	if (frame_.width <= 0 || frame_.height <= 0)
	{
		delete node;
		return nullptr;
	}
	/* the GUI's thread waits while this runs: its fit can be read */
	if (fresh || fitNew_)
	{
		/* told to the GUI's thread, the picture's size at 1x as it is fitted, its game padding with it */
		double nw = 0, nh = 0;
		fit_.surfaceSize(frame_.width, frame_.height, frame_.shownWidth, frame_.shownHeight, nw, nh);
		QMetaObject::invokeMethod(this, "sizeSeen", Qt::QueuedConnection, Q_ARG(int, static_cast<int>(nw + 0.5)),
			Q_ARG(int, static_cast<int>(nh + 0.5)), Q_ARG(int, frame_.width), Q_ARG(int, frame_.height));
		fitNew_ = false;
	}
	if (node == nullptr)
	{
		node = new QSGSimpleTextureNode;
		node->setOwnsTexture(true);
	}
	/* Chimera's prescaling: grown pixel by pixel by whole steps before it
	 * is drawn (and smoothed only from there) */
	int stepX = 1, stepY = 1;
	fit_.prescaleSteps(frame_.width, frame_.height, frame_.shownWidth, frame_.shownHeight, width(), height(), smooth_, kLargestSide, stepX, stepY);
	if (fresh || node->texture() == nullptr || stepX != stepX_ || stepY != stepY_)
	{
		/* the image owns its pixels: the texture may be uploaded after this
		 * copy of the frame has been overwritten by the next one */
		QImage image(frame_.width, frame_.height, QImage::Format_RGB32);
		const size_t row = static_cast<size_t>(frame_.width) * sizeof(uint32_t);
		for (int y = 0; y < frame_.height; y++)
			std::memcpy(image.scanLine(y), frame_.pixels.data() + static_cast<size_t>(y) * frame_.width, row);
		if (stepX != 1 || stepY != 1) image = image.scaled(frame_.width * stepX, frame_.height * stepY, Qt::IgnoreAspectRatio, Qt::FastTransformation);
		node->setTexture(window()->createTextureFromImage(image));
		stepX_ = stepX;
		stepY_ = stepY;
		shown_++;
	}
	node->setFiltering(smooth_ ? QSGTexture::Linear : QSGTexture::Nearest);

	const vm::DisplayFit::Rect from = fit_.source(frame_.width, frame_.height);
	node->setSourceRect(QRectF(from.x * stepX, from.y * stepY, from.w * stepX, from.h * stepY));
	const vm::DisplayFit::Rect to = fit_.place(frame_.width, frame_.height, frame_.shownWidth, frame_.shownHeight, width(), height());
	node->setRect(QRectF(to.x, to.y, to.w, to.h));
	/* told to the window's thread when it moves, for what is drawn over it */
	const vm::DisplayFit::Rect all = fit_.surface(frame_.width, frame_.height, frame_.shownWidth, frame_.shownHeight, width(), height());
	const QRectF shown(to.x, to.y, to.w, to.h), source(from.x, from.y, from.w, from.h), surface(all.x, all.y, all.w, all.h);
	if (shown != saidShown_ || source != saidSource_ || surface != saidSurface_)
	{
		saidShown_ = shown;
		saidSource_ = source;
		saidSurface_ = surface;
		QMetaObject::invokeMethod(this, "placedAt", Qt::QueuedConnection, Q_ARG(QRectF, shown), Q_ARG(QRectF, source), Q_ARG(QRectF, surface));
	}
	return node;
}

} // namespace chimera::qt
