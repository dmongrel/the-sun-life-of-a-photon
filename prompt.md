# The Sun: The Life of a Photon — build prompt

You are building a Windows screen saver called **The Sun: The Life of a Photon**. It flies from
Pluto in to the Sun past the planets, opens the Sun with a cut-away wedge, follows one photon from
the core to the surface, then backs slowly out of the solar system and starts again. What counts
is the quality of the code, its correctness, how good the result looks, and how faithfully it
follows this specification. Read the whole prompt before you do anything.

## 0. How to work

- **Work autonomously.** Nobody will answer questions during the run. When something is unclear,
  decide it yourself with your best judgement, note the decision, and carry on. List those
  decisions in your final report.
- **Physics and astronomy questions go to the web, not to the user.** The user is not a physicist
  and cannot answer them. Search, read the sources, and save what you learn (section 1).
- **Aesthetics come first, but do not throw reality away.** Where accuracy and beauty conflict,
  favour beauty, keep the result plausible, and say in your report what you changed and why.
- **Tests: use them sparingly.** This is a screen saver, not a production application. Write a
  test only when it proves something you need to be true, such as the input rules that let a
  person dismiss the saver.
- **Benchmarks and profiling: use them when there is a real choice.** When two ways of doing
  something could each work, measure both on this machine and keep the faster one. Report what
  you measured.
- **Look at what you make.** Give the saver a way to write a frame to an image file and exit on
  its own (section 5), and use it to check your work as you go.

## 1. The project and the references

Build the saver in this directory, `C:\_C_Projects\The Sun - Life of a Photon`. It is already a git
repository on `main` (section 6).

The research directory is:

    C:\_C_Projects\The Sun References

**Read it before you search the web**: it already holds research on the photon's journey, the
Sun's layers, its rotation, its surface and the planets' distances
(`physics/life-of-a-photon.md`). Everything you gather from the web goes there, not only into the
project:

- Notes as Markdown (`.md`) or plain text, with the source URL and the date for every fact.
- Images and data files as downloaded, with their source URL, credit line and licence recorded
  next to them.
- One line per file in `INDEX.md`: the file, what it is, where it came from.
- Add to the directory; do not delete or rewrite what is there. If you disagree with a note, add
  your own saying why.

You may read any directory under `C:\_C_Projects`. Useful ones:

- `C:\_C_Projects\The Milky Way References`: NASA sky images with their credits in `imagery/`,
  and physics notes.
- `C:\_C_Projects\The Milky Way-claude`: a finished Vulkan screen saver built on the same template,
  with the same toolchain, overlays in Michroma, frame capture and input rules. Learn from it and
  reuse its approach where it fits, but write this saver's code for this saver. Do not change it.

Copy what the build needs from the references into `assets/`, and credit every third-party image
in `THIRD_PARTY_NOTICES.md`.

## 2. What is here already

| Path                                   | What it is                                                     |
|----------------------------------------|----------------------------------------------------------------|
| `CMakeLists.txt`                       | The build: compiles `src/**/*.cpp`, volk and the resource script; compiles every shader in `shaders/` with `glslc -Werror`; embeds the SPIR-V. |
| `cmake/EmbedShaders.cmake`             | Turns the SPIR-V files into `build/gen/shaders_generated.c`.   |
| `src/render/shaders_embedded.{h,cpp}`  | `shader_blob("sun.frag")` returns an embedded blob, or null. Names have no `.spv`. |
| `shaders/fullscreen.vert`              | A full-screen triangle generated from `gl_VertexIndex`.        |
| `shaders/sun.frag`                     | Placeholder that outputs opaque black.                         |
| `third_party/volk/`                    | The volk Vulkan loader, unmodified.                            |
| `assets/fonts/`                        | Michroma and its SIL Open Font License, unmodified.            |
| `the-sun.rc`, `src/resource.h`         | The Settings-list name, the font and licence as `RCDATA`, the manifest. |
| `the-sun.manifest`                     | Per-monitor-v2 DPI awareness, Common Controls v6.              |
| `src/main.cpp`                         | A stub `wWinMain` that exits 0. Replace it.                    |
| `LICENSE`, `THIRD_PARTY_NOTICES.md`, `.gitignore` | Keep them; update them as what you add requires.    |

Treat these files as yours: change, add or replace them as your design needs, but keep
every decision in section 3.

## 3. Technical decisions (fixed, not yours to change)

