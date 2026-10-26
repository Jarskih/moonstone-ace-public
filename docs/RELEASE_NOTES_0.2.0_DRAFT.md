<!-- DRAFT for the owner to edit. Generated from docs/RELEASE_NOTES_TEMPLATE.md and the ROADMAP release plan by tools/package.py,
     then filled in by hand. Not published by any tool: the release is made in moonstone-ace-public (ROADMAP M12 release plan). -->
# Moonstone ACE 0.2.0

Released: (date of the tag).

Moonstone: A Hard Days Knight (Mindscape, 1991) rebuilt in C++ on ACE for the Amiga 1200, with a PC build alongside.
**No game data is included**: you need your own copy of the three original disks (`.adf` images); the game reads them
at start-up.

## Highlights

- **Network play.** Two to four players over a LAN or the internet, in the classic game and in co-op.
- **Easy to start.** One archive, drop your three ADFs into `Moonstone/disks/`, double-click `moonstone`. A ready WinUAE
  configuration is inside.

## What is new

Network play (bsdsocket.library: Roadshow, AmiTCP, Miami, Genesis, WinUAE emulation)
- Lobby: HOST GAME opens the knight-select screen as a lobby; the host starts when everybody has picked.
- Direct games (port 6502, TCP and UDP) and games listed on the lobby server, with a relay for players who cannot be
  reached (router, firewall). FIND GAME lists open games.
- Co-op and classic mode, up to four players; every player sees their knight in their own colour on their machine.
- UDP input with history: lost packets cost nothing; the game falls back to TCP by itself.
- Drops and rejoin: a dropped classic knight becomes an AI knight, a dropped co-op member is played by the host's
  controller; the player can REJOIN from the Multiplayer screen.
- The ending plays in network games too.
- A ping / loss line in network games (F10 toggles it, `stats` in net.ini).

Options and mods
- The Options screen; rule tweaks and creature data as `.ini` files in the `mods` drawer, with reference files in
  `mods/defaults` and four examples in `mods_examples`. See docs/MODDING.md.

Other
- The version is shown on the title screen; two machines play together only when version, build and disks match.
- The release archive is built by `py tools/package.py` and holds no original data (a guard and the leak scan check it).

## Plan this release was cut from (ROADMAP)

network play as built (lobby, direct + relay, co-op and classic, drops, rejoin 12.15a, the ending) plus UDP input with
history (NETPLAY.md 2.2), after a 4-player co-op test in WinUAE.

## Downloads

| File | What |
|---|---|
| `moonstone-ace-0.2.0-amiga.zip` | the Amiga 1200 release: `Moonstone` drawer, WinUAE config, README, SHA256SUMS.txt |

SHA-256: `76636ea42d1bb3d1b6c23ac4b23822be8533cb8212d34bf8ce96b8f655130e32`. The archive is `.zip`: no lha packer was available
to the build machine; repack with lha on an Amiga if you prefer.

## Install

- **Amiga (hard disk, WHDLoad not needed):** unpack, copy the `Moonstone` drawer to `DH0:`, put your three ADFs into
  `Moonstone/disks/`, double-click `moonstone` (no icon is included: give it any Tool icon) or start it from a Shell.
- **WinUAE:** put the ADFs into `Moonstone\disks\` and run `start-winuae.bat` (needs your own Kickstart 3.1 A1200 ROM).

## Network play notes

- Needs `bsdsocket.library` (Roadshow, AmiTCP, Miami, Genesis or WinUAE's emulation).
- Direct games: forward port 6502 TCP and UDP. Lobby / relay server: `moonstone.shatteredallegiance.com`, port 6510.
- Every machine must run the same version with the same three disks.

## Known issues

- Networking has been exercised in WinUAE with bsdsocket emulation; real Amiga network stacks and internet paths have not been tested on hardware.
- No quit-to-Workbench yet: reset leaves the game.
- Floppy-only play is not supported (hard disk only); there is no boot floppy.

## Checksums

Each archive has a `SHA256SUMS.txt` inside; the hash of the archive itself is in its `.sha256` file.
