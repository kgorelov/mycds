# Reference Disc Layout & Address Convention Analysis (Phase 2)

## 1. Reference Disc and Image

- **Physical Drive:** `/dev/sr0` (`ASUS SBW-06D5H-U`)
- **Reference Disc:** 12-track CD-DA audio disc (*Radiohead — OK Computer*)
- **BIN/TOC Image:** `/home/kgorelov/img/album.toc` and `/home/kgorelov/img/okcomputer.bin` (`566,098,176` bytes = `240,688` sectors $\times$ `2,352` bytes/sector)

---

## 2. Address Convention Differences & Normalization

### 2.1. `libcdio` (`lba_t` vs `lsn_t`)

- `libcdio` defines two sector address types in `<cdio/types.h>` and `<cdio/sector.h>`:
  - `lsn_t` (**Logical Sector Number**): 0-based logical sector address where Track 1 `INDEX 01` (`00:02:00` MSF) is `0`.
  - `lba_t` (`libcdio` LBA): offset by `+CDIO_PREGAP_SECTORS` (`+150`), so `00:02:00` MSF is `150`.
- `cdio_read_audio_sectors(p_cdio, p_buf, i_lsn, i_blocks)` takes `lsn_t` (`0` = `00:02:00` MSF).
- `cdio_get_track_last_lsn(p_cdio, track)` returns an **inclusive** end sector; adding `1` converts it to a half-open range `[startLba, endLba)`.
- **Normalization rule:** Use `libcdio`'s `lsn_t` (`cdio_get_track_lsn`, `cdio_get_track_last_lsn + 1`, `cdio_get_track_pregap_lsn`) as the application's 0-based absolute LBA.

### 2.2. `libMirage` (Red Book Lead-In Pregap & Track-Relative Offsets)

- When loading a CD image, `mirage_parser_add_redbook_pregap()` prepends a synthetic 150-sector NULL fragment covering `[-150, 0)` to the first track of each session and increments that track's `track_start` (`INDEX 01` offset) by `150`.
- Therefore, for Track 1:
  - `mirage_track_layout_get_start_sector(track1)` returns `-150`.
  - `mirage_track_get_track_start(track1)` returns `150 + P` (where `P` is any additional `INDEX 00` pregap length from `START` in the `.toc` file).
  - `INDEX 01` is at `-150 + (150 + P) = P` (`0` when `P = 0`), which already matches the 0-based LBA convention!
- For any track $k$:
  - `raw_start = mirage_track_layout_get_start_sector(track)`
  - `raw_len = mirage_track_layout_get_length(track)`
  - `track_start_rel = mirage_track_get_track_start(track)`
  - `index01Lba = raw_start + track_start_rel`
  - `endLba = raw_start + raw_len`
  - If fragment 0 at `-150` is the 150-sector synthetic NULL Red Book lead-in fragment (`main_data_size == 0` and `length == 150`), the addressable track start is `startLba = raw_start + 150 = 0`; otherwise `startLba = raw_start`.
  - Any `INDEX 02+` entry from `mirage_track_get_index_by_number(track, i, ...)` has track-relative address `rel`, which normalizes to `raw_start + rel`.

### 2.3. Subchannel Pregaps (`INDEX 00`) on Physical CDs vs BIN/TOC Images

- On a physical CD, the Lead-In TOC only stores `INDEX 01` start addresses for each track. `cdio_get_track_pregap_lsn()` returns `CDIO_INVALID_LSN` because `INDEX 00` transitions exist only in the Q-subchannel stream inside the audio area around track boundaries.
- On the reference disc, **Track 3** has a 7-sector (`00:00:07`) `INDEX 00` pregap at LBA `50086..50093` before `INDEX 01` at LBA `50093`:
  - `libMirage` (`album.toc`) reports Track 2 as `[21330, 50086)` and Track 3 as `[50086, 70170)` with `INDEX 00 = 50086` and `INDEX 01 = 50093`.
  - `libcdio` (`/dev/sr0`) reports Track 2 as `[21330, 50093)` and Track 3 as `[50093, 70170)` with `INDEX 01 = 50093` (and pregap `n/a`).
