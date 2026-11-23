# Projects

A project is a `Song` (src/song.h) saved as text by src/project.c (the
format is in project.h's header comment): one record a line, every patch
field by name, floats with 9 significant digits so a round trip is exact,
unknown keys skipped. `tests/test_project.c` round-trips a song of every
band and compares every field and event.

Undo keeps the last 64 states as snapshots of the whole song (a few KB
plus the events), so every edit is one `history_push`.
