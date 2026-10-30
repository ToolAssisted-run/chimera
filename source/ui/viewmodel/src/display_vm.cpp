#include "chimera/vm/display_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/vm/json.hpp"

#include <algorithm>
#include <cmath>

namespace chimera::vm {

DisplayFit::Rect DisplayFit::source(int width, int height) const
{
	Rect r;
	const int l = std::max(0, cropLeft), t = std::max(0, cropTop), rr = std::max(0, cropRight), b = std::max(0, cropBottom);
	/* a crop that would leave nothing leaves the picture whole */
	if (l + rr >= width || t + b >= height) return {0, 0, static_cast<double>(width), static_cast<double>(height)};
	r.x = l;
	r.y = t;
	r.w = width - l - rr;
	r.h = height - t - b;
	return r;
}

void DisplayFit::naturalSize(int width, int height, double shownWidth, double shownHeight, double &w, double &h) const
{
	const Rect s = source(width, height);
	if (aspectMode == 2 && customWidth > 0 && customHeight > 0)
	{
		/* the person's size for the whole picture; a crop takes its share off */
		w = customWidth * s.w / width;
		h = customHeight * s.h / height;
		return;
	}
	if (aspectMode == 1 && shownWidth > 0 && shownHeight > 0)
	{
		/* the size the machine says, as Chimera's: a 640x400 picture declared
		 * 640x480 is 640x480, not 533x400 (issue 414); a crop takes its share off */
		w = shownWidth * s.w / width;
		h = shownHeight * s.h / height;
		return;
	}
	double shape = static_cast<double>(width) / height;
	if (aspectMode == 3 && ratioX > 0 && ratioY > 0) shape = ratioX / ratioY;
	/* the whole picture's shape, then the crop's share of each side */
	h = s.h;
	w = height * shape * s.w / width;
}

void DisplayFit::surfaceSize(int width, int height, double shownWidth, double shownHeight, double &w, double &h) const
{
	naturalSize(width, height, shownWidth, shownHeight, w, h);
	const Rect s = source(width, height);
	if (s.w <= 0 || s.h <= 0) return;
	/* the padding in the picture's own pixels, each as wide as one shown */
	w += std::max(0, padLeft + padRight) * w / s.w;
	h += std::max(0, padTop + padBottom) * h / s.h;
}

DisplayFit::Rect DisplayFit::surface(int width, int height, double shownWidth, double shownHeight, double areaW, double areaH) const
{
	Rect r;
	if (width <= 0 || height <= 0) return r;
	/* the client padding is the window's own: what is left is the area */
	const double cl = std::max(0, clientLeft), ct = std::max(0, clientTop);
	areaW -= cl + std::max(0, clientRight);
	areaH -= ct + std::max(0, clientBottom);
	if (areaW <= 0 || areaH <= 0) return r;
	double nw = 0, nh = 0;
	surfaceSize(width, height, shownWidth, shownHeight, nw, nh);
	if (nw <= 0 || nh <= 0) return r;
	if (!letterbox)
	{
		/* stretched: whole steps of each side when asked, else the area */
		r.w = integerScale && areaW >= nw ? std::floor(areaW / nw) * nw : areaW;
		r.h = integerScale && areaH >= nh ? std::floor(areaH / nh) * nh : areaH;
	}
	else
	{
		double scale = std::min(areaW / nw, areaH / nh);
		/* whole steps only, once there is room for one */
		if (integerScale && scale >= 1) scale = std::floor(scale);
		r.w = nw * scale;
		r.h = nh * scale;
	}
	r.x = cl + (areaW - r.w) / 2;
	r.y = ct + (areaH - r.h) / 2;
	return r;
}

DisplayFit::Rect DisplayFit::place(int width, int height, double shownWidth, double shownHeight, double areaW, double areaH) const
{
	const Rect all = surface(width, height, shownWidth, shownHeight, areaW, areaH);
	const Rect s = source(width, height);
	if (all.w <= 0 || all.h <= 0 || s.w <= 0 || s.h <= 0) return all;
	/* the picture inside its padding: the surface's pixels are the picture's */
	const double across = s.w + std::max(0, padLeft + padRight), down = s.h + std::max(0, padTop + padBottom);
	Rect r;
	r.w = all.w * s.w / across;
	r.h = all.h * s.h / down;
	r.x = all.x + all.w * std::max(0, padLeft) / across;
	r.y = all.y + all.h * std::max(0, padTop) / down;
	return r;
}

void DisplayFit::prescaleSteps(int width, int height, double shownWidth, double shownHeight, double areaW, double areaH, bool smooth, int maxSide,
	int &stepX, int &stepY) const
{
	stepX = stepY = 1;
	if (width <= 0 || height <= 0) return;
	/* the person's step, taken back while a side would pass the largest */
	int user = std::clamp(prescale, 1, 16);
	while (user > 1 && (width * user > maxSide || height * user > maxSide)) user--;
	stepX = stepY = user;
	/* growing to fit makes no sense without smoothing, as Chimera says */
	if (!autoPrescale || !smooth) return;
	const Rect from = source(width, height), to = place(width, height, shownWidth, shownHeight, areaW, areaH);
	if (from.w <= 0 || from.h <= 0) return;
	/* the whole steps that fit where it is drawn, each side its own, at least one */
	int x = std::max(1, static_cast<int>(to.w / (from.w * user) + 1e-9)), y = std::max(1, static_cast<int>(to.h / (from.h * user) + 1e-9));
	while (x > 1 && width * user * x > maxSide) x--;
	while (y > 1 && height * user * y > maxSide) y--;
	stepX = user * x;
	stepY = user * y;
}

DisplayViewModel::DisplayViewModel(Dispatcher &dispatcher) : ViewModel("display", dispatcher)
{
	expose("aspectMode", "the picture's shape: 0 its raw pixels, 1 the machine's own, 2 a size (customWidth x "
		"customHeight), 3 a ratio (ratioX:ratioY)", aspectMode, true);
	expose("customWidth", "the size the picture is shown as, across", customWidth, true);
	expose("customHeight", "the size the picture is shown as, down", customHeight, true);
	expose("ratioX", "the ratio the picture is shown in, across", ratioX, true);
	expose("ratioY", "the ratio the picture is shown in, down", ratioY, true);
	expose("letterbox", "the picture kept to its shape with bars; else stretched to the window", letterbox, true);
	expose("integerScale", "the picture grown by whole steps of its size only", integerScale, true);
	expose("cropLeft", "pixels cut off the picture's left edge", cropLeft, true);
	expose("cropTop", "pixels cut off the picture's top edge", cropTop, true);
	expose("cropRight", "pixels cut off the picture's right edge", cropRight, true);
	expose("cropBottom", "pixels cut off the picture's bottom edge", cropBottom, true);
	expose("finalFilter", "the picture drawn sharp (0) or smoothed (1)", finalFilter, true);
	expose("prescale", "Chimera's User Prescale: the picture grown by this whole step (1 to 16), pixel by pixel, before it "
		"is drawn", prescale, true);
	expose("autoPrescale", "Chimera's Auto Prescale: with smoothing, the picture first grown by the largest whole steps "
		"that fit where it is drawn", autoPrescale, true);
	expose("fullscreenHacks", "Chimera's Windows Fullscreen Hacks: a border kept round the window in full screen, so that "
		"Windows does not take the screen for the window alone (some cards then flicker other monitors); Windows only",
		fullscreenHacks, true);
	expose("scaleOsd", "Chimera's Scale OSD text with system scale: off, what is drawn over the picture keeps its size at "
		"100% on a scaled screen", scaleOsd, true);
	expose("osdFollowsWindow", "what is drawn over the picture grows with the window: its 12 pixels are a 480-pixel-tall "
		"picture area's, never smaller (issue 401)", osdFollowsWindow, true);
	for (auto &[name, p] : std::initializer_list<std::pair<const char *, Property<int64_t> *>>{{"padLeft", &padLeft}, {"padTop", &padTop},
			 {"padRight", &padRight}, {"padBottom", &padBottom}, {"clientLeft", &clientLeft}, {"clientTop", &clientTop},
			 {"clientRight", &clientRight}, {"clientBottom", &clientBottom}})
		expose(name, "an extra padding a script set: the game's in its pixels, the client's in the window's", *p);
	command("setPadding", "the picture's padding, the game's (\"game\") or the window's (\"client\"): the sides' "
		"padding, left, top, right and bottom", {"which", "left", "top", "right", "bottom"},
		[this](const Args &a, std::string &error) -> Value {
			std::string which;
			int64_t l = 0, t = 0, r = 0, b = 0;
			if (!arg(a, 0, which, error) || !arg(a, 1, l, error) || !arg(a, 2, t, error) || !arg(a, 3, r, error) || !arg(a, 4, b, error)) return {};
			if (which != "game" && which != "client")
			{
				error = tr("display.padding_values");
				return {};
			}
			const bool game = which == "game";
			(game ? padLeft : clientLeft).set(l);
			(game ? padTop : clientTop).set(t);
			(game ? padRight : clientRight).set(r);
			(game ? padBottom : clientBottom).set(b);
			return {};
		});
	expose("placement", "where the window shows the picture, told by the window: the area's width and height, the "
		"picture's x, y, width and height in it, and the part of the machine's picture shown (x, y, width, height)", placement, true);
	expose("saveWindowPosition", "the window's place and size kept for the next start", saveWindowPosition, true);
	expose("allowDoubleClickFullscreen", "a double click on the picture goes full screen and back", allowDoubleClickFullscreen, true);
	expose("menuWindowed", "the menu bar shown in a window", menuWindowed, true);
	expose("captionWindowed", "the title bar's buttons shown in a window", captionWindowed, true);
	expose("frameWindowed", "the window's frame: 0 none, 1 thin, 2 thick", frameWindowed, true);
	expose("menuFullscreen", "the menu bar shown full screen", menuFullscreen, true);
	expose("statusBarFullscreen", "the status bar shown full screen", statusBarFullscreen, true);
	expose("autohideMouseFullscreen", "full screen, the pointer hidden while it is still", autohideMouseFullscreen, true);
	expose("stayOnTop", "the main window kept over others", stayOnTop, true);
	expose("mouseCaptureForcesTop", "the main window kept over others while the pointer is captured (Chimera's Mouse "
		"Capture Forces Stay on Top)", mouseCaptureForcesTop, true);
	expose("minimalDisplay", "Display Features' Minimal picked (drawn as Full, as Chimera's)", minimalDisplay, true);
	expose("staticWindowTitles", "the main window's title keeps to its last words", staticWindowTitles, true);
	expose("titleSuffix", "the words every main window title ends with (\"\" for \"Chimera\")", titleSuffix, true);
	expose("windowGeometry", "the window's place and size kept, \"x,y,w,h\"", windowGeometry, true);
	expose("resizeWithPicture", "the window follows the picture's size (View > Window Size > Static Size off)", resizeWithPicture, true);
	expose("windowScales", "each system's window scale, {\"NES\": 3} (2 when a system has none)", windowScales, true);
	command("scaleFor", "a system's window scale: the picture's size times it (2 at first, as Chimera's)", {"system"},
		[this](const Args &a, std::string &error) -> Value {
			std::string system;
			if (!arg(a, 0, system, error)) return {};
			return scaleFor(system);
		});
	command("setScaleFor", "a system's window scale, 1 to 10, kept", {"system", "scale"}, [this](const Args &a, std::string &error) -> Value {
		std::string system;
		int64_t scale = 0;
		if (!arg(a, 0, system, error) || !arg(a, 1, scale, error)) return {};
		json::Json all;
		std::string why;
		if (!json::parse(windowScales.get(), all, why) || all.kind != json::Json::Kind::Object)
		{
			all = json::Json();
			all.kind = json::Json::Kind::Object;
		}
		json::Json n = json::Json::number(static_cast<double>(std::clamp<int64_t>(scale, 1, 10)));
		bool found = false;
		for (auto &[k, v] : all.members)
			if (k == system)
			{
				v = n;
				found = true;
			}
		if (!found) all.members.emplace_back(system, n);
		windowScales.set(json::write(all));
		return scaleFor(system);
	});
}

int64_t DisplayViewModel::scaleFor(const std::string &system) const
{
	json::Json all;
	std::string why;
	if (json::parse(windowScales.get(), all, why))
		if (const json::Json *v = all.find(system); v != nullptr && v->kind == json::Json::Kind::Number)
			return std::clamp<int64_t>(static_cast<int64_t>(v->number()), 1, 10);
	return 2;
}

DisplayFit DisplayViewModel::fit() const
{
	DisplayFit f;
	f.aspectMode = static_cast<int>(std::clamp<int64_t>(aspectMode.get(), 0, 3));
	f.customWidth = static_cast<int>(std::max<int64_t>(0, customWidth.get()));
	f.customHeight = static_cast<int>(std::max<int64_t>(0, customHeight.get()));
	f.ratioX = ratioX.get();
	f.ratioY = ratioY.get();
	f.letterbox = letterbox.get();
	f.integerScale = integerScale.get();
	f.cropLeft = static_cast<int>(cropLeft.get());
	f.cropTop = static_cast<int>(cropTop.get());
	f.cropRight = static_cast<int>(cropRight.get());
	f.cropBottom = static_cast<int>(cropBottom.get());
	f.padLeft = static_cast<int>(padLeft.get());
	f.padTop = static_cast<int>(padTop.get());
	f.padRight = static_cast<int>(padRight.get());
	f.padBottom = static_cast<int>(padBottom.get());
	f.clientLeft = static_cast<int>(clientLeft.get());
	f.clientTop = static_cast<int>(clientTop.get());
	f.clientRight = static_cast<int>(clientRight.get());
	f.clientBottom = static_cast<int>(clientBottom.get());
	return f;
}

} // namespace chimera::vm
