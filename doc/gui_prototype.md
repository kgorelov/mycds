# MyCDs GUI Application prototype

The goal is to make the first version of the app which would be a fully functional CD player (no library support yet) with GUI and keyboard control and mouse control.

The app will be implemented with "Dear ImGUI" library: https://github.com/ocornut/imgui

## Phase 1 - Infrastructure
  - Make imgui a git submodule
  - Add a new build target: mycds
  - Setup the build system to compile the app using SDL3 and OpenGL backends.

The goal of this phase is to get a working app displaying an empty window.
The app must terminate when a user presses 'q' or F10.


## Phase 2 - Widgets

The goal of this phase is to create visual widgets and make mock inputs to demonstrate widgets working. No real cd playing functionality at this point.

### Phase 2.1 - CD Player Display

Main display widget resembling classic hardware CD player's displays.
It must have a 7-segment style display that will be displaying track number and time, digits must be amber orange (glowing), non-active segments are dark-dark amber.
Underneath there must be several small green graphics (icons) indicators for "repeat (track/disk)", "shuffle", "cloud", "CDDB".
When inactive, the indicator must be dark-dark green, when active, glowing green. The could and CDDB indicators may also turn red when there's no connectivity.

At this phase we're only making the widget, so no need to port the real player functionality yet. 
Let's do a simulation, time ticking when "playing" showing track time (when not paused).

Control keys:
  - space: play/pause
  - 's': toggles 'Shuffle'
  - 'r' toggles repeat
  - 'n' | pgdown: next track
  - 'p' | pgup: prev track

The "cloud" and "CDDB" indicators must start disabled and must turn on after 3 seconds.


### Phase 2.2 - Track list
A list, pretty sparse. Track number, track name



### Phase 2.3 - LED volume indicator
Square LEDs: red, yellow and green. Like on the old recorders.
Should be placed under the main CD display.
In the prototype simulation it should behave as if the music was playing (when not paused), when paused - empty (all segments not lit).


### Phase 2.4 - Controls

Grey sqare buttons of brushed metal, ligher than the background.
Each button having a backlit (cyan-blue) graphical pictogram.
Let's make controls look and feel like real hardware buttons.

Normal sized sqaure buttons:
  - Play/Pause: > ||
  - Stop
  - Prev track: << (seek backward when long-pressed)
  - Next track: >> (seek forward when long-pressed)
  - Eject (load) button - will open a dialog to select a CD, image from disk or a CD from the library

Two smaller half size buttons one on top of the other:
  - Shuffle
  - Repeat

Two round knobs next to each other:
  - Volume
  - Balance
