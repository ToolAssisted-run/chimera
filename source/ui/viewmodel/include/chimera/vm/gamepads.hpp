#pragma once
/* Gamepads and joysticks through SDL2, found beside the program at run time.
 * Each input held is named as Chimera has always named it, so a package's default bindings mean the same: "X1 A",
 * "X1 DpadUp", "X1 LStickUp" for a game controller and "J1 B1", "J1 POV0U"
 * for any other joystick, numbered by SDL's device order from 1. Without
 * SDL2 there are no gamepads, and nothing else changes.
 *
 * One thread only: the one that made it polls it. */

#include <memory>
#include <string>
#include <vector>

namespace chimera::vm {

class Gamepads
{
public:
	Gamepads();
	~Gamepads();
	Gamepads(const Gamepads &) = delete;
	Gamepads &operator=(const Gamepads &) = delete;

	/* SDL2 was found and started */
	bool available() const;
	/* why not, when it is not */
	const std::string &problem() const { return problem_; }

	/* the names of every input held now; picks up pads plugged in or out.
	 * With `axes`, each axis too, by Chimera's names ("X1 LeftThumbX Axis",
	 * "J1 X Axis"), from -10000 to 10000 as Chimera reads them */
	void poll(std::vector<std::string> &held, std::vector<std::pair<std::string, int>> *axes = nullptr);

	/* the pads open now, by name */
	std::vector<std::string> names() const;

	/* whether a key's name is one a pad sends ("X1 A", "J2 POV0U") */
	static bool isPadInput(const std::string &name);

private:
	struct Api;
	struct Pad;
	void reopen();

	std::unique_ptr<Api> api_;
	std::vector<Pad> pads_;
	std::vector<int32_t> ids_;
	std::string problem_;
};

} // namespace chimera::vm
