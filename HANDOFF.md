# Cross Hermit port — handoff

Read this first when picking the work up (another AI, or a new session).

## What this is
A C++20 + SDL2 (+ Emscripten) port of the PC game Cross Hermit. The port is built function by
function from Ghidra decompiles of the original MSVC debug exe. Rules: SDL2 behind abstractions,
a non-blocking loop (tasks are C++20 coroutines, `co_await yieldFrame()`), a fixed timestep, a VFS
for data files, fixed-width types, no undefined behaviour.

| What | Where |
|---|---|
| Port | `~/Documents/modernize` (git: freeze-man-1/OpenCrossHermit on GitHub) |
| Imported game data (both releases) | `gamedata/` in the port (git-ignored; `build/gamedata` links to it) |
| Later TW build (memory dump, source of `ch_data.bin`) | `~/Documents/CrossHermit6/ch.exe` |
| JP 1.2 update | `~/Downloads/Cross Hermit Update/Update/Cross Hermit.exe` (md5 8ffb4f59…) |
| JP exe (old copy) | `~/Documents/CrossHermit3/Cross_Hermit_orig.exe` |
| Full decompile (whole exe) | `~/Downloads/CH.exe.c` |
| Battle decompile (thiscall fixed, best) | `tools/decomp/ch_battle_v2.c` (functions start `// FUN_xxxxxxxx @`) |
| Coverage inventory | `tools/inventory/` (script, CSV, overrides, HTML report) |

