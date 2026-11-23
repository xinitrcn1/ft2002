# FT2002

A guitar-first music workstation, early-2000s style: modelled guitars (and
basses, synths, drums) playing pre-generated chord voicings with strums and
sustains, an autogenerator that writes whole songs, and export to MP3 and
Opus.

C23, CMake, raw Vulkan (the UI layer is vxide's: one instanced quad draw),
SDL3 for the window and audio. Every sound is synthesised.

## Build and run

Needs SDL3, Vulkan, FreeType, glslang, LAME, libopus, libogg (FreeBSD:
`pkg install sdl3 vulkan-loader freetype2 glslang lame opus libogg`).

    cmake -S . -B build -G Ninja && ninja -C build && (cd build && ctest)
    build/ft2002 [song.ft2]                               # the workbench (on the X display)
    build/ft2002 --headless 1440x900 --shot out.png      # a headless screenshot
    build/ft2002 --headless 1440x900 --script s.txt      # a scripted UI run (see src/main.c)
    build/ft2002-cli gen --seed 7 --mood triumph --band rock -o song.mp3
    build/ft2002-cli render song.ft2 -o song.opus

Options: `--scale N`, `--light`, `--darker`. Environment: `FT_GPU` (pick a
device by name), `FT_VALIDATE=1`.

Keys: Space play / pause, Home to start, L loop, K metronome, Ctrl+G
generate, Ctrl+E export, Ctrl+S save, Ctrl+Z / Ctrl+Y undo / redo, Enter
insert the palette chord at the cursor, Del delete, arrows move the
cursor (Shift: by bars) and the track selection.

## Docs

- [docs/design.md](docs/design.md): what it is, decisions, the plan
- [docs/ui.md](docs/ui.md): the workbench, its views and editors, UI scripts
- [docs/guitar.md](docs/guitar.md): the string model, articulations, amps, the chord library
- [docs/audio.md](docs/audio.md): the player and live audio
- [docs/composer.md](docs/composer.md): geosim's composer and the parity check
- [docs/autogen.md](docs/autogen.md): the autogenerator and the bands
- [docs/export.md](docs/export.md): rendering, WAV / MP3 / Opus
- [docs/projects.md](docs/projects.md), [docs/cli.md](docs/cli.md)

## Credits

- The UI layer (gpu.c, font.c, draw.c, icons.c, theme.c, menu.c, base.c) comes from vxide.
- The synthesizer and the composer come from geosim's procedural music (ported from C++20).
