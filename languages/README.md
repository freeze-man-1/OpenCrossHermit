# Language packs

A language pack is a `.zip` you add on the game files screen (**F9**): drop it on
the window, use **Add game files…** on the web, or run `game --import pack.zip`.
It then appears there and in the language list on the Option screen.

A pack runs on top of the Japanese release, so the Japanese files must be imported.
Anything it doesn't include stays Japanese, so a pack can start small and grow.

## Layout

```
language.json
text/*.tsv
images/data/tactics/option.bin/1.png
data/...
```

The files may also sit inside one top folder in the zip. Other files, such as a
README, are ignored.

### `language.json`

```json
{ "id": "en", "name": "English", "base": "jp", "author": "you", "version": "1" }
```

- `id`: 1 to 16 characters from `a-z 0-9 - _`, and not `jp`, `tw` or `common`. A pack
  with the same id replaces the installed one.
- `name`: what the menus show, up to 24 bytes of UTF-8.
- `base`: must be `jp`.

### `text/*.tsv`: translations

UTF-8, one string per line: the key, a tab, the translation. The key is the
exact Japanese or, in a pack you share, its hash.

```
# a comment
言語	Language
@9ef792f855d560d8	Dark God Stone Zem
```

Don't write the hashes by hand. Translate in the drafts instead:

```bash
python3 tools/export_text.py export --pack languages/en
```

This reads your imported files and writes `languages/work/en/<group>.tsv`, one
Japanese line and its translation per row, keeping what the pack already has.
Fill in the second column, then seal them into the pack:

```bash
python3 tools/export_text.py seal --pack languages/en
```

`languages/work/` holds the game's text, so it is ignored by git: keep it to
yourself. The sealed files have only hashes and your translations, so the pack
can be shared. Sealing replaces those groups' files in the pack; `port.tsv` is
yours to edit directly and holds the port's own labels.

- A line with no translation stays Japanese, which makes a useful to-do list.
- `\n`, `\r`, `\t` and `\\` stand for a line break, a carriage return, a tab and a backslash.
- The Japanese must match exactly, including trailing spaces and control codes.
- A match replaces the whole string, wherever the game decodes it: exe text,
  scripts and the port's own labels. Two places with the same Japanese get the
  same translation.
- Text doesn't wrap by itself. The scrolling panels (mission briefings, lectures,
  job, skill, item and stat descriptions) are exported as whole entries, one row each
  with their Japanese line breaks as `\n`. Write them as running text and `seal` wraps
  them to the panel's width with the game's font; use `\n` only for a break you want,
  and `\n\n` for a gap. Other long text is one row per line, in story order: story lines
  fade in one by one, so keep the row count and let the English read across rows, at
  most about 400 px wide (50 characters or so).
- Much of the interface is pictures, not text: name plates, job and skill plates,
  buttons and headings. Those need `images/`. `tools/make_english_pack.py` shows one
  way to draw them from a list of labels and boxes.

### `images/<game file>/<frame>.png`: pictures

Painted over one frame of a sprite bank as the game reads it. The path is the game
file's, in lower case, and the number is the frame. The PNG must be 8-bit RGB or RGBA
and exactly the frame's size. Transparent pixels leave the original showing.

A `<frame>.erase.png` beside it makes the frame transparent wherever the PNG isn't, before
`<frame>.png` is painted. Use it for text on a transparent sheet: a patch would show as a
box, while erasing keeps the game's transparency. On a plate, leave the patch's pixels
transparent wherever the frame's are, so the plate keeps its shape.

A `<frame>.diff.png` rebuilds art, such as what was under a word on textured art. Wherever
it isn't transparent, its colour is added to the game's texel (each channel mod 256) and the
texel becomes opaque. It applies after the erase and before `<frame>.png`. Write it as
(rebuilt − game) mod 256 against the JP frame as the game decodes it (5-bit channels
widened as `v << 3 | v >> 2`, BMP palettes as they are): it means nothing without the
game's own file, so it holds no game art. `diff_png` in `tools/english_opening.py` writes one.

Paint only your own text and plain colours, never pixels copied from the game, so the
pack can be shared; put anything rebuilt from the game's art in a diff.

Prefer overlays that hold only your new labels: the zip then contains no game art
and can be shared. See `tools/make_english_pack.py`.