## Build and check (always stop on a green build)
```
cmake --build build -j16          # must print no warnings
cd build && ctest                 # 11 tests; those needing game files read CH_TEST_DATA (default gamedata/), skip (77) without
cmake --build build-asan -j8      # ASan/UBSan build for runtime checks
```
Headless battle run (from any scratch dir holding `../tr/pre_prep.SAV`, a save just before preparation):
```
CH_MAP=<n> CH_AUTOSKIP=1 SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 90 \
  build/game --tw --load ../tr/pre_prep.SAV --scene 10
```
Debug env vars:
- Logging: `CH_SCRIPT` (stage script trace), `CH_UNITS` (unit states and hits), `CH_PERF`, `CH_DEBUG` (map/cell info line).
- Control: `CH_MAP` (stage), `CH_AUTOSKIP` (skip dialogue), `CH_EXIT_SCENE=<n>`.
- Input and capture: `CH_INPUT_LOG` (each tick's mouse button edges and touch gestures), `CH_SHOTS=t1,t2` (BMP screenshots of the whole window at ticks), `CH_MOUSE="x,y@tick:d;..."` (scripted clicks, in 1024x768 game coordinates; with `CH_MOUSE_WINDOW=1` in window coordinates, mapped like real mouse events). `CH_KEYS="41@tick;..."` (scripted keys by SDL scancode, 41 = Escape; released 2 ticks later).

## How coverage is measured
- Regenerate: `python3 tools/inventory/build_inventory.py`. It writes `functions.csv` and `report.html`.
- A function counts as ported when its `FUN_xxxxxxxx` appears in a comment under `game/` (also `.inc`), `tests/` or `engine/`. A mention whose sentence says "not ported" makes it missing or partial.
  - **So every ported function needs its original name in a comment.**
- `tools/inventory/overrides.csv` holds hand corrections:
  - Category moves, e.g. versus scoring goes to Network.
  - `not needed` for game functions nothing in the exe calls; the report leaves these out of all percentages.
  - Always state the reason in the note.
- After each batch, republish `tools/inventory/report.html` as the "Cross Hermit Port Coverage" artifact:
  https://claude.ai/artifact/DQgp4X9SijNyyES83HssSa
- Work order (the user's wish): finish whole categories to 100% first, so completed sections show.

## Release layout (2026-11-06)
The repo is prepared for GitHub: no game data, no exe-derived tables, no secrets. See `README.md`.
- **Import:** `game/import/` reads ISO, InstallShield 3 `data.z`, T-TIME `cd1` and the update's SFX ZIP.
  - It writes `<data root>/{jp,tw,common}/data/...`, plus `common/static/{jp.exe,tw.exe,ch_data.bin}` and `installed.txt`.
  - The game files screen (`data_scene.cpp`) opens at start when nothing is installed, and on F9. On the desktop it has the web page's two buttons itself: Add game files… (`file_dialog.cpp`: GetOpenFileNameW, osascript, or zenity/kdialog, on a thread) and Erase imported files (a second press confirms; removes jp/, tw/, common/, lang/ and installed.txt, never the saves).
  - CLI: `game --data DIR --import FILE...`.
- **Static data:** every table and text the port used to carry as generated headers is now read at runtime by address.
  - `game/state/exe_image.hpp`: `exe::logicData(v)` is the ch_data build, `exe::own(v)` the release's own exe.
  - Port new tables the same way: TW VA and JP VA in code, never the data itself.
  - `CH_TRACE_TWONLY=1` logs JP-mode reads of TW-only text.
- **The patch:** `resources/ch_data.chpatch` comes from `tools/make_ch_patch.py ch_data.bin JP12.exe TWDISC.exe OUT`.
  - It is rebuilt from JP 1.2 exe copies, TW-disc Big5 text copies, and literals.
  - Regenerate it if `ch_data.bin` addresses that the logic reads change.
- **Text** from data files is decoded per variant with `gameTextToUtf8` (`game/text/sjis.hpp`).
- **Network:** the default server is a local relay, `ws://127.0.0.1:8787/` (`node tools/netserver/relay.mjs`).
  - An Apinator region is built in only with `APINATOR_AUTH_URL` in its `.env`, or with `-DCH_APINATOR_EMBED_SECRET=ON` (private builds only).
  - Both regions now sign through Cloudflare Workers (`apinator-auth-{us,eu}.booddr1980.workers.dev`). `apinator-{us,eu}.public` (committed: key, cluster, auth URL, no secret) stand in for the git-ignored `.env` files in a fresh clone or CI, so the GitHub builds and Pages get both servers; a local `.env` wins, and a secret in a `.public` is never read.
- **Web:** `web/shell.html` mounts IDBFS at `/save` and `/gamedata` before `main()`. It has file add, drag-and-drop and erase.
  - Build locally: `export EM_CACHE=$HOME/.cache/emscripten EM_FROZEN_CACHE=0 && cmake --build build-web -j16`. `.github/workflows/build.yml` also builds it and deploys to Pages.
  - The canvas fills the page (`#stage`, absolutely positioned so its own size never feeds back into the layout) and the window is `SDL_WINDOW_RESIZABLE`, so SDL sizes the canvas from its CSS size, and the engine pillarboxes the 4:3 frame into it as on desktop. A `ResizeObserver` on `#stage` sends the window a `resize` event, because SDL only re-reads the CSS size then. (Before, the canvas sized itself by `aspect-ratio`, and the resizable flag made it collapse to about 1x0.)
  - WebGL is required; without it `SDL_CreateRenderer` fails (the app's built-in browser pane has none). Test with Chromium: `--headless=new --use-angle=swiftshader --enable-unsafe-swiftshader`.
  - Tested: a full JP import (ISO plus update), reload, and the opening; the TW disc ISO added to it, and the title, Load, team and Save screens in TW.
  - Save download and upload: `webInput` / `webDraw` in `saveload_task.cpp` (web only; native has empty bodies) and `chDownload` / `chPickSave` / `addSaves` in the shell. Uploads go through `logic::saveCheck` (`tests/save_check_test.cpp`); `saveValid` stays the looser check for listing and loading. The buttons act on the release, because the file dialog would swallow the mouse-up of a press and leave the button stuck down. Tested in headless Chromium only: the dialog opening is confirmed by Playwright's file chooser event, so a real dialog, Firefox and Safari are untried (Safari is the likeliest to refuse a dialog opened from rAF).
- **CI:** builds and tests Linux/Windows/macOS and builds the web version (deployed to Pages from main). The tests that need game files are skipped there.
- **Removed:** the old `tools/extract_*.py`, `tools/gen/extract_*.py` and `import_assets.sh`. Copies are in the session scratchpad only.
  - `assets/` is the old data folder: ignored and unused.
  - `tools/decomp/` is ignored (decompiler output).

## State at this handoff (2026-11-05)
- **Overall: 59.0%.**
- **At 100%:** ADV/story VM, Battle flow/UI, Battle script VM, Game state/common.
- **Menus/preparation: 76.8%, about 9,700 lines left.** This is the current target. Largest gaps:
  - `hata\demotask.cpp` 4,247 lines: DemoTask, scene 0x17, not ported.
  - `yoshida\titletrialtask*.cpp` about 2,300 lines: the trial-build title, scenes 2 and Coming.
  - `hata\hatatask.cpp` 882 lines: the base class of the Hata tasks (OpTask, EdTask?); check it before porting.
  - `allresulttask.cpp` 448, `system\syschatbox.cpp` 444, `kojintask.cpp` 422, `system\syslistboxctrl.cpp` 397, `slprogtask.cpp` 345, `hantask.cpp` 212.
  - List the missing functions with:
    `python3 -c "import csv;[print(x['address'],x['lines'],x['source_file']) for x in csv.DictReader(open('tools/inventory/functions.csv')) if x['category']=='Menus/preparation' and x['status'] in('missing','partial')]"`
- **Other gameplay:** Battle units/AI 73.6%, map/fog 67.7%, skills/combat 64.0%.

### Done in the last batch
- Inventory now scans `.inc` files. That counted the generated opening layers in `game/op/op_layers.gen.inc`: Menus went from 58.5% to 73.3%.
- Task boilerplate: `game/game_system.cpp` names each scene's create and constructor functions.
  - Fixed the start params to the original's, `start(2)` for WorkRoom, Preparation, TacticsResult and SaveLoad.
  - Destructors are named.
  - Coming and Option destructors now also stop the music and voice (FUN_0041d280), as the original does.
- `ysub::outline` / `ysub::box` (FUN_00410680 / FUN_004d2690), a 1-pixel frame. This fixed a bug in KojinTask: the hover over a skill or item cell drew a solid white box instead of a frame.
- Fixed a crash in `tactics::coarseRoute` (`tactics_path.cpp`), which stepped below index 0. The `router` test passes again.
- `game/coming_task.*` ports scene 0x11 (ComingTask). `ctx.comingResult` stands for DAT_005ff720, which the trial-version battle end would set; that is not wired up yet.

### Fixed after the handoff was first written
- The sortie (stages whose row +4 is set, e.g. map 17): `selectCard` used `down()`, which is true for every unit off the field, so no face could be picked and nobody could go out. Fixed; the picked units now sortie at the start cell before the opening event.
- Calling waiting units in (FUN_0048d720 -> FUN_004812c0): own units off the field can be selected (FUN_00468f40), and a right-click on one of ours standing on the field (a tower; not a base or gate) queues command 11 (`orderCall` -> `callParty`). `callIn` now takes units waiting at +0xf 0 or 2 (FUN_0046a120) and uses the caller's move type.
- The "party goes out in order" stand-in after the opening now only runs when nothing of ours stands on the field.
- `tactics_task.cpp` logs the sortie settings (`[tactics] sortie on/off: start x,y ...`).

### Known gaps and notes
- Battle HUD: names and job labels in the unit panel are blank. The original draws them from pre-rendered text sprites (FUN_00408f10).
- Learnt and area skills are ported but not seen in play yet (test enemies don't have them).
- The battle's sortie card UI (FUN_00499da0) is partial: the party goes in order.
- Opcodes 0x81 and 0x94 of the battle script are not ported.
- Helper scripts in the old scratchpad (not kept with the repo):
  - `compact.py FILE LO HI` gives a cleaned decompile of `ch_battle_v2.c`.
  - `show.py ADDR...` shows a function from `CH.exe.c`.
  - `callers.py ADDR...` lists callers in `CH.exe.c`.
  - `pe.py` reads static data from the exe by VA.
  - These are quick to rewrite: functions in `CH.exe.c` start at a column-0 line containing `FUN_xxxxxxxx(` followed by a line `{`.

## The decompile is not the release (2026-11-07)
`ch.exe` (the source of the decompiles and `ch_data.bin`) is the TW disc's code with a few hand patches, so `CH.exe.c` shows those patches, not the game as released. A function-by-function comparison of `tw.exe` and `ch.exe` (about 3,950 functions, pointers followed into `.rdata` and `.data`) found only these differences. All are now handled:
| Where | Disc | ch.exe | Port |
|---|---|---|---|
| `FUN_0046bc20` TW 0x46bc8a, JP 0x435f64 | unit time counts down | store NOPed | ported |
| `FUN_00479410` TW 0x4797cf, JP 0x43f563 | a retreat costs a minute | store NOPed | ported |
| `FUN_004799c0` TW 0x479d9b, JP 0x43f92c | a fall cuts time to 50%, 75% with skill 0x54 | store NOPed | ported |
| `0x6253c4` | unit cap 8,500,000 | 0x5f5e0f0f, no real cap | `kTwDiscValues`, JP already overridden |
| `0x7577b0` ending text rows | sizes for the disc's image | wider | `kTwDiscValues` (JP's ending reads its own exe) |
| `FUN_004d7830` and 4 font references | 12px, MS Gothic | 11px, 細明體 | no change: the port has its own font |

Other differences are not logic: copy-protection stubs on API calls, and pointers moved by the changed strings. `ch.exe`'s extra `.text1` is not called from game code.

Not covered:
- The C runtime after 0x56ed90: encrypted on the disc, so its bytes can't be compared.
- The JP exe's code: it is an optimised build and can't be matched mechanically. Only the changes above were checked there.

## Port options (2026-11-07)
`options::PortOptions` lives in `port_options.txt` in the save folder, not in Option.bin. The instant-win key (2026-11-08) is one too. Fullscreen (F11 in `main.cpp`, `Engine::setFullscreen`) and infinite unit time (ignored in network play, as all players must count alike) are switched from the panel in `option_task.cpp`. The fullscreen switch acts on the press because a browser only allows the request during a mouse event, and it runs at the release.

## Window size, widescreen and scaling (2026-11-09)
- The scene always draws into a 1024x768 target texture (`Engine::frame_`); `Engine::frame()` then copies it once into the window at `frameRect()`. So screens keep their original 1024x768 coordinates at any window size, and nothing in `game/` knows the output size.
- `ScaleMode` (`port_options.txt` `scale=`): `smooth` (default: as large as fits, bilinear), `sharp` (nearest), `integer` (whole multiples, nearest; below 1x it fits like sharp), `stretch` (fills, aspect ignored). All but stretch pillarbox on wide outputs and letterbox on tall ones.
- Window size (`window=WxH`, desktop only; the web page sizes its canvas): the Option panel lists common 4:3, 16:9, 16:10 and 21:9 sizes that fit the desktop (a dropdown, as the scaling and the language are; after a change `Engine::keepMouseAt` keeps the pointer on the button while the window moves and resizes, so it does not land on OK). Fullscreen (F11) is always the desktop's resolution (`SDL_WINDOW_FULLSCREEN_DESKTOP`), no mode change.
- CLI for this run only: `--window 1920x1080`, `--scale integer`, `--fullscreen`, `--windowed`.
- Mouse: the engine maps SDL mouse events itself (`logicalFromWindow`, high-DPI aware; points in the black bars clamp to the frame's edge). SDL's touch-to-mouse events take the same path. Code reading `SDL_FINGER*` events (normalized 0..1) must use `Engine::logicalFromNormalized`, not multiply by 1024x768.
- Tested: desktop headless at 1024x768, 1280x720, 1920x1080 (smooth and integer), 2560x1080 (the Tutorial battle), with clicks given in window coordinates; web in headless Chromium at 1280x720, 1920x1080, 1024x768, 2560x1080, a live resize to 1000x900, and F11 fullscreen. Not tried: desktop fullscreen on a real display (the offscreen driver's display is 1024x768), high-DPI screens, Windows and macOS.
- **Split battle view (switched off 2026-11-10: it clashed with the widescreen view; only `CH_WIDE=1` turns it on, and a finger is the engine's mouse stand-in in a battle as on every other screen):** `game/tactics/tactics_wide.cpp` shows the battle frame in pieces (`Engine::setPieces`) with touch gestures; its header comment is the description.
- **Routes go through friends (2026-11-10, the user's wish, NOT the original):** `planRoute` no longer closes the cells of walkers of the unit's own side or its allies, only other sides' units and every building, base and gate. The walk itself is as the original (half step on a friend no slower, never two units resting on one cell).
- **Widescreen battle (2026-11-10):** a battle shows more of the map sideways on a window wider than 4:3, up to 16:9 (wider still gets side bars). The vertical view is unchanged, so a wide window sees more, never less. Menus, story scenes and every other screen stay 1024x768 with side bars.
  - Engine: `Engine::setMargin(m)` (0..`kMaxMargin` = 171) widens the frame target to `frameW() = 1024 + 2m`. Logical coordinates do not move: x 0 is where it was, and the frame now shows x from -m to 1024+m-1. `GameSystem::fixedUpdate` sets it back to 0 before the tasks run and the scene sets it again each tick, so a scene that does nothing stays 4:3. `DrawList::render` shifts every command by the margin, and a fill across the whole 1024 (fades, dims) is widened over the margins. Pointer mapping returns x in the same range. Split-view pieces keep logical `src` rects.
  - Battle: `TacticsMap::scrollX()` is still the original's camera, clamped for a 1024 view, and the scripts (`tactics_script.cpp`) use only that, so scripted camera moves and their timing are the original's. Drawing and pointer picking use `TacticsMap::viewX()`: the same, kept `margin` in from the map's sides so the wider view never shows past the map. The roster's faces (`TacticsUnits::faceX`), their hit boxes and the MENU button sit at the wider view's edges; the bars run on over the margins with plain stretches of their own art (`TacticsTask::draw`).
  - Width (`TacticsTask::viewMargin`): single player takes what the window's shape has room for when `PortOptions::wideBattle` is on (`battle_view=wide|original`, the Option row 戦闘の視野 / 戰鬥視野 / Battle view). **Network play always uses `kMaxMargin`, whatever the window or the option,** so every player sees the same width; a 4:3 window shows that frame smaller with bars above and below. The split view (`tactics_wide.cpp`, New UI) sets its own margin from the room its map piece has.
  - The view is no part of the units' state: `stateHash()` reads no scroll or margin. `CH_HASH=<n>` logs that hash every n battle frames in single player and `CH_MARGIN=<n>` forces the margin; the same run (`--scene 16`, `CH_PARTY=8 CH_MAP=3 CH_MOVE=40,30@200 CH_AUTOSKIP=1`) gave the same 20 hashes at margin 0 and 171. Two players through the local relay, one at 1024x768 and one at 1366x768 (versus entry 0), started on the same frame and hash with no desync note.
  - The Option panel's rows now shrink (`OptionTask::portRowH`, 30 down to 27) when ten rows would run off the screen.
  - Not tried: a real wide display or phone, Windows and macOS, a network game against a build from before this (its players would see only 1024: there is no version check for it), and a long network game with real orders given in the margins.

The same panel switches language: `options::languages()` is filled in by `main.cpp`, and its `switchTo` saves the choice and restarts on the option screen through `SwitchScene` (`data_scene.hpp`), so the old `GameSystem` and its audio device are gone before the new one opens. The option screen is only reachable from the title, so no game in progress is lost; the volumes are written first because they normally go in only on OK.

`~MixerAudio` clears the output node's `onaudioprocess` on the web. SDL 2.30's close leaves it set, and a queued event then ran on the freed device (divide by zero or out-of-bounds in `HandleAudioProcess`). A quick close and reopen hit it every time; F9 could too.

## UI style and touch (2026-11-08)
Groundwork for a switchable mobile UI; the new UI's design itself is not built yet (a parallel design study picks it).
- **UI style:** `PortOptions::modernUi` (`ui_style=classic|new` in `port_options.txt`), a row in the Option screen's port panel (UIスタイル: クラシック / 新; TW 介面風格: 經典 / 新; English in `languages/en/text/port.tsv`, and `languages/english.zip`'s copy of that file was replaced to match). Game code asks `ch::ui::modern()` (`options.hpp`). Nothing reads it yet, so Classic and New play the same today. A new UI must still turn input into the same mouse/keys the game logic reads: network play is lockstep.
- **Touch:** `engine/touch_gestures.hpp` (`legacy::touch::Recognizer`, SDL-free, tested by `tests/touch_test.cpp`). The engine feeds it SDL finger events (`SDL_HINT_TOUCH_MOUSE_EVENTS` is off, and mouse events with `SDL_TOUCH_MOUSEID` are ignored). SDL's renderer already normalizes a finger to the letterboxed logical viewport, so `x * 1024` is the logical x.
  - One finger works the mouse in both styles: tap = left click where it lifts; long-press (450 ms, within 14 logical px) = right click while still held; drag = left held from the start point, released where it lifts (the selection box). The steps go out one tick at a time (`InputState::applyTouch`), so the game sees the pointer arrive before a press, and a drag's press at its start.
  - A second finger ends the one-finger gesture (a drag is released where it is); two-finger pan and pinch are only reported, nothing more happens until every finger is up.
  - `InputState::touch()` gives each tick's gestures (tap, long-press, drag edges, fingers, pan, pinch); `setTouchMouse(false)` stops the mouse stand-in for a UI that reads gestures itself.
- **Web page:** no zoom (viewport meta), `touch-action: none` and no selection or callout on the canvas, the page's own touch handling prevented. The first touch (a touchend, which browsers accept) asks for fullscreen on the page and a landscape lock; a refusal (iPhone Safari has no element fullscreen) is ignored. Once touched, the canvas may fill the height less the import bar (`--bar`). Page fullscreen is not SDL's, so `Engine::fullscreen()` still says off there.
- **Tested:** native Option screen headless (the row toggles, `ui_style=new` saved, both labels fit). Headless Chromium with CDP touch emulation (`--use-angle=swiftshader`), mobile 900x700 at 2x, on the game files screen with `CH_INPUT_LOG` (set through `ENV` in a copy of the page's `preRun`): tap, long-press, drag and pinch gave exactly the expected mouse ticks at the right logical points, also after the page went fullscreen; mouse left/right clicks unchanged; no scroll or zoom. Not tried: a real phone, iOS Safari, a native touchscreen, and a battle by touch (no game data in that browser).

Language packs: `game/lang/language_pack.cpp`, format in `languages/README.md`, English source in `languages/en/` (`tools/make_english_pack.py` writes `languages/english.zip`).
  - The opening's image text (`tools/english_opening.py`, called by `make_english_pack.py`): `opdata.bin` frames 0 (captions), 1 (logo subtitle, staff), 13 (map plates) and 22 (title card). All the English sits on a new frame 48 (2048x1024), and `images/data/op/opdata.bin/layout.txt` sends `HataTask::opSprite` there (`lang::spriteParts`). `CH_OP_TRACE=<file>` logs each draw from those frames, which is how the `CAPTIONS` rectangles were found; it holds every rectangle the Japanese opening drew in a full run. `opdata2.bin` holds animation data and is shared with TW; `opdata_256.bin` is not read. opdata.bin is painted when loaded and never cached (96 MB).
  - Frame 22 has art under its text. The rings are redrawn, the rest comes from the TW sheet's art between its text, but the TW glow is a different drawing, so the JP glow's left half is mirrored about x=207 (`mirrored_glow`). Past the bars and at the X's arch, the text pixels are filled from what's around them. The sheet zooms past in about 90 ticks.
  - Packs are kept in `<data>/lang/<id>/`. A pack's id is a language next to `jp` and `tw`: `language.txt`, `--lang id`, `lang::choices`. The game runs it as `GameVersion::Jp`, so every version check, save and network path sees the Japanese release.
  - `vfs::setOverlay` adds the pack folder before the variant. `vfs::setAssetFilter` lets `paintImages` paint PNGs on banks as `readAsset` returns them (`paintFrames`, one rewrite per bank), rewriting the frame as the port's `CH` bank entry (32-bit, unlike the 16-bit originals).
  - `sjisToUtf8` looks every decoded string up in the pack's table (`setTextTable`), which covers exe text and scripts in one place. The port's Japanese labels use `tr()`. Text cached across a switch must check `textGeneration()`; `exe::strings` is dropped by `exe::reload`.
  - `tools/export_text.py`: `export` writes `languages/work/<pack>/<group>.tsv` (ignored by git, it holds game text) from exe strings found by reference plus every `.ybc` text table; `seal` writes them into the pack keyed by `textKey` (FNV-1a 64 of the UTF-8). The loader also takes readable Japanese keys, as `port.tsv` does. Group by address is in `EXE_GROUPS`, junk filter in `SKIP`.
  - Passages: `TextList::select` (the `.ybc` panels) asks `translatePassage` for the hash of the entry's decoded lines joined by `\n`; a hit supplies its own lines, a miss falls back to line by line. `export` writes those groups as whole entries, and `seal` wraps them with `PANELS` (px, width) using the renderer's font, so the engine never wraps. A re-export reads back the sealed text with its wrapped breaks, so edit `languages/work/` rather than re-exporting.
  - English drafted so far (all drafts awaiting review): `names`, `items`, `help`, `system`, `jobexp`, `skillexp`, `itemexp`, `kscapabilityup`, `tacticsexp`, `lectureexp`. In `tacticsexp` and `lectureexp`, the original's unfilled placeholders (`ここに記述`) stay blank. `stages` is the multiplayer maps' names and notes (`netgame::mapTitle`/`mapLine`, one row per panel line) and is drafted; `multiplayer` (new group, 0x4e4000-0x4e6100) is the network message boxes. `story` is fully drafted (9,852 rows, all awaiting review; the one blank row, `Ｃｒｏｓｓ　Ｈｅｒｍｉｔ　───`, is already English in full-width, and `languages/work/en/story.tsv` has rows past the chapters: the home-room and MVP speeches) `battle` (`tactics/script/*.bin`, exported as NUL-terminated strings) is fully drafted too (1,328 rows, awaiting review). Battle events use the ADV message system, so they reach the pack's text table; the first battle's opening event was seen in English. The tutorial in `t0080` splits sentences mid-word and two of its fragments (`ます。`, `せん。`) are owned by `stages`, which is why those two rows are filled there. The test lines `export_text.py` filters out of `t0080`, `t0082` and `t0086` (`スクリプト…`) stay Japanese.
  - Story lines are one row per on-screen line, unlike the panels: each line is its own script op with its own fade, so English keeps the Japanese line count and reads across rows in order. Windows hold about 400 px at 16 px (the opening's about 608, centred with full-width spaces).
  - A pack's painted banks are made once, in `lang::activate`, and kept with `vfs::cacheAsset`: painting on every read stopped the game for about 0.4 s (about 1.4 s on the web, where the sound shares the thread) each time the team screen was entered.
  - The team and character screens draw names, jobs, skills, buttons and headings from images, so these are overlays: `LABELS` in `tools/make_english_pack.py` lists a box and text per frame of `handata.bin` and `kojindata.bin`. It needs `gamedata/`, sampling only colours and alpha. A box opaque all round gets a patch following the frame's alpha (closed by 3 texels, for holes keyed along the strokes), otherwise `<frame>.erase.png` clears it. Text takes the Japanese's colour, so dim button states stay dim. Boxes of one size in a frame share a font size.
  - Character names follow the ending credits (`eddata.bin` frame 1: Palmra Ruverdracloa, Denehere, Callot Winfy, Syalon Flayspid, Fanna Hyverios, Quest Shnonsaw, Alsea Bellthird, ...), including their odd spellings. Everyone else's is a plain reading. The portrait plate says ヴィンテルライゼ for Oskir where the script and credits say Vainheart; the pack uses Vainheart.
  - The battle menu (`common.bin` frame 7, `quit_menu` in `tools/make_english_pack.py`): "continue or go to the title?" and its two buttons, in both looks. The question sits on a flat colour, so only the Japanese and its shadow are covered; each button label is covered row by row from the face either side of it, and both labels ("Continue", "Title") are redrawn in one font. The frame is an 8-bit BMP (`decode_bmp`).
  - `<frame>.diff.png` (`PaintKind::Diff` in `game/lang/language_pack.cpp`): its colour is added to the frame's texels, mod 256, and they become opaque; it applies after the erase and before the paint. The tools write every rebuilt background this way (`Sheet` in `make_english_pack.py`, `diff_png` in `english_opening.py`), so the `.png`s hold only the English and the pack has no game texels. A diff is against the JP frame as decoded (5-bit `v << 3 | v >> 2`, BMP palettes as they are), so it must be redrawn if that decode changes.
  - The battle top bar (`common.bin` frame 0, `top_bar`): each label is covered with the stretch of the same rows whose edges match best (the stone is alike along the bar), blended over 4 texels at the sides.
  - The results screen (`tacticsresult.bin`, `results_screen`): frame 1's table labels are rebuilt cell by cell from the JP and TW discs (`clean_cell`: the TW texel where only the Japanese covers it, a fill from the cell's plain texels where both have text; the light left strip is done apart from the cell); its 進行 button (both looks) uses `face_label`, as the battle menu's buttons do. Frame 2's names for the unit cards (16-texel rows from (256, 0)) are erased and redrawn from `STUDENTS` plus Denehere. The Prize rows were English already ("Hory" is the original's).
  - The week screen (`workroom.bin` frame 0, `week_screen` and `week_buttons`): the date is redrawn through `layout.txt` (`drawLaid` in `workroom_task.cpp`): each month digit maps to its month name on new frame 3, 第 to "Week", 月 and 週 to a clear texel, and the week digit moves right. The four buttons (both looks) use `face_label`.
  - The map screen (`workroom.bin` frames 1, 2 and 4, `map_screen`): the opening's `opdata.bin` frame 43 is the same map without names (at +112, +53), so each name comes off frame 1 as a diff of that copy, colour-fitted per label to what is round it. The English is drawn on the map; a name past `MAP_RIGHT` takes two lines. The lit plates (frame 2's 200 x 50 cells) are too narrow for the English, so `layout.txt` draws each as its icon from frame 2 plus a 340-wide strip from new frame 4, and the rest of each cell is erased. The descriptions and the left list's articles are `mappos0401` and `words`.
  - The character screen's buttons (`kojindata.bin` frame 7, `koji_buttons`): covered from the face row by row, and drawn in each of the three looks' JP colours, so the selected tab's light text stands out.
  - The team screen's top bar and 待機 plates (`handata.bin` frame 0, `han_bar`): 執務室 and 名簿 are covered with `cover_along` (the bar's own stretch of the same rows, as `top_bar` does) and drawn like LOAD and SAVE; their lit looks (76 x 24 cells at (332, 816) and (332, 840)) are erased and redrawn; 待機 is covered from the face either side. The menu's click sound now plays once per press (the original repeats it every held frame, heard on the TW Tutorial entry, which does nothing there).
  - The team screen's Tutorial entry (`HanTask` state 0xa, JP 0x45755c): fades out, runs `Data\ADV\DAT\mm001.ybc` (the tutorial main menu) inline, then back to state 2 with the area BGM. TW keeps the state (0x4a7333) but no menu entry for it, so there the entry works only with `twTutorial` on (`HanTask::hasTutorial`). The scripts (`mm*`, `han*`, `kojin*`, `menu*`, `battlemain`, `subchack` in `data/adv/dat`) are the `tutorial` text group. Their pictures are `tools/english_tutorial.py` (run from `make_english_pack.py`): the menus `mm001`-`mm004` and lit buttons `mmp001`-`mmp003`, the callout tabs on `h-part*`/`k-part*` (kept within each tab's width: the game cuts each from the sheet by its own rectangle), the SAVE label (erased and redrawn), and the `b-part001` nav. Checked in JP and TW (option on/off): menus, Getting Started, Forming Teams.
  - The tutorial's full-screen pages (`han*`, `kojin*`, `battle*`) are `PAGES` in `tools/english_tutorial.py`: each item's box holds its Japanese, which is filled in with `inpaint` and the English drawn where it began (labels with their circled number redrawn, notes, titles, callout tabs, the battle pages' nav links). These are backgrounds drawn whole, but the character portrait is drawn over them from x ~ 712, so English on the left half must end before it. The story's chapter cards (`tc*`, 16 in `common`, 43 JP-only; date from the name `tcMMWW`) are `CHAPTERS`, erased and drawn anew. `sc*` and `mc*` (portraits) hold no text. Checked in JP: Getting Started, the Roster tutorial through Status Area, the battle notes' first pages; the other pages only in previews, the chapter cards not in game.
  - The network screens (`tools/english_net.py`: `netdata.bin` frames 1-5, `mesbox.bin` frame 1): each label is a region on its face, covered from the face either side and redrawn in its own colour; frame 5's heading half is erased. Checked in the game through name entry, connect, lobby and the setup screen. The same module does the SAVE/LOAD screens (`slprog.bin` frames 3-8: DELETE/CANCEL in LOAD's serif capitals, the Yes/No dialogs and their messages). `make_english_pack.py` runs it.
  - The battle-start banners (`data/tactics/tactstart/tactstartNN.bin`, bank 0x5b, every file incl. `151`) and the starting-member panel (`common.bin` frames 9-10) are `tools/english_tactics.py` (`BANNERS`, run from `make_english_pack.py`). On black the Japanese is erased; on the pictures (`PICTURE`: 14, 15, 37-40, 48-68) its strokes are filled in from the art round them (`picture_of`, `fill_in`), so a faint smudge can remain on busy art. Markup `{r:}`/`{b:}`/`{g:}` colours words like the original. Checked through the engine's pack loader (14, 09, 80); not yet seen in a live battle.
  - Still Japanese in the tutorial: the plain screenshots' own game UI (e.g. `han001`, the bases of the `kojin*` pages), the ドロップ under each Drop!, and the debug chapter select's `m001`-`m008` (~80 small Chapter buttons each). The chapter cards' titles are drafts awaiting review.
  - Still Japanese in images: the rest of `common.bin` in battle, the save screen, the unused `◆強制`, `初級 NN` and similar placeholder plates in `handata.bin`.
  - Not done: Literal Japanese in port code without `tr()` is untranslated, e.g. the date columns in `SaveLoadTask::scan` ("年 月 週"). English lines have no wrapping: a translation breaks its own lines with `\n`, and fitting the text boxes is untested.
  - The F9 screen shows 4 packs and the Option list 8; past that they are not reachable there.

**PERFECT checklist (2026-11-08, the port's own):** the battle's pause menu (Esc) shows above Continue/Title what the stage's PERFECT asks for: an empty box, a check once done, a red cross once it can no longer be met, counts (`2 / 3`, `5 left`, time left), and the best result still to be had (the results screen's PERFECT .. BAD LUCK). `StageChecks::goals` (`game/tactics/tactics_goals.cpp`) words each story map's check in `tactics_checks.cpp` as goals built from the same tests, each with the rank left once it fails (`drop`); lower ranks' time steps are hidden goals. Goals that only ask that something lasts stay open until they fail. Side goals say take (buildings), collect (chests 0x195/0x196), win over, or keep (ours at the start, `startOwn_`); buildings/bases/gates say take or keep by `startBuildings_`. Map 11's value 2/0 is the enemies that broke through (its script's 二匹目 ..). Drawn by `TacticsTask::drawGoals`: Japanese via `tr()` (English in `languages/en/text/port.tsv`), Chinese literals on TW. Network maps and script-ended maps have no list. `CH_GOALS=1` logs each map's records (name, kind, team) at the battle's first check.

**Network battle start (2026-11-07):** the battle's start phases (`sortieFrame` in `tactics_task.cpp`: the opening event, the start banners) now run on the relay's turns, with `loaded()` sent before them, as FUN_004539b0 syncs from the first frame. Before, the opening's dialog waited for the other players' 0x42 reports, which no turn could bring, so a co-op map with a scripted opening (entry 67, stage 151) hung at "Waiting for the other players". The opening event now takes FUN_00499da0's arguments (undim 1, hold 0, in single player too): the port's hold freed the units as soon as an event with no script ended, so enemies attacked under the 勝利条件 banner. In network play the banners are timed (FUN_00499910 with FUN_00456b20) instead of waiting for a click, so every machine starts the battle on the same frame (`CH_NET_TRACE` logs it, with the state hash). netDebug with `--scene 15` now opens the real room screen once its room is joined (`CH_MOUSE=837,437@100:d;837,437@103:u` on the host picks the first co-op map) and leaves the start to it. Checked over Apinator US: versus entry 0, co-op 64 and 67, both peers on the same start frame and hash, no desync alerts.

The chapter select (scene 0x16, `SceneId::AdvMenu`) is the original's debug menu: `menu001.ybc` to `menu008.ybc` over the `m001`-`m008` pictures (Chapter buttons, サブキャラクタチェック = `SubChack.ybc`, MVP scripts, the <マニュアル> link to `MM001.ybc`), each script it starts run with next scene 0x16, so it comes back after a chapter. Nothing in either exe requests it (only the ADV's next-scene variable could), and it is not the dev harness of scene 0x14 (`Master_Debug_Menu`: Walk Test, Anim Viewer, Font Test, Gamma Test, Fade Test, Sound Test, still a stub). The port's Options has a 章選択 (デバッグ) row (`kItemChapters`) that leaves for it; Esc (port-only) leaves a chapter for the menus and the menus for the title. Checked headless in JP (Options → menu → Chapter 1 → back → title) and the TW Options row; chapters were not played through.

`PortOptions::twTutorial` adds the JP title's Tutorial item to the TW menu (`kMenuTwTutorial`, in the empty cell under Option). ch.exe has no such item, but the TW disc has the translated script (`t0080.bin`). The button is built from the TW title art in `TitleTask::drawTutorial`, as that art has none. Checked natively in TW through the opening dialogue only, not played to the end.

## Resume checklist
1. Build plus ctest, all green.
2. `python3 tools/inventory/build_inventory.py`, then pick the next missing functions in Menus/preparation.
3. Port them with `FUN_` names in comments, checking against the decompile. Mark functions nothing calls as `not needed` in the overrides, with the reason.
4. Build, ctest, and an ASan run where battle or runtime code changed.
5. Regenerate the inventory, republish the report, and update this file.

## Battle AI audit (2026-11-08)
The AI states and helpers (`yoshida\unitctrlai.cpp`, `tactics_units.cpp`) were compared function by function with `ch_battle_v2.c`. Party and enemy units share this code; the AI record (`u.ai`, 0x40 bytes) is what differs.
- **Matched, no change:** dispatcher 0x475700, choose 0x476470, hold 0x4763d0, move 0x476dd0, guard 0x4776a0, wander 0x4771d0, route 0x47ed90 / 0x47efb0 / 0x47f090, ambush 0x47a470, scan 0x47a920 / 0x47a9d0 / 0x47aed0 (0x469a30, its "pick anyone" roll, is always 0), heal pick 0x47ac80, help call 0x47b2c0 / 0x47b340 / 0x47b790 / 0x47b800 / 0x47b920, counter 0x47cc90 / 0x47ce70, learnt-skill roll 0x47c690 / 0x47c8a0, sight 0x4ddbb0 (double 1.2 / 0.8, checked in the exe), call-in 0x480480 / 0x480740 / 0x4807e0 / 0x47f1d0, the sight test 0x47c210 / 0x47e460.
- **Fixed:**
  - Stand 0x476250 and occupation 0x480a60: wait while the opening runs (+0x108b48, set by FUN_00499c90, cleared at FUN_00499da0's end; `setOpening`). Before, units could start choosing, and buildings could change hands, in the sortie's three seconds.
  - Occupation 0x480a60, the takeover: at 0 it queues message 0xe (FUN_0048e020, once) and FUN_0048ef60 is tried each frame until the building is free. Before, the port tried once, so a tower in the middle of a shot never changed hands.
  - Skill state 0x477980: the target check is FUN_0047ba20 by skill kind. An AI unit in reach of another side's structure guards (0x13) unless its record's +0x1c is set. The cast starts the frame after the reach test. The "may not chase" case (0x47bed0 = 2) goes on with the frame.
  - Skill state 0x477980: the unit that hit this one takes over only on FUN_0047b210's roll under record +0x13 (it was always). Record +0x1b 1 rescans during the skill state and may switch targets (LAB_00477dbd, missing before).
  - Aim 0x47c580 / 0x47c8a0: stores the aimed cell (+0x4f0), so an AI ground skill (kind 4) goes where the target stood when chosen. The caster's own cell keeps the original's y / 32. The sight test 0x47c210 case 4 uses that cell. The MP fallback to the job skill re-takes it.
  - Flee 0x47e680: on the way only +0x12 2 (run again) and 4 (skill state) act; the port's extra aim/heal branch is gone. In move/wander a +0x12 2 reaction no longer skips that frame's walk step.
  - Movement 0x470250 and facing (0x46c120): double atan2 / cos / sin, each result kept as a float, as the CRT calls do. This is also safer for lockstep between native and web peers than atan2f / cosf.
- **Route walk 0x476680, the best of both:** the port keeps its own whole-route planner (one search up front, a smoother walk) but now behaves like the original:
  - `walk()` returns the original's codes: `Walk::Arrived` 1, `Blocked` 2 (the next cell is closed, FUN_0047da30), `NoRoute` 3 (the first search found nothing).
  - A route cut short (a later leg found no way) waits and searches again, 0xb4 frames for the computer's units and 0x1e for the players' (+0x4e0, `routeWait`), as often as it takes. A retry keeps the walk and pause counts; a fresh `plan()` resets them.
  - The skill state's chase goes back to step 0 when it arrives (the original's 1), so a moving target is chased on, and gives up only on 2 or 3.
  - Kept from the port: the chase re-plans whenever the target changes cell (not during a wait), and the original's mid-route step-aside, FUN_00470840, only runs at the end. (Units no longer pass through other sides' units: see "Enemy aggression" below.)
  - Port guard: a chase with nowhere nearer to go and the target still out of reach gives up, where the original re-plans every few frames for ever.
- **Still different:** hit timing in the strike (0x16) is the port's own.
- **Checked:** two native peers over the local relay, co-op entry 64. Same start frame and hash, identical aim and state logs (446 changes), no desync alerts.

## Battle damage audit (2026-11-08)
Each damage and heal step was compared with `ch_battle_v2.c` (ch.exe). The JP exe is an optimised build, so the two rolls were found by their constants and read by hand (JP 0x444b50 weapon, 0x444cb0 spell, RNG 0x432700): same as TW.
- **Matched, no change:**
  - Weapon roll 0x4824d0: hit chance = accuracy - evade x terrain / 100 - rand % 100. A critical comes on roll < 5, or on chance >= 0x96 (x chance / 100, at least x1.5); a miss on roll >= 0x5f or chance < 0. Damage = (atk + rand % spread) x scale / 100 - guard x terrain / 100. A plain hit of < 1 is result 4 (no damage); a critical below 0 becomes 1 (0 stays 0); results 2 / 3 above 1x / 3x toughness. Two draws, the second only when it hits and spread != 0.
  - Spell roll 0x4826f0: (atk + rand % spread) x (100 - resist + [+0x1c]) / 100 - magic guard x terrain / 100, at least 1. The +0x1c term is always 0: FUN_004de930 clears it and nothing writes it (the port leaves it out).
  - Heal roll 0x482810: atk + rand % spread.
  - Fighter values FUN_004deaa0 = FUN_004de930 (+0x3a / +0x3c / +0x38 / +0x4c / +0x4d / +0x50 / +0x4f / +0x4e / +0x52) + FUN_004dea30 (status handlers 0x4deaf0 .. 0x4df9b0, offsets and clamps checked); terrain FUN_0046b3e0 -> FUN_0043b9e0 (move type 4 as 0).
  - Learnt-skill values FUN_00488af0 -> 0x48a260 / 0x48a380 / 0x48a730. Spread kinds 0 / 1 really copy the attack (checked in the machine code at 0x48a76c), so those skills roll atk + rand % atk.
  - Job skill 0x48aae0 -> 0x48ac90 / 0x48b080 / 0x48b1b0, splash 0x48adc0, area 0x488bc0 (target list, order, which roll by +5 / row +6, steady -> 1).
  - HP / MP 0x472550 (with +0x54), the queue 0x493020 / 0x493450, popups 0x493090 / 0x493180.
  - Stats: party FUN_004b93c0 (`stats::derive`), enemies FUN_004dfc20 (`deriveCompact`), field by field.
  - The RNG is FUN_00467e00 in both exes, and one hit goes out per strike.
- **Fixed** (`land`, `hit`, `areaHit` in `tactics_units.cpp`):
  - Formula row +6 2 / 5 changes **MP**, and 3 / 6 changes HP and MP (FUN_00491ef0 -> FUN_00493180). The port changed HP for all of them, so skills 22, 41, 47, 51, 63, 66, 70, 79 and 85 hurt or healed HP. Row +6 0 changes nothing and shows no number (the port showed a 0).
  - Nothing lands once the attacker or the target is down or off the field (FUN_004919f0 -> FUN_004690a0). The port still applied a hit whose attacker had fallen.
  - Job 0x1c6 takes nothing from learnt skills (< 100) and job 0x1c7 nothing from job skills (> 99): it shows a 0 and does not react (FUN_004919f0). This was missing.
  - Heal 0x48b1b0 raises only a negative amount to 1, so a roll of 0 heals 0 (the port had at least 1). +0xa6 and the counter mark are set for heals too, as in case 1.
  - Job skill aim +0x18 0 / 4 / 5 does nothing (0x48aae0 asserts); the port rolled a weapon hit.
  - A skill without its own hit (anim +0xc 0) adds the unit's +0x45 effect on result 4 too (0x488bc0). The cure 0x1d and the dispel 0xc / 0x2f run on result 4 too (FUN_00491ef0's tail).
- **Still different:** a skill without its own hit takes its hit effect from its own row of DAT_00613b78, where the original takes the job skill's row (0x488bc0 -> FUN_00482d60). This is only visual, plus a rand % 5 for some effects. Area and splash hits land at once, without the original's 10..59-frame delays.
- **Checked:** `CH_UNITS=1` now also logs each roll's inputs (`weapon on` / `spell on`: roll, chance, evade, terrain rates, guard, toughness). Map 1 headless, with hits checked by hand. Spell 37 x 93 / 100 - 2 = 32. Weapon: chance 92 - 7 x 75 / 100 - 59 = 28, and 13 - 25 x 75 / 100 < 1 gives result 4. Native, ctest 10/10 and the web build pass.

## Enemy aggression (2026-11-08)
Map 1's enemies seemed more aggressive than in the original. The choice, scan, switch roll, help call, route walk, movement speed and tick rate all match: the original's QPC pacing waits 16666 µs a frame, so 60 Hz. The differences were in the walk step FUN_0047dec0 and the post it reads (`walk()` and `create()` in `tactics_units.cpp`):
- **Post:** FUN_004680b0 sets every unit's post (+0x2e8) to its start cell, after FUN_004750b0 -> FUN_004da710 cleared it. The port set a post only for player orders, so the "defensive" tether never held enemies. Most of map 1's enemies are defensive (+0x9f 1), and they now stop chasing once their start cell is out of their sight. The post is set only for the computer's units on the field. Off-field units get 0xff / 0xff in the original, and the port leaves those without a post (no tether). The original has no defined behaviour here:
  - Appearing (FUN_0048e790) never sets the post, so a defensive reinforcement keeps (255, 255). That cell is never in sight (FUN_0047d010), so its first walk step turns it toward the post: FUN_0047d190, then FUN_0047d210 searches to (255, 255).
  - The cell checks see off-map as closed (FUN_0043bfa0), so the search goes on to the coarse route FUN_0044ea80. Its FUN_0044e6e0 reads the node grid at (255, 255) with no bounds check, which is past the grid's end, and its loop only ends on a found route.
  - So it depends on memory: either it hangs, or it "finds" a route whose goal is (255, 255). In that case FUN_0047d0e0 counts the unit as heading home and lets it walk on, toward wherever that garbage node is.
  - The user, who has played the original, says map 1's later reinforcements (groups 7 / 8, +0x9f 1) charge at the party. So the port keeps them without a post, with no tether (decided 2026-11-08). The party is placed after creation (FUN_0049a480), which this does not change.
- **Tether pause:** the tether's 3 makes FUN_00476680 search for the post after 0x3c frames (0x1e for the players' units); before, the port turned back at once.
- **Blocking:** a walk whose next cell holds a unit of another side that is not an ally stops (4, returned as 2 = `Walk::Blocked`). Behind one of ours or an ally that is no slower, the step is halved. Before, units walked through each other. This is also what keeps the light-wall gates closed to other sides; `restingOn`'s gate rule stays for the planner.

