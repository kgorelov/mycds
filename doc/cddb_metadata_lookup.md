# Phase 2.2 — CDDB-Compatible Metadata Lookup Specification

## 1. Selected Service and Terms of Use

- **Default Service:** **GnuDB** (`http://gnudb.gnudb.org/~cddb/cddb.cgi`)
- **Configurability:** The endpoint URL is configurable via `CddbClientConfig::endpointUrl` and the `--cddb-url <url>` command-line flag, allowing any FreeDB/CDDB-compatible server (such as a local mirror or self-hosted CDDB CGI service) to be used.
- **Terms of Use & Client Etiquette:**
  - GnuDB provides free CDDB-compatible access for open-source audio players and rippers under the GPL/FreeDB database heritage.
  - Every request includes a standard `hello=<user>+<host>+<client>+<version>` handshake and `proto=6` (UTF-8 encoding).
  - Explicit connection (`5000 ms`) and request (`10000 ms`) timeouts are enforced.
  - Automatic retry storms are avoided on network or server errors.
  - Once a record is selected and saved to the local `.mycds.json` sidecar file, subsequent inspections and playback sessions load metadata directly from disk without issuing network requests.

---

## 2. CDDB Disc ID Calculation Algorithm

Given a `DiscInfo` and ordered `std::vector<TrackInfo>` (`N` tracks):

1. **Lead-In Frame Offset (`150` frames):**
   Red Book CD-DA places Logical Block Address (LBA) `0` at `00:02:00` (`150` frames from the start of the disc).
   For each track $i \in \{0, \dots, N-1\}$:
   $$\text{frame\_offset}_i = \text{track}_i.\text{index01Lba} + 150$$
   And for the lead-out:
   $$\text{leadout\_frame} = \text{discInfo.endLba} + 150$$

2. **Checksum of Track Start Seconds ($n$):**
   For each track $i$, compute the integer start time in seconds $s_i = \lfloor \text{frame\_offset}_i / 75 \rfloor$, and sum the decimal digits of $s_i$:
   $$\text{cddb\_sum}(x) = \sum \text{decimal digits of } x$$
   $$n = \sum_{i=0}^{N-1} \text{cddb\_sum}\!\left(\lfloor \text{frame\_offset}_i / 75 \rfloor\right)$$

3. **Total Audio Span in Seconds ($t$):**
   $$t = \lfloor \text{leadout\_frame} / 75 \rfloor - \lfloor \text{frame\_offset}_0 / 75 \rfloor$$

4. **32-bit Disc Identifier:**
   $$\text{discid} = ((n \bmod 255) \ll 24) \;\mid\; ((t \ \& \ \text{0xFFFF}) \ll 8) \;\mid\; (N \ \& \ \text{0xFF})$$
   Formatted as an 8-character lowercase hexadecimal string (`%08x`).

5. **Total Seconds Parameter in `cddb query`:**
   $$\text{total\_seconds} = \lfloor \text{leadout\_frame} / 75 \rfloor$$

### Reference Disc Verification (*Radiohead — OK Computer*)

- Track `INDEX 01` LBAs: `0, 21330, 50093, 70170, 90030, 112475, 132098, 140898, 158195, 179585, 196725, 216198`
- Frame offsets (`+150`): `150, 21480, 50243, 70320, 90180, 112625, 132248, 141048, 158345, 179735, 196875, 216348`
- Lead-out LBA: `240688` $\rightarrow$ Lead-out frame: `240838` $\rightarrow$ `total_seconds = 3211`
- Digit sum $n = 166$ (`0xa6`), span $t = 3211 - 2 = 3209$ (`0x0c89`), track count $N = 12$ (`0x0c`)
- Computed CDDB Disc ID: **`a60c890c`**

---

## 3. CDDB HTTP CGI Protocol (Level 6)

### 3.1 Query Request (`cddb query`)
```text
GET ?cmd=cddb+query+<discid>+<N>+<off_1>+...+<off_N>+<total_seconds>&hello=<user>+<host>+<client>+<version>&proto=6
```

Supported status codes:
- `200 <category> <discid> <Artist / Album>`: Single exact match.
- `210 Found exact matches, list follows (until terminating '.')`: Multiple exact matches, one `<category> <discid> <Artist / Album>` per line until `.`.
- `211 Found inexact matches, list follows (until terminating '.')`: Multiple inexact matches until `.`.
- `202 No match for disc ID <discid>`: Zero matches.
- `4xx` / `5xx`: Server, rate-limit, or protocol error.

### 3.2 Read Request (`cddb read`)
```text
GET ?cmd=cddb+read+<category>+<discid>&hello=<user>+<host>+<client>+<version>&proto=6
```

Returns status `210 <category> <discid> CD database entry follows (until terminating '.')` followed by an XMCD record containing:
- Comment header with `# Track frame offsets:` and `# Disc length: <seconds>`
- `DISCID=`, `DTITLE=<Artist> / <Album>`, `DYEAR=`, `DGENRE=`
- `TTITLE0=` .. `TTITLE<N-1>=` (multi-line keys are concatenated; `\n`, `\t`, `\\` escapes decoded)
- `EXTD=`, `EXTT0=` .. `EXTT<N-1>=`, `PLAYORDER=`

---

## 4. Validation & Canonical Persistence

1. **Candidate Validation (`MetadataMatcher`):**
   - Because 32-bit CDDB disc IDs can collide across unrelated pressings (for example, `rock/a60c890c` vs `data/a60c8990`), `MetadataMatcher::validate` checks:
     - Track count equality against the local `DiscInfo`.
     - Contiguous `1..N` track ordering and non-empty track titles.
     - Frame offset delta (`maxFrameOffsetDelta`) and disc length delta (`discLengthSecondsDelta`) against the XMCD header's `# Track frame offsets` and `# Disc length`.
   - Local `DiscInfo` and `TrackInfo` sector boundaries are **never** altered by remote metadata.

2. **Sidecar Metadata Store (`MetadataStore`):**
   - Canonical editable JSON sidecar file (`<image_stem>.mycds.json` or `disc_<discid>.mycds.json`) storing album & track metadata, provider name, external record ID (`<category>/<discid>`), computed CDDB disc ID, ISO-8601 lookup timestamp, and user correction flags/notes.
   - Safe CUE export (`MetadataStore::exportToCue`) updates `TITLE`, `PERFORMER`, `SONGWRITER`, `CATALOG`, `ISRC`, `REM GENRE`, `REM DATE`, and `REM DISCID` while preserving all `FILE`, `TRACK`, `INDEX`, `PREGAP`, `POSTGAP`, and `FLAGS` layout directives verbatim.
