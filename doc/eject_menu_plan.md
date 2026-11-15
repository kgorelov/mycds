# Eject Menu Implementation Plan

## 1. Overview & Goals

This plan details the complete replacement of the current stub modal popup (`RenderEjectLoadModal` in `src/gui/cd_controls_widget.cpp`) with a full-window **Eject Menu** screen matching the specification in `doc/eject_menu.md` and the existing brushed-metal / cyan-amber Hi-Fi hardware aesthetic of `mycds`.

### Key Architectural Shifts
- **Full-Window View Instead of Popup Modal:** When invoked (via the hardware **Eject** button or `E` key), the Eject Menu takes over the entire `MyCDsMainWindow` client area (`viewport->WorkPos`, `viewport->WorkSize`) instead of opening an ImGui modal popup.
- **Persistent Left Sidebar Navigation:** A fixed-width left panel houses the five screen-switch buttons (`CD`, `Image/File`, `Library`, `Disk Info`, `Settings`) at the top and a separated `Back` button anchored at the bottom.
- **Shared Hardware UI Primitives:** Extract the brushed-metal button, socket, and diffused-cyan glow drawing helpers currently private to `src/gui/cd_controls_widget.cpp` into a shared GUI helper module so the Eject Menu and main player share identical styling without code duplication.
- **Real Backend Wiring + Clean Mock Boundaries:** Wire **CD** drive enumeration and **Image/File** (`.cue`/`.toc`) loading to `mycds_core` (`PhysicalCdSource`, `ImageSource`), while keeping **Library** and **Disk Info** rich text/inlay views cleanly structured with mock/fallback data until full library and artwork fetching are implemented.

---

## 2. Proposed File & Module Structure

### New Files
- `src/gui/cd_ui_style.h` / `src/gui/cd_ui_style.cpp`
  Shared hardware styling constants and drawing primitives extracted from `src/gui/cd_controls_widget.cpp`:
  - Color palette (`kColSocketBg`, `kColBtnMetalBase`, `kColCyanGlowCore`, `kColCyanBright`, `kColCyanMuted`, bevel colors, etc.).
  - `DrawHardwareButtonSocket(...)`, `DrawButtonBrushedFace(...)`, `DrawDiffusedLine(...)`, `DrawDiffusedRect(...)`, `DrawDiffusedTriangle(...)`.
  - A reusable `RenderHardwareTextButton(...)` (and/or icon+label variant) for sidebar navigation and action buttons, matching the tactile bevels and cyan backlit look of the transport buttons.
- `src/gui/cd_eject_menu_widget.h` / `src/gui/cd_eject_menu_widget.cpp`
  Full-window Eject Menu renderer and per-screen sub-renderers:
  - `RenderEjectMenu(CdDisplayModel& model, const ImVec2& size)`
  - Internal helpers for each screen:
    - `RenderCdLoadScreen(...)`
    - `RenderImageFileScreen(...)`
    - `RenderLibraryScreen(...)`
    - `RenderDiskInfoScreen(...)`
    - `RenderSettingsScreen(...)`
- `src/gui/eject_menu_state.h` / `src/gui/eject_menu_state.cpp` (or integrated cleanly into `CdDisplayModel` + pure-logic helpers)
  Pure C++ state and logic (independent of OpenGL/SDL windows) so navigation, file-browser filtering, library search/filter/sort, inlay cycling, and settings can be unit-tested headlessly:
  - `EjectMenuScreen` enum: `Cd`, `ImageFile`, `Library`, `DiskInfo`, `Settings`.
  - `FileBrowserState`: current directory (`std::filesystem::path`), directory entries, extension filter (`.cue`, `.toc`), selected entry, error message.
  - `LibraryFilterState`: search query, genre filter, year filter, sort mode (`Relevance`, `Year`, `RecentlyAdded`), mock catalog + deterministic filter/sort function.
  - `DiskInfoState`: active inlay index, list of inlay descriptors, active editorial source tab (`Wikipedia`, `Music Magazines`, etc.).
  - `AppSettings`: `play_when_started`, `auto_play_on_disc_load`.

