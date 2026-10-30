#pragma once
/* Draws a view model's picture, centred, fitted as the Display window says
 * (vm::DisplayFit: its shape, bars or stretched, whole steps, crop). */

#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QVariantMap>

#include <atomic>

#include "chimera/vm/display_vm.hpp"
#include "chimera/vm/picture.hpp"

namespace chimera::qt {

class QmlPicture;

class PictureItem : public QQuickItem
{
	Q_OBJECT
	Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
	Q_PROPERTY(bool smooth READ smoothScaling WRITE setSmoothScaling NOTIFY smoothScalingChanged)
	/* the Display window's fitting, by the display view model's names
	 * (aspectMode, customWidth, ... cropBottom) */
	Q_PROPERTY(QVariantMap fit READ fit WRITE setFit NOTIFY fitChanged)
	/* the picture's size at 1x as the Display settings show it (the machine's
	 * own: the size it says), 0 before the first: for the window's sizes */
	Q_PROPERTY(int pictureWidth READ pictureWidth NOTIFY pictureSizeChanged)
	Q_PROPERTY(int pictureHeight READ pictureHeight NOTIFY pictureSizeChanged)
	/* the machine's picture's own pixels, 0 before the first: the size Encode
	 * Video writes without Capture OSD */
	Q_PROPERTY(int frameWidth READ frameWidth NOTIFY pictureSizeChanged)
	Q_PROPERTY(int frameHeight READ frameHeight NOTIFY pictureSizeChanged)
	/* where the picture is drawn in the item, and the part of its pixels
	 * shown there (the crop taken off): for what is drawn over it in the
	 * machine's pixels */
	Q_PROPERTY(QRectF shownRect READ shownRect NOTIFY placementChanged)
	Q_PROPERTY(QRectF sourceRect READ sourceRect NOTIFY placementChanged)
	/* where the surface scripts draw on is: the picture with its game padding (Chimera's "emu" surface) */
	Q_PROPERTY(QRectF surfaceRect READ surfaceRect NOTIFY placementChanged)

public:
	explicit PictureItem(QQuickItem *parent = nullptr);

	QObject *source() const;
	void setSource(QObject *source);
	bool smoothScaling() const { return smooth_; }
	int pictureWidth() const { return pictureWidth_; }
	int pictureHeight() const { return pictureHeight_; }
	int frameWidth() const { return frameWidth_; }
	int frameHeight() const { return frameHeight_; }
	void setSmoothScaling(bool on);
	QVariantMap fit() const { return fitMap_; }
	QRectF shownRect() const { return shownRect_; }
	QRectF sourceRect() const { return source_rect_; }
	QRectF surfaceRect() const { return surfaceRect_; }
	void setFit(const QVariantMap &fit);

	/* a picture of the machine's drawn as this item draws it now - its
	 * shape, bars, crop and filter - on black, in an image of the item's
	 * size (Encode Video's Capture OSD, as Chimera's); with where the
	 * surface scripts draw on is in it, and how large its pixels are there */
	struct Drawn
	{
		QImage image;
		QRectF surface;
		double scaleX = 1, scaleY = 1;
	};
	Drawn drawnAs(const uint32_t *pixels, int width, int height, double shownWidth, double shownHeight) const;

	/* pictures handed to the GPU so far, for measuring (any thread) */
	qint64 framesShown() const { return shown_.load(); }

Q_SIGNALS:
	void sourceChanged();
	void smoothScalingChanged();
	void pictureSizeChanged();
	void fitChanged();
	void placementChanged();

protected:
	QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
	QPointer<QmlPicture> source_;
	QMetaObject::Connection watch_;
	bool smooth_ = false;
	QVariantMap fitMap_;
	vm::DisplayFit fit_;
	bool fitNew_ = false;
	int pictureWidth_ = 0, pictureHeight_ = 0, frameWidth_ = 0, frameHeight_ = 0;
	/* the GUI thread's: the sizes the render thread saw */
	Q_INVOKABLE void sizeSeen(int width, int height, int frameWidth, int frameHeight);
	Q_INVOKABLE void placedAt(QRectF shown, QRectF source, QRectF surface);
	QRectF shownRect_, source_rect_, surfaceRect_;
	/* the render thread's: what it last said */
	QRectF saidShown_, saidSource_, saidSurface_;
	std::atomic<qint64> shown_{0};
	/* the render thread's copy of the newest frame */
	vm::Picture::Frame frame_;
	/* the steps the texture was grown by, and the largest side one may have
	 * (Chimera's is the GPU's largest texture; 8192 is had by any) */
	int stepX_ = 1, stepY_ = 1;
	static constexpr int kLargestSide = 8192;
};

} // namespace chimera::qt
