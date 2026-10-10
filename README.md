# The Sun: The Life of a Photon

A Windows screen saver, rendered with Vulkan, that flies from Pluto in to the Sun past the
planets, cuts the Sun open to follow one photon from the core to the surface, and flies back out.

The saver is built in this directory from [prompt.md](prompt.md). Research gathered from the web
goes to `C:\_C_Projects\The Sun References`.

## What is here so far

```
CMakeLists.txt                 the build: C++20, volk, resources, shaders -> embedded SPIR-V
cmake/EmbedShaders.cmake       SPIR-V files -> one generated C file
src/main.cpp                   stub wWinMain that exits 0
src/resource.h                 resource ids
src/render/shaders_embedded.*  look up an embedded shader by name
shaders/                       full-screen triangle, black placeholder fragment shader
assets/fonts/                  Michroma, the overlay font, and its OFL licence
third_party/volk/              Vulkan loader
the-sun.rc, the-sun.manifest   Settings-list name, embedded font and licence, DPI awareness
prompt.md                      the build prompt
```

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
