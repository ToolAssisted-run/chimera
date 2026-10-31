#include "chimera/vm/input_log.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>

#include "chimera/engine.h"

namespace chimera::vm {

std::string movieLineFromBar(const std::string &line)
{
	/* as Chimera reads a line: from its first '|', only
	 * whitespace and the digits 0-9 before it dropped (an editor's indent or
	 * tab, a copied frame number); anything else is left to be refused */
	if (line.empty() || line[0] == '|') return line;
	size_t i = 0;
	while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) i++;
	while (i < line.size() && line[i] >= '0' && line[i] <= '9') i++;
	while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) i++;
	return i < line.size() && line[i] == '|' ? line.substr(i) : line;
}


bool decodeEntry(const ce_session *s, const std::string &line, Buttons &buttons, int32_t *axes)
{
	std::vector<uint8_t> states(static_cast<size_t>(ce_session_button_count(s)));
	if (ce_session_movie_entry_decode(s, line.c_str(), states.data(), axes) != 0) return false;
	buttons = Buttons::fromStates(states.data(), states.size());
	return true;
}

/* The shared lock guards only which machine this is: the movie's own lock is
 * the engine's. */

int64_t InputLog::size() const
{
	std::shared_lock lock(m_);
	return s_ != nullptr ? ce_session_movie_length(s_) : 0;
}

Buttons InputLog::at(int64_t frame) const
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return {};
	std::vector<uint8_t> states(static_cast<size_t>(ce_session_button_count(s_)));
	ce_session_movie_get(s_, frame, states.data(), nullptr);
	return Buttons::fromStates(states.data(), states.size());
}

void InputLog::read(int64_t first, std::vector<Buttons> &out) const
{
	std::shared_lock lock(m_);
	std::vector<uint8_t> states(s_ != nullptr ? static_cast<size_t>(ce_session_button_count(s_)) : 0);
	for (size_t i = 0; i < out.size(); i++)
	{
		out[i].clear();
		if (s_ == nullptr) continue;
		ce_session_movie_get(s_, first + static_cast<int64_t>(i), states.data(), nullptr);
		out[i] = Buttons::fromStates(states.data(), states.size());
	}
}

bool InputLog::bound() const
{
	std::shared_lock lock(m_);
	return s_ != nullptr;
}

bool InputLog::set(int64_t frame, const Buttons &buttons)
{
	return setRow(frame, buttons, nullptr);
}

void InputLog::resize(int64_t frames)
{
	std::shared_lock lock(m_);
	if (s_ != nullptr) ce_session_movie_resize(s_, frames);
}

bool InputLog::setRow(int64_t frame, const Buttons &buttons, const int32_t *axes)
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return false;
	const std::vector<uint8_t> states = buttons.states(static_cast<size_t>(ce_session_button_count(s_)));
	if (axes != nullptr) return ce_session_movie_set(s_, frame, states.data(), axes) == 0;
	/* read, then written back with the half kept: one edit at a time, or
	 * another's between the two would be lost */
	std::lock_guard edit(edit_);
	std::vector<int32_t> was(static_cast<size_t>(ce_session_axis_count(s_)));
	ce_session_movie_get(s_, frame, nullptr, was.empty() ? nullptr : was.data());
	return ce_session_movie_set(s_, frame, states.data(), was.empty() ? nullptr : was.data()) == 0;
}

void InputLog::insert(int64_t frame, int64_t count)
{
	std::unique_lock lock(m_);
	if (s_ == nullptr || frame < 0 || count <= 0) return;
	ce_session_movie_insert(s_, frame, count);
	if (frame < static_cast<int64_t>(lags_.size())) lags_.insert(lags_.begin() + frame, static_cast<size_t>(count), kNotRun);
	if (frame < static_cast<int64_t>(lengths_.size())) lengths_.insert(lengths_.begin() + frame, static_cast<size_t>(count), 0);
}

void InputLog::remove(int64_t frame, int64_t count)
{
	std::unique_lock lock(m_);
	if (s_ == nullptr || frame < 0 || count <= 0) return;
	ce_session_movie_remove(s_, frame, count);
	if (frame < static_cast<int64_t>(lags_.size()))
		lags_.erase(lags_.begin() + frame, lags_.begin() + std::min<int64_t>(frame + count, static_cast<int64_t>(lags_.size())));
	if (frame < static_cast<int64_t>(lengths_.size()))
		lengths_.erase(lengths_.begin() + frame, lengths_.begin() + std::min<int64_t>(frame + count, static_cast<int64_t>(lengths_.size())));
}

void InputLog::neutralAxes(int32_t *out) const
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return;
	for (int64_t i = 0; i < ce_session_axis_count(s_); i++) out[i] = ce_session_axis_neutral(s_, i);
}

