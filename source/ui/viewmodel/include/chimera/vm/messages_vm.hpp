#pragma once
/* Chimera's Messages window (Config > Messages): where each thing drawn
 * over the picture goes, the colours of the text, how long a message
 * stays and whether messages stack. Each place is a corner of the picture
 * (anchor: 0 top left, 1 top right, 2 bottom left, 3 bottom right) and a
 * distance from it in pixels, as Chimera is; the window
 * places the text. A colour left at Chimera's default is the theme's, as
 * Chimera's OSDManager decides; one chosen here wins over the theme. All
 * of it is remembered. */

#include <cstdint>
#include <string>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class MessagesViewModel : public ViewModel
{
public:
	explicit MessagesViewModel(Dispatcher &dispatcher);

	/* {"Fps": {"x": 0, "y": 0, "anchor": 0}, ...}: the nine places by
	 * Chimera's names (Fps, Frame Counter, Lag Counter, Input Display,
	 * Watches, Messages, Rerecords, Autohold, Game Time) */
	Property<std::string> positions;
	/* "#AARRGGBB": the messages, the alerts (a lag frame), the previous
	 * frame's input and the movie's input */
	Property<std::string> messagesColor{"#FFFFFFFF"}, alertColor{"#FFFF0000"}, lastInputColor{"#FFFFA500"},
		movieInputColor{"#FF808080"};
	/* how long a message stays, 1 to 10 seconds; and whether they stack */
	Property<int64_t> messageSeconds{2};
	Property<bool> stackMessages{true};
	/* the open core has a timer of its own (SessionViewModel's hasGameTimer):
	 * only then is Game Time one of the types the window lists (issue 470) */
	Property<bool> gameTimer{false};
	/* the types the window lists, in Chimera's order, as a JSON array */
	Property<std::string> types;
	static std::string typesShown(bool gameTimer);

	static std::string defaultPositions();

private:
	Connection secondsWatch_, positionsWatch_, gameTimerWatch_;
};

} // namespace chimera::vm
