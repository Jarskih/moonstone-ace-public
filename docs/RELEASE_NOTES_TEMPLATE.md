<!-- Release notes template. tools/package.py writes the filled draft to build/release/RELEASE_NOTES_<ver>_DRAFT.md: the version,
     the date, the ROADMAP release plan of that version, the archives with their SHA-256 and the PC build status replace the
     @-marked words below. Edit the draft by hand: the tools never publish it. -->
# Moonstone ACE @VERSION@

Released @DATE@.

Moonstone: A Hard Days Knight (Mindscape, 1991) rebuilt in C++ on ACE for the Amiga 1200, with a PC build alongside.
**No game data is included**: you need your own copy of the three original disks (`.adf` images); the game reads them
at start-up.

## Highlights

- (the one-line sales pitch of this release)

## What is new

- (grouped list: gameplay, network, tools, fixes)

## Plan this release was cut from (ROADMAP)

@PLAN@

## Downloads

@FILES@

PC build status: @PC_STATUS@.

## Install

- **Amiga (hard disk, WHDLoad not needed):** unpack, copy the `Moonstone` drawer to `DH0:`, put your three ADFs into
  `Moonstone/disks/`, double-click `moonstone`.  The archive also holds a ready WinUAE configuration
  (`moonstone-ace-a1200.uae`, `start-winuae.bat`).
- **Windows:** unpack, put your three ADFs into `disks/`, run `moonstone.exe`.

## Network play notes

- Needs `bsdsocket.library` (Roadshow, AmiTCP, Miami, Genesis or WinUAE's emulation).
- Direct games use port 6502 TCP and UDP; the lobby / relay server is `moonstone.shatteredallegiance.com` port 6510.
- Every machine must run the same version with the same three disks.

## Known issues

- (list)

## Checksums

Each archive has a `SHA256SUMS.txt` inside; the table above is the hash of the archive itself.
