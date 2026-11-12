/* Not a test: what the window's picture costs on a real screen and GPU
 * (issue 568). It does what the window does with each frame, step by step,
 * and times each step:
 *
 *   publish   the machine hands the frame over (a copy, every A set)
 *   take      the window's render thread copies the newest frame into the
 *             image it keeps (a new one when the last texture still holds it)
 *   prescale  grown by whole steps on the CPU (Config > Display's prescale)
 *   texture   a texture made from it (on Direct3D and Vulkan uploaded later, in the render pass)
 *   render    the render pass: the upload and the drawing, the wait for the screen not in it
 *
 * A thread hands frames over at 60 a second, as a machine does, while the
 * window draws in step with the screen. It prints each step's median and
 * 95th percentile in milliseconds, how many frames a second the window
 * drew, and how many of the machine's frames it showed.
 *
 *   picturebench <frame w> <frame h> <window w> <window h> [--prescale N] [--auto] [--smooth] [--seconds S] [--cpu]
 *
 * --cpu times the copies and the prescaling alone, 300 frames, with no window.
 * --compare N draws one frame in the window, grown first by N on the CPU and
 * not grown, and counts the pixels that differ. Sharp, none differ on
 * Direct3D 11 and OpenGL, so the window grows a sharp picture only on Qt's
 * software renderer, where they do.
 *
 * Built with the window (build_by_default false):
 *   meson compile -C <build> picturebench
 */
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "chimera/vm/display_vm.hpp"
#include "chimera/vm/picture.hpp"

namespace {

using chimera::vm::Picture;

struct Times
{
	std::vector<double> publish, take, prescale, texture, render;
};

class Item : public QQuickItem
{
public:
	Item(Picture &picture, chimera::vm::DisplayFit fit, bool smooth, Times &times)
		: picture_(picture), fit_(fit), smooth_(smooth), times_(times)
	{
		setFlag(ItemHasContents, true);
	}
	int shown = 0;
	/* a step forced, the fit's not asked (--compare) */
	int forced = 0;

protected:
	/* as PictureItem::updatePaintNode, timed */
	QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override
	{
		auto *node = static_cast<QSGSimpleTextureNode *>(old);
		QElapsedTimer t;
		t.start();
		const bool fresh = picture_.takeIfNewer(frame_.serial, frame_, [this](int32_t w, int32_t h) {
			if (image_.width() != w || image_.height() != h || !image_.isDetached())
			{
				image_ = QImage(w, h, QImage::Format_RGB32);
				made++;
			}
			return reinterpret_cast<uint32_t *>(image_.bits());
		});
		const double take = t.nsecsElapsed() / 1e6;
		if (frame_.width <= 0) return node;
		if (node == nullptr)
		{
			node = new QSGSimpleTextureNode;
			node->setOwnsTexture(true);
		}
		int stepX = 1, stepY = 1;
		fit_.prescaleSteps(frame_.width, frame_.height, frame_.shownWidth, frame_.shownHeight, width(), height(), smooth_, 8192, stepX, stepY);
		if (forced > 0) stepX = stepY = forced;
		if (fresh || node->texture() == nullptr || forced > 0)
		{
			times_.take.push_back(take);
			t.restart();
			QImage image = image_;
			if (stepX != 1 || stepY != 1) image = image.scaled(frame_.width * stepX, frame_.height * stepY, Qt::IgnoreAspectRatio, Qt::FastTransformation);
			times_.prescale.push_back(t.nsecsElapsed() / 1e6);
			t.restart();
			node->setTexture(window()->createTextureFromImage(image));
			times_.texture.push_back(t.nsecsElapsed() / 1e6);
			steps = std::to_string(stepX) + "x" + std::to_string(stepY);
			shown++;
		}
		node->setFiltering(smooth_ ? QSGTexture::Linear : QSGTexture::Nearest);
		const chimera::vm::DisplayFit::Rect from = fit_.source(frame_.width, frame_.height);
		node->setSourceRect(QRectF(from.x * stepX, from.y * stepY, from.w * stepX, from.h * stepY));
		const chimera::vm::DisplayFit::Rect to = fit_.place(frame_.width, frame_.height, frame_.shownWidth, frame_.shownHeight, width(), height());
		node->setRect(QRectF(to.x, to.y, to.w, to.h));
		return node;
	}

public:
	std::string steps = "1x1";
	/* images made: the frame copied into a new one, not the one kept */
	int made = 0;

private:
	Picture &picture_;
	chimera::vm::DisplayFit fit_;
	bool smooth_;
	Times &times_;
	Picture::Frame frame_;
	QImage image_;
};

const char *api(QSGRendererInterface::GraphicsApi a)
{
	switch (a)
	{
	case QSGRendererInterface::OpenGL: return "OpenGL";
	case QSGRendererInterface::Direct3D11: return "Direct3D 11";
	case QSGRendererInterface::Direct3D12: return "Direct3D 12";
	case QSGRendererInterface::Vulkan: return "Vulkan";
	case QSGRendererInterface::Metal: return "Metal";
	case QSGRendererInterface::Software: return "software";
	default: return "another API";
	}
}

void say(const char *name, std::vector<double> v)
{
	if (v.empty())
	{
		std::printf("  %-9s -\n", name);
		return;
	}
	std::sort(v.begin(), v.end());
	std::printf("  %-9s median %7.3f ms, 95%% %7.3f ms (%zu)\n", name, v[v.size() / 2], v[v.size() * 95 / 100], v.size());
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 5)
	{
		std::fprintf(stderr, "picturebench <frame w> <frame h> <window w> <window h> [--prescale N] [--auto] [--smooth] [--seconds S]\n");
		return 2;
	}
	const int fw = std::atoi(argv[1]), fh = std::atoi(argv[2]), ww = std::atoi(argv[3]), wh = std::atoi(argv[4]);
	chimera::vm::DisplayFit fit;
	bool smooth = false;
	double seconds = 10;
	for (int i = 5; i < argc; i++)
	{
		const std::string a = argv[i];
		if (a == "--prescale" && i + 1 < argc) fit.prescale = std::atoi(argv[++i]);
		else if (a == "--auto") fit.autoPrescale = true;
		else if (a == "--smooth") smooth = true;
		else if (a == "--seconds" && i + 1 < argc) seconds = std::atof(argv[++i]);
	}

