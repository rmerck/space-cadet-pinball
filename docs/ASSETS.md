# Art and assets

All art is original and drawn by Python scripts in `tools/art` (skia-python, numpy, Pillow) from the same geometry the physics uses. No image editor, no generated painting and no pixels from outside the scripts are involved. The look is "rendered 3D" in the manner of early-1990s pre-rendered graphics: spot-lit deck plates, chrome, glossy plastic, cast shadows, an ordered dither, with pixel lettering in the display windows.

## What draws what

`pinball_layout` prints `src/core/TableLayout.hpp`, `src/core/PanelLayout.hpp` and `src/core/ScreenLayout.hpp` as JSON; every script reads that file (`build/layout.json`, or the path in `PINBALL_LAYOUT`), so a wall, lamp, window or button in the art is where the code has it.

| Files | Script | Notes |
|---|---|---|
| `table/background.png` | `make_table.py` | 1536 x 1920, 2 pixels per unit, opaque. Every wall, post, guide and chain of the layout; the unlit lamp inserts baked in at the positions and sizes the renderer uses. Lit lamps are drawn over it by the game |
| `sprites/*.png` | `make_sprites.py` | 4 pixels per unit, straight alpha, light from the upper left. `ball.png` is 32 units for a 24-unit ball (drawn at radius x 4/3, last, with its own rim and no halo); `bumper.png` is tinted under the untinted `bumper_cap.png`; `insert_arrow.png` and `insert_round.png` are tinted for lit lamps; `door.png`, `spinner_0.png` (edge-on) and `spinner_1.png` (flat), `kickback.png`, `bar.png` (turbo targets), `flipper.png`, `plunger.png`, `glow.png` (additive light, also the balls' shadow), `flare.png` (additive flasher burst) and `ring.png` (additive shock ring), the last three numpy-only; `button.png` (tinted) and `button_gloss.png` (its lights, untinted), both nine-part with 32-unit corners |
| `ui/title.png`, `panel.png`, `strip.png`, `deck.png`, `backdrop.png`, `dialog.png` | `make_ui.py` | 2 pixels per unit. The title is the only art that holds the game's name (from `layout.json`). The panel's and the strip's display windows are drawn where `strip::` and `panel::` (the single source) have them. The backdrop is a seamless 256-unit tile; the dialog card is nine-part with 48-unit corners |
| `packaging/ios/Assets.xcassets/AppIcon.appiconset/icon_1024.png`, `packaging/macos/icon_1024.png` | `make_icons.py` | The application icons, outside `assets` and the manifest. iOS: opaque RGB, full bleed. macOS: an 824-pixel rounded square on transparency; `tools/make_icns.sh` (macOS only) makes `packaging/macos/AppIcon.icns` from it |
| `src/generated/ArtPalette.hpp` | `gen_header.py` | The colours and constants the renderer shares with the art: lamp colours and lit levels, arrow aims, insert sizes, tints, glow ceiling, ball sprite size and shadow, the backdrop colour (`screen::kBackdropRgb`, and the iOS launch colour) |

`tools/art/palette.py` is the one source of colours. The renderer asserts at compile time that `ArtPalette.hpp` describes this table (one colour per lamp, seven arrow aims) and that its backdrop equals `screen::kBackdropRgb`.

The renderer (`src/platform/TableRenderer.cpp`) draws over the background, in order: the lit lamps and their glows, the spinner (sprite by phase) and the kickback, the turbo targets and the standing bay doors, the bumpers, slingshot flashes, the plunger, the flippers, and last the balls with their shadows. Which lamps are lit and which doors are marked comes from one adapter, `src/platform/TableLights.hpp`. Every glow is held to `art::kMaxGlowStrength` so that nothing outshines the ball. With the light show (`src/core/LightShow.*`, drawn by `src/platform/TableEffects.*`) the order gains the general illumination under the lamps, lamps at their own level in neon (`art::kLampNeon`), hit flashes and chases over the elements, and flasher domes, washes, sweeps, the flipper highlight and the balls' trails under the balls, with a glint on each ball after it; the balls stay last but for that glint, each with its dark rim, and flashers are held to `art::kFlasherStrengthMax`.

When the build has no `table/background.png` the game draws the table from its layout in plain colours instead (walls, guides, lamps, switches, holes, rails), into a texture made once, with the lit lamps, dropped doors and the spinner's blade drawn over it; the sprites are the same. Pause, About and the initials prompt use the dialog card, the title and the button art where the build has them, and plain frames, lettering and buttons where it has not.

## How to regenerate

```sh
python3 -m venv build/art-venv
build/art-venv/bin/python -m pip install --require-hashes -r tools/art/requirements.txt
cmake --build build --target pinball_layout && build/pinball_layout > build/layout.json
build/art-venv/bin/python tools/art/make_all.py      # into the tree; or make_all.py <scratch_root>
python3 tools/check_assets.py assets
python3 tools/art/check_sums.py                      # or check_sums.py <scratch_root>
```

`make_all.py` writes every asset, both icons and `ArtPalette.hpp`. Each generator can be run alone with an output path; `make_table.py` bakes the inserts from `assets/sprites`, so `make_sprites.py` runs first. A lamp that moves in `TableLayout.hpp` needs the table drawn again; so does any change to a window of the strip or the panel.

Ball visibility is checked by `tools/art/visibility_full.py` (it writes `build/art-review/visibility_full.md` and `.png`): every lamp lit, every bumper flashing, the doors up and marked, the ball at every reachable place on a 9-unit grid, in both lock bays, at both rail landings and along the rails, at five device scales from a small iPhone to a 13-inch iPad. Rule: the ball's weakest edge at least 3:1 against what is round it everywhere.

## Reproducibility

The scripts use fixed seeds and write PNGs with IHDR, IDAT and IEND chunks only (no timestamps, text or profiles). `tools/art/requirements.txt` pins skia-python, pybind11, numpy and Pillow by version and by the SHA-256 of each wheel (Linux x86_64 and macOS arm64, CPython 3.13 and 3.14); `--require-hashes` makes pip refuse anything else.

The committed files are the single source of the art; regenerating is a check, not a build step, and nothing in a build runs Python to draw. `tools/art/SHA256SUMS` records their hashes, and ctest `art_sums` holds the committed files to it.

- Linux x86_64: regeneration is byte-identical to the committed files, with CPython 3.13 and 3.14, from fresh virtual environments installed with `--require-hashes`.
- macOS arm64 (macOS 27.2, CPython 3.14, same pinned wheels): regeneration is deterministic (two runs give the same bytes) but not byte-identical to Linux. Every file skia rasterises differs in anti-aliased edges and dither thresholds (in the table background at most 14 levels in 7.6 percent of pixels; in sprites mostly in the colour of nearly transparent edge pixels); `glow.png` (numpy only) and `ArtPalette.hpp` are identical. So the Linux x86_64 output is the reference: on a Mac, check a regeneration for stability and by eye, not against `SHA256SUMS`.

ctest `art_palette` checks `src/generated/ArtPalette.hpp` and the iOS launch colour against `palette.py`; it needs only Python.

To look at the result without opening a window: `cd build && ./pinball_snapshot 1200 frame.png [state] [--layout preset]` plays 1200 steps and writes the frame to the current directory. States include `pause`, `about`, `initials`, `locked` and `bankdown`; presets include `phone`, `phone-pro`, `pad-portrait` and `pad-landscape`. `pinball_snapshot 1200 x.png check --layout <preset>` (ctest `layout_check_*`) fails unless every control, window and dialog is really drawn: lettering in the display windows and the initials is looked for in the colours it is drawn in, captions in either caption colour (`kCaptionPlain`, `kCaptionOnArt`).

## Integrity

Assets are external files, so the game checks them before it runs:

- At build time CMake records the size and SHA-256 of every PNG under `assets/`, applying the same name and size rules the game enforces, and compiles that manifest into the executable (`cmake/GenerateAssetManifest.cmake`).
- At startup every listed asset is read into memory and compared with the manifest. Any missing, resized or altered file, or anything that is not a regular file, stops the game with exit code 1 before a window opens.
- Each image is then decoded from the same bytes that were hashed, so a file cannot be swapped between the check and the use.
- The manifest is also the allowlist. A file that is not in it is never opened.
- `ctest` runs `tools/test_asset_integrity.py`, which flips a bit, appends, truncates, empties and deletes each asset, and replaces it with a FIFO, a symbolic link to a genuine copy and a directory, and confirms every case is refused.

What this does not cover: the hashes live in the executable, so someone able to replace the executable can replace them too. That is the job of code signing at release time (`codesign` and notarisation on macOS), which protects the executable and with it the manifest. A signed macOS bundle also seals the asset files themselves, but the system checks that seal only when Gatekeeper assesses the app, not at every start; the game's own check runs every time.

The game only opens files inside the `assets` directory next to the executable (`Contents/Resources/assets` in the macOS bundle: the fixed directory under SDL's base path). Files over 16 MB or images over 4096 pixels on an edge are refused before decoding.

Assets are exported with all metadata off. `tools/check_assets.py` (run by `ctest`) fails if a PNG carries EXIF, text, timestamp, profile or thumbnail chunks, is malformed, or if anything but a regular `.png` file is in `assets`.
