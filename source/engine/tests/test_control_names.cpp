/* test_control_names.cpp - the letters and headers a package gives its
 * controls, and the rule for a name it gave none.
 *
 * The rule is the only naming the engine does, so it is pinned here character
 * by character: a frontend shows what this answers, and an old package's
 * columns must not change under a person from one build to the next.
 */

#include "../source/control_names.hpp"

#include <cassert>
#include <cstdio>
#include <string>

using chimera::ControlNames;

static void aNameWithoutItsPlayer()
{
	assert(chimera::bareControlName("P1 Start") == "Start");
	assert(chimera::bareControlName("P12 L Stick") == "L Stick");
	assert(chimera::bareControlName("Power") == "Power");
	/* not players: no digits, no space after them, nothing after the space */
	assert(chimera::bareControlName("Pause") == "Pause");
	assert(chimera::bareControlName("P1") == "P1");
	assert(chimera::bareControlName("Px Up") == "Px Up");
}

static void theRule()
{
	/* the first character of the last word */
	assert(chimera::genericMnemonic("P1 Up") == 'U');
	assert(chimera::genericMnemonic("Stick Fire") == 'F');
	assert(chimera::genericMnemonic("Insert Disk 2") == '2');
	assert(chimera::genericMnemonic("Key A") == 'A');
	assert(chimera::genericMnemonic("X") == 'X');
	assert(chimera::genericMnemonic("P2 select") == 's');
	/* a word that begins with something an entry cannot carry is passed
	 * over for the one before it: a '.' is a button NOT pressed, a '|' parts
	 * the groups, and a character of another script is more than one byte */
	assert(chimera::genericMnemonic("Key .") == 'K');
	assert(chimera::genericMnemonic("Keyboard 0 \xE3\x82\x8F \xE3\x82\x92") == '0');
	assert(chimera::genericMnemonic("Pipe |") == 'P');
	/* and when no word will do */
	assert(chimera::genericMnemonic(".") == '?');
	assert(chimera::genericMnemonic("") == '?');
	assert(chimera::genericMnemonic("P1 ") == '?');
	assert(!chimera::usableMnemonic('.') && !chimera::usableMnemonic('|') && !chimera::usableMnemonic(' '));
	assert(chimera::usableMnemonic('!') && chimera::usableMnemonic('~') && chimera::usableMnemonic(','));
}

static void theRuleForAnAxis()
{
	assert(chimera::genericAxisHeader("P1 Left Stick X") == "P1LSX");
	assert(chimera::genericAxisHeader("Left Trigger") == "LT");
	assert(chimera::genericAxisHeader("P1 Gun Screen X") == "P1GSX");
	assert(chimera::genericAxisHeader("Mouse_X") == "MX");
	assert(chimera::genericAxisHeader("Paddle") == "P");
	/* nothing to abbreviate: the name itself */
	assert(chimera::genericAxisHeader("") == "");
	assert(chimera::genericAxisHeader(" - ") == " - ");
}

