# The Sun: The Life of a Photon

A Windows screen saver, rendered with Vulkan, that flies from Pluto in to the Sun past the
planets, cuts the Sun open to follow one photon from the core to the surface, and flies back out.

The saver is built in this directory from [prompt.md](prompt.md). Research gathered from the web
goes to `C:\_C_Projects\The Sun References`.

## The loop

About eleven minutes (680 s at speed 1), every frame a pure function of the loop time:

| Phase | Loop seconds | What happens |
|---|---|---|
| 1. In | 0 – 200 | From beyond Pluto, the Sun a bright star, past Pluto, Neptune, Uranus, Saturn, Jupiter, Mars, Earth, Venus and Mercury. Each planet comes up dark side first; the camera turns to hold it as it passes, so its lit side comes round, then turns back to the Sun. |
| 2. Arrival | 200 – 222 | Into an orbit that turns with the Sun. The credit fades in, lower right. |
| 3. The photon | 222 – 440 | A 135° wedge opens. The photon's path is drawn as crawling ants, from the core out, with a label for each layer and how long the energy spends there, and two lines of caption. The wedge closes. |
| 4. Out | 440 – 680 | The flight in, backwards: lit side first, dark side last, until Pluto is gone. Then it repeats. |

The whole timetable is one table in [src/sim/schedule.cpp](src/sim/schedule.cpp).

## What is here

```
src/main.cpp, src/app/         arguments, windows per monitor, input gate, the loop, log
src/sim/schedule.*             the timetable
src/sim/system.*               planets, the route, camera, Sun spin, wedge, photon path, eruptions
src/render/renderer.*          Vulkan: scene, particles, bloom, tonemap, overlay, captures
src/render/overlay.*           Michroma text atlas; planet names, title, photon path, labels, captions
shaders/scene.frag             ray traced: starfield, mapped and procedural planets, rings, the cut Sun, corona
assets/textures/               NASA / USGS planet maps, sized for a 4K screen
shaders/particles.vert         790,000 stateless particles: surface flow, interior, loops, CMEs
tools/capture.sh, bench.sh     one frame at a loop time; GPU timings
```

## Running and testing

Install by copying `build/the-sun.scr` to `C:\Windows\System32` (or right-click > Install).
Test it in a window from PowerShell:

```powershell
& ".\build\the-sun.scr" /w
```

Environment variables:

| Variable | Effect |
|---|---|
| `SUN_PHOTON_SPEED` | speed factor for the whole loop (default 1; 0.05 – 20) |
| `SUN_PHOTON_TIME` | loop time to start at, seconds |
| `SUN_PHOTON_CAPTURE` | `/w` only: write the frame at `SUN_PHOTON_CAPTURE_AT` (loop seconds) to this PNG, then exit |
| `SUN_PHOTON_OVERLAYS` | draw whatever overlay the moment has at full strength |
| `SUN_PHOTON_LOG` | `1` writes `%TEMP%\the-sun.log`; any other value is the path |
| `SUN_PHOTON_OFFSCREEN` | render without presenting (captures and benchmarks) |
| `SUN_PHOTON_VALIDATION` | Khronos validation layer, if the loader can find it. MSYS2's `vulkan-validation-layers` is not registered with Windows: also set `VK_ADD_LAYER_PATH=C:\msys64\ucrt64\bin` |

## Liberties taken

Distances are squeezed (Pluto at 330 solar radii, not 8,500) and planets enlarged (radius grows as
true radius^0.6), or nothing would be visible for nearly the whole trip. Granules are drawn ~10×
life size. The Sun is drawn the way SDO's 304 Å images show it, not at its real brightness. Planets
are NASA and USGS maps where they exist (Earth with its city lights, Mars, Jupiter, Mercury) and
procedural elsewhere. Pluto is procedural too, painted after New Horizons' images with its features
where they are, and held still with its heart sunward; Charon hangs over its far face.

26 moons: each planet's closest ten that are big enough to be round, plus Phobos and Deimos as
their triaxial ellipsoids, and Pan in the Encke gap. They are sized like the planets; orbits
beyond four planet radii are squeezed by a square root, and all are circular, at their true
periods relative to Earth's day. The Moon, Charon, Phobos and Deimos are painted procedurally from
NASA's images; the rest are plain white for now. Saturn has a faint E ring along Enceladus's
orbit. The core / radiative / tachocline times are an estimate; see
`The Sun References\physics\photon-time-by-layer.md`.

## Building

MSYS2 UCRT64 (`gcc`, `g++`, `windres`, `glslc` from `mingw-w64-ucrt-x86_64-shaderc`) on `PATH`,
plus CMake and Ninja (CLion's bundled copies work):

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

This builds `build/the-sun.scr`.

## License

MIT. See [LICENSE](LICENSE). Third-party work: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
