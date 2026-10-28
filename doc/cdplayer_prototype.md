# Technical Specification
## CD Audio Playback from Physical Discs and BIN/TOC Images

**Project:** CD Player Core Prototype
**Language:** C++20
**Initial Platform:** Linux
**Application Type:** Console application
**Primary Objective:** Implement a common playback interface for physical CD-DA discs and CD images while preserving track boundaries, indexes, pregaps, and gapless transitions.

---

# 1. Objectives

The prototype shall demonstrate that audio can be played from two independent source types through a common C++ interface:

1. A physical CD-DA disc accessed through an optical drive.
2. A BIN/TOC disc image created using cdrdao.

The implementation shall separate disc access, disc structure, audio decoding/output, and playback control.

The prototype is not intended to provide a graphical interface, metadata management, ripping to compressed formats, or a complete music library.

## 1.1. Design Principles

- Prefer existing libraries over implementing CD protocols or image parsers.
- Preserve the original logical layout of the disc wherever the drive and image format permit.
- Avoid coupling playback logic to a particular source type.
- Keep the public C++ interfaces independent of third-party library types.
- Support continuous playback across track boundaries.
- Make track and index information available to the playback controller.
- Keep the architecture extensible to additional image formats and audio output backends.

## 1.2. Non-Goals

The initial prototype will not implement:

- A graphical user interface.
- CD ripping to FLAC, WAV, or other file formats.
- Online metadata lookup.
- CD burning.
- DVD, Blu-ray, or general data-disc playback.
- Guaranteed bit-perfect archival extraction from every optical drive.
- Complete preservation of all physical-disc features, including all subchannel information.

---

# 2. Technology Selection

| Component | Proposed technology | Responsibility |
|---|---|---|
| Language | C++20 | Application and abstraction layers |
| Physical CD access | libcdio | Drive access, disc information, track layout, audio-sector reading |
| Image access | libMirage | Loading BIN/TOC images and exposing their structure and contents |
| Audio output | SDL3 Audio | PCM output to the system audio device |
| Build system | CMake | Configuration, compilation, dependency management |
| Test framework | Catch2 or GoogleTest | Unit and integration tests |
| Reference image creation | cdrdao | Creating BIN/TOC images for testing |

Dependencies shall be isolated behind internal interfaces. The application must not expose libcdio or libMirage types in its public domain model.

Before implementation, verify the installed versions of libcdio and libMirage, their development headers, and the exact APIs available for audio-sector access and track/index enumeration.

---

# 3. Disc Image Format

## 3.1. Primary Format: BIN/TOC

The initial image format shall be cdrdao's BIN/TOC.

The image consists of:

- `album.bin` — binary disc data.
- `album.toc` — textual description of the disc layout.

The TOC file is part of the image and must be retained. The BIN file alone is insufficient to reliably reconstruct all track boundaries and indexes.

The image loader shall obtain the disc structure from libMirage rather than implement a separate TOC parser.

## 3.2. Image Creation

Create a reference image using:

```bash
mkdir -p ~/CDImages
cd ~/CDImages

cdrdao read-cd \
    --device /dev/sr1 \
    --driver generic-mmc \
    --datafile album.bin \
    album.toc
```

The actual drive device and supported driver must be configurable.

For the initial prototype, use an ordinary audio CD. Test discs with unusual pregaps and index layouts should be introduced during later validation.

### Subchannel data

Capturing subchannel data may preserve additional information, but support depends on the drive, extraction method, image representation, and reader implementation.

Subchannel capture is therefore a separate investigation. It must not be assumed that a BIN/TOC image created without subchannel data is a complete physical-disc replica.

## 3.3. Image Validation

The image loader must validate:

- That the image can be opened.
- That the TOC references valid data.
- That all required audio tracks have readable sector ranges.
- That track boundaries and indexes are internally consistent.
- That sector reads remain within valid image ranges.