- Both sources report identical `INDEX 01` LBAs for all 12 tracks and identical total disc bounds `[0, 240688)`.

---

## 3. Recorded Inspection Output

### 3.1. Physical CD (`./build/cdplayer --device /dev/sr0`)

```text
====================================================================================
Source Type : Physical CD (libcdio)
Source Path : /dev/sr0
Sessions    : 1
Tracks      : 1 .. 12 (12 total)
LBA Range   : [0, 240688) (240688 sectors, 53:29:13)
Raw Range   : [150, 240838)
------------------------------------------------------------------------------------
Trk  Type     Start LBA  INDEX 01   End LBA    Length   Duration  Pregap      Indexes
------------------------------------------------------------------------------------
  1  Audio            0          0      21330    21330  04:44:30  n/a (TOC)   I01:0
  2  Audio        21330      21330      50093    28763  06:23:38  n/a (TOC)   I01:21330
  3  Audio        50093      50093      70170    20077  04:27:52  n/a (TOC)   I01:50093
  4  Audio        70170      70170      90030    19860  04:24:60  n/a (TOC)   I01:70170
  5  Audio        90030      90030     112475    22445  04:59:20  n/a (TOC)   I01:90030
  6  Audio       112475     112475     132098    19623  04:21:48  n/a (TOC)   I01:112475
  7  Audio       132098     132098     140898     8800  01:57:25  n/a (TOC)   I01:132098
  8  Audio       140898     140898     158195    17297  03:50:47  n/a (TOC)   I01:140898
  9  Audio       158195     158195     179585    21390  04:45:15  n/a (TOC)   I01:158195
 10  Audio       179585     179585     196725    17140  03:48:40  n/a (TOC)   I01:179585
 11  Audio       196725     196725     216198    19473  04:19:48  n/a (TOC)   I01:196725
 12  Audio       216198     216198     240688    24490  05:26:40  n/a (TOC)   I01:216198
====================================================================================
```

### 3.2. BIN/TOC Image (`./build/cdplayer --image /home/kgorelov/img/album.toc`)

```text
====================================================================================
Source Type : Disc Image (libMirage)
Source Path : /home/kgorelov/img/album.toc
Sessions    : 1
Tracks      : 1 .. 12 (12 total)
LBA Range   : [0, 240688) (240688 sectors, 53:29:13)
Raw Range   : [-150, 240688)
------------------------------------------------------------------------------------
Trk  Type     Start LBA  INDEX 01   End LBA    Length   Duration  Pregap      Indexes
------------------------------------------------------------------------------------
  1  Audio            0          0      21330    21330  04:44:30  0f          I01:0
  2  Audio        21330      21330      50086    28756  06:23:31  0f          I01:21330
  3  Audio        50086      50093      70170    20084  04:27:59  7f @50086   I00:50086, I01:50093
  4  Audio        70170      70170      90030    19860  04:24:60  0f          I01:70170
  5  Audio        90030      90030     112475    22445  04:59:20  0f          I01:90030
  6  Audio       112475     112475     132098    19623  04:21:48  0f          I01:112475
  7  Audio       132098     132098     140898     8800  01:57:25  0f          I01:132098
  8  Audio       140898     140898     158195    17297  03:50:47  0f          I01:140898
  9  Audio       158195     158195     179585    21390  04:45:15  0f          I01:158195
 10  Audio       179585     179585     196725    17140  03:48:40  0f          I01:179585
 11  Audio       196725     196725     216198    19473  04:19:48  0f          I01:196725
 12  Audio       216198     216198     240688    24490  05:26:40  0f          I01:216198
====================================================================================
```
