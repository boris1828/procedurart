# procedurart — project guide for Claude

A procedural-art generator written in C++. Each artwork ("piece") is its own compiled
program under `pieces/<name>/`, built on a shared `core` library. Pieces are *stills*: a
render is a pure function of (parameters, seed), so the same inputs always give the same
image. The first piece is **signrec**: closed or open rectilinear ("sign-like") outlines
grown from a starting state by adding random bumps.

The owner is Matteo Boroni Grazioli (boris1828 on GitHub). Write user-facing text in
English; the user writes in English or Italian. Keep answers short and concrete.

## Repository layout

```
CMakeLists.txt            top level: FetchContent deps, add_subdirectory(core), add_subdirectory(pieces)
cmake/Dependencies.cmake  raylib 5.5, Dear ImGui v1.92.9b, rlImGui main (imgui_rl static lib)
cmake/AddPiece.cmake      add_piece(<name> <sources...>) — one exe per piece, links `core`;
                          on Emscripten it emits an .html using web/shell.html
core/
  CMakeLists.txt          `core` static lib; picks src/app.cpp (desktop) or src/web_app.cpp (web)
  include/core/           public headers: canvas.h, piece.h, rng.h, palette.h
  src/app.cpp             desktop App: window, ImGui panels, sheet export, in-app viewer
  src/web_app.cpp         web App: no ImGui, exported C functions for the page
  src/canvas.cpp          Canvas implementation on raylib render textures
  src/palette.cpp         OKLCH palette generation
pieces/
  CMakeLists.txt          globs pieces/*/CMakeLists.txt
  README.md               short "how to add a piece" note
  signrec/
    CMakeLists.txt        add_piece(signrec main.cpp)
    main.cpp              Signrec piece: parameters, generation loop, colors, randomize, perturb
    geometry.h            Vec2, Shape (closed/open polyline), stroke_rect, rect_gap, fill, nudge
    spawner.h             Common (shared params view), Spawner interface, sel_slider helper
    spawners.h            RectSpawner (concentric rectangles), LineSpawner (parallel lines)
web/
  shell.html              the phone page: HTML/CSS/JS around the Emscripten module
  build.ps1               builds the web target and writes docs/ (stamps cache-busting URLs)
docs/                     GENERATED static site (index.html, signrec.js, signrec.wasm) — GitHub Pages
README.md                 project list with links to each piece's live page
.gitignore                build/, build-web/, out/, exports/, .vs/, imgui.ini
```

Git: branch `main`, remote `https://github.com/boris1828/procedurart.git`, GitHub Pages
serves `main` / `/docs`. Live signrec page: https://boris1828.github.io/procedurart/

## Toolchain and environment

- Windows 11, Visual Studio 2022/Build Tools (MSVC), CMake 4.x, Ninja (web build only).
- C++20, `CMAKE_CXX_EXTENSIONS OFF`. MSVC gets `/W4 /utf-8` on `core`.
- Emscripten SDK at `C:\Users\Work\emsdk` (installed, version latest). Activate per
  shell with `emsdk_env.ps1`; set `EMSDK_QUIET=1` first to silence it.
- The Bash tool on this machine has a broken PATH (no python/sed/grep). Prefer PowerShell
  or the Read/Edit/Grep tools. `python` on PATH is the Windows Store stub — use the
  Python bundled with emsdk: `C:\Users\Work\emsdk\python\3.13.3_64bit\python.exe`.
- `gh` (GitHub CLI) is **not** installed. Pushes go through plain `git push`.
- Locale of the shell output is Italian; error text may be Italian.

## Building and running

Desktop (ImGui app):

```powershell
cmake -S . -B build
cmake --build build --config Release --target signrec
build\bin\Release\signrec.exe
```

- If the exe is running, the link fails with LNK1104: stop it first
  (`Get-Process signrec | Stop-Process -Force`).
- Add a piece: create `pieces/<name>/CMakeLists.txt` with `add_piece(<name> main.cpp)`,
  re-run the configure step, then build `--target <name>`.

Web (phone page):

```powershell
.\web\build.ps1
```

- Configures `build-web/` with `emcmake`, `-DPLATFORM=Web`, Ninja, Release, builds the
  `signrec` target and copies the output to `docs/`. The HTML is stamped with a
  build-time version on `signrec.js` and `signrec.wasm`.
- Local test server (run from the project root, leave the window open):
  `C:\Users\Work\emsdk\python\3.13.3_64bit\python.exe -m http.server 8000 --directory docs`
  Phone on the same Wi-Fi: `http://<PC Wi-Fi IPv4>:8000`.
