# Translations

Chimera can be shown in other languages. This document says how its texts
are kept, how the language is chosen, and how a language is added or
corrected.

## The rules

The owner set these on 2026-10-28:

1. Every text Chimera shows a person is a named key (for example
   `common.file`) whose words are in a language table. English is one of
   the tables.
2. No text is written into the code itself.
3. Every language Chimera has gives every key.
4. The same holds for the cores, their settings included: each core
   package carries tables of its own, one per language.

What counts is what a person reads: the window, its messages and errors,
and what a core declares. These are not translated: the logs written for
developers (the core log, standard error), names in file formats, the
descriptions of Lua functions, of MCP tools and of view-model properties
(they describe English function names), chimera-run's own output (a tool
for tests), control names such as `P1 Up` (identifiers that movies, key
bindings and scripts use), and names that are data rather than writing:
game titles, MAME's machine names, the community's patch and cheat names.

## The tables

`languages/<code>.json`, one file per language, `en.json` among them:

```json
{
	"name": "Deutsch",
	"english": "German",
	"texts": {
		"common.file": "&Datei",
		"display_config.framerate_hz": "Bildrate: %1 Hz",
		"main.frozen_addresses": { "one": "%n Adresse", "other": "%n Adressen" }
	}
}
```

- `name` is the language's name in itself; `english` its name in English.
- A key is lowercase words joined by `_`, after the window or the subject
  they belong to and a dot. `common.` holds texts many windows share.
- `%1` to `%9` are filled in by the program. A text that depends on a
  number has one form per plural category of its language (`one`, `few`,
  `many`, `other`, as the CLDR names them), and `%n` is that number.
- `&` before a letter marks a menu's shortcut letter; `&&` is an `&`.

The build copies the tables beside the engine (`languages/` in the build
folder, `lib/languages/` in a bundle), and the engine reads them when the
program runs. The window asks for a key with `qsTrId("key")`, which the
engine answers from the table in use; the engine and the view models ask
through `ce_text`. A key that a table lacks shows as the key itself, so a
missing one is seen.

## A core's texts

A core package carries its own tables, `languages/<code>.json` inside the
package, one for every language Chimera has. They have only `texts`; the
language is Chimera's.

