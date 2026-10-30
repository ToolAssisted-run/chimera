#include "chimera/vm/idle_eye.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace chimera::vm {

namespace {
/* how bright the mark gets at its brightest pixel, out of 255 */
constexpr float kOpacity = 0.10f;
/* the mark's height as a fraction of the screen */
constexpr float kMarkScale = 0.62f;
/* the eye within the mark, in the art: the central oval and its pupil, the
 * part that moves; the four slabs around it stay */
constexpr float kEyeLeft = 62.0f / 160.0f, kEyeRight = 98.0f / 160.0f;
constexpr float kEyeTop = 41.0f / 160.0f, kEyeBottom = 118.0f / 160.0f;
/* below this the art is pupil rather than iris */
constexpr float kPupilDarkerThan = 0.35f;
/* how far the oval may slide, in the art's pixels */
constexpr float kMaxGaze = 12.0f;
constexpr double kPi = 3.14159265358979323846;

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace

IdleEyeViewModel::IdleEyeViewModel(Dispatcher &dispatcher) : ViewModel("idleEye", dispatcher)
{
	expose("drawn", "pictures made so far (a still eye makes none)", drawn);
	picture("eye", "the faint Chimera mark shown while nothing is open", eye, eyeChanged);
	expose("snowWhen", "Chimera's Snowy null core: snow while nothing is open 0 never, 1 always, 2 the week of Christmas "
		"(December 20th to 26th)", snowWhen, true);
	expose("snowIntensity", "the snow's brightness, 0.1 to 1", snowIntensity, true);
	expose("snowBias", "the snow's bias, -2 to 2: each grey a random byte raised to 10^-bias", snowBias, true);
	expose("snowFramerate", "the snow is new 60 / this times a second (1 to 20)", snowFramerate, true);
	dayOfYear = [] {
		const std::time_t now = std::time(nullptr);
		std::tm local{};
#ifdef _WIN32
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		return local.tm_yday + 1;
	};
	command("tick", "a sixtieth of a second on; gaze is the pointer's place, -1 hard left to +1 hard right",
		{"gaze"}, [this](const Args &a, std::string &error) -> Value {
			double g = 0;
			if (!arg(a, 0, g, error)) return {};
			return tick(g);
		});
}

void IdleEyeViewModel::setMark(int32_t size, const std::vector<uint32_t> &argb)
{
	if (size <= 0 || argb.size() < static_cast<size_t>(size) * size) return;
	srcSize_ = size;
	still_.assign(static_cast<size_t>(size) * size, 0.0f);
	eye_.assign(static_cast<size_t>(size) * size, 0.0f);
	for (int32_t y = 0; y < size; y++)
	{
		const float v = y / static_cast<float>(size);
		for (int32_t x = 0; x < size; x++)
		{
			const uint32_t p = argb[static_cast<size_t>(y) * size + x];
			const float a = (p >> 24) / 255.0f, r = ((p >> 16) & 0xFF) / 255.0f, g = ((p >> 8) & 0xFF) / 255.0f, b = (p & 0xFF) / 255.0f;
			if (a <= 0.0f) continue;
			/* some of the art's own shading kept, so the blocks do not flatten into one silhouette */
			const float lum = (r + g + b) / 3.0f;
			const float shaded = a * (0.40f + 0.45f * lum);
			const float u = x / static_cast<float>(size);
			const size_t at = static_cast<size_t>(y) * size + x;
			if (u >= kEyeLeft && u <= kEyeRight && v >= kEyeTop && v <= kEyeBottom)
				/* the pupil is the darkest part of the art, and dark cannot be seen
				 * against black: here it is the brightest, so the oval does not
				 * slide about as a hollow ring */
				eye_[at] = lum < kPupilDarkerThan ? a * 0.8f : shaded;
			else
				still_[at] = shaded;
		}
	}
	drawnKey_ = -1;
}

bool IdleEyeViewModel::snowing() const
{
	if (snowWhen.get() == 1) return true;
	if (snowWhen.get() != 2 || !dayOfYear) return false;
	/* Chimera's days, December 20th to 26th in a year of 365 */
	const int day = dayOfYear();
	return day >= 354 && day <= 360;
}

/* the snowy picture with no machine: a random byte each pixel, raised and brightened, grey */
void IdleEyeViewModel::snow()
{
	const float power = std::pow(10.0f, static_cast<float>(-std::clamp(snowBias.get(), -2.0, 2.0)));
	const float intensity = static_cast<float>(std::clamp(snowIntensity.get(), 0.1, 1.0));
	std::uniform_int_distribution<int> byte(0, 255);
	for (uint32_t &px : snow_)
	{
		const float f = std::pow(byte(snowRng_) / 255.0f, power) * intensity;
		const uint32_t g = static_cast<uint32_t>(static_cast<uint8_t>(f * 255.0f));
		px = 0xFF000000u | g << 16 | g << 8 | g;
	}
}

bool IdleEyeViewModel::tick(double gaze)
{
	if (snowing())
	{
		/* new snow every snowFramerate turns, the first at once */
		const int64_t every = std::clamp<int64_t>(snowFramerate.get(), 1, 20);
		const bool now = !wasSnowing_ || snowTurn_ % every == 0;
		wasSnowing_ = true;
		snowTurn_++;
		if (!now) return false;
		snow();
		eye.publish(snow_.data(), kSnowWidth, kSnowHeight, kSnowWidth, kSnowHeight);
		drawn.set(drawn.get() + 1);
		eyeChanged.emit();
		return true;
	}
	/* the snow over: the eye drawn again */
	if (wasSnowing_)
	{
		wasSnowing_ = false;
		snowTurn_ = 0;
		drawnKey_ = -1;
	}
	gazeTarget_ = static_cast<float>(std::clamp(gaze, -1.0, 1.0));
	animate();
	/* a still eye is the same picture: drawn again only when it would change */
	const int key = (static_cast<int>(lid_ * 255.0f) << 16) | ((static_cast<int>((gaze_ + 1.0f) * 127.0f) & 0xFF) << 8) | breath();
	if (key == drawnKey_ || srcSize_ == 0) return false;
	drawnKey_ = key;
	draw();
	eye.publish(buffer_.data(), kWidth, kHeight, kWidth, kHeight);
	drawn.set(drawn.get() + 1);
	eyeChanged.emit();
	return true;
}

/* a slow rise and fall in brightness, so a still eye is not a dead one: 0..15 */
int IdleEyeViewModel::breath() const
{
	return static_cast<int>(7.5 * (1.0 + std::sin(frame_ * 2.0 * kPi / 420.0)));
}

void IdleEyeViewModel::animate()
{
	frame_++;
	/* the eye eases to where it was pointed, so a glance lands; settled, it snaps */
	const float d = gazeTarget_ - gaze_;
	gaze_ = std::fabs(d) < 0.004f ? gazeTarget_ : gaze_ + d * 0.18f;
	switch (doing_)
	{
	case Act::Waiting:
		if (--wait_ > 0) break;
		begin(Act::Closing, next(3, 6));
		break;
	case Act::Closing:
		lid_ = 1.0f - ease();
		if (done()) begin(Act::Opening, next(4, 8));
		break;
	case Act::Opening:
		lid_ = ease();
		if (done())
		{
			lid_ = 1.0f;
			/* blinks sometimes come in pairs, the way real ones do */
			if (next(0, 100) < 22) begin(Act::Closing, next(3, 6));
			else wait();
		}
		break;
	}
}

void IdleEyeViewModel::begin(Act act, int steps)
{
	doing_ = act;
	step_ = 0;
	stepsTotal_ = std::max(1, steps);
}

void IdleEyeViewModel::wait()
{
	doing_ = Act::Waiting;
	wait_ = next(90, 320);
}

float IdleEyeViewModel::ease() const
{
	return smooth(std::min(1.0f, static_cast<float>(step_) / static_cast<float>(stepsTotal_)));
}

void IdleEyeViewModel::draw()
{
	std::fill(buffer_.begin(), buffer_.end(), 0u);
	if (lid_ <= 0.01f) return;
	const float mark = kHeight * kMarkScale;
	const float left = (kWidth - mark) / 2.0f, top = (kHeight - mark) / 2.0f;
	const float midline = top + mark / 2.0f;
	const float scale = srcSize_ / mark;
	const float gaze = gaze_ * kMaxGaze;
	const float bright = 255.0f * kOpacity * (0.85f + breath() / 100.0f);
	/* the lid squashes the mark towards its midline, so more of the art falls
	 * into each row the further it closes: averaged, or a closing eye crawls */
	const int rows = std::min(8, std::max(1, static_cast<int>(std::ceil(1.0f / lid_))));
	const int y0 = std::max(0, static_cast<int>(midline - mark * lid_ / 2.0f) - 1);
	const int y1 = std::min(kHeight - 1, static_cast<int>(midline + mark * lid_ / 2.0f) + 1);
	const int x0 = std::max(0, static_cast<int>(left) - 1);
	const int x1 = std::min(kWidth - 1, static_cast<int>(left + mark) + 1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			const float sx = (x + 0.5f - left) * scale;
			float value = 0.0f;
			for (int s = 0; s < rows; s++)
			{
				const float dy = y + (s + 0.5f) / rows;
				const float sy = ((dy - midline) / lid_ + mark / 2.0f) * scale;
				value += sample(still_, sx, sy) + sample(eye_, sx - gaze, sy);
			}
			value /= rows;
			if (value <= 0.004f) continue;
			const uint32_t level = static_cast<uint32_t>(std::min(255, static_cast<int>(value * bright)));
			buffer_[static_cast<size_t>(y) * kWidth + x] = 0xFF000000u | (level << 16) | (level << 8) | level;
		}
}

/* a bilinear read of the art, in its pixels; outside it is nothing */
float IdleEyeViewModel::sample(const std::vector<float> &mask, float x, float y) const
{
	x -= 0.5f;
	y -= 0.5f;
	const int xi = static_cast<int>(std::floor(x)), yi = static_cast<int>(std::floor(y));
	if (xi < -1 || yi < -1 || xi >= srcSize_ || yi >= srcSize_) return 0.0f;
	const float fx = x - xi, fy = y - yi;
	auto at = [&](int px, int py) {
		return px < 0 || py < 0 || px >= srcSize_ || py >= srcSize_ ? 0.0f : mask[static_cast<size_t>(py) * srcSize_ + px];
	};
	return at(xi, yi) * (1 - fx) * (1 - fy) + at(xi + 1, yi) * fx * (1 - fy) + at(xi, yi + 1) * (1 - fx) * fy + at(xi + 1, yi + 1) * fx * fy;
}

} // namespace chimera::vm