Errors shall be reported without terminating the application unexpectedly.

---

# 4. Architecture

The application shall consist of five logical layers.

```text
                  PlaybackController
                          |
                     DiscSource
                     /        \
                    /          \
          PhysicalCdSource   ImageSource
                 |                |
              libcdio          libMirage
                    \          /
                     \        /
                   Audio PCM
                       |
                  AudioOutput
                       |
                    SDL3 Audio
```

The architecture shall distinguish between the logical layout of a disc and the mechanism used to read its data.

Both source implementations shall provide equivalent track metadata and audio-sector access where the underlying source contains the required information.

---

# 5. Domain Model

## 5.1. DiscInfo

Represents the overall disc.

Required fields:

- First track number.
- Last track number.
- Total logical sector range, where available.
- Number of sessions, if exposed by the source.
- Optional disc metadata.

The prototype may restrict actual playback to single-session CD-DA discs, but the data model should not assume that all future sources will contain only one session.

## 5.2. TrackInfo

Represents a track.

Required fields:

- Track number.
- Track type.
- Start LBA.
- End LBA or another unambiguous representation of its extent.
- Available index positions.
- Pregap information, where available.

All sector addresses shall use a consistent convention.

Use **absolute logical block addresses (LBA)** internally. Document whether every end address is inclusive or exclusive; prefer half-open ranges `[start, end)` for application-level ranges.

The model shall not assume that a track's INDEX 01 position is necessarily the beginning of all audio associated with that track.

## 5.3. IndexInfo

Represents an index within a track.

Required fields:

- Track number.
- Index number.
- Absolute LBA.
- Whether the source explicitly provides this index or the value was inferred.

INDEX 00 and INDEX 01 must be distinguished where available.

The application must not invent index positions or silently treat an unavailable pregap as an empty interval of a known length.

## 5.4. AudioFormat

The initial supported audio format shall be CD-DA PCM:

- Sample rate: 44,100 Hz.
- Channels: 2.
- Sample depth: 16 bits per channel.
- Sector payload: 2,352 bytes.
- Audio frames per sector: 588.
- Byte order from the source: handled explicitly by the source adapter.

The audio-output layer shall receive PCM in a documented format. Endianness conversion must be performed when necessary.

---

# 6. Common DiscSource Interface

The interface shall abstract the differences between a physical drive and an image.

Illustrative C++ interface:

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

enum class TrackType {
    Audio,
    Data,
    Unknown
};

struct IndexInfo {
    int number;
    std::int64_t lba;
};

struct TrackInfo {
    int number;
    TrackType type;

    std::int64_t startLba;
    std::int64_t endLba; // Exclusive

    std::vector<IndexInfo> indexes;
};

struct DiscInfo {
    int firstTrack;
    int lastTrack;

    std::int64_t startLba;
    std::int64_t endLba; // Exclusive
};

class DiscSource {
public:
    virtual ~DiscSource() = default;

    virtual DiscInfo getDiscInfo() const = 0;

    virtual std::vector<TrackInfo> getTracks() const = 0;