### Modified Files
- `CMakeLists.txt`: Add `src/gui/cd_ui_style.cpp`, `src/gui/cd_eject_menu_widget.cpp`, and `src/gui/eject_menu_state.cpp` to the `mycds` target (and test targets as needed).
- `src/gui/cd_display_model.h` / `src/gui/cd_display_model.cpp`:
  - Replace `eject_dialog_open_` / `open_eject_dialog()` / `close_eject_dialog()` / `is_eject_dialog_open()` with `eject_menu_open_` / `open_eject_menu()` / `close_eject_menu()` / `is_eject_menu_open()` and `EjectMenuScreen` state + `AppSettings`.
  - Add helper methods for applying loaded `DiscInfo` + `std::vector<mycds::TrackInfo>` + `DiscMetadata` into `CdDisplayModel` (respecting `auto_play_on_disc_load`).
- `src/gui/cd_controls_widget.cpp`:
  - Remove `RenderEjectLoadModal` and its call in `RenderCdControls`.
  - Update the Eject hardware button to call `model.open_eject_menu()`.
  - Delegate button/glow drawing to `cd_ui_style.h`.
- `src/gui/main.cpp`:
  - Switch top-level rendering inside `MyCDsMainWindow` between the main CD player layout and `RenderEjectMenu(display_model, avail_size)` based on `display_model.is_eject_menu_open()`.
  - Handle `ImGuiKey_Escape` to close the Eject Menu.
  - Gate main-player keyboard shortcuts (`Space`, `X`, `S`, `R`, `N`, `P`, `1`-`0`, arrows, etc.) when the Eject Menu is open (and/or `ImGui::GetIO().WantTextInput` is true) so typing in search or path fields does not trigger playback controls.
  - Apply `play_when_started` setting on startup (while honoring explicit CLI flags like `--stopped` / `--paused`).
- `src/source/physical_cd_source.hpp` / `src/source/physical_cd_source.cpp`:
  - Add a drive enumeration helper (e.g., `enumerate_cd_drives()`) using `libcdio` (`cdio_get_devices_with_cap` / `CDIO_FS_AUDIO` or `CDIO_FS_MATCH_ALL`) so `libcdio` remains encapsulated inside `physical_cd_source.cpp` per `doc/architecture.md`.

---

## 3. UI Layout & Visual Design

### Root Layout (Full Window)
Inside `MyCDsMainWindow` (with `GetBrushedMetal().RenderBackground(...)` already drawn behind it):
- **Left Navigation Panel** (fixed width ~`200px`–`220px`, full content height):
  - Framed with `GetBrushedMetal().RenderPanelChassis(...)` and subtle chassis bolts, matching `CdDisplayWidget` / `CdPlaylistWidget`.
  - Top stack of 5 full-width hardware buttons (`height ~44px`, `spacing ~10px`):
    1. `CD`
    2. `Image/File`
    3. `Library`
    4. `Disk Info`
    5. `Settings`
  - Active screen button displays a bright cyan-lit indicator bar/LED and brighter cyan text/glow (consistent with Shuffle/Repeat active states).
  - Flexible spacer pushing the bottom section down.
  - Horizontal recessed divider.
  - Bottom button: `Back` (separated at the bottom of the left panel; clicking returns to the main player screen).
- **Right Content Area** (remaining width `avail_w - sidebar_w - gap`, full content height):
  - Framed with `GetBrushedMetal().RenderPanelChassis(...)`.
  - Header bar showing the active screen title in cyan/amber, plus a subtle status badge (e.g., currently loaded disc or error banner if a load fails).
  - Scrollable/structured inner region for the active screen.

---

## 4. Screen-by-Screen Design

### 4.1 CD Load Screen (`EjectMenuScreen::Cd`)
- **Drive Enumeration:**
  - On entering the screen (or clicking **Refresh Drives**), call `mycds::enumerate_cd_drives()` to discover optical drives present in the system (e.g., `/dev/sr0`, `/dev/sr1`, vendor/model string if available, and whether media is present).
  - Display drives in a selectable list or hardware-styled selector with status badges (`[DISC READY]`, `[EMPTY TRAY]`, `[NO ACCESS]`).
