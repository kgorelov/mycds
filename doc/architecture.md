# Architecture and Known Limitations

## 1. Layered Design

`mycds` separates source access, playback state management, real-time audio output, metadata persistence, and user interaction into independent layers:

```text
src/
├── domain/      Canonical CD-DA constants, MSF/LBA conversions, DiscInfo, TrackInfo, DiscMetadata
├── source/      DiscSource interface + PhysicalCdSource (libcdio), TocImageSource (libMirage), CueImageSource
├── playback/    PlaybackController, contiguous LBA planner, position resolver, producer thread
├── audio/       PcmRingBuffer (lock-free SPSC) + AudioOutput (SDL3 SDL_AudioStream)
├── metadata/    CDDB Disc ID calculator, HTTP GnuDB client (libcurl), JSON sidecar store, CUE exporter
└── cli/         CommandLoop interactive shell and CLI argument runner
```

Key architectural invariants:

1. **No backend leakage:** `libcdio` types exist only inside `src/source/physical_cd_source.cpp`; `libMirage`/GLib types exist only inside `src/source/toc_image_source.cpp`; SDL3 types exist only inside `src/audio/audio_output.cpp` and signal-hint initialization in `src/main.cpp`.
2. **Normalized LBA addressing:** All `DiscSource` implementations normalize absolute disc addresses so that Track 1 `INDEX 01` on a standard Red Book audio CD is `LBA 0` (`MSF 00:02:00`). Track ranges `[startLba, endLba)` are half-open.
3. **Canonical PCM format:** Every `DiscSource::readAudioSectors()` call outputs `sectorCount * 2352` bytes of 44,100 Hz, 16-bit signed little-endian (`S16LE`), 2-channel interleaved stereo PCM (`588` stereo sample frames per sector). Big-endian raw `.bin` files produced by `cdrdao` are byte-swapped inside the TOC source adapter before reaching `PlaybackController`.
4. **Continuous sector streaming (gapless playback):** `PlaybackController` treats contiguous `TrackType::Audio` tracks as a single continuous `[startLba, audioEndLba)` LBA stream. Track and index numbers (`INDEX 00`, `INDEX 01`, `INDEX 02+`) are derived dynamically from the consumed frame count (`anchor_lba * 588 + consumed_frames`) without stopping, flushing, or reopening `AudioOutput` at track boundaries.
5. **Lock-free real-time audio callback:** A background producer thread reads multi-sector chunks from `DiscSource` into `PcmRingBuffer`. SDL3's audio stream callback reads directly from `PcmRingBuffer` without mutex locks, heap allocations, file I/O, or `DiscSource` calls. Stream reset operations (`stop()`, `clearBuffer()`) acquire `SDL_LockAudioStream` to serialize safely against the SDL callback.

## 2. Error Handling Model

- **Open-time validation:** `PhysicalCdSource`, `TocImageSource`, and `CueImageSource` validate track/index bounds, monotonic layout invariants, and backing data file readability/length when constructed, throwing typed `DiscSourceError` (`OpenFailed`, `NoDisc`, `UnsupportedLayout`, `CorruptImage`).
- **Read-time validation:** Out-of-bounds sector requests throw `DiscOutOfRangeError` using overflow-safe bounds arithmetic. Data-track sector reads throw `DiscSourceError(NonAudioTrack)`. Short reads, truncated backing files, and optical drive I/O errors throw `DiscSourceError(ReadFailed)` or `DiscSourceError(DiscRemoved)` — unreadable sectors are never silently substituted with zeroed PCM.

## 3. Known Limitations

1. **Physical CD sub-indexes (`INDEX 00` and `INDEX 02+`):**
   - Optical drive Lead-In TOC tables (`libcdio`) record only each track's `INDEX 01` start address and the Lead-Out address. Detecting `INDEX 00` pregaps or `INDEX 02+` sub-index markers on physical discs requires slow sector-by-sector Q-subchannel scanning, which is intentionally omitted in `PhysicalCdSource`.
   - Consequently, on a physical CD with a non-zero `INDEX 00` pregap (such as Track 3 on the reference disc, where `INDEX 00` starts 7 sectors before `INDEX 01`), those pregap sectors appear at the tail of Track `N-1` when playing from `/dev/sr0`, whereas `.toc` and `.cue` images expose the explicit `INDEX 00` pregap on Track `N`. Both sources play the identical underlying continuous LBA audio stream without interruption, and `seek-track N` lands on the identical `INDEX 01` LBA on all sources.
2. **Optical drive hardware read sample offset:**
   - Physical CD-ROM/DVD/BD drives exhibit a constant model-specific combined read sample offset (typically tens to hundreds of stereo samples) relative to pressed glass-master framing unless calibrated ripper offset correction is applied. Direct `PhysicalCdSource` playback plays raw drive sectors without AccurateRip/drive-offset shifting, so raw sector boundaries on `/dev/sr0` can be shifted by a constant number of samples relative to an offset-corrected or different-drive rip.
3. **Real-time DAE vs. archival ripping:**
   - `PhysicalCdSource` uses `cdio_read_audio_sectors` for low-latency real-time streaming rather than `libcdio-paranoia` multi-pass overlap/scratch recovery. Heavily scratched discs may fail with `[Read Failure]` rather than performing jitter/scratch reconstruction.
4. **Supported CUE sheet dialect:**
   - `CueImageSource` supports single-file and multi-file uncompressed `BINARY` (and `MOTOROLA` big-endian) raw sector files with `AUDIO` and `MODE1`/`MODE2` track declarations, `PREGAP`/`POSTGAP`, and `INDEX 00..99`. Compressed audio backing files (`.flac`, `.mp3`, `.ogg`, `.wv`) referenced from `.cue` sheets are outside the prototype scope.
5. **Single-session / contiguous audio focus:**
   - Mixed-mode discs open cleanly and play contiguous `TrackType::Audio` ranges while skipping/rejecting `TrackType::Data` tracks (`NonAudioTrack`). Multi-session CD-Extra discs with discontiguous audio sessions stop at the end of the contiguous audio span unless the user explicitly seeks into another audio track.
6. **Platform scope:**
   - Build configuration and physical optical device defaults (`/dev/sr0`, POSIX `poll()`/`sigaction`) target Linux first, though `PlaybackController`, `CueImageSource`, and SDL3 `AudioOutput` are portable C++20.