- Publishing = commit `docs/` and `git push`. Pages redeploys in about a minute.
- **Do not rebuild `docs/` or push the website unless the user asks.** Desktop-only work
  stays local.

## Core library API

### `core::Piece` (core/include/core/piece.h)

A piece overrides:

| Method | Purpose |
|---|---|
| `name()` | exe/window title and export file prefix |
| `width()` / `height()` | canvas size in px (default 1080x1080) |
| `ui()` | draw ImGui controls; return true if anything changed (desktop only) |
| `render(Canvas&, Rng&)` | draw; must be a pure function of (params, rng) |

Optional hooks used by the web page and tests (defaults do nothing):
`set_canvas_size(w,h)`, `randomize_all()`, `randomize_colors()`, `perturb()`,
`palette(Color*, max)`, `spawner_count()`, `spawner()`, `set_spawner(i)`.

`set_seed()`/`seed()` let `ui()` preview what a render will produce. The app sets the seed
before calling `ui()`.

Entry point: `int main() { MyPiece p; return core::run(p); }`.

### `core::Canvas` (canvas.h)

Backend-agnostic drawing surface, origin top-left, +y down. Methods: `clear`, `line`,
`polyline`, `bezier`, `circle`, `circle_outline`, `rect`, `rect_outline`, `triangle`,
`polygon` (convex, fan from centroid), `save_png`, `read_rgba(w,h)` (resized RGBA8 rows,
top row first), `show_in_imgui`. `begin()`/`end()` are private and only `App` calls them.

### `core::Rng` (rng.h)

PCG32. `reseed`, `next_u32`, `uniform()` in [0,1), `range(float,float)`,
`range(int,int)` **inclusive** (lo ≤ hi required — an inverted range is undefined), `chance(p)`.

### Palette (palette.h, palette.cpp)

`struct Palette { Color background; Color foreground[4]; int foreground_count; }`.
`random_palette(Rng&)` picks a harmony scheme (mono, analogous, complementary,
split-complementary, triadic, tetradic) in OKLCH, a dark or light background, and 2–4
foregrounds stratified across a lightness band away from the background. Gamut is
handled by bisecting chroma.

## The signrec piece

### Concept

Start from a set of **spawned shapes**, then run an extrusion loop: pick a random edge,
split it into `r1 | l | r2`, replace the middle `l` with three new edges pushed out by a
depth `d` (either direction). Accept only if all constraints hold; stop after
`target_successes_` bumps or `max_iters_` attempts.

### Spawners (the initial state)

`Spawner` (spawner.h) is the strategy interface:

- `name()`, `closed_shapes()`, `spawn(Common) -> vector<Shape>`
- `sanitize(Common)`, `max_margin(Common)`
- `ui(Common)`, `randomize(Rng&, Common, only_selected)`, `perturb(Rng&, Common)`

Registered spawners (spawners.h): `RectSpawner` (index 0) and `LineSpawner` (index 1).
They are listed in `Signrec::spawners()` in main.cpp. **To add a method:** write a
subclass in spawners.h, add a member to `Signrec`, and add it to `spawners()`.

- **RectSpawner:** 1–4 concentric closed rectangles. Parameters: `rects_`, `spacing_`
  (centerline distance between neighbours; disabled when rects < 2). The outermost sits at
  `inset`, and each next one is `spacing_` further in. Corners snap to the grid rounding
  inward, so the margin is never violated.
- **LineSpawner:** 1–20 open parallel lines, evenly spread across the axis perpendicular
  to them, with a single line centered. Parameters: `count_` (capped by what fits with
  width + epsilon between neighbours), `orientation_` (0 = horizontal, 1 = vertical),
  `h_margin_`, `v_margin_`. A margin along a line's own direction can be **0** (the line
  touches the border; the stroke's end cap sticks out by half the stroke width). Across the
  lines the margin is at least the common inset.

Spawners never draw; they return `Shape`s.

### Shared parameters (`Common`, and fields in `Signrec`)

Shared by every spawner: edge width (3–100), margin (0–300 slider, randomized 0–150),
l min (10–300), d min (10–300), d max (10–600), epsilon min (3–200), grid step (1–100),
successful iterations (1–500), max iterations (1–20000), "No layer collisions", "Fill
layers" + fill mode (Background / Edge / One color), palette, stroke/fill color seeds.

