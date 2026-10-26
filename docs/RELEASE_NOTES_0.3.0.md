# Moonstone ACE 0.3.0

**Moonstone on Windows.** This release adds a native Windows PC version next to the Amiga 1200 version: the whole game,
local co-op for up to four knights, and online play. As always, no game data is included: you need your own copy of the
three original disks (`.adf` images); the game reads them when it starts.

## Highlights

- **Windows PC version**: the same game as on the Amiga, with the original's look and sound: intro, map, towns, shops,
  lairs, black knights, the wizard, the Valley and Stonehenge (the dragon and the ending are not tested on PC yet).
- **Pixel-perfect scaling**: the window opens as large as your screen allows, resizes in whole steps, F11 for fullscreen.
- **Optional CRT effect** (F12): scanlines, mask and glow.
- **Better sound on PC**: band-limited Paula synthesis (no aliasing), 70 % stereo separation and the A1200 output filter.
- **Co-op computer partners**: knights 2–4 can be controlled by the computer ("Control: Computer"); they fight with you and
  copy your purchases in town, while you always lead the party on the map.
- **Online play on PC**: Find public games, host without router setup, or join on your home network; games connect directly
  when they can and through the relay otherwise.

## What is new

**PC version**
- Local co-op on one keyboard (cursor keys + Right Ctrl, W A S D + Alt) and gamepads.
- Options: `--scale N`, `--aspect` (Amiga 6:5 pixels), `--fullscreen`, `--crt`, `--audio-separation`, `--audio-filter`.

**Network play**
- Direct connections through the relay (hole punching) with automatic fallback to the relay; mixed 4-player games work.
- The lobby asks you to SELECT YOUR KNIGHT before it waits for the host; F10 hides the ping line.
- The fight pause key is ignored in network games (it could freeze both machines).

**Amiga**
- Network play is a separate build again (`…-amiga-netplay.zip`, needs fast RAM), so the standard build fits a stock A1200
  with 2 MB chip RAM.

**Fixes (both platforms)**
- Co-op: when one knight fell, the surviving knight could be killed with it and the fight ended (flying creatures, the spear
  trogg, the dragon's bite). Now the fallen knight stays down and the other fights on.
- Co-op: after a mudman dragged one knight under, the fight could freeze; mudmen killed or struck while carrying a knight now
  let go properly, and a carrying flyer keeps the knight it holds.
- Co-op: black knights in a duel are drawn in their own colours; the turn corner is now simply "< NAME >".
- A rare crash after a dragon fight (stale cel sets) and a screen-shake overflow in long fights.
- PC: a crash at the end of a mudmen fight.

## Downloads

@FILES@

## Install

- **Windows:** unzip, put your three ADFs into `disks`, run `moonstone.exe` (needs the Microsoft Visual C++ Redistributable x86).
- **Amiga:** unzip, copy the `Moonstone` drawer to your hard disk, put your three ADFs into `Moonstone/disks`,
  start `moonstone`. The zip includes a ready WinUAE configuration. For online play use the netplay zip.

## Known issues

- Windows and Amiga cannot play online together yet.
- PC: the dragon fight and the ending have not been played through on PC yet; please report anything odd there.
- Online: only the leader's machine sends keyboard input on each screen, so the other player waits on shared screens.

## Checksums

Each archive has a `SHA256SUMS.txt` inside; the table above is the hash of the archive itself.