- **Disc Preview & Actions:**
  - When a drive with media is selected, show basic TOC summary (track count, total duration) or a prompt to probe/load.
  - **Load Disc** button: opens `PhysicalCdSource(selected_device)` (catching `DiscSourceError` and displaying inline error feedback without crashing), extracts `DiscInfo`, `tracks`, and `DiscMetadata`, and loads them into `CdDisplayModel` (and closes the menu on success, or stays open with error banner on failure).
  - **Eject Tray** button: stops playback if playing from that drive and triggers tray eject / clears loaded disc state.
  - Note: Handle environments with no optical drives gracefully (`"No optical CD drives detected"` with a **Refresh** button).

### 4.2 Image / File Load Screen (`EjectMenuScreen::ImageFile`)
- **In-App File Browser (No External OS Dialog Dependency):**
  - Uses `std::filesystem` to browse directories directly inside the ImGui panel.
  - **Top Navigation Bar:**
    - Quick-jump buttons: `Home` (`$HOME`), `Root` (`/`), `Up` (`..`), and editable current path input (`InputText` + `Go` on Enter) for direct path entry.
    - Filter indicator showing supported extensions: `.cue`, `.toc` (plus toggle for `Show All Files` if helpful, defaulting to `.cue` / `.toc` + directories).
  - **Directory & File List:**
    - Two-column or table view: Name (directories prefixed/badged `[DIR]`, `.cue` / `.toc` files highlighted in cyan), Size / Type.
    - Single click selects a file; double-click on a directory enters it; double-click on a `.cue`/`.toc` file loads it.
    - Handle `std::filesystem::filesystem_error` (permission denied, unreadable directory) gracefully with an inline status message.
  - **Bottom Action Bar:**
    - Selected file path display.
    - **Load Image** button (enabled when a `.cue` or `.toc` file is selected): constructs `mycds::ImageSource(selected_path)`, validates and reads `DiscInfo` + `tracks` + `DiscMetadata`, updates `CdDisplayModel`, and returns to the main player screen. Any `DiscSourceError` is caught and displayed in an inline amber/red status bar.

### 4.3 Library Screen (`EjectMenuScreen::Library`) — Mock Implementation
- **Search & Filter Toolbar:**
  - **Search Bar:** `InputTextWithHint` searching across artist name, album title, and track names (case-insensitive substring match).
  - **Filters:**
    - Genre combo/dropdown (`All Genres`, `Rock`, `Jazz`, `Electronic`, `Classical`, etc.).
    - Year / Decade filter (`All Years`, specific years or ranges present in mock catalog).
  - **Sorting Selector:**
    - `Relevance` (match score when searching, fallback to default order)
    - `Year` (newest/oldest)
    - `Recently Added` (date added to library)
- **Catalog List / Grid:**
  - Displays filtered and sorted mock albums (Artist, Album, Year, Genre, Track Count, Date Added).
  - Selecting an album shows its tracklist preview on the side or below, with a **Load Album** button that populates `CdDisplayModel` and returns to the player screen.
  - Clearly marked in code as mock data backed by a clean `LibraryEntry` struct so a real SQLite/JSON library backend can replace the mock vector later without UI changes.

### 4.4 Disk Info Screen (`EjectMenuScreen::DiskInfo`)
- **Left / Top Metadata & Artwork Column:**
  - Displays current disc's **Artist**, **Album Title**, **Release Date / Year**, and **Track Info** (track numbers, titles, durations, total runtime) sourced from `CdDisplayModel` (and `DiscMetadata` when available).
  - **Multi-Image Cover / Inlay Viewer:**
    - Supports multiple cover/inlay slots (e.g., `Front Cover`, `Back Inlay`, `Booklet p.1`, `CD Label`).
    - Previous / Next (`<` / `>`) hardware-styled buttons and dot/index indicator (`1 / 4`) to cycle through available inlay images.
    - Renders an OpenGL texture if an image is loaded, or a styled hardware jewel-case / disc-art placeholder card when no external image assets are present.
