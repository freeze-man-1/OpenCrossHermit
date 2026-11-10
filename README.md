# OpenCrossHermit — a modern Cross Hermit port

A fan-made port of the 2002 PC strategy RPG *Cross Hermit* to C++20 and SDL2,
for Windows, Linux, macOS and the web (Emscripten). It runs the original
game's logic, rebuilt from its executable, on top of the original data files.

**No game files are included.** You import your own copy: the port reads
your discs and update, then extracts and stores what it needs. Both releases
are supported:

| Release | What to import |
|---|---|
| Japanese | the disc (`EB2002CH.iso`, or just `data.z` from it) **and** the 1.2 update |
| Traditional Chinese | the disc (its image, or just `cd1` from it) **and** the Japanese 1.2 update |

The Japanese 1.2 update (`Cross Hermit Update 1.2.exe`) is needed for either
release. Its executable is the Japanese game's, and it is also the base the
Chinese release's tables are rebuilt from (see [How the import works](#how-the-import-works)).

One disc is enough to play that release from start to end. With both
imported you can switch languages at any time from the game files screen.

## Playing

1. Start the game. Without imported files it opens the **game files** screen.
2. Press **Add game files…** (or drop the files onto the window, or run
   `game --import FILE...` from a terminal). The screen lists what is still missing.
3. Press **Import**. The files are identified by their contents, not their
   names. A full import takes a minute or two.
4. Press **Play 日本語** or **Play 繁體中文**.

Press **F9** at any time to return to the game files screen: add the other
disc, or switch languages. The choice is remembered.

Press **F11** at any time for fullscreen, in any release and in the browser.
The game keeps its 4:3 1024x768 picture and scales it to the window or screen with
black bars at the sides on a wide display. The Option screen picks the window size
(desktop) and the scaling: Smooth, Sharp, Whole multiples or Stretch. These are kept in
`port_options.txt` next to the saves; `--window 1920x1080`, `--scale integer`,
`--fullscreen` and `--windowed` override them for one run.
The Option screen has the same switch, and **infinite unit time** for an easier
game: units on the field keep their time left. It has no effect in network play.

The Option screen also switches **language**, straight away, and in the Chinese
version it can add the **Tutorial** back to the title. The original Chinese menu
leaves it out, though the disc has it translated.

**Language packs** add more languages on top of the Japanese release. Add a pack's
`.zip` on the game files screen, like any other file. `languages/english.zip` is
the English pack: the story, battles, menus, tutorial and most on-screen art, as
drafts still being reviewed. To make one, see
[languages/README.md](languages/README.md).

The imported data goes to:

- the `gamedata/` folder next to the game, if that folder exists (a portable install);
- otherwise your user profile: `%APPDATA%\legacy-port\game\gamedata` on Windows,
  `~/.local/share/legacy-port/game/gamedata` on Linux,
  `~/Library/Application Support/legacy-port/game/gamedata` on macOS;
- or the folder named by `--data DIR` or the `CH_DATA` environment variable.

Command-line options: `--jp` / `--tw` pick a release; `--data DIR` sets the
data folder; `--import FILE...` imports and exits.

### In the browser

The web build runs entirely in your browser. Choose your files with **Add
game files…** or drop them onto the page; nothing is uploaded. The imported
data is kept in the browser's storage (IndexedDB), so you import only once
per browser. **Erase imported files** removes it; your saves are kept.

On the web, import `data.z` / `cd1` rather than whole disc images: the page
has to hold each file in memory while it reads it.

The Save and Load screens have **download** and **upload** buttons, so you can
back up a save or move one between browsers and the desktop version. Download
takes the selected save. Upload takes one or more `.SAV` files; you can also
drop them on those screens. Each file is checked first (size, version, checksum,
party lists and date) and refused with a reason if something is wrong. An upload
never replaces a save: one with a name already in use gets a number added.

## Building

You need CMake 3.20+, a C++20 compiler, SDL2, SDL2_ttf, SDL2_mixer (for
audio; `-DCH_AUDIO=OFF` builds without it), zlib, and optionally OpenSSL (for
`wss://` servers). Node.js 18+ is used by the network tests.

### Linux

```bash
sudo apt install build-essential cmake libsdl2-dev libsdl2-ttf-dev libsdl2-mixer-dev zlib1g-dev libssl-dev nodejs
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

```bash
./build/game
```

### macOS

```bash
brew install cmake sdl2 sdl2_ttf sdl2_mixer openssl node
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

### Windows