`Common` (spawner.h) exposes the shared values read-only to spawners, plus helpers:
`inset()` (margin + half edge width), `min_gap()` (edge width + epsilon), `room()`
(half the short side minus 50 px), and the grid snapping functions `snap_round`,
`snap_up`, `snap_down`, `snap_nearest`.

### Geometry rules

- Every edge is axis-aligned and drawn as a rectangle: the segment inflated by half the
  stroke width on every side, so ends are square (not round).
- Distance between two edges = `rect_gap` between their stroke rectangles (0 if touching).
- A new edge must keep at least `eps_min_` from every non-neighbouring edge of the same
  shape. With "No layer collisions" on, it must also keep `eps_min_` from every edge of the
  other shapes. Neighbours are skipped (adjacent edges share a vertex).
- For **open** shapes, the first and last edges are *not* neighbours (see `Shape::closed`).
- The bump's new vertices must stay inside the box inset by the margin.
- All vertices live on the grid. The extrusion works in grid units: `Lg`, `mg`
  (l min rounded up), `lk`, `r1k`, `dk`.
- Fills: `fill_orthogonal` in geometry.h scan-fills the interior of closed shapes. Lines
  are open, so the Fill controls are disabled for them.

### Known limitation (pre-existing, deliberate for now)

The **starting** shapes are not checked for epsilon among themselves. Grid snapping can
bring neighbouring rectangles or lines closer than epsilon. The extrusion only checks the
edges it creates. Any verification must exclude pairs of initial edges.

### Colors and seeds

- `colors_[0]` is the background, `colors_[1..n_fg_]` the foregrounds (n_fg_ = 2–4).
- Each shape's stroke color is `1 + stroke_rng.range(0, n_fg_-1)` from `stroke_seed_`.
- Fill color depends on mode: background (index 0), the stroke's own color, or one fixed
  foreground chosen by `fill_seed_`.
- Colors never touch geometry: stroke/fill seeds are separate from the geometry rng.
  "Randomize colors", "Swap colors" and the palette edit only change colors.

### Randomization

- `Randomize all`: randomizes everything (50 retries until the result has ≥1 bump).
- `Randomize selected` (desktop only): only the parameters whose checkbox is ticked.
  Spawner parameters have their own checkboxes inside the spawner.
- The **spawn method is never randomized** — it is chosen by hand.
- Randomize ranges are chosen so the result is coherent (d min ≤ d max, spacing within
  what fits, count within what fits).
- Every randomize/perturb ends with `sanitize()`, which keeps parameters valid:
  everything on the grid, rectangles dropped when they don't fit, lines capped, margins
  clamped by the spawner.

### Small perturbation

`perturb()` nudges parameters, never colors or the spawn method:

- Each numeric parameter has a 50% chance to change, by up to ±10% of its value, and at
  least one grid step (`nudge` in geometry.h).
- Rectangle count changes by ±1 with 50%, and the switches (no collisions, fill, fill
  mode) flip with 10%.
- If the result has no bumps, it retries up to 20 times, then restores the original.

## Desktop app (core/src/app.cpp)

- ImGui window "Controls" (440 px wide, left) and a "Canvas" window showing the render
  fit to the remaining area. Render-on-change: `dirty_` triggers a re-render.
- Seed controls: InputInt, Random seed, Re-render.
- Export: **Save PNG** (canvas as-is to `exports/<name>_<seed>.png`) and **Export NxM
  sheet** — a grid of consecutive seeds (`seed_ + n`, row by row), cells at a
  percentage of the canvas (`sheet_percent_`), saved to `exports/<name>_sheet_<seed>.png`.
  Sheet dimensions are sliders 2–10 for columns and rows, cell size 5–50%.
- After a sheet export, an in-app viewer opens over the canvas area with **Delete**
  (removes the file), **Open in system viewer**, and **Close**.
- Sheet rendering happens *outside* the ImGui frame (`sheet_requested_`) because it draws
  to offscreen textures.

## Web app (core/src/web_app.cpp, web/shell.html)

Architecture: `core/CMakeLists.txt` compiles `web_app.cpp` instead of `app.cpp` when
`EMSCRIPTEN` is set. The page owns all UI; the C++ side exposes plain `extern "C"` functions
(`web_resize`, `web_set_grid`, `web_randomize_all`, `web_randomize_colors`, `web_perturb`,
`web_set_spawner`, `web_spawner`, `web_spawner_count`, `web_palette_count`,
`web_palette_color`, `web_pixels`, `web_out_w`, `web_out_h`, `web_save_png`). Add new
functions with `EMSCRIPTEN_KEEPALIVE`.