	Picture picture;
	Times times;
	/* --cpu: the copies and the prescaling alone, 300 frames, no window (a locked screen draws none) */
	if (std::find_if(argv + 5, argv + argc, [](const char *a) { return std::string(a) == "--cpu"; }) != argv + argc)
	{
		std::vector<uint32_t> pixels(static_cast<size_t>(fw) * fh);
		Picture::Frame frame;
		QImage kept;
		int stepX = 1, stepY = 1;
		fit.prescaleSteps(fw, fh, 0, 0, ww, wh, smooth, 8192, stepX, stepY);
		for (uint32_t n = 0; n < 300; n++)
		{
			for (size_t i = 0; i < pixels.size(); i += 64) pixels[i] = n * 2654435761u + static_cast<uint32_t>(i);
			QElapsedTimer t;
			t.start();
			picture.publish(pixels.data(), fw, fh, 0, 0);
			times.publish.push_back(t.nsecsElapsed() / 1e6);
			t.restart();
			picture.takeIfNewer(frame.serial, frame, [&](int32_t w, int32_t h) {
				if (kept.width() != w || kept.height() != h || !kept.isDetached()) kept = QImage(w, h, QImage::Format_RGB32);
				return reinterpret_cast<uint32_t *>(kept.bits());
			});
			times.take.push_back(t.nsecsElapsed() / 1e6);
			t.restart();
			QImage image = kept;
			if (stepX != 1 || stepY != 1) image = image.scaled(fw * stepX, fh * stepY, Qt::IgnoreAspectRatio, Qt::FastTransformation);
			times.prescale.push_back(t.nsecsElapsed() / 1e6);
		}
		std::printf("frame %dx%d for a window of %dx%d, prescale %dx%d (%s), %s; the CPU's steps alone\n", fw, fh, ww, wh, stepX, stepY,
			fit.autoPrescale ? "auto" : std::to_string(fit.prescale).c_str(), smooth ? "smoothed" : "sharp");
		say("publish", times.publish);
		say("take", times.take);
		say("prescale", times.prescale);
		return 0;
	}
	int compare = 0;
	for (int i = 5; i + 1 < argc; i++)
		if (std::string(argv[i]) == "--compare") compare = std::atoi(argv[i + 1]);
	if (compare > 0)
	{
		/* a frame of pixels each unlike its neighbours, drawn grown by N and not, the window's own pixels compared */
		std::vector<uint32_t> pixels(static_cast<size_t>(fw) * fh);
		for (size_t i = 0; i < pixels.size(); i++) pixels[i] = 0xff000000u | (static_cast<uint32_t>(i) * 2654435761u >> 8);
		picture.publish(pixels.data(), fw, fh, 0, 0);
		QQuickWindow window;
		window.resize(ww, wh);
		Item item(picture, fit, smooth, times);
		item.setParentItem(window.contentItem());
		item.setSize(QSizeF(ww, wh));
		window.show();
		QImage grown, plain;
		item.forced = compare;
		item.update();
		for (int i = 0; i < 20; i++) QCoreApplication::processEvents(), QThread::msleep(10);
		grown = window.grabWindow();
		item.forced = 1;
		item.update();
		for (int i = 0; i < 20; i++) QCoreApplication::processEvents(), QThread::msleep(10);
		plain = window.grabWindow();
		int64_t differ = 0;
		const bool sized = grown.size() == plain.size() && !grown.isNull();
		if (sized)
			for (int y = 0; y < grown.height(); y++)
				for (int x = 0; x < grown.width(); x++)
					differ += (grown.pixel(x, y) & 0xffffff) != (plain.pixel(x, y) & 0xffffff);
		std::printf("frame %dx%d in a window of %dx%d (%dx%d grabbed), %s, %s: grown by %d and not differ in %lld pixels%s\n", fw, fh, ww, wh,
			plain.width(), plain.height(), smooth ? "smoothed" : "sharp", api(window.rendererInterface()->graphicsApi()), compare,
			static_cast<long long>(differ), sized ? "" : " (the grabs are not the same size)");
		return differ == 0 && sized ? 0 : 1;
	}
	QQuickWindow window;
	window.resize(ww, wh);
	Item item(picture, fit, smooth, times);
	item.setParentItem(window.contentItem());
	item.setSize(QSizeF(ww, wh));

