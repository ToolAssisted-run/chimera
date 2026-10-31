#include "chimera/vm/messages_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {
using json::Json;

const char *const kNames[] = {"Fps", "Frame Counter", "Lag Counter", "Input Display", "Watches", "Messages", "Rerecords",
	"Autohold", "Game Time", "Real Time"};
/* Chimera's default message positions; Real Time under Game Time */
const int kDefaults[][3] = {{0, 0, 0}, {0, 14, 0}, {0, 42, 0}, {0, 28, 0}, {0, 70, 0}, {0, 0, 2}, {0, 56, 0}, {0, 0, 1},
	{0, 14, 1}, {0, 28, 1}};
static_assert(std::size(kNames) == std::size(kDefaults));

Json place(int64_t x, int64_t y, int64_t anchor)
{
	Json p;
	p.kind = Json::Kind::Object;
	p.members.emplace_back("x", Json::integer(x));
	p.members.emplace_back("y", Json::integer(y));
	p.members.emplace_back("anchor", Json::integer(anchor));
	return p;
}

bool isColor(const std::string &s)
{
	return s.size() == 9 && s[0] == '#' &&
		std::all_of(s.begin() + 1, s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}
} // namespace

std::string MessagesViewModel::defaultPositions()
{
	Json all;
	all.kind = Json::Kind::Object;
	for (size_t i = 0; i < std::size(kNames); i++) all.members.emplace_back(kNames[i], place(kDefaults[i][0], kDefaults[i][1], kDefaults[i][2]));
	return json::write(all);
}

MessagesViewModel::MessagesViewModel(Dispatcher &dispatcher) : ViewModel("messages", dispatcher)
{
	positions.set(defaultPositions());
	expose("positions", "where each thing drawn over the picture goes: {\"Fps\": {\"x\", \"y\", \"anchor\"}, ...} (anchor 0 "
		"top left, 1 top right, 2 bottom left, 3 bottom right; x and y in pixels from that corner)", positions, true);
	expose("messagesColor", "the messages' colour, #AARRGGBB (Chimera's default is the theme's)", messagesColor, true);
	expose("alertColor", "the alerts' colour (a lag frame's number, the lag counter), #AARRGGBB", alertColor, true);
	expose("lastInputColor", "the previous frame's input, #AARRGGBB", lastInputColor, true);
	expose("movieInputColor", "the movie's input, #AARRGGBB", movieInputColor, true);
	expose("messageSeconds", "how long a message stays over the picture, 1 to 10 seconds", messageSeconds, true);
	expose("stackMessages", "messages stack, the newest at their corner; else only the newest is shown", stackMessages, true);
	command("setPosition", "where one thing goes: its name (Fps, Frame Counter, Lag Counter, Input Display, Watches, "
		"Messages, Rerecords, Autohold, Game Time, Real Time), x and y in pixels from its corner, and the corner (0 top left, 1 top "
		"right, 2 bottom left, 3 bottom right)", {"name", "x", "y", "anchor"}, [this](const Args &a, std::string &error) -> Value {
			std::string name;
			int64_t x = 0, y = 0, anchor = 0;
			if (!arg(a, 0, name, error) || !arg(a, 1, x, error) || !arg(a, 2, y, error) || !arg(a, 3, anchor, error)) return {};
			if (std::find(std::begin(kNames), std::end(kNames), name) == std::end(kNames))
			{
				error = tr("messages.no_element", {name});
				return {};
			}
			if (x < 0 || y < 0 || anchor < 0 || anchor > 3)
			{
				error = tr("messages.place_values");
				return {};
			}
			Json all;
			std::string err;
			if (!json::parse(positions.get(), all, err) || all.kind != Json::Kind::Object) json::parse(defaultPositions(), all, err);
			bool found = false;
			for (auto &[k, v] : all.members)
				if (k == name)
				{
					v = place(x, y, anchor);
					found = true;
				}
			if (!found) all.members.emplace_back(name, place(x, y, anchor));
			positions.set(json::write(all));
			return {};
		});
	command("setColor", "one of the four colours (messages, alert, lastInput, movieInput), #AARRGGBB or #RRGGBB",
		{"which", "color"}, [this](const Args &a, std::string &error) -> Value {
			std::string which, color;
			if (!arg(a, 0, which, error) || !arg(a, 1, color, error)) return {};
			if (color.size() == 7 && color[0] == '#') color.insert(1, "FF");
			for (char &c : color) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			if (!isColor(color))
			{
				error = tr("messages.not_colour", {color});
				return {};
			}
			Property<std::string> *p = which == "messages" ? &messagesColor : which == "alert" ? &alertColor
				: which == "lastInput"																	 ? &lastInputColor
				: which == "movieInput"																	 ? &movieInputColor
																										 : nullptr;
			if (p == nullptr)
			{
				error = tr("messages.no_colour", {which});
				return {};
			}
			p->set(color);
			return {};
		});
	command("resetDefaults", "Chimera's places, colours (the theme's), two seconds and stacking", {}, [this](const Args &, std::string &) -> Value {
		positions.set(defaultPositions());
		messagesColor.set("#FFFFFFFF");
		alertColor.set("#FFFF0000");
		lastInputColor.set("#FFFFA500");
		movieInputColor.set("#FF808080");
		messageSeconds.set(2);
		stackMessages.set(true);
		return {};
	});
	secondsWatch_ = messageSeconds.changed.connect([this] {
		if (messageSeconds.get() < 1 || messageSeconds.get() > 10) messageSeconds.set(std::clamp<int64_t>(messageSeconds.get(), 1, 10));
	});
	/* a remembered value from before a place was added keeps the rest, and gains the new one */
	positionsWatch_ = positions.changed.connect([this] {
		Json all;
		std::string err;
		if (!json::parse(positions.get(), all, err) || all.kind != Json::Kind::Object)
		{
			positions.set(defaultPositions());
			return;
		}
		bool added = false;
		for (size_t i = 0; i < std::size(kNames); i++)
			if (all.find(kNames[i]) == nullptr)
			{
				all.members.emplace_back(kNames[i], place(kDefaults[i][0], kDefaults[i][1], kDefaults[i][2]));
				added = true;
			}
		if (added) positions.set(json::write(all));
	});
}

} // namespace chimera::vm