What a core declares holds keys of them, never words: in `waterbox.config`
a setting's `display`, `description` and `optionLabels`, a firmware entry's
`display`, `description` and `label`, a preset's `label` and `description`,
and the texts of `movieImport` (`menu`, the movie's and the files' `label`,
the options' `display` and `description`); in `file_slots.json` a slot's
`title`, `help` and `nameHelp`. The engine gives the window these files with
every key replaced by its text in the language in use
(`ce_package_entry_texts`).

So does what the core returns while it runs: the reason `Init` refused
(`GetLoadError`), the answers of `SuggestSettings` and `ImportMovie` (a
note, notes, a refusal, the game's own settings and firmware), the game's
settings (`GetGameSettings`) and its properties' descriptions. In a JSON
answer only the strings that are keys change; the rest stays byte for byte,
so a number no double holds is not rounded.

A text with arguments is its key followed by each argument after a 0x1F
byte: `error.not_loaded` 0x1F `game.bin` is the text of `error.not_loaded`
with `%1` replaced by `game.bin`. In JSON the byte is written `\u001f`; in C
it is best a string of its own, `#define TEXT_ARG "\x1f"`, written
`"error.not_loaded" TEXT_ARG "%s"`, since a hex escape takes every hex digit
after it. A key with plural forms takes its count as the first argument
(`%n`, which is `%1` too). An argument that is itself a key is its text, so
an emulator's own reason can sit inside the core's sentence; such a reason
separates its own arguments with a 0x1E byte (`TEXT_INNER "\x1e"` in C), and
reads the same when it is shown on its own. A report of several sentences is
several texts, each after a 0x1D byte, and is shown a line each.

A package with no table for the language in use, or a table without a
key, shows its English; one with no English either shows the first table it
has that holds the key, in the order Chimera lists its languages (owner,
2026-10-29). Nothing about a missing or broken table stops a core. A string
that is no key of any table is shown as it is. These stay as they are, being
names rather than writing: control names, option values, memory domain,
bus, register, surface and drive names, a property's name, group and value
names (scripts use them), and data the core reads from its emulator or a
database, such as an arcade game's dip switches or a patch's name. So do
the reasons an emulator gives in its own words, which a core passes on as
an argument: among them those of the core's stand-ins for what a sandbox
lacks (a network, a disc drive, a second graphics context), which answer
the emulator in its terms and which it reports as its own.

## How the language is chosen

Config > Language lists every table there is, each language named in
itself and then in English, whatever language the window is in:
"Deutsch - German", "Nederlands - Dutch", and English alone (owner,
2026-10-31, issue 430). A dash parts them, since some names have
brackets of their own (Brazilian Portuguese's). The window's texts
change at once.

The choice is remembered (`customize.language` in the window's settings).
At the first open, when nothing is remembered yet, the window takes the
first of the system's languages it has a table for - a more precise name
first, then the general one, so a Canadian French system takes `fr` and a
Brazilian one `pt` - and English when there is none. That choice is then
remembered too, so a later change of the system's language does not change
Chimera's (owner, 2026-10-28). A remembered language whose table is gone is
chosen again the same way.

In every language but English the Language menu's own name carries
"(Language)" beside it, so a person who picked a language they cannot read
can find the way back.

## The languages

There are 26: English (`en`), German (`de`), Spanish (`es`), French
(`fr`), Italian (`it`), Japanese (`ja`), Brazilian Portuguese (`pt`),
Simplified Chinese (`zh_CN`), Korean (`ko`), Traditional Chinese
(`zh_TW`), Russian (`ru`), Ukrainian (`uk`), Polish (`pl`), Czech (`cs`),
Dutch (`nl`), Swedish (`sv`), Danish (`da`), Norwegian Bokmal (`nb`),
Finnish (`fi`), Turkish (`tr`), Hungarian (`hu`), Romanian (`ro`), Greek
(`el`), Indonesian (`id`), Vietnamese (`vi`) and Catalan (`ca`). Every core
carries the same 26.

The texts in languages other than English were translated from the
English by an AI model (Claude), a language at a time, and checked by the
tests below; no speaker of each language has read them yet. A correction
from someone who speaks the language is welcome.

## Adding or correcting a language

A correction is an edit of one text in that language's file. A `%` before
a digit is read as one of the program's `%1` to `%9`, so a language that
writes the percent sign first (Turkish `%50`) spells a percentage out in
words.

A language is added as `languages/<code>.json` with every key `en.json`
has, and its code in `languages/meson.build`. The engine needs the
language's plural rule (`pluralForms` and `pluralForm` in
`source/engine/source/language.cpp`).

## The tests

`engine_language` checks every table against English: the same keys and
no others, the same `%1`..`%9`, the plural forms the language takes, a
menu's shortcut letter kept, and the spaces and line breaks at either end
kept. It also checks the plural rules, the filling of `%1`..`%9`, a
language used and another in its place, a core package's texts (keys with
arguments, plural forms, a key inside, JSON changed only where it holds a
key, a missing language shown in English) and the C calls. `translations`
checks the window's side: the language it starts in for a given system, a
remembered choice, and what `qsTrId` answers.

`texts` (`tools/check-texts.py`) finds words written into Chimera's code:
string literals that read as words and are none of what the rules leave
English. Identifiers that happen to be English words (hotkey names, the
on-screen elements, config names) are listed in
`tools/check-texts-identifiers.txt`.

For the cores, `test-packages` checks every installed package: a table for
every language Chimera has, each against the package's English as above,
and every text field of its declarations a key of it, given every argument
its text uses. What a core's code returns only the code knows:
`tools/check-core-texts.py <core checkout>` reads the core's own sources and
says each key-like string not in its English table and each key of the
table nothing names. `synth_texts` runs it on the synthetic core, and the
witness's `E:core-text` sees a refusal the synthetic core gives as a key
with an argument reach chimera-run in English.
