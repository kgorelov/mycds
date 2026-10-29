# mycds — Gapless CD-DA Player Prototype (`cdplayer`)

`cdplayer` is a C++20 command-line Compact Disc Digital Audio (CD-DA) player prototype that plays physical audio CDs and raw disc images (`.toc`/`.bin` and `.cue`/`.bin`) through a single source-independent gapless playback engine.

## Features

- **Unified `DiscSource` abstraction** over:
  - Physical optical drives via `libcdio` (`PhysicalCdSource`)
  - `cdrdao` `.toc` + `.bin` images via `libMirage` (`TocImageSource`)
  - Standard `.cue` + `.bin` images via a native CUE parser (`CueImageSource`)
- **True gapless playback** across `INDEX 00` pregaps, `INDEX 01` track starts, and `INDEX 02+` sub-indexes without reopening or resetting the audio device.
- **Lock-free real-time audio path** feeding canonical 44,100 Hz, 16-bit signed little-endian stereo PCM (`S16LE`) through an SPSC ring buffer into an SDL3 `SDL_AudioStream`.
- **CDDB/GnuDB metadata lookup & local sidecar persistence** (`.mycds.json`) plus safe CUE metadata export.
- **Interactive console interface** (`play`, `pause`, `stop`, `seek-track`, `seek-lba`, `position`, `tracks`, `info`, `metadata`, `quit`) and headless inspection modes (`--inspect`, `--play`, `--test-tone`).

## Prerequisites (Linux)

- CMake >= 3.20 and a C++20 compiler (`g++` >= 11 or `clang++` >= 14)
- `pkg-config`, `gettext` / `intltool`, `flex`, `bison` (for building the bundled `libMirage` submodule)
- Development packages:
  - `libcdio` (`libcdio-dev`)
  - `libcurl` (`libcurl4-openssl-dev` or equivalent)
  - `glib-2.0`, `gobject-2.0`, `gmodule-2.0`, `gio-2.0` (`libglib2.0-dev`)
  - `libsndfile` (`libsndfile1-dev`), `zlib` (`zlib1g-dev`), `liblzma` (`liblzma-dev`), `libbz2` (`libbz2-dev`)
  - Audio backend headers for SDL3 (`libpulse-dev`, `libpipewire-0.3-dev`, or `libasound2-dev`)
  - Optional: GoogleTest (`libgtest-dev`) to build and run the unit/validation test suite

Initialize submodules if cloning fresh:

```bash
git submodule update --init --recursive
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

*(If SDL3 is not installed system-wide, CMake automatically fetches SDL3 `release-3.4.18` with console-only audio enabled.)*

## Usage

### Open Interactive Console

```bash
# Physical CD drive
./build/cdplayer --cd /dev/sr0

# BIN/TOC image
./build/cdplayer --image /path/to/album.toc

# BIN/CUE image (and start playing immediately)
./build/cdplayer --image /path/to/album.cue --play
```

### Inspect Disc Layout or Test Audio Output

```bash
# Print normalized disc/track/index layout and exit
./build/cdplayer --image /path/to/album.toc --inspect
./build/cdplayer --image /path/to/album.toc --inspect --lookup-cddb

# Verify SDL3 audio output independently of any CD/image source
./build/cdplayer --test-tone 2.0
```

### Interactive Commands

| Command | Description |
| --- | --- |
| `info` | Display source type, disc layout summary, CDDB ID, and metadata |
| `tracks` | Display track numbers, types, LBA ranges, durations, and indexes |
| `play` | Start or resume continuous CD-DA playback |
| `pause` | Pause active audio playback |
| `stop` | Stop playback and reset cursor to start of disc |
| `seek-track <n>` | Seek to track `<n>` (`INDEX 01`) |
| `seek-lba <lba>` | Seek to normalized absolute sector `<lba>` |
| `position` | Display playback state, LBA, MSF times, track/index, and buffer fill |
| `metadata lookup [n]` | Query GnuDB/CDDB and optionally apply candidate `<n>` |
| `metadata show` | Show current album and track metadata |
| `metadata edit` | Interactively edit album and track metadata |
| `metadata save [path]` | Save metadata to `<image>.mycds.json` (or custom path) |
| `metadata export-cue [path]` | Export CUE sheet with updated CD-TEXT fields |
| `help` | List available commands |
| `quit` | Stop playback, close source, and exit cleanly |

### Creating a Reference BIN/TOC Image

To extract a physical audio CD into a `.toc` + `.bin` image suitable for `cdplayer`:

```bash
cdrdao read-cd --read-raw --datafile album.bin --device /dev/sr0 --driver generic-mmc-raw album.toc
```

## Running Tests

```bash
ctest --test-dir build --output-on-failure
```

See [`doc/architecture.md`](doc/architecture.md) for architectural details and known limitations, and [`doc/testing.md`](doc/testing.md) for test procedures and validation coverage.
