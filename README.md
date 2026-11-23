# FT2002

A guitar-first music workstation, early-2000s style: modelled guitars (and
basses, synths, drums) playing pre-generated chord voicings with strums and
sustains, an autogenerator that writes whole songs, and export to MP3 and
Opus.

C23, CMake, raw Vulkan (the UI layer is vxide's: one instanced quad draw),
SDL3 for the window and audio. Every sound is synthesised.

## Build and run

    cmake -S . -B build -G Ninja && ninja -C build && (cd build && ctest)
    build/ft2002                                          # the workbench (on the X display)
    build/ft2002 --headless 1280x800 --shot out.png      # a headless screenshot
    build/ft2002 --headless 1280x800 --script s.txt      # a scripted UI run (see src/main.c)

Options: `--scale N`, `--light`, `--darker`. Environment: `FT_GPU` (pick a
device by name), `FT_VALIDATE=1`.

## Docs

- [docs/design.md](docs/design.md): what it is, decisions, the plan

## Credits

- The UI layer (gpu.c, font.c, draw.c, icons.c, theme.c, menu.c, base.c) comes from vxide.
- The synthesizer and the composer come from geosim's procedural music (ported from C++20).
