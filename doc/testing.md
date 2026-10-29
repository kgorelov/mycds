# Testing and Validation Guide

## 1. Automated Test Suite

When configured with GoogleTest installed, CMake builds eight test executables registered with `CTest`:

```bash
cmake -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Test Target | Scope |
| --- | --- |
| `address_conversion_tests` | Red Book MSF <-> LBA normalization, negative pregap MSF formatting, CD-DA byte/frame constants, PCM16 endian swapping |
| `cue_support_tests` | Native CUE parser, single/multi-BIN layout calculation, `PREGAP`/`INDEX 00`/`INDEX 02+`, CD-TEXT fields, safe CUE metadata export |
| `cddb_metadata_tests` | CDDB Disc ID calculation (`a60c890c` reference verification), GnuDB protocol parser, fuzzy layout matcher, `.mycds.json` sidecar persistence |
| `image_source_tests` | `TocImageSource` (libMirage) and `CueImageSource` parity, `open_image_source` dispatch, invalid/corrupt image rejection, reference disc layout checks |
| `audio_output_tests` | `PcmRingBuffer` wrap-around and frame alignment, SDL3 `AudioOutput` lifecycle, pause/resume/clear/EOS draining, sine-wave test tone |
| `playback_tests` | `PlaybackController` state machine, gapless multi-track progression, `seekToTrack`/`seekToLba`, non-audio track boundary stop, read-failure handling |
| `cli_tests` | CLI flag parsing (`--cd`, `--image`, `--inspect`, `--play`, `--test-tone`), interactive `CommandLoop` commands, metadata workflow, error exit codes |
| `validation_hardening_tests` | Phase 7 end-to-end validation: phase-coherent sine gapless verification across TOC & CUE, cross-source metadata & offset-aligned physical CD sample comparison, truncated/corrupt/mid-playback read failure hardening, simulated disc removal, and multi-threaded callback/buffer/controller stress tests |

All automated audio/playback tests use SDL3's deterministic `"dummy"` audio driver (`AudioOutputConfig::preferredDriver = "dummy"`), allowing headless CI execution without sound hardware.

## 2. Hardware and Reference Image Validation

When `/home/kgorelov/img/album.toc`, `/home/kgorelov/img/album.cue`, and `/dev/sr0` (containing the matching 12-track reference audio CD) are present on the host machine:

- `image_source_tests`, `playback_tests`, and `validation_hardening_tests` automatically run live cross-source consistency checks:
  1. **Metadata parity:** Verifies identical track counts (`1..12`), `startLba = 0`, `endLba = 240688`, CDDB Disc ID (`a60c890c`), and identical `INDEX 01` LBAs across `TocImageSource`, `CueImageSource`, and `PhysicalCdSource` (accounting for Lead-In TOC vs. Q-subchannel `INDEX 00` on Track 3).
  2. **PCM sample parity:** Verifies exact byte-for-byte `S16LE` equality between `TocImageSource` and `CueImageSource` across Track 1, the Track 2 -> Track 3 pregap/boundary (`LBA 50084..50096`), and the final sectors of Track 12 (`LBA 240680..240688`), plus exact sample-frame match against `/dev/sr0` within the optical drive's constant hardware read offset window.

If `/home/kgorelov/img` or `/dev/sr0` is absent (or holds a different disc), hardware-dependent checks skip gracefully without failing the suite.

## 3. Manual Interactive Verification Checklist

1. **Test-tone sanity check:**
   ```bash
   ./build/cdplayer --test-tone 2.0
   ```
   Confirm a clean 440 Hz stereo tone plays for 2 seconds and exits cleanly.

2. **Gapless transition check (Image & Physical CD):**
   ```bash
   ./build/cdplayer --image /home/kgorelov/img/album.toc
   ```
   Inside `cdplayer>`:
   ```text
   seek-lba 49850
   play
   position
   ```
   Confirm uninterrupted audio across `LBA 50086` (Track 3 `INDEX 00`) and `LBA 50093` (Track 3 `INDEX 01`) with zero clicks, silence, or device reopens.

3. **Signal & interruption handling:**
   ```bash
   ./build/cdplayer --image /home/kgorelov/img/album.cue --play
   ```
   Press `Ctrl+C` (or send `SIGTERM`) during active playback and confirm immediate clean shutdown (`exit code 130` for `SIGINT`, `143` for `SIGTERM`).