    virtual std::vector<std::byte> readAudioSectors(
        std::int64_t startLba,
        std::uint32_t sectorCount
    ) = 0;
};
```

This is a starting interface, not a complete production API. The implementation should use a suitable error-reporting mechanism, enforce source bounds, and avoid unnecessary allocations in the playback path.

The contract for `readAudioSectors()` shall specify that:

1. The requested range is expressed in absolute LBAs.
2. The request must not silently cross an unsupported track type.
3. The result represents exactly the requested number of audio sectors or reports an error.
4. Returned data uses the documented byte order and sector layout.
5. The method does not change the logical position of another independent reader unless that behavior is explicitly documented.

The interface must not assume that all tracks on a disc are audio tracks.

---

# 7. PhysicalCdSource

## 7.1. Responsibilities

`PhysicalCdSource` shall:

- Open a specified optical drive.
- Query the disc's structure.
- Enumerate tracks and available indexes.
- Read audio sectors by absolute LBA.
- Report drive and read errors.
- Detect when the source becomes unavailable.

## 7.2. Implementation

Use libcdio to access the physical drive.

The implementation shall convert libcdio's track and address information into the application's domain model.

Do not expose libcdio-specific types outside the adapter.

## 7.3. Initial Limitations

The initial implementation may require a single-session, audio-only CD.

Mixed-mode and multisession discs may be rejected with an explicit error until their behavior is properly implemented and tested.

## 7.4. Error Handling

The adapter shall report:

- No disc inserted.
- Drive cannot be opened.
- Unsupported disc layout.
- Audio-sector read failure.
- Disc removed during playback.

The application must not treat unreadable sectors as valid silence without explicitly reporting the substitution.

---

# 8. ImageSource

## 8.1. Responsibilities

`ImageSource` shall:

- Open a BIN/TOC image.
- Enumerate tracks, indexes, and sector ranges.
- Read audio sectors by absolute LBA.
- Validate requests against the image layout.
- Report corrupt or unsupported images.

## 8.2. Implementation

Use libMirage to load and interpret the image.

Create a C++ adapter around the library's GObject-based API. GLib and libMirage objects shall remain private implementation details.

The adapter must verify the mapping between libMirage's logical addresses and the addresses exposed through `DiscSource`.

In particular, it must not assume that an LBA is directly equivalent to an offset into the BIN file. The image format can describe multiple tracks, different sector types, and layout information that affects the mapping.

## 8.3. Audio-Sector Reading

The adapter shall return audio PCM in the same format as `PhysicalCdSource`.

Data tracks must be identified and rejected by the audio-reading interface.

If libMirage cannot provide the required sector access or index information through its public API, document the limitation before implementing a custom parser or introducing another dependency.

---

# 9. PlaybackController

## 9.1. Responsibilities

The controller shall operate exclusively through `DiscSource` and `AudioOutput`.

Required operations:

```cpp
openSource(...)
closeSource()
play()
pause()
stop()
seekToTrack(int trackNumber)
seekToLba(std::int64_t lba)
```

The public interface may evolve during implementation, but source-specific operations must remain inside the corresponding adapter.

## 9.2. Playback State

The controller shall track:

- Current source.
- Current playback state.
- Current absolute LBA.
- Current track and index, where known.
- Current playback position.
- End-of-disc state.
- Read errors.

Suggested playback states:

```cpp
enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Error
};
```

Track identity should be determined from the disc's index layout, not inferred from the audio content.

## 9.3. Gapless Playback

Gapless playback is a primary requirement.

The controller shall read audio as a continuous stream of sectors across track boundaries whenever the requested playback range is contiguous audio.

It must not automatically insert silence between tracks.

The logical track transition must not require stopping and reopening the audio device.

The controller shall still report the track transition at the correct sector boundary.

Pregaps and index positions must remain part of the logical disc timeline. Track navigation and display may use INDEX 01 as the conventional track-start point, but continuous playback must not discard other valid audio sectors merely because they precede INDEX 01.

The implementation must define and test the behavior of `seekToTrack()` separately from continuous playback.

## 9.4. Buffering

The playback controller shall maintain a buffer of audio sectors ahead of the playback position.

Physical CD reads may block and must not execute inside the real-time audio callback.

Use a producer-consumer arrangement:

```text
DiscSource
    |
Sector Reader
    |
PCM Buffer / Queue
    |
SDL3 Audio Callback
    |