| Area            | Decision                                                                          |
|-----------------|-----------------------------------------------------------------------------------|
| Language        | C++20. C only for vendored third-party code.                                      |
| Platform        | Win32 API, Unicode throughout (`wWinMain`, `-municode`, `UNICODE`/`_UNICODE`).    |
| Toolchain       | MSYS2 UCRT64 `gcc`, `g++`, `windres` and `glslc`, in `C:\msys64\ucrt64\bin`.     |
| Build system    | CMake with the Ninja generator. CMake and Ninja are CLion's: `%LOCALAPPDATA%\Programs\CLion\bin\cmake\win\x64\bin` and `%LOCALAPPDATA%\Programs\CLion\bin\ninja\win\x64`. |
| Output          | One statically linked GUI executable, `build/the-sun.scr`.                       |
| Graphics API    | Vulkan, loaded at run time through volk with `VK_NO_PROTOTYPES`. Nothing may import `vulkan-1.dll` statically. |
| Shaders         | GLSL, compiled at build time and embedded. Never read from disk at run time.      |
| Assets          | Embedded in the `.scr` as resources. The saver reads no files at run time.        |
| Third-party code | Only volk. No zlib, no image library: decode embedded images and write capture PNGs with your own code (a PNG can be written with stored, uncompressed deflate blocks; a JPEG or PNG asset may instead be converted at build time to a raw format you embed). |
| Overlay font    | Michroma, for any text drawn on screen, loaded from the embedded resource with `AddFontMemResourceEx`; never installed. |
| Warnings        | `-Wall -Wextra`. The build must produce zero warnings.                            |
| License         | MIT, `Copyright (c) 2026 dmongrel`.                                               |

The machine is an AMD Strix Halo with an integrated Radeon GPU. The saver should run smoothly at
the display's refresh rate there.

## 4. What to build

The saver is one loop of four phases, repeated forever. Choose the length of each phase for the
best pacing; as a starting point, aim for a loop of about 10 to 12 minutes: about 3 minutes in, about
4 minutes at the Sun, about 3 to 4 minutes out. Report the lengths you chose.

### Phase 1: the approach, Pluto to the Sun

- **Start.** A starfield fills the sky. Either use one of the NASA images in the references
  (check what each one actually shows; a deep field is galaxies, not stars), or render the stars
  yourself as points. The Sun is a tiny bright dot, as it looks from Pluto.
- **Route.** The camera flies past Pluto, then travels in toward the Sun past **Neptune, Uranus,
  Saturn, Jupiter, Mars, Earth, Venus and Mercury**, in that order, and arrives at the Sun. The
  path need not be a straight line: let it waver a little, so the movement keeps the view
  interesting.
- **Looking at each planet on the way in.** Flying in, the camera faces the Sun, so each planet
  ahead shows its dark side. As the camera passes a planet, it turns to keep the planet in view,
  so the planet's lit side comes round into sight during the flyby. When the planet is almost out
  of view, the camera turns back toward the Sun and flies on to the next one. Every turn is
  smooth; nothing snaps.
- **Scale.** Real distances span 40 AU, and real planets would be invisible specks for nearly the
  whole trip. Compress distances and vary the camera's speed so each flyby is a visible event and
  the Sun grows from a dot to a disk. Place each planet on its own orbit, at whatever orbital
  position makes the route work. Say in your report how you scaled distance, size and speed.
- **Planets.** Each planet is a sphere wrapped in suitable NASA imagery (public domain or
  credited; record the source and licence in the references directory), unless a procedural
  surface is convincing enough to replace it. Light each one from the Sun. Give Saturn's rings,
  Jupiter's bands and Earth's clouds the look people expect.
- **Planet labels.** Whenever a planet is on screen and close enough to recognise, show its name
  in Michroma, attached to the planet in 3D so it moves with it. **No underline.** Labels fade in
  and out; nothing pops. This applies on the way back out too.

### Phase 2: the Sun

- **Arrival.** The camera settles into a **Sun-stationary orbit**: it moves with the Sun's
  rotation, so the same face of the Sun stays toward the camera. The Sun rotates
  counter-clockwise seen from above its north pole, and differentially (faster at the equator than
  at the poles); see the references. Speed the rotation up enough that the surface visibly
  swirls.
- **Not real brightness.** Do not draw the Sun as a blinding white disk. Draw it the way famous
  solar videos show it: the granulation of the Inouye (DKIST) first-light video, or the false
  colour of SDO/AIA (304 Å orange-red, 171 Å gold), with limb darkening. Choose the look; say
  which and why.
- **Swirling particles.** The body of the Sun, its surface and its interior, is drawn with
  swirling particles. Start with **790,000 particles**, the count used by The Milky Way screen
  saver, and change it only if a benchmark on this machine shows you should. Report the final
  count and the measurement.