void InputLog::countRerecords(bool on)
{
	std::unique_lock lock(m_);
	strokeCounts_ = on;
	if (s_ != nullptr) ce_session_rerecord_counting(s_, strokeCounts_ && movieCounts_ ? 1 : 0);
}

void InputLog::setCountingRerecords(bool on)
{
	std::unique_lock lock(m_);
	movieCounts_ = on;
	if (s_ != nullptr) ce_session_rerecord_counting(s_, strokeCounts_ && movieCounts_ ? 1 : 0);
}

bool InputLog::countingRerecords() const
{
	std::shared_lock lock(m_);
	return movieCounts_;
}

size_t InputLog::axisCount() const
{
	std::shared_lock lock(m_);
	return s_ != nullptr ? static_cast<size_t>(ce_session_axis_count(s_)) : 0;
}

void InputLog::axesAt(int64_t frame, int32_t *out) const
{
	std::shared_lock lock(m_);
	if (s_ != nullptr) ce_session_movie_get(s_, frame, nullptr, out);
}

bool InputLog::setAxesAt(int64_t frame, const int32_t *values)
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return false;
	std::lock_guard edit(edit_);
	std::vector<uint8_t> states(static_cast<size_t>(ce_session_button_count(s_)));
	ce_session_movie_get(s_, frame, states.data(), nullptr);
	return ce_session_movie_set(s_, frame, states.data(), values) == 0;
}

std::string InputLog::text() const
{
	std::shared_lock lock(m_);
	if (s_ == nullptr) return {};
	uint64_t len = 0;
	const char *t = ce_session_movie_text(s_, &len);
	return std::string(t != nullptr ? t : "", static_cast<size_t>(len));
}

std::vector<std::string> InputLog::lines(int64_t first, int64_t last) const
{
	std::vector<std::string> out;
	const std::string all = text();
	ce_movie_log *log = ce_movie_log_new();
	if (ce_movie_log_parse(log, all.data(), all.size()) == 0)
		for (int64_t f = std::max<int64_t>(first, 0); f <= last; f++)
			if (const char *entry = ce_movie_log_entry(log, f)) out.emplace_back(entry);
	ce_movie_log_free(log);
	return out;
}

bool InputLog::ranAt(int64_t frame) const
{
	std::shared_lock lock(m_);
	return frame >= 0 && frame < static_cast<int64_t>(lags_.size()) && lags_[static_cast<size_t>(frame)] != kNotRun;
}

bool InputLog::setLag(int64_t frame, bool lag)
{
	if (frame < 0) return false;
	std::unique_lock lock(m_);
	if (frame >= static_cast<int64_t>(lags_.size())) lags_.resize(static_cast<size_t>(frame) + 1, kNotRun);
	const uint8_t now = lag ? 1 : 0;
	if (lags_[static_cast<size_t>(frame)] == now) return false;
	lags_[static_cast<size_t>(frame)] = now;
	return true;
}

bool InputLog::forgetLag(int64_t frame)
{
	std::unique_lock lock(m_);
	if (frame < 0 || frame >= static_cast<int64_t>(lags_.size()) || lags_[static_cast<size_t>(frame)] == kNotRun) return false;
	lags_[static_cast<size_t>(frame)] = kNotRun;
	return true;
}

bool InputLog::lagAt(int64_t frame) const
{
	std::shared_lock lock(m_);
	return frame >= 0 && frame < static_cast<int64_t>(lags_.size()) && lags_[static_cast<size_t>(frame)] == 1;
}

bool InputLog::setLength(int64_t frame, int64_t picoseconds)
{
	if (frame < 0) return false;
	std::unique_lock lock(m_);
	if (frame >= static_cast<int64_t>(lengths_.size())) lengths_.resize(static_cast<size_t>(frame) + 1, 0);
	if (lengths_[static_cast<size_t>(frame)] == picoseconds) return false;
	lengths_[static_cast<size_t>(frame)] = picoseconds;
	return true;
}

int64_t InputLog::lengthAt(int64_t frame) const
{
	std::shared_lock lock(m_);
	return frame >= 0 && frame < static_cast<int64_t>(lengths_.size()) ? lengths_[static_cast<size_t>(frame)] : 0;
}

void InputLog::forgetRuns()
{
	std::unique_lock lock(m_);
	lags_.clear();
	lengths_.clear();
}

void InputLog::bind(ce_session *s)
{
	std::unique_lock lock(m_);
	s_ = s;
	/* a new movie counts its rerecords, as each of Chimera's does */
	strokeCounts_ = movieCounts_ = true;
}

} // namespace chimera::vm
