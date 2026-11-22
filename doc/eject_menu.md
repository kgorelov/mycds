# Eject Menu

The "eject" menu is the main program menu screen.
The main purpose is to load a new disc into the player either from a real CD drive or a CD image (file), or from the library.

The menu must take the entire window space. On the left side there will be a pannel with buttons which will switch screens.

The buttons are:
  - CD
  - Image/File
  - Library
  - Disk Info
  - Settings
  - Back

The back button should be separated, should be at the bottom.

The screen must preserve the style of the main application. Same color scheme, button design.

## CD load screen
Shall give the user a way to select one of the CD drives present in the system.

## Image / File load screen
Shall implement a file open dialog. Must let the user navigate the file system and choose cue/toc files to load.

## Library
This page will have:
  - A search bar to let the user search discs by artist/album/track name.
  - Various filters: by genre / year / ...
  - Sorting order: relevance / year / date when added to the library (recently added)

This screen will be a mock for now unless we implement the library.

## Disk info
Shows the disk cover image, date of the release, tracks info.
Ideally, here we can have multiple "cover" images that the user can list to show the CD inlay images. 
Also we can have some text from various sources about the band and the album. (Wiki or Music magazines)

## Settings

Various progeam settings:
  - Play when started
  - Auto play on disc load


## Back
This is not a scree. The back button just takes the user back to the main program screen (the CD player).
Same can be achieved by pressing ESC.
