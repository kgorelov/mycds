  There are two places where the numbers differ by a small offset—Track 2 / Track 3 (by 7 sectors) and the Raw Range header (by 150 sectors), while Track 1 itself is
  21,330 sectors (04:44:30, LBA [0, 21330)) in both:

  ### 1. Why Track 2 (28763 vs 28756) and Track 3 (20077 vs 20084) differ by 7 sectors

  Look at Track 3 in album.toc:22-29:

    // Track 3
    TRACK AUDIO
    ...
    FILE "okcomputer.bin" 11:07:61 04:27:59
    START 00:00:07

  • What cdrdao read-cd does (album.toc / libMirage):
  When cdrdao read-cd rips the CD, it doesn't just read the disc's Lead-In Table of Contents (TOC); it also scans the Q-subchannel along the audio tracks to find INDEX 00
  (pregap) and INDEX 02+ markers.
  It found that Track 3 has a 7-sector (00:00:07) INDEX 00 pregap starting at LBA 50086 (11:07:61), before Track 3's INDEX 01 starts at LBA 50093 (11:07:68).
  Because cdrdao groups a track's INDEX 00 pregap with that track's TRACK AUDIO block:
      • Track 2 ends where Track 3's INDEX 00 begins: [21330, 50086) → length 28,756 (06:23:31).
      • Track 3 spans [50086, 70170) (7 sectors of INDEX 00 + 20,077 sectors of INDEX 01) → length 20,084 (04:27:59).
  • What the physical CD TOC exposes (/dev/sr0 / libcdio):
  The hardware TOC stored in the CD's Lead-In area only records INDEX 01 start addresses for each track—it does not store INDEX 00 pregap boundaries (those only exist in
  the Q-subchannel inside the audio sectors at LBA 50086..50092).
  Therefore, when libcdio reads the drive's TOC:
      • It only sees Track 2 INDEX 01 = 21330 and Track 3 INDEX 01 = 50093.
      • It attributes the 7 pregap sectors [50086, 50093) to the end of Track 2 (28,756 + 7 = 28,763, 06:23:38), and starts Track 3 at INDEX 01 = 50093 (20,084 - 7 = 20,
      ￺077, 04:27:52).
      • Notice that 28763 + 20077 = 48840 and 28756 + 20084 = 48840, and all INDEX 01 positions (21330, 50093, 70170, ...) are identical.


  ### 2. Why Raw Range shows [150, 240838) vs [-150, 240688)

  • Under the Red Book CD-DA specification, there is a mandatory 2-second (150-sector) pause before Track 1 INDEX 01 (00:00:00 to 00:02:00 MSF).
  • libcdio defines its raw lba_t (cdio_get_track_lba) with 00:00:00 MSF = 0, so Track 1 INDEX 01 (00:02:00 MSF) is at 150 ([150, 240838)), while libcdio's lsn_t
  (cdio_get_track_lsn) puts Track 1 INDEX 01 at 0 ([0, 240688)).
  • libMirage puts 00:02:00 MSF at 0 and prepends the 150-sector Red Book lead-in pause to Track 1 at [-150, 0), giving a raw range of [-150, 240688) and a playable audio
  range of [0, 240688).