A PNG one past a bank's last frame adds a new frame of any size, which gives text
that outgrows its sheet somewhere to go. New frames are added in order, so `3.png` must
exist before `4.png` counts. The opening and the week and map screens use this.

### Name and skill plates: drawing them for another language

Several lists of names are pictures drawn 1:1 in small cells. `tools/make_english_pack.py`
has one function per strip, each taking the names (and, for skills and quests, a font), so a
script for another language can import it and call it with its own lists (set
`make_english_pack.PACK` to your pack's folder first):

| What | Where | Function |
|---|---|---|
| Skill names (character screen, skill lists) | `data/menu/kojindata.bin` frame 8: 120 x 12 cells from (0, 756), 4 columns of 21 | `skill_names(columns, font)` |
| Students' short names on the character screen's face cards | `data/menu/kojindata.bin` frame 10: 64 x 16 rows at x 304, 34 rows | `name_column(rel, 10, 304, names)` |
| Short names on the battle results' unit cards | `data/tacticsresult/tacticsresult.bin` frame 2: 64 x 16 rows at x 256, 35 rows (the students, then Denehere) | `name_column(rel, 2, 256, names)` |
| Quest names on the team screen's plates | `data/menu/handata.bin` frame 4: the boxes in `QUEST_LABELS` | `quest_plates(preview, labels, font)` |

- The lists run in the game's order. `STUDENTS`, `SKILL_COLUMNS` and `QUEST_LABELS` hold the
  English ones; copy them and translate. In a skill or quest list, `None` keeps that
  plate's Japanese.
- Skill and quest names use the pack's outlined label style (`render_line(..., steady=True)`:
  light text with a dark halo, every name on one baseline). One size serves the whole
  strip: the largest at which every name fits, squeezed sideways by up to a quarter
  (`fitted_size`), so a long name makes the whole strip smaller. Shorten it rather than
  let the strip shrink.
- Skill plates are redrawn whole on opaque black, as the Japanese is. Quest plates keep
  the game's art. The texels under the Japanese are rebuilt from the plain texels of
  the same colour's other plates in that column, including the ones whose names are
  kept, so the light inner frame and the fill stay whole. They are stored as a diff,
  so the pack still holds no game pixels. Pass `--preview <dir>` to see the result
  (`handata_4.png`) before importing.
- Pick a font that covers your script and whose licence lets you share pictures made
  with it (DejaVu, Liberation and Noto all do). The English uses DejaVu Serif Condensed
  Bold (`FONT`). For Chinese, Japanese or Korean, pass a Noto Sans/Serif CJK font.

### `images/<game file>/layout.txt`: moved sprite rectangles

For screens whose text is drawn from fixed rectangles of their sheets. Each line says
where one rectangle's pixels now are:

    frame x0 y0 x1 y1  new-frame nx0 ny0 nx1 ny1  dx dy

`dx dy` is where the new rectangle's top left goes, relative to the old one's. A rectangle
may have several lines, each drawn in turn. `#` starts a comment. The new size is kept at
the sprite's own zoom, so a longer word simply draws wider. Two screens read this: the
opening (`OpTask`, `layoutBank_`; set `CH_OP_TRACE=<file>` to log what it draws from frames 0,
1, 13 and 22), and the week and map screens (`WorkRoomTask`, `drawLaid`, for `workroom.bin`:
the date, and the map's lit place names).

### `data/...`: whole files

Replaces the game file at the same path, in lower case. Use it for files the
other two can't reach. A pack with game files in it shouldn't be shared.

## Working on a pack

An added pack is unpacked to `<game data>/lang/<id>/`. Edit that folder and restart
the game (or switch language and back) to see changes, with no need to zip each time.

## English

`en/` is the English pack's source. `python3 tools/make_english_pack.py` draws its
images and writes `english.zip`. So far it covers the Option screen, the web build's
save buttons, the opening (`tools/english_opening.py`), the team and character screens,
the team screen's tutorial menus and callouts (`tools/english_tutorial.py`), the network and SAVE/LOAD screens (`tools/english_net.py`), and in battle
the menu, the top bar, the results screen, and the battle-start banners and starting-member
panel (`tools/english_tactics.py`). Where text sat on art, the
art under it is rebuilt as a diff (the opening's title card, the battle menu, top bar and
results screen), so the pack holds no game pixels and can be shared.
