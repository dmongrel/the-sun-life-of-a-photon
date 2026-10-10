# The Milky Way

A Windows screen saver of the Milky Way, rendered with Vulkan, and a testbed for judging how well
different AI models write code.

This directory is the **template**. It holds the build, the third-party dependencies and the
prompt, and no screen-saver code of its own. Each model under test copies it to
`C:\_C_Projects\The Milky Way-<LLM NAME>` and builds the saver there from
[prompt.md](prompt.md), so every entry starts from the same files and can be compared.

## Running a model

1. Replace every `<LLM NAME>` in [prompt.md](prompt.md) with the model's name. `qwen122b` gives
   the project `C:\_C_Projects\The Milky Way-qwen122b`.
2. Give the model the whole prompt, unchanged otherwise.

The prompt tells the model to copy this template, work autonomously, build the galaxy simulation
with local git only, verify it with frame captures, and report. Entries are pushed to GitHub at
the end, if they earn it.

Research the models gather from the web goes to the shared directory
`C:\_C_Projects\The Milky Way References`, so every model can use it.

## What the template contains

```
CMakeLists.txt                 the build: C++20, volk, resources, shaders -> embedded SPIR-V
cmake/EmbedShaders.cmake       SPIR-V files -> one generated C file
src/main.cpp                   stub wWinMain that exits 0
src/resource.h                 resource ids
src/render/shaders_embedded.*  look up an embedded shader by name
shaders/                       full-screen triangle, black placeholder fragment shader
assets/fonts/                  Michroma, the overlay font, and its OFL licence
third_party/volk/              Vulkan loader
the-milky-way.rc, .manifest    Settings-list name, embedded font and licence, DPI awareness
prompt.md                      the build prompt
```

## Building

MSYS2 UCRT64 (`gcc`, `g++`, `windres`, `glslc` from `mingw-w64-ucrt-x86_64-shaderc`) on `PATH`,
plus CMake and Ninja (CLion's bundled copies work):

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The template builds `build/the-milky-way.scr`, which does nothing but exit.

## License

MIT. See [LICENSE](LICENSE). Third-party work: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