- **Right Editorial / Notes Column:**
  - Source switcher tabs/buttons (e.g., `Wikipedia`, `Music Magazines`, `Liner Notes`).
  - Scrollable read-only text pane displaying band and album background text for the active source (populated with mock/placeholder editorial content for known/demo albums and a clean fallback message when no notes are available).

### 4.5 Settings Screen (`EjectMenuScreen::Settings`)
- **Playback Startup & Loading Options:**
  - `Play when started` (checkbox / hardware toggle switch): controls whether `CdDisplayModel` begins in playing state on launch (overridden if `--stopped` / `--paused` CLI flag is passed).
  - `Auto play on disc load` (checkbox / hardware toggle switch): controls whether loading a CD, image file, or library album automatically starts playback (`play()`) or stays stopped/cued at Track 1.
- **Persistence:**
  - Store settings in a simple config file (e.g., `~/.config/mycds/settings.json` or `XDG_CONFIG_HOME/mycds/settings.json`, reusing the project's JSON/sidecar conventions or minimal serializer) with safe defaults if the file does not exist.

### 4.6 Back Action & Keyboard Navigation
- Clicking **Back** or pressing `ESC` (`ImGuiKey_Escape`) closes the Eject Menu (`model.close_eject_menu()`) and returns immediately to the main CD player screen.
- Pressing `E` on the main player screen opens the Eject Menu (`model.open_eject_menu()`). When the Eject Menu is already open, `E` should not interfere with text inputs (e.g., typing `'e'` in the Library search bar or file path).

---

## 5. Implementation Phases

1. **Phase 1 — Shared UI Style & Navigation Shell**
   - Extract `cd_ui_style.h/.cpp` from `cd_controls_widget.cpp` and verify main player visuals remain identical.
   - Replace `eject_dialog_open_` with `eject_menu_open_` + `EjectMenuScreen` in `CdDisplayModel`.
   - Create `cd_eject_menu_widget.h/.cpp` with the full-window two-pane layout, left sidebar buttons (`CD`, `Image/File`, `Library`, `Disk Info`, `Settings`, and bottom-anchored `Back`), and `ESC` handling in `main.cpp`.
   - Remove `RenderEjectLoadModal` from `cd_controls_widget.cpp`.

2. **Phase 2 — CD & Image/File Screens (Real Source Integration)**
   - Implement `enumerate_cd_drives()` in `src/source/physical_cd_source.hpp/.cpp`.
   - Build `RenderCdLoadScreen` wired to `enumerate_cd_drives()` and `PhysicalCdSource`.
   - Build `FileBrowserState` and `RenderImageFileScreen` wired to `std::filesystem` and `ImageSource` for `.cue` and `.toc` loading.

3. **Phase 3 — Library (Mock), Disk Info, & Settings Screens**
   - Implement `LibraryFilterState` (search by artist/album/track, genre & year filters, relevance/year/recently-added sorting) and `RenderLibraryScreen`.
   - Implement `DiskInfoState` (multi-inlay switcher, release date, track list, Wiki/Magazine source tabs) and `RenderDiskInfoScreen`.
   - Implement `AppSettings` (`play_when_started`, `auto_play_on_disc_load`), persistence, and `RenderSettingsScreen`.

4. **Phase 4 — Testing & Verification**
   - Update existing GUI tests (`tests/cd_display_model_tests.cpp`, `tests/cd_controls_widget_tests.cpp`) for the renamed menu state (`open_eject_menu`, `close_eject_menu`, `is_eject_menu_open`).
   - Add unit tests for:
     - `EjectMenuScreen` switching and `ESC`/Back state transitions.
     - `FileBrowserState` directory navigation and `.cue`/`.toc` extension filtering.
     - `LibraryFilterState` search, genre/year filtering, and sorting (`Relevance`, `Year`, `RecentlyAdded`).
     - `AppSettings` `auto_play_on_disc_load` behavior when loading a disc.
   - Build `mycds` and run `ctest` to verify all unit tests pass with zero warnings (`-Wall -Wextra -Wpedantic`).