**Objectives banner and healers (2026-11-08):**
- The 勝利条件 banner was darkened by the script's dim. The port filled the map view at priority 0x50, in front of the banner at 100. Both exes fill the whole screen (0x400×0x300) at priority 0x65 (FUN_00497c90; the fill is at JP 0x44d824, TW 0x497cda), so the banner now stays bright, as in the original.
- Healers were checked against FUN_00476dd0, FUN_004776a0, FUN_0047ac80 and FUN_00477980 step 0x18, plus three headless runs on map 1 (unit 2, skill 107, is the healer). All already matched:
  - A move order turns the AI off (+0x4ec), so no heal happens on the way.
  - An idle unit's guard scan heals the most hurt friend in reach.
  - A right-click heal on a friend repeats while the friend is in sight, until a new order is given.
  - So no code changed. If a healer still seems to stop mid-walk, look at the walk ending early: blocked by another side's unit (FUN_0047dec0), after which the stand and AI states take over.
- New debug hook: `CH_MOVE=<x>,<y>@<battle frame>` makes every unit of ours walk to that cell then (beside CH_ORDER / CH_HP).

## Gameplay audit, the rest (2026-11-08)
Everything the earlier audits (AI, damage, aggression, healers, banner) left out was compared with the decompiles; the inventory shows no game function the exe calls left unported (the leftovers have no caller and no pointer to them in ch.exe), so this looked for behaviour the port changed.
- **Matched, no change:** the skill state's cast / strike / recovery (0x477980 steps 0x14..0x18: times from +0x32 / +0x34 or the skill record +0xa / +0xe, skills 10 and 0x10, FUN_00415040 is the identity); conditions (FUN_00490a50 tick, poison 0x4903c0 / 0x490540, drain 0x490430 / 0x4905b0, FUN_00493320); occupation 0x480a60 / 0x480d00 (600 / 900 hold-out, team 4 never takes); the fall 0x4799c0 (time cut, out of action at <= 0xe0f); results: exp and school points 0x4bbd40 (checked in the machine code at 0x4bc999: points = total x rate / counted / 100 + spill; Ghidra's version is wrong) and item drops 0x4bca40.
- **Fixed:**
  - Confirm (Z / Enter) won any single-player stage at once with rank 2. That key is the port's, not the original's; it now works only with the Options panel's "Instant win with Enter (solo)" (`PortOptions::instantWin`, `instant_win=1` in `port_options.txt`), `CH_DEBUG` or `CH_WINKEY`.
  - Splash (0x48adc0) and area (0x488bc0) hits now wait as the original's do: each hit's effect starts after its packet's first word counts down (FUN_0040a800: the node stays still and unseen; `Unit::delayed`, `strike(.., delay)`). Splash: rand() % 50 + 10. Area by pattern (anim +0xe, steps DAT_00618ee8): 1 i x 12 + 10, 2 (n - i) x 10 + 10, 3 distance x 10 + 10, 4 0x8a - distance x 20 (at least 10), 5 i x 6 + 10, else rand() % 50 + 10; the aimed unit itself (aim 1..3, 6) none. A miss still shows at once (FUN_00482d60 case 0). The learnt skill's added effects, cure and dispel (FUN_00491ef0's tail, `learntTail`) now come when the hit lands, not when it is rolled; the unit's own effect (+0x45) of a skill without its own hit still goes at once.
  - The area hit's effect: a skill with its own hit plays anim +0xc (the port played its DAT_00613b78 row); one without plays the job skill's row (FUN_00482d60 with the job's skill). The original's rand() % 20 - 10 offsets are drawn but not drawn on screen (the port's effect slot has no offset).
  - Kill credit (FUN_004799c0): the killer's +0xaa / +0xac and the team's worth (+0x2ef14) keep their old value when the 16-bit sum would wrap, as the original's "only if it grew" does (the port clamped to 0x7fff).
  - Results (0x4bbd40): the party's damage total is summed in 16 bits, then clamped, so past 32767 it wraps and a negative total becomes 0, as in the original.
- **Still different (for the user to decide):** a learnt skill with its own hit effect that misses (a weapon roll) shows MISS at once in the port, where the original sends it through the effect and FUN_004919f0, so it lands as 0 and its added effect can still take hold (kept as MISS, the user's choice, 2026-11-08); one effect slot per unit, so a new hit effect on a unit lands the one before it early (the original keeps several nodes); the strike's hit falls back to the clip's end when no flag-4 frame comes; appearing units skip cells where a unit stands (the original may place them on it, and they step aside later); a step-aside after a walk takes the nearest free cell.

## Movement re-check (2026-11-10)
`planRoute` / `walk` were read again beside FUN_00476680, FUN_0047d210 / 0x47d570 / 0x47d730, FUN_0047d830 / 0x47d930 / 0x47da30 / 0x47dbd0 / 0x47dec0, FUN_00470840 / 0x4707b0 / 0x4709d0 and FUN_00437d10 / 0x437d90 / 0x437e40. Nothing new was found wrong; the search queue never makes a unit wait (FUN_00437e40 loops until it has its answer) and +0x4e4 is only ever cleared. No battle logic changed since the 2026-11-08 audits except the route through friends above. What is knowingly not the original's, for the user to decide:
- Routes go through friends (the user's wish, 2026-11-10); the original's inner searches close every unit's cell (FUN_004388e0 / FUN_004380b0 with FUN_0043c0c0).
- **Group arrival is the original's now (2026-11-11):** every unit of a group order walks to the clicked cell; `planRoute` no longer moves a taken goal. `Unit::want` is +0x4cc (where it was sent), `Unit::goal` +0x4d0 (where the walk goes). `stepAside` (FUN_00470840 -> FUN_004709d0) takes a cell from the placement table around `want` (`appearCell`, FUN_0043d660, range 0x1f) as the new goal. `walk` calls it as FUN_0047da30 does, once for each new next cell when a unit is on that cell and one on the goal (the computer's units then wait 0x1e frames), and as FUN_0047dbd0 does on arrival when another unit is on the cell (`countOn`, FUN_0043c0c0, 2 or more). `CH_UNITS=1` logs each step aside. Not the original's: the test on the way runs on the last outer leg only (the original runs it for every outer waypoint), and routes still go through friends (above). Also put right: `appearCell` counts a cell's units as FUN_0043be60 does (move type 4 is not asked), and FUN_0047dbd0's 3-cell rule turns for the next outer waypoint (planned into `planRoute`) where the port used to end the walk there, up to 2 cells short. `tactics::nearestFree` has no caller left. `CH_NET_MOVE=<x>,<y>@<frame>` is `CH_MOVE` for a network game.
- The whole route is planned at once and a chase follows its target cell by cell; a hopeless chase gives up.
- `localRoute` goes on where the original's rounds would repeat (a goal walled in).
- Reinforcements on "defensive" have no post; the pointer picks the front-most unit; one hit-effect slot per unit; a missed learnt skill shows MISS; the strike's hit falls back to the clip's end.
- Options, off by default or solo only: instant win, endless unit time; the wide battle view shows more map (the game's sight and fog are unchanged).

## Unit panel spread, scenes, held Backspace (2026-11-11)

- **Unit panel in the wider view** (`TacticsUnits::drawHud`, `setPanelSpread`): with a margin
  and the split HUD off, the panel's first two columns move left into the margin and their
  boxes grow (a = min(margin/3, 48) for column 1, b = min(margin/4, 36) for each half of
  column 2). The boxes are pieces of common.bin frame 3 drawn over frame 4, each stretching
  a 2-texel column of its inside; every number and label moves with its box's anchor.
  At 4:3 (margin 0) the original draw runs unchanged.
- **Scenes hold the conditions**: FUN_0046b760 skips a unit's whole block while a scene holds
  the battle, so `update()` no longer runs `tickStatus` / `regenerate` while `frozen_`.
  (Animations, effects and `u.timer` still run in the port while frozen.)
- **Held Backspace** repeats in text fields (`InputState::onTextRepeat`, from SDL's key repeat).
- **Week's results in English**: `week_results()` in `tools/make_english_pack.py`.