- **The title.** When the camera reaches the Sun, show "The Sun: The Life of a Photon" in large
  Michroma, **underlined**, right-aligned in the lower-right corner, fixed to the screen like a
  film credit. It fades in, stays for about 10 seconds, and fades out. Phase 3 begins once the
  title has faded.

### Phase 3: the life of a photon

- **The cut-away.** Cut a wedge of about **135 degrees** out of the Sun, centred on a vertical
  axis through the Sun's centre (its rotation axis), facing the camera. The cut leaves two flat
  faces, each running from the axis at the centre out to the surface, through which the interior
  shows. The wedge is fixed in the Sun's rotating frame, and the camera rotates with it, so it
  stays open toward the camera. Open it gradually; nothing pops.
- **The interior.** On the faces, show the layers the photon passes through: core, radiative
  zone, tachocline, convective zone and photosphere, at their real relative radii (see the
  references), each with its own motion in the particles. The core glows hottest; the radiative
  zone barely moves; the convective zone boils in rising and sinking cells; the surface shows
  granulation. Colour by temperature in the same style as the surface.
- **The photon.** A real photon is too small to see. Show it as a **crawling-ants line**: a dashed
  line whose dashes crawl in the photon's direction of travel, drawn on or just in front of one
  of the cut faces.
  - Where the motion is back and forth, as in the random walk of the core and the radiative zone,
    and the rising and sinking of the convective zone, draw **two parallel lines with ants
    crawling in opposite directions**.
  - Where the motion is one way, as when the photon finally leaves the photosphere, draw a single
    line.
  - The path makes net progress outward from the centre to the surface, then out into space.
- **What the photon really is.** Fusion in the core produces gamma rays; the energy is absorbed
  and re-emitted countless times on the way out and leaves as visible light; in the convective
  zone it is carried by moving plasma. The single photon on screen stands in for that energy.
  Keep the story true to this; do not claim one photon survives the whole trip.
- **The photon labels.** While the photon is in a layer, show a label in Michroma with the
  layer's name and how long the photon stays there (for example "Radiative zone" and
  "~170,000 years"). A **leader line** runs from the label to the photon and follows it. Use the
  unit that fits each stage: years in the core and radiative zone, days in the convective zone,
  seconds or "moments" at the photosphere, and about 8 minutes 20 seconds to Earth if you show
  the photon leaving. Labels fade in and out as the photon crosses from one layer to the next.
- **The explanation.** As the photon's path is revealed, explain what is happening in at most
  **two lines** of Michroma text at a time, each line up to the width of the screen,
  left-justified along the bottom edge of the screen. Explain what the photon is doing in each
  stage (created by fusion as a gamma ray, absorbed and re-emitted in a random walk, carried up
  by convection, escaping from the photosphere), in plain words for someone who is not a
  physicist. Each caption fades in, stays long enough to read comfortably, and fades out before
  the next.
- **The times.** Sources disagree, from about 10,000 to over a million years for the radiative
  part (the references list them). Mitalas and Sills' ~170,000 years to the base of the
  convective zone is the standard figure. No source found so far splits that time between the
  core and the radiative zone: find one, or estimate the split from a standard solar model, and
  say in your report which values you show and where they come from.
- **Pacing.** The photon's trip on screen is a dramatisation; its on-screen time per layer need
  not be proportional to the real time. The labels carry the real numbers.

### Phase 4: the departure

- The wedge closes, and the camera slowly backs out of the solar system the way it came, flying
  backwards and facing the Sun, past the planets (labelled again), until it is beyond Pluto.
- **Looking at each planet on the way out** is the approach in reverse. The camera stays on the
  Sun until it nears a planet, whose lit side then faces it. It turns to view the planet and keeps
  turning through the flyby, so the planet's dark side comes round into view as it falls behind.
  Then it turns back to the Sun and keeps flying backwards to the next planet out. As on the way
  in, the path may waver, and every turn is smooth.
- The phase, and the loop, ends when Pluto is out of sight. Then the animation starts again from
  phase 1.

### Overlays in general

All overlay text is set in Michroma, from the embedded font (section 3). All lines (underlines,
leader lines, the crawling-ants lines) are drawn as lines, never as text characters. Choose the
sizes, colours, line weights and fade times for the best look, and keep the overlays
understated: the Sun and the planets are the subject. Give `/w` a way to jump to a chosen time in
the loop and to force the title, labels and captions on, so captures can show every phase.

## 5. Screen-saver behaviour

Set the name Windows lists in Screen Saver Settings (string 1 in the resource script) to
`The Sun: The Life of a Photon` (already set).

Keep `main.cpp` to argument parsing and dispatch. Windows starts a screen saver with one of these
arguments. Accept `/` or `-` as the prefix and treat the letter case-insensitively.