static void whatAPackageDeclares()
{
	ControlNames names;
	names.mnemonics["Cross"] = 'X';
	names.mnemonics["Up"] = 'U';
	names.mnemonics["P2 Cross"] = 'x'; /* a second pad that differs says so by its whole name */
	names.axisHeaders["P1 Left Stick X"] = "LX";

	/* one line serves every pad */
	assert(names.mnemonicOf("P1 Cross") == 'X');
	assert(names.mnemonicOf("P3 Cross") == 'X');
	assert(names.mnemonicOf("Cross") == 'X');
	/* the whole name wins over the bare one */
	assert(names.mnemonicOf("P2 Cross") == 'x');
	/* a control it said nothing about gets the rule - and so does a control
	 * a movie carries that this machine does not have */
	assert(names.mnemonicOf("P1 Circle") == 'C');
	assert(names.mnemonicOf("Eject Disc") == 'D');

	/* an axis is named whole: its header is a column's, and two players'
	 * columns are two */
	assert(names.axisHeaderOf("P1 Left Stick X") == "LX");
	assert(names.axisHeaderOf("P2 Left Stick X") == "P2LSX");

	/* a header is found as a mnemonic is, and a button with none is headed
	 * by its letter (chimera#225) */
	names.mnemonics["Keyboard CAPS SHIFT"] = '^';
	names.buttonHeaders["Keyboard CAPS SHIFT"] = "CS";
	names.buttonHeaders["Start"] = "STA";
	assert(names.buttonHeaderOf("Keyboard CAPS SHIFT") == "CS");
	assert(names.mnemonicOf("Keyboard CAPS SHIFT") == '^');
	assert(names.buttonHeaderOf("P2 Start") == "STA");
	assert(names.buttonHeaderOf("P1 Cross") == "X");
	assert(names.buttonHeaderOf("P1 Circle") == "C");

	/* what a header may be */
	assert(chimera::usableButtonHeader("CS"));
	assert(chimera::usableButtonHeader("F 1"));
	assert(chimera::usableButtonHeader("12345678"));
	assert(!chimera::usableButtonHeader(""));
	assert(!chimera::usableButtonHeader("123456789"));
	assert(!chimera::usableButtonHeader(" CS"));
	assert(!chimera::usableButtonHeader("CS "));
	assert(!chimera::usableButtonHeader("C\tS"));
	/* any printable character, counted as characters and not bytes (issue 607) */
	assert(chimera::usableButtonHeader("\xC3\x89"));                                       /* E acute */
	assert(chimera::usableButtonHeader("\xE2\x96\xB3"));                                   /* U+25B3 triangle */
	assert(chimera::usableButtonHeader("\xE2\x97\x8B"));                                   /* U+25CB circle */
	assert(chimera::usableButtonHeader("\xE2\x9C\x95"));                                   /* U+2715 cross */
	assert(chimera::usableButtonHeader("\xE2\x96\xA1"));                                   /* U+25A1 square */
	assert(chimera::usableButtonHeader("\xF0\x9F\x8E\xAE"));                               /* U+1F3AE, four bytes, one character */
	assert(chimera::usableButtonHeader("\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3")); /* eight, 24 bytes */
	assert(!chimera::usableButtonHeader("\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3\xE2\x96\xB3")); /* nine */
	assert(chimera::usableButtonHeader("\xE2\x96\xB3 1"));                                /* a space between */
	assert(!chimera::usableButtonHeader("\xE2\x96\xB3 "));                                /* a space at the end */
	assert(!chimera::usableButtonHeader("\xC2\x85"));                                      /* U+0085, a control */
	assert(!chimera::usableButtonHeader("\x7F"));
	assert(!chimera::usableButtonHeader("\xE2\x96\xB3\n"));
	assert(!chimera::usableButtonHeader("\xC2\xA0"));                                      /* no-break space */
	assert(!chimera::usableButtonHeader("\xE2\x80\xAE"));                                  /* right-to-left override */
	assert(!chimera::usableButtonHeader("\xE2\x96"));                                      /* cut short */
	assert(!chimera::usableButtonHeader("\xC3"));                                          /* cut short */
	assert(!chimera::usableButtonHeader("\xC0\xAF"));                                      /* overlong */
	assert(!chimera::usableButtonHeader("\xED\xA0\x80"));                                  /* a surrogate */
	assert(!chimera::usableButtonHeader("\xFF"));

	/* a package that declares nothing: the rule everywhere */
	const ControlNames none;
	assert(none.buttonHeaderOf("P1 Cross") == "C");
	assert(none.mnemonicOf("P1 Cross") == 'C');
	assert(none.axisHeaderOf("P1 Left Stick X") == "P1LSX");
}

int main()
{
	aNameWithoutItsPlayer();
	theRule();
	theRuleForAnAxis();
	whatAPackageDeclares();
	std::printf("test_control_names: ok\n");
	return 0;
}