Audio Device
```

The audio callback shall consume already prepared PCM data and must not access libcdio or libMirage directly.

The buffer must tolerate ordinary read latency without introducing audible gaps during normal playback.

---

# 10. AudioOutput

Implement the initial audio output using SDL3 Audio.

Responsibilities:

- Open an audio output device.
- Request 44.1 kHz stereo PCM.
- Convert source samples when required.
- Consume buffered PCM.
- Support pause and resume.
- Report audio-device errors.

The audio callback must perform bounded, predictable work. It must not allocate memory, block on optical-disc I/O, or acquire locks that can be held for an unbounded period.

The initial prototype may use a simple ring buffer or an equivalent bounded PCM queue.

The playback controller shall own playback semantics; the output component shall not interpret track boundaries or parse disc metadata.

---

# 11. Console Application

The console interface shall support the following commands.

```text
cdplayer --device /dev/sr1
cdplayer --image ~/CDImages/album.toc
```

The application shall provide a simple interactive command loop.

Required commands:

```text
info
tracks
play
pause
stop
seek-track <number>
seek-lba <lba>
position
quit
```

`info` shall display the source type and disc information.

`tracks` shall display track numbers, types, address ranges, and available indexes.

`position` shall display the current LBA, track, and playback state.

The application shall report errors in a readable form and return a non-zero exit status when startup fails.

---

# 12. Testing and Acceptance Criteria

## 12.1. Source Inspection

**Acceptance criteria:**

- A physical audio CD can be opened.
- A valid BIN/TOC image can be opened.
- Both sources expose a consistent domain model.
- Track and index information is reported correctly where the source provides it.
- Invalid images and unsupported disc layouts produce explicit errors.

## 12.2. Playback

**Acceptance criteria:**

- Audio can be heard from the physical disc.
- The same disc image can be played through the same controller.
- Pause, resume, stop, and track seeking work.
- Audio output has the expected sample rate and channel count.
- End-of-disc handling is correct.

## 12.3. Gapless Transitions

Use a disc containing adjacent tracks without an audible gap, preferably a live recording.

**Acceptance criteria:**

- The controller does not insert artificial silence at the track boundary.
- Playback remains continuous when the track number changes.
- The reported transition occurs at the expected LBA.
- Playback does not restart the audio device at each boundary.

## 12.4. Source Consistency

For the reference disc:

- Compare the physical-disc track table with the image track table.
- Compare start addresses and index positions after accounting for each library's address conventions.
- Verify that the same logical audio range can be read from both sources.
- Compare the returned PCM data where exact equality is expected.

A mismatch must be investigated rather than automatically interpreted as a library bug. Drive read offsets, extraction behavior, and source-specific address conventions may affect comparisons.

## 12.5. Reliability

Test:

- Removing the disc during playback.
- Opening a nonexistent image.
- Requesting an invalid track.
- Seeking outside the valid sector range.
- Reading across track boundaries.
- Encountering a failed sector read.
- Closing the source while playback is active.

The application must terminate cleanly and must not leave the audio callback accessing destroyed objects.

---

# 13. Implementation Plan

## Phase 1 — Environment and Dependency Verification

Tasks:

1. Create the CMake project.
2. Verify C++20 support.
3. Install and link libcdio.
4. Install and link libMirage and its dependencies.
5. Install SDL3.
6. Verify that the application can open an audio output device.
7. Confirm the installed libMirage API supports the required image operations.

Deliverable: a minimal executable that builds and initializes all required dependencies.

**Exit criterion:** the build works on the target Linux environment, and the relevant APIs are verified.

## Phase 2 — Reference Image and Disc Inspection

Tasks:

1. Create a BIN/TOC image using cdrdao.
2. Record the original disc's track layout.
3. Implement a small libcdio-based inspection utility.
4. Implement a libMirage-based image inspection utility.
5. Print track numbers, types, LBAs, and indexes.
6. Compare both results and resolve address-convention differences.

Deliverable: two source-inspection commands with consistent output.

**Exit criterion:** the physical disc and its image expose a consistent logical layout for the reference disc.

## Phase 3 — DiscSource Abstraction

Tasks:

1. Define the domain model.
2. Implement `PhysicalCdSource`.
3. Implement `ImageSource`.
4. Normalize sector-address conventions.
5. Define audio byte order and error semantics.
6. Add unit tests for address conversion and bounds checking.

Deliverable: interchangeable source adapters.

**Exit criterion:** the same test code can inspect and read audio sectors from both sources without source-specific logic.

## Phase 4 — Audio Output

Tasks:

1. Implement `AudioOutput` using SDL3 Audio.
2. Establish the PCM format.
3. Implement the PCM buffer.
4. Implement the audio callback.
5. Test playback of a known PCM test signal independently of CD access.

Deliverable: a reliable PCM output component.

**Exit criterion:** continuous audio output works without involving the disc-reading layer.

## Phase 5 — Playback Controller

Tasks:

1. Implement the playback state machine.
2. Implement sector reading and buffering.
3. Implement play, pause, stop, and seek.
4. Implement track identification from absolute LBAs.
5. Implement continuous playback across track boundaries.
6. Implement source-error propagation.

Deliverable: a source-independent playback controller.

**Exit criterion:** the same controller plays audio from a physical CD and a BIN/TOC image.

## Phase 6 — Console Interface

Tasks:

1. Implement source selection through command-line arguments.
2. Implement the interactive command loop.
3. Add track and position reporting.
4. Add readable diagnostics.
5. Verify shutdown and resource cleanup.

Deliverable: a usable console prototype.

**Exit criterion:** the prototype supports the complete required command set.

## Phase 7 — Validation and Hardening

Tasks:

1. Test gapless playback with a suitable disc.
2. Compare source metadata.
3. Compare audio samples where appropriate.
4. Test invalid images and sector-read failures.
5. Test disc removal.
6. Verify callback and buffer thread safety.
7. Document known limitations.

Deliverable: a validated prototype with documented limitations.

**Exit criterion:** all acceptance criteria pass for the supported disc and image types.

---

# 14. Repository Structure

A proposed layout:

```text
cdplayer/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── architecture.md
│   └── testing.md
├── src/
│   ├── main.cpp
│   ├── domain/
│   │   ├── disc_info.hpp
│   │   ├── track_info.hpp
│   │   └── audio_format.hpp
│   ├── source/
│   │   ├── disc_source.hpp
│   │   ├── physical_cd_source.hpp
│   │   ├── physical_cd_source.cpp
│   │   ├── image_source.hpp
│   │   └── image_source.cpp
│   ├── playback/
│   │   ├── playback_controller.hpp
│   │   └── playback_controller.cpp
│   ├── audio/
│   │   ├── audio_output.hpp
│   │   └── audio_output.cpp
│   └── cli/
│       ├── command_loop.hpp
│       └── command_loop.cpp
└── tests/
    ├── address_conversion_tests.cpp
    ├── image_source_tests.cpp
    ├── playback_tests.cpp
    └── test_data/
```

---

# 15. Future Extensions

The architecture should permit the following extensions without rewriting the playback controller:

- Additional image formats.
- CD-TEXT and metadata extraction.
- Ripping to FLAC or WAV.
- Accurate extraction diagnostics.
- A graphical interface.
- Remote storage of disc images.
- Server-side conversion to compressed audio.
- More advanced archival and subchannel preservation.

These features are explicitly outside the initial prototype's scope.

---

# 16. Definition of Done

The prototype is complete when:

1. A physical CD-DA disc can be opened and played.
2. A BIN/TOC image can be opened and played.
3. Both sources implement the same `DiscSource` interface.
4. Track and index metadata are exposed consistently.
5. Playback is continuous across adjacent audio tracks.
6. Seeking and playback controls work.
7. Invalid sources and read failures are handled safely.
8. The implementation builds through CMake and has documented setup and test procedures.

The primary deliverable is not a full-featured player. It is a small, well-separated playback core that proves the source abstraction and gapless playback model before additional functionality is introduced.
