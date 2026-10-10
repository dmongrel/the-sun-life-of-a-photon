// Resource ids shared by the-sun.rc and the code that loads them.
#ifndef SUN_RESOURCE_H
#define SUN_RESOURCE_H

#define IDR_OVERLAY_FONT  201  // RCDATA: assets/fonts/Michroma-Regular.ttf
#define IDR_FONT_LICENSE  202  // RCDATA: assets/fonts/Michroma-OFL.txt, which must travel with it

// Planet maps (NASA / USGS, public domain; see THIRD_PARTY_NOTICES.md). Consecutive: the renderer
// loads IDR_MAP_FIRST + i for i in 0 .. MAP_COUNT - 1, in the order of `maps` in shaders/scene.frag.
#define IDR_MAP_FIRST     302
#define IDR_MAP_MARS      302  // RCDATA: assets/textures/mars.jpg
#define IDR_MAP_EARTH     303  // RCDATA: assets/textures/earth.jpg
#define IDR_MAP_CLOUDS    304  // RCDATA: assets/textures/earth_clouds.jpg
#define IDR_MAP_JUPITER   305  // RCDATA: assets/textures/jupiter.jpg
#define IDR_MAP_MERCURY   306  // RCDATA: assets/textures/mercury.jpg
#define IDR_MAP_NIGHT     307  // RCDATA: assets/textures/earth_night.jpg
#define MAP_COUNT         6

#endif
