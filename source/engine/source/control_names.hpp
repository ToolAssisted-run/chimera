/* control_names - what a control is called where a person reads very little
 * of it: the one character a pressed button writes into a movie's text and
 * heads its input column with, and the short header of an axis's column.
 *
 * The frontend used to keep these, in tables keyed by system: a table of
 * names every machine shared, another of what each machine called its own,
 * and the same pair again for axes - a thousand lines that had to grow before
 * a new core's columns could be told apart. They are the core's now. A package
 * declares them beside the controls they name (waterbox.config):
 *
 *   "input": {
 *     "buttons": [ "P1 Up", "P1 Cross", "P2 Up", "P2 Cross", "Power" ],
 *     "mnemonics": { "Up": "U", "Cross": "X", "Power": "P" },
 *     "headers": { "Power": "PWR" },
 *     "axes": [ { "name": "P1 Left Stick X", "min": 0, "max": 255, "neutral": 128,
 *                 "header": "LX" } ]
 *   }
 *
 * A mnemonic is looked up by the control's whole name first and then by its
 * name without the player ("P2 Cross" -> "Cross"), so one line serves every
 * pad; a keyboard, or a pad whose second player differs, names them whole.
 *
 * A header is what heads a button's input column where one character cannot
 * tell it from its neighbours - a keyboard has two shifts and forty letters
 * (chimera#225). It is looked up the way a mnemonic is, and a button with
 * none is headed by its mnemonic. It is never written into a movie.
 *
 * What is here is that lookup and THE RULE FOR A NAME NOBODY DECLARED - an
 * older package, or a control a movie carries that the running machine does
 * not have. The rule is the only naming this engine does, and it knows no
 * machine: the first character of the last word.
 *
 * None of it is what a movie means. An entry is read by position, and any
 * character but '.' is a pressed button, so a letter can change and every
 * movie made before still plays.
 */
#pragma once

#include <map>
#include <string>

namespace chimera {

/* "P2 Start" -> "Start"; a name with no player in front is returned as it is */
std::string bareControlName(const std::string &name);

/* A character a button may write: printable ASCII, and neither '.' (which is
 * a button not pressed) nor '|' (which parts the groups of an entry). One
 * byte, because an entry is walked a byte at a time. */
bool usableMnemonic(char c);

/* A header a button's column may carry: one to eight characters of printable
 * ASCII that neither begin nor end with a space. */
bool usableButtonHeader(const std::string &header);

/* The rule: the first character of the last word that begins with a usable
 * one ("Stick Fire" is F, "Insert Disk 2" is 2, a key named in another
 * script falls back on the word before it), and '?' when no word does. */
char genericMnemonic(const std::string &name);

/* The rule for an axis: the initials of its words, a word of one or two
 * characters kept whole ("P1 Left Stick X" is P1LSX, "Left Trigger" is LT). */
std::string genericAxisHeader(const std::string &name);

/* What one controller's package declared, and the rule behind it. */
struct ControlNames
{
	std::map<std::string, char> mnemonics;          /* by whole or bare name */
	std::map<std::string, std::string> axisHeaders; /* by whole name */
	std::map<std::string, std::string> buttonHeaders; /* by whole or bare name */

	char mnemonicOf(const std::string &button) const;
	/* the declared header, or the mnemonic as one character */
	std::string buttonHeaderOf(const std::string &button) const;
	std::string axisHeaderOf(const std::string &axis) const;
};

} // namespace chimera