| Argument                    | Behaviour                                                                 |
|-----------------------------|---------------------------------------------------------------------------|
| none, `/s`                  | Full screen: one borderless top-most window per monitor, cursor hidden.   |
| `/p <hwnd>`, `/p:<hwnd>`    | Preview: the saver, live, in a child window filling the given parent's client area. Exit when the parent window disappears. Exit 0 if the handle is missing or invalid. |
| `/c`, `/c:<hwnd>`           | Settings: a message box, owned by the window if one is given, saying there are no settings. |
| `/w`                        | Development: an ordinary resizable 1600×900 window. Esc or closing it exits. |
| anything else               | Exit immediately with code 0, showing nothing.                            |

Rules for ending a full-screen run:

- Any key press, any mouse button, or mouse movement of more than 8 pixels ends the run.
- Ignore all input for the first 0.5 seconds; the key that launched the saver is often still held.
- A key already held when that grace period ends does not count until it has been released.
- Input must end the run even when no window of yours has focus, so poll keyboard and cursor
  state each frame as well as handling window messages.
- A failure to read input (for example `GetCursorPos` failing on another desktop) is not input.

Until the first frame is presented, every window paints black. A machine without a Vulkan driver
must still run the saver, showing black. A screen saver must never show an error dialog in any
mode except `/c`.

Logging: when the environment variable `SUN_PHOTON_LOG` is `1`, write a timestamped trace to
`%TEMP%\the-sun.log`; any other non-empty value is the log file's path. With it unset,
write nothing. Log the `VkResult` of every Vulkan call that can fail.

Frame capture: when `SUN_PHOTON_CAPTURE` names a PNG file, the `/w` window writes the frame shown
at `SUN_PHOTON_CAPTURE_AT` seconds of loop time (default 5) to that file and exits. With it unset,
nothing changes. The capture must be the rendered frame, read back from the GPU.

## 6. Git

Git is local only. Do not create a GitHub repository, add a remote, or push; the user does that
when the project is ready.

1. The repository already exists, on `main`, with the identity set for this repository only
   (`dmongrel`, `deathmongrel@gmail.com`). Do not re-initialise it or change global git
   configuration.
2. Commit as you go, one commit per coherent step, on `main`. Do not commit build outputs. Each
   message is a short summary line plus a body saying what changed and why, and claims only what
   you built and verified. Do not add `Co-Authored-By`, "Generated by", or any other
   AI-attribution line.

## 7. Verify before you report

Run each check and report its actual output. Fix and re-run anything that fails.

1. With `C:\msys64\ucrt64\bin` and CLion's CMake and Ninja directories first on `PATH`,
   `cmake -S . -B build -G Ninja` and `cmake --build build` succeed from a fresh `build`
   directory with **zero** warnings, and `build/the-sun.scr` exists.
2. Starting the `.scr` with the argument `/x` exits with code 0 within a few seconds.
3. Frame captures from `/w`, which you have opened and looked at, showing: the starfield with the
   Sun as a dot from near Pluto; each planet's flyby with its label; the Sun on arrival with the
   title; the open wedge with the layers; the photon's crawling-ants line in at least the core,
   the radiative zone and the convective zone, each with its label and leader line; an
   explanation caption at the bottom left; a planet turning from dark side to lit side during a
   flyby, in and out; and the departure past Pluto. Keep them in `docs/captures/` and commit them.
4. Two captures a second or so apart that show the ants crawling and a label following the photon.
5. The frame rate in `/w` at 1600×900 in each phase, from the log, with the particle count.
6. `git status` is clean, and `git log` shows your commits on `main`.

Never start the `.scr` with `Start-Process`, `ShellExecute`, `start`, `Invoke-Item` or a
double-click: Windows adds `/S` to a `.scr` started that way, and it takes over the screen. Call it
directly. Do **not** run it with no argument, `/s`, `/p` or `/c`, and do not run `/w` without
`SUN_PHOTON_CAPTURE` set: those take over the screen or need a person at the keyboard.

## 8. Report

Finish with a short report:

- The commits you made.
- Each verification check and its result.
- The phase lengths, and how you scaled distance, size and speed on the route.
- The Sun's look you chose, the particle count, and how the particles move in each layer.
- The time you show for each layer and the source of each, including how you split the core
  from the radiative zone.
- How the overlays are drawn: the text, the leader lines, the crawling ants, and the 3D anchoring
  of the planet labels.
- The benchmarks you ran and what they decided.
- What you saved to the references directory, and every image you embedded with its credit.
- Every decision you made on your own, every place you departed from this specification, and
  where you chose beauty over accuracy, with the reason for each.
