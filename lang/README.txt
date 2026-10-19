WarBreeds HD languages
======================

The game shows its text in the language picked on the main menu's
Settings page (Interface tab, Language). English is built in. Every other
language is a file in this folder:

  WarBreedsHD_fr.txt   Français
  WarBreedsHD_de.txt   Deutsch

  WarBreedsHD_fr.fonts the fonts French is shown in (see FONTS below)

The patch looks for WarBreedsHD_<code>.txt files every time the Settings page
opens, so a new file shows up without restarting the game.

The French and German game text ([game], [maps], [messages], [names]) is
the 1998 French and German releases' own, with a few typos fixed; it is used
with the original developers' blessing. WarBreeds HD's own text ([patch]) was
drafted by machine (an AI model) from the English.
They will have mistakes and clumsy wording: corrections are very welcome, at
https://github.com/Deadpoodle/warbreeds-hd (open an issue, or send a changed
file).


MAKING OR FIXING A TRANSLATION
------------------------------

A language file is plain text, saved as UTF-8 (Notepad: File > Save As >
Encoding: UTF-8).

  Language = Español        the name shown on the Settings page (first line)

  [game]                    the game's own text, by its string number
  104 = Cargar partida

  [patch]                   WarBreeds HD's own text, by its English
  Settings = Ajustes

  [maps]                    campaign mission titles, by map file
  SPTAN1A.WBM = ...

  [messages]                messages campaign maps send, by a code
  1DE8CB31 = ...

  [names]                   names of things and gizmos, by their English
  Pod = Cocon               (build and pod lists, labs, map editor)

- A line starting with # is a comment. A key left out (or left empty) stays
  in English, so a half-finished file works.
- Keep {1}, {2}, ... and %d, %s exactly: they are filled in with numbers and
  names. {1} and {2} can change places.
- \n is a line break (mission briefings).
- Start a value with ~ for a tight spot (a button, a column heading): it is
  then squeezed a little, if needed, to fit where the English fitted.
- Letters: anything in Western European languages works (é è ç ß ü ...).
  The game's fonts have no « »: use plain "quotes".


FONTS
-----

A language can bring its own fonts: WarBreedsHD_<code>.fonts beside its text
file. French and German have one. The 1998 French and German releases drew
their text in narrower fonts than the English game (Bell Gothic, Geometric
231 and Sherwood, instead of the English bitmap fonts), and their text is
written to fit them. So in French and German:
- if you have those fonts (copy the files in the CD's FONTS folder into the
  game's Fonts folder, or install them), they are used, as in 1998, with the
  few text boxes the release resized for them (the Layout lines of the
  .fonts file: the Device Lab's and Gene Lab's, the briefing's);
- otherwise the menus use Roboto Condensed (from the Fonts folder), a
  look-alike narrowed where needed to the width a line had in 1998; the HUD,
  the labs and the headings use the game's own fonts, which read best at
  their small sizes. A line marked ~ is set a pixel tighter, or one size
  smaller, where it would run out of its button.
The .fonts file is made by tools/font_metrics.py in the source repository.
Without one, a language uses the game's own fonts. The Font setting
(Orbitron) applies to English only.

TO START A NEW LANGUAGE
-----------------------

1. Start the game once with WarBreeds HD installed. It writes
   WarBreedsHD_template.txt into this folder, made from your own copy of the
   game: every line to translate, with the English (and the French, for
   reference) in a comment above it.
2. Copy it and rename the copy WarBreedsHD_<code>.txt, for example
   WarBreedsHD_es.txt. (The template itself is rewritten every time the game
   starts, so don't edit it directly.)
3. Put the language's name after "Language =" at the top, then fill in the
   lines you want. Anything left empty stays in English, so you can test a
   few lines at a time.
4. Start the game: the new language is on Settings > Interface > Language.

For checking a file there is tools/lang_check.py in the source repository:
it reports missing placeholders, letters the fonts can't draw, and text that
is too long for where it is shown.


VOICES
------

The spoken announcements ("construction complete", "unit lost", ...) are all
in one file, DATA\GLOBSFX.MHK. To hear them in another language, copy that
file from your own CD of that version of the game (WARBREED\DATA\GLOBSFX.MHK)
into a folder named after the language's code, next to this Lang folder:

  Voices\fr\GLOBSFX.MHK
  Voices\de\GLOBSFX.MHK

Never replace the game's own DATA\GLOBSFX.MHK: it is the English one, and is
used whenever English voices are chosen. Settings > Interface > Voices lists
the voices it finds; "By language" uses the voices of the language the game
is shown in, if you have them, else English.