- The canvas is rendered at 1080 px wide, with the height from the screen's aspect ratio
  (clamped 400–4000). Physical screen pixels are capped at 2800 px on the long side.
- Grid N×N: N² renders with consecutive seeds, each cell the screen size divided by N,
  composed into one RGBA buffer and drawn to a 2D canvas with `putImageData`.
- PNG download: `web_save_png` writes `/art.png` in the Emscripten MEMFS; the page reads
  it with `Module.FS.readFile` and triggers a download.
- `main()` must not return on the web: the piece would be destroyed while the page keeps
  calling it (RuntimeError: null function). `web_app.cpp` calls
  `emscripten_exit_with_live_runtime()` at the end of `start()`.
- `onRuntimeInitialized` fires **before** `main()` has created the piece in some cases.
  Any call into the piece must happen inside `act(...)`, which runs it after a timeout.
- Caching: `shell.html` passes `locateFile` with `?v=__BUILD__`, and `build.ps1` replaces
  `__BUILD__` and the `signrec.js` script URL with a timestamp. Without this, a phone can
  keep an old `.wasm` and run a stale page. `AddPiece.cmake` relinks when `shell.html`
  changes (`LINK_DEPENDS`).
- Layout: `#gl` is a hidden canvas (raylib needs one for its GL context). `#view` shows
  the image. The bottom bar holds: grid buttons + palette swatches, spawn method
  buttons (icons from `SPAWN_ICONS`), and Rand All / Rand Small / Rand Color / download.
  Tapping the image hides/shows the bar. Monospace font, 44 px touch targets.
- Emscripten link flags (add_piece): `-sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1
  -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=HEAPU8,FS`.

## Conventions

- Code style follows the surrounding code: 4-space indent, `snake_case` functions and
  variables, `kEnumValue` for enum constants, trailing `_` for members, short comments
  that explain *why*.
- Keep `render()` deterministic: all randomness comes from the `Rng` passed in, and
  color/geometry use separate seeds.
- Integer sliders for every parameter. Distances and sizes are multiples of the grid step.
- Warnings are errors in spirit: `core` builds with `/W4`. Keep the build warning-free.
- New spawners and pieces should not touch `core`; extend through the interfaces.
- Commit messages: a short imperative summary, a blank line, and a body if needed. End
  with the `Co-Authored-By:` trailer the harness specifies for the model in use.
- Do not commit generated build output (`build/`, `build-web/`, `exports/`).
  `docs/` is committed only when publishing the web version.

## Testing

There is no unit test suite. Verification so far:

- Builds with no warnings (`cmake --build`).
- Visual checks in the browser pane at 375×812 (`preview_start` / `navigate`), with
  JavaScript hashing of the pixel buffer to confirm that buttons change the image.
- Temporary stress tests for the generator, written as a throwaway piece folder that
  includes `signrec/main.cpp` with `#define private public` (after including the standard
  headers first, since MSVC forbids macroizing `private`). These check: vertices inside the
  margin box, grid alignment, axis alignment, epsilon between non-neighbouring edges
  (excluding starting-edge pairs), and bump counts over thousands of configurations.
  Delete the folder after the run; do not commit it.

When testing the web page, reset the viewport (`resize_window` with preset `desktop`)
afterwards, and stop the local server.

## Gotchas

- `ImGui::BeginDisabled` / `EndDisabled` must be balanced; the spawner UI calls them.
- `Rng::range(int,int)` is inclusive: `range(lo, hi)` with hi < lo is undefined. Always
  clamp the upper bound before calling it (see `randomize_once`, `LineSpawner::randomize`).
- The Signrec copy in `perturb()` (`const Signrec before = *this;`) copies the spawner
  objects too. Keep spawners copyable.
- LNK1104: a running `signrec.exe` locks the output; stop it before rebuilding.
- `docs/index.html` must keep the `?v=__BUILD__` replacement from `build.ps1`; editing
  `shell.html` by hand without rebuilding leaves the placeholder in place.
- `git` prints CRLF warnings on this machine. They are harmless.

## Not yet done / ideas

- Starting-state epsilon: make the spawners produce shapes that already satisfy epsilon
  (or check it when spawning).
- Phone page has no spawn-method randomization and no "Randomize selected" equivalent.
- Lines spawner: fill is disabled; a "fill between two lines" variant is an open question.
- Export of sheets on the web (only the grid of the current view is exported today).
