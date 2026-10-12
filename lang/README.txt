WarBreeds HD languages
======================

The game shows its text in the language picked on the main menu's
Settings page (Interface tab, Language). English is built in. Every other
language is a file in this folder:

  WarBreedsHD_fr.txt   Français
  WarBreedsHD_de.txt   Deutsch

The patch looks for WarBreedsHD_<code>.txt files every time the Settings page
opens, so a new file shows up without restarting the game.

The French and German translations were drafted by machine (an AI model)
from the game's English. They will have mistakes and clumsy wording:
corrections are very welcome, at https://github.com/Deadpoodle/warbreeds-hd
(open an issue, or send a changed file).


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

- A line starting with # is a comment. A key left out (or left empty) stays
  in English, so a half-finished file works.
- Keep {1}, {2}, ... and %d, %s exactly: they are filled in with numbers and
  names. {1} and {2} can change places.
- \n is a line break (mission briefings).
- Start a value with ~ for a tight spot (a button, a column heading): it is
  then squeezed a little, if needed, to fit where the English fitted.
- Letters: anything in Western European languages works (é è ç ß ü ...).
  The game's fonts have no « »: use plain "quotes".

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