With Visual Studio 2022 and [vcpkg](https://vcpkg.io):

```bash
vcpkg install sdl2 sdl2-ttf "sdl2-mixer[mpg123]" zlib openssl --triplet x64-windows
```

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
```

```bash
cmake --build build --config Release
```

### Web

With the [Emscripten SDK](https://emscripten.org) active:

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web -j
```

Serve `build-web/` over HTTP (for example `python3 -m http.server -d build-web`)
and open `game.html`. The GitHub Actions workflow in `.github/workflows/build.yml`
builds every platform and publishes the web build to GitHub Pages from `main`.
To use it, enable Pages with the source set to "GitHub Actions".

### A portable folder

```bash
cmake --install build --prefix dist/cross-hermit
```

Create an empty `dist/cross-hermit/gamedata/` folder to keep the imported
files next to the game instead of in your profile.

### Tests

```bash
ctest --test-dir build --output-on-failure
```

The tests that need game files read an imported data folder: `gamedata/` in
the source tree, or `-DCH_TEST_DATA=DIR`. Without one they are skipped.

## Multiplayer

The multiplayer lobby talks to a relay server that keeps the battle's turn
clock. The builds from this repository have two public servers built in,
**Apinator US** (the default) and **Apinator EU** (see below). You can also
run a relay on your own machine, `ws://127.0.0.1:8787/`:

```bash
node tools/netserver/relay.mjs
```

To play over the internet, run `relay.mjs` on any host that Node.js runs on.
Then pick **Custom** on the server row of the multiplayer screen and enter
its `ws://` (or `wss://`, behind a TLS proxy) URL. `CH_RELAY=URL` sets the
server from the environment. The relay has no accounts and stores nothing.

### Apinator (optional)

The port can also use an [Apinator](https://apinator.io) app as its
transport. For each region, create `tools/netserver/apinator-<region>.env`:

```
APINATOR_KEY=...
APINATOR_CLUSTER=us
APINATOR_AUTH_URL=https://your-signer.example.workers.dev/
```

Then rebuild. The app secret stays on your signer:
`tools/netserver/apinator-auth.mjs` runs as a Cloudflare Worker or under
Node. Only the key, cluster and signer URL go into the game. `.env` files are
ignored by git.

The repository's own regions are in `apinator-us.public` and
`apinator-eu.public`: the same settings without any secret, committed so that
a fresh clone and the GitHub builds get both servers. A local `.env` for a
region takes the place of its `.public`, and CMake never reads a secret from a
`.public` file.

A region with no `APINATOR_AUTH_URL` is left out of the build unless you
configure with `-DCH_APINATOR_EMBED_SECRET=ON`. That option builds the secret
into the game, where anyone with a copy can read it, so never use it for a
build you share.

## How the import works

- **Archives.** The port reads the discs itself: ISO 9660 images, the Japanese
  disc's InstallShield 3 `data.z` (PKWARE DCL, a port of zlib's `blast`), the
  Chinese disc's T-TIME `cd1`, and the update's self-extracting ZIP.
  The Japanese 1.2 update's data files replace the disc's.
- **Storage.** Files that are the same in both releases are stored once
  (`common/`); the rest go under `jp/` or `tw/`.
- **Executables.** The game logic was rebuilt from a later Chinese build than
  the one on the disc, so it reads its tables at that build's addresses.
  `resources/ch_data.chpatch` recreates that build's data section from your
  executables:
  - most of it is copied from the Japanese 1.2 exe;
  - the Chinese text is copied from the Chinese disc's exe;
  - the patch itself holds only what neither exe has: pointer tables (the
    strings sit at other addresses), changed numbers and file names, and
    371 bytes of short Chinese labels that are missing from the disc's exe.

  The result is checked against a CRC of the original. Without the Chinese
  disc, its text stays empty; the Japanese release never reads it.
  `tools/make_ch_patch.py` builds the patch.
- **Text.** All game text and tables are read from your files when the game
  runs, by address; none are in this repository. Language packs key their
  translations by a hash of the Japanese, and the drafts that show the Japanese
  stay in the ignored `languages/work/`.

## Modding and translation (planned)

- **Translations:** language packs load today ([languages/README.md](languages/README.md)).
  `tools/export_text.py` exports the game's text from your imported files for
  translating, and seals the result so a pack holds no game text.
- **Mods:** an overlay folder whose files take the place of the imported
  ones (scripts, maps, sprites, music), so a mod ships only what it changes.

## Repository layout

| Path | Contents |
|---|---|
| `engine/` | SDL2 platform layer: window, input, audio, file system |
| `game/` | the game, ported from the original's functions |
| `game/import/` | the importer and the game files screen |
| `resources/` | files that ship with the port: the CJK font and the data patch |
| `web/shell.html` | the web page around the Emscripten build |
| `tools/netserver/` | the relay server and the Apinator signer |
| `tools/inventory/` | the porting progress report |
| `tests/` | unit, headless and network tests |

The port's own code is under the MIT License (`LICENSE`). The font is Noto Sans CJK, under the SIL Open Font License (`resources/fonts/`).
*Cross Hermit* and its data belong to their rights holders. This project ships
none of it, and you need your own copy to play.
