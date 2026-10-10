# Third-party notices

The Sun: The Life of a Photon builds two pieces of third-party work into `the-sun.scr`: the Michroma
typeface and the volk Vulkan loader.

## Michroma

Copyright 2011 The Michroma Project Authors (https://github.com/googlefonts/Michroma-font)

Licensed under the SIL Open Font License, Version 1.1. The full licence is
`assets/fonts/Michroma-OFL.txt`. It is embedded in the `.scr` as a resource next to the font,
because OFL section 2 requires the licence to travel with the font, and a licence left in the
repository would not reach anyone who has only the screen saver.

The font sets every on-screen overlay. The file is version 1.100,
taken unmodified from `google/fonts@main/ofl/michroma`:

    file    assets/fonts/Michroma-Regular.ttf
    sha256  b62301163788bc5b7f8fcac0b74b184e34e1827e577b499ecb724da065098f87
    bytes   64344

The font has not been subsetted, renamed or otherwise modified. It declares no Reserved Font Name.

## volk

Copyright (c) 2018-2026 Arseny Kapoulkine (https://github.com/zeux/volk). MIT License; the
licence text is `third_party/volk/LICENSE.md`. volk is vendored unmodified and compiled into the
`.scr`. It loads `vulkan-1.dll` at run time.

## Planet maps

Embedded in the `.scr` as RCDATA resources (`assets/textures/`), resized with Lanczos filtering
to the largest size each planet reaches on a 4K screen (about π × its peak on-screen diameter),
and re-encoded as JPEG. Clouds and Mercury are stored greyscale. Works of NASA and the USGS are
in the public domain in the United States; credits are given as the sources ask.

| File | Size | Source | Credit |
|---|---|---|---|
| `earth.jpg` | 4096×2048 | Blue Marble Next Generation, July 2004, topography and bathymetry, `world.topo.bathy.200407.3x5400x2700.jpg` (eoimages.gsfc.nasa.gov, record 73751) | NASA Earth Observatory, Reto Stöckli |
| `earth_night.jpg` | 2048×1024, grey | Black Marble 2016 city lights, `BlackMarble_2016_01deg.jpg` (eoimages.gsfc.nasa.gov, record 144898), greyed and thresholded to the lights alone; taken from The Black Hole saver | NASA Earth Observatory, Joshua Stevens, from Suomi NPP VIIRS data by Miguel Román, NASA GSFC |
| `earth_clouds.jpg` | 4096×2048 | Blue Marble clouds, `cloud_combined_8192.tif` (eoimages.gsfc.nasa.gov, record 57747) | NASA Goddard Space Flight Center |
| `jupiter.jpg` | 3601×1801 | PIA07782, Cassini's best maps of Jupiter, cylindrical (unmodified) | NASA/JPL/Space Science Institute |
| `mars.jpg` | 4096×2048 | Mars Viking MDIM 2.1 colorized global mosaic, 1 km browse (astrogeology.usgs.gov) | NASA/JPL/USGS |
| `mercury.jpg` | 4096×2048 | PIA15063, MESSENGER MDIS global mosaic | NASA/Johns Hopkins University Applied Physics Laboratory/Carnegie Institution of Washington |
| `pluto.jpg` | 2048×1024 | PIA19956, Pluto in extended color | NASA/Johns Hopkins University Applied Physics Laboratory/Southwest Research Institute |

The originals are kept in `C:\_C_Projects\The Sun References\textures`. Saturn, Uranus,
Neptune and Venus stay procedural: Saturn's only global colour map is licensed CC BY-NC-ND,
Venus has no visible-light global map, and Uranus and Neptune were never mapped.