	/* the machine: a new frame every 1/60 s, its pixels changed */
	std::atomic<bool> stop{false};
	std::vector<double> publish;
	std::thread machine([&] {
		std::vector<uint32_t> pixels(static_cast<size_t>(fw) * fh);
		auto due = std::chrono::steady_clock::now();
		for (uint32_t n = 0; !stop; n++)
		{
			for (size_t i = 0; i < pixels.size(); i += 64) pixels[i] = n * 2654435761u + static_cast<uint32_t>(i);
			QElapsedTimer t;
			t.start();
			picture.publish(pixels.data(), fw, fh, 0, 0);
			publish.push_back(t.nsecsElapsed() / 1e6);
			due += std::chrono::microseconds(16667);
			std::this_thread::sleep_until(due);
		}
	});

	/* the window drawn again at every swap, in step with the screen */
	int swaps = 0;
	QElapsedTimer clock;
	QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
		if (swaps++ == 0) clock.start();
		item.update();
		window.update();
	}, Qt::QueuedConnection);
	/* the render pass on the render thread, where the texture is uploaded and drawn (the wait for the screen not in it) */
	QElapsedTimer pass;
	QObject::connect(&window, &QQuickWindow::beforeRendering, &window, [&] { pass.start(); }, Qt::DirectConnection);
	QObject::connect(&window, &QQuickWindow::afterRendering, &window, [&] {
		if (pass.isValid()) times.render.push_back(pass.nsecsElapsed() / 1e6);
	}, Qt::DirectConnection);
	QTimer::singleShot(static_cast<int>(seconds * 1000), &app, [&] { app.quit(); });
	window.show();
	item.update();
	app.exec();
	const double elapsed = clock.isValid() ? clock.nsecsElapsed() / 1e9 : seconds;
	stop = true;
	machine.join();
	times.publish = publish;

	std::printf("frame %dx%d in a window of %dx%d, prescale %s (%s), %s; %s\n", fw, fh, ww, wh, item.steps.c_str(),
		fit.autoPrescale ? "auto" : std::to_string(fit.prescale).c_str(), smooth ? "smoothed" : "sharp",
		api(window.rendererInterface()->graphicsApi()));
	say("publish", times.publish);
	say("take", times.take);
	say("prescale", times.prescale);
	say("texture", times.texture);
	say("render", times.render);
	std::printf("  the window drew %.1f frames a second and showed %.1f of the machine's 60, %d of them in a new image\n", swaps / elapsed,
		item.shown / elapsed, item.made);
	return 0;
}
