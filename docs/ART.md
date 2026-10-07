# Art pipeline: templates, palettes, redrawn art (ROADMAP 5.2b)

`tools/artconv.py` turns the original graphics into indexed PNG templates and builds game files back from edited PNGs, at
5 planes (the original) or 6 planes (the 64-colour redesign). Needs Pillow and numpy. Everything it writes goes under
`build/art/` (git-ignored: the original art is personal-use material, never commit it).

```sh
py tools/artconv.py export                               # build/disks -> build/art/export/<disk>/<file>/
py tools/artconv.py import build/art/export/B/ki.cel --planes 6 --out build/art/import/B
py tools/artconv.py import --all --planes 6              # everything -> build/art/import/<disk>/<file>
py -m unittest tests.test_artconv                        # round trip (about a minute)
```

Options: `export --masks` also writes derived 1-bit masks, `--only "B/*.ob"` limits the set, `--preview-palette X.piv`
colours the sprite templates from another picture. `import --keep-planes|--min-planes` see "Planes" below.

## 1. Templates (`build/art/export/<disk>/<file>/`)

Disk is `A`/`B`/`C` (the extracted ADFs in `build/disks`); identical files on B and C are exported twice. Each directory holds
`sidecar.json` (everything needed to rebuild the file: source name/size/SHA-1, header fields, frame table, palette words)
and the PNGs. Files are recognised by content, not extension.

| Format | Files (count) | Template |
|---|---|---|
| cel family | `.cel` `.ob` `.c` `.f` `.font` (51) | `frames/NNN.png`, one per frame, cropped to the frame size; `sheet.png` is a contact sheet for looking only |
| picture (`.piv`, `.p`, `mindscape`) | 23 | `image.png`, always 320x200 |
| picture pack | `B/test` (1) | nine pictures back to back; `00/image.png` .. `08/image.png` |
| tile map | `co.stile`, `intro.stile` (2) | no PNG: `sidecar.json` holds the 480 index words (the tiles themselves are cut from the background pictures; the map is not art) |
| `palettes.json` (export root) | | every picture palette (raw word + 24-bit), per-colour-index diversity, palette DATA found in `mog.asm` |

Not graphics, so not exported (135 files): the hunk executables `nb`/`program`/`mog`/`Crystal`, `.a` (8SVX audio), `.cmp`
(RNC music), `.t` (arena object tables), `collide.hit`, `s/startup-sequence`, and `A/kn1.ob` (a 5-byte stub).

### Frame fields (cel sidecar)

`width`/`height` (taken from the PNG on import, so a frame may change size), `hot_x` (high nibble of byte 8: pixels subtracted
from the x position when drawing, 0 in every original), `flags` (low nibble, 1 in every original, meaning unknown, kept),
`plane_bits` (which planes the original frame stores; see Planes), `offset` (informational: import recomputes it). A frame with
width or height 0 has no PNG (`png: null`); a frame whose `plane_bits` is 0 (the font's space glyphs) is an all-colour-0 PNG.
To add or drop frames edit the `frames` list in `sidecar.json`; the game addresses frames by index, so order matters.

## 2. Colours and palettes

### Enhanced 64-colour templates

The current templates in `build/art/export/A`, `B` and `C` have been enhanced with
64-entry indexed palettes. The unmodified templates are backed up under
`build/art/export-original`. The previous unsplit enhancement is backed up in
`build/art/export-before-split`. All 77 sidecars are retained, including the two
tile maps (which have no pixels). `enhancement.json` at the export root records
the changed-pixel and used-colour counts for each of the 2,426 source PNGs.

`tools/artenhance.py` adds conservative intermediate shading between nearby
tones inside opaque artwork. It preserves dimensions, the pixel grid, colour-0
pixels, sprite transparency and palette entries 0–31. The new range is split:

* **32–47:** sixteen shared sprite shades, identical in every sprite, font,
  picture and picture-pack palette. Sprites and fonts never use indices 48–63.
* **48–63:** sixteen screen-local background shades. Pictures may also use
  shared shades at 32–47 where these approximate their target shading better.

Shared shades are quantized from sprite/font shading targets only; background
shades are quantized separately from each original picture palette's targets.
The split enhancement was regenerated from the original templates, rather
than repeatedly quantizing the previous enhancement.

Strong colour boundaries and flat areas remain sharp; there is no
dithering or resizing. Every PNG has 64 palette entries, but small or flat
images may use fewer than 64 colours. Contact sheets are regenerated from the
enhanced frames. Files with the same original preview palette share the same
expanded palette, and all palettes share the sixteen sprite shades.

```sh
py tools/artenhance.py                         # export -> export64; refuses an existing output
py tools/artenhance.py --in-place              # backs up to export-original; refuses an existing backup
py tools/artenhance.py --in-place --original build/art/export-original # redo; backup to export-before-split
py tools/artconv.py import --all --planes 6 --out build/art/import6
py tools/artenhance.py --verify-import build/art/import6
py -m unittest tests.test_artenhance tests.test_artconv
```

These are automatically enhanced artist templates, not hand-redrawn assets.
Sprite palettes remain previews, but the shared new shades now occupy the same
slots in every picture's palette. Verification checks this invariant and
rejects sprites using screen-local slots, in addition to comparing rebuilt
pixels and palettes. Existing runtime
recolouring/cycling of entries 0–31 does not automatically recolour the new
shades at 32–63; corresponding ramps need runtime palette updates. The
six-plane runtime requirements in section 4 still apply.

* **Index 0 is transparent in every sprite.** The renderer (`LAB_04B5`) stores no mask: it ORs the frame's planes into one and
  cookie-cuts through it, so a pixel is opaque iff its index is not 0. Colour 0 is also black on every background, so nothing
  inside a sprite can be opaque black through index 0; use another index for a black that should show. Sprite PNGs are written with
  `tRNS` on index 0 so an editor shows the cut-out.
* **Masks.** There is no stored mask in any format, so `--masks` only writes what the game would derive. If a `NNN_mask.png`
  exists on import it is honoured in one direction only: a pixel whose mask is 0 is forced to index 0. A mask 1 over index 0 cannot be
  represented (a warning is printed). Because an edited frame plus a stale mask would silently drop new pixels, masks are off by default.
* **Sprites have no palette.** `.cel/.ob` files only carry colour indices; the colours come from whichever picture is on screen.
  The PNG palette of a sprite template is a preview (default: `A/bg1a.PIV`) and means nothing to the game. On import the PNG palette is
  written to `<file>.pal` for reference only.
* **Pictures carry their own palette**: 32 words for 5 planes, 16 for 4 planes (`bg7`, `message.piv`, `ch.piv`), after the 6-byte
  header. A word is `$0RGB` (4 bits per gun); if bit 15 is clear the loader doubles the word (3-bit guns), all originals have bit 15
  set. Templates show it as 24-bit by nibble replication (`$F` -> 255). On import at 5 planes colours you did not change keep their
  original word (and flag); changed colours are rounded to the nearest 4-bit value and written with bit 15 set.
* **What is shared (observed in `palettes.json`, not a rule of the engine).** Only index 0 (black) is identical in all 22 pictures
  except the splash (`mindscape`). Among the backgrounds `bg1a/b/c`, `bg2/2a`, `bg3`, `bg5/5a`, `bg8` (disk A) a block repeats almost
  exactly: 8-11 (reds, `f00 a00 600 300`), 13-14 (`fa0 f60`), 19-22 (browns, `210 321 531 852`), 28-31 (`f85 c53 821 510`); these carry the
  knights' skin/armour and UI tones that sprites rely on. The disk C pictures (`HighWood`, `WaterDeep`, `hea`, `tav`, `mys`, `dice`, `WI1/2`,
  `hen1.p`) use their own palettes. `ch.piv` (combat backdrop) and `message.piv` are 4-plane: only colours 0-15 come from them.
  The code also writes colours at run time: `mog.asm` copies `LAB_08D1..LAB_08D5` (knight/creature variants, in `palettes.json`
  as `code_palettes_mog`) and immediate colour words (`LAB_03F5..LAB_0401`) over the live combat palette, and colour cycles/ramps/fades
  (`LAB_0575` family) change entries on the fly. Palette DATA in code is **not** rewritten by artconv.
* **Rule for redrawn art:** a sprite is drawn on top of a picture, so use the colour indices of the screen it appears on. Design one
  64-entry palette per screen (picture) and keep sprite colours in the same indices; keep index 0 black/transparent; keep any index
  that code recolours or cycles (the knight/creature ranges above) in the same role. At 5 planes you have 32 indices, at 6 planes 64.

### 256-colour (8-plane) templates (ROADMAP 4.8d, art side)

`tools/artenhance256.py` regenerates every template from `build/art/export-original` into `build/art/export256` (about 45 s; the 64-colour
tool `artenhance.py` is untouched and still byte-identical, `tests/test_artenhance256.py` regenerates it and diffs against `build/art/export`).
Same sidecars, plus `"colours": 256`, piv/pivpack records say `planes: 8` (`planes_original` keeps the old value). Every PNG has a 256-entry palette.
`enhancement256.json` holds the layout and per-image counts. These are templates for judging the look; no runtime reads 8 planes yet.

| Indices | Content |
|---|---|
| 0..31 | original palette (picture palette; bg1a preview for sprites), as today |
| 32..47 | 16 shared sprite shades, as in the 64-colour templates |
| 48..63 | 16 picture-local shades (black in sprite previews), as in the 64-colour templates |
| **64..159** | **96 shared sprite/cel/font shades, identical in every picture/sprite/font palette** |
| **160..255** | **96 picture-local shades** (black in sprite previews); sprites never use them |

0..63 are bit-identical to the 64-colour palette, so 64-colour art and the game's palette effects on 0..31 keep their meaning (as before, runtime
ramps/cycles do not recolour 32+; those need runtime palette updates). Sprites use 1..47 and 64..159 only; index 0 stays transparent.

Why 96/96 (`py tools/artenhance256.py --measure`, shading targets of the original art, mean RGB distance of the nearest of K median-cut colours):

* sprites + cels + fonts: 23,176 distinct targets over 486,849 candidate pixels. Shared K=16: 20.4, 32: 14.1, 48: 11.6, 64: 9.2, 80: 8.0, 96: 6.6,
  112: 5.5, 128: 5.2. The curve flattens past about 96-112.
* backgrounds: 27 palette groups, 89 .. 13,100 distinct targets per picture (median 2,774). Local K=16: 12.0, 32: 7.4, 48: 5.4, 64: 4.3, 96: 3.1,
  112: 2.7, 128: 2.5 (mean over groups; the worst group is 6.2-7.4 at 96-128).
* 192 slots are free above 64. Sprites get the knee (96); backgrounds get the other 96 plus their existing 0..63 and the shared block (they may use all
  of it), so they end with about 3 mean error. Shared 80/local 112 would trade 1.4 sprite error for 0.4 background error, so 96/96 was kept.
  Shift the split with `--shared N` (then `SHARED_START`/`LOCAL_START` in the tool and the tests follow `enhancement256.json`).

What the shader does (all inside regions, never across colour 0, same pixel grid, no resizing, no new detail):

* a finer 3x3 gaussian blend of each opaque pixel with neighbours that are opaque and close in colour (distance <= 110), strength 0.7, then nearest of
  the allowed palette entries (1..63 as allowed for sprite/picture, shared block, local block) when that is clearly closer than the old tone;
* **de-dither**: a pixel whose 5x5 neighbourhood has exactly two colours (no 0, distance <= 150), the minority at least 4 pixels and never
  4-adjacent to itself (checkerboard, 25/75% ordered dots) is replaced by the gaussian mean, i.e. the intended intermediate tone. Real edges (connected runs)
  and isolated single highlight pixels are not touched. Dither of three or more tones in one window (Bayer 4x4 gradients) is not recognised;
* the local block is quantised from the targets that 0..63 plus the shared block still miss by more than 6 RGB units, so it adds new tones instead of
  repeating old ones; unused slots are black.

```sh
py tools/artenhance256.py                       # -> build/art/export256 (refuses an existing output)
py tools/artenhance256.py --verify build/art/export256 --against64 build/art/export
py tools/artconv.py import --all --export build/art/export256 --planes 8 --out build/art/import8
py tools/art256_preview.py                      # original 32 | current 64 | new 256 -> build/shots/art256-{outdoor,arena,sprites}.png
py -m unittest tests.test_artenhance256
```

`artconv import --planes 8` writes picture headers with plane count 8 and 256 palette words, cel `plane_bits` up to `$FF`, and `.pal` with 256 entries; the
loader and the 8-plane display are 4.8d's runtime side and are not done here.

## 3. Import

`import <dir>` reads `sidecar.json` and the PNGs and writes the game file (same name as the original) to `--out`. The PNGs
must be **indexed** (mode P); RGB images are refused. Frame sizes may change; pictures must stay 320x200. A colour index that
does not fit the chosen plane count is an error naming the file (`--planes 5`: 0-31, `--planes 6`: 0-63).

### 5 planes (`--planes 5`, the original format)

Same layout as the originals; decoded content (header, frame table, every plane byte, palette words) is byte-identical to the original
when the PNGs are unchanged: `tests/test_artconv.py` checks all 77 graphics files, then decodes both the original and the new
packed streams with the project's `ms::lzssDecode` (host build of `src/engine/lzss.cpp`) and compares. Packed bytes differ from the
originals (own LZSS encoder: window 2047, length 3..34, hash chains, one-step lazy match, no end marker; 0-7% smaller, 2.5% on average). A 4-plane
picture stays 4-plane unless the edited image needs index 16+.

### 6 planes (`--planes 6`, enhanced)

* **Picture**: `BE16 6, BE32 packed size, 64 palette words ($8000 | 12-bit rounding of the colour), LZSS body`. The body is 48000 bytes: plane
  0..5 one after another, each 200 rows of 40 bytes (the same plane-sequential layout as the originals, one more plane). Sidecar
  `<name>.pal`: `"MSPL"`, byte version 1, byte 0, `BE16 count` (64), then `count` x RGB bytes: the **full 24-bit palette** from the PNG. The
  12-bit words in the header exist only so a loader that does not know `.pal` still gets the colours the original path can show.
  A pack (`test`) writes `test.00.pal`, `test.01.pal`, ...
* **Sprite (`.cel/.ob/...`)**: header unchanged (`BE16 frames, BE32 packed size, BE32 8 x decoded size`), 10-byte frame records, `plane_bits` now
  uses bit 5 (`$20`) for the sixth plane; planes stored per frame in ascending bit order, each `ceil(w/16)*2 x h`. Default is `--min-planes`:
  a frame stores only the planes it uses (absent planes draw as zero under the mask, so this is equivalent and smaller). `--keep-planes`
  (default at 5 planes) keeps the original frame's planes as well. `<file>.pal` is the PNG palette, for reference. No mask is stored (see above).

Chosen: a separate `.pal` instead of widening the header, because the cel family has no palette field and the picture header's palette area
is sized `1 << planes` by the original loader (`LAB_03FC`: 32 bytes for 4 planes, else 64). A 6-plane loader needs `128` bytes there anyway,
so the rewritten loader (below) reads `1 << planes` words and, when present, `<file>.pal`.

## Trying your art in the game (ROADMAP 5.2a)

`rt/files` serves a replacement from `art/` before it looks at the original disk files, so redrawn art drops in file by file.

1. Build 5-plane game files from your PNGs (the game is still 5-plane): `py tools/artconv.py import --all --planes 5`, or one file,
   `py tools/artconv.py import build/art/export/B/ki.cel --planes 5 --out <dir>`. The output file has the original's name (`bg1a.PIV`, `ki.cel`).
2. Put the file in **`art/` under the install directory**, which is `PROGDIR:` (where the `moonstone` executable is). With the HD install that is
   `<build dir>/hd/art/` (`build-game-debug/hd/art/` or `build-game/hd/art/`, next to `hd/data/`; `hdinstall` wipes and re-creates the whole `hd/` tree, `art/` included, so keep your files elsewhere (e.g. `build/art/import`) and copy
   them in after each stage; `docs/FILES.md` section 5 has an optional CMake step that does it).
3. Run the game. `PROGDIR:files.log` shows `art override <name> <size>` once per replaced file; the following `open PROGDIR:art/<name> <size>` line
   is the normal per-open record. Delete the file from `art/` (or the whole directory) to get the original back; nothing else changes when `art/` is absent.

What is checked, per `Open` by the game:

* **Name**: the game's file name must be a plain name (no `/` or `:`, 30 characters at most, not `.`/`..`); only `art/<name>` is tried, never a subdirectory. Case does not
  matter (AmigaDOS: `art/BG1A.piv` overrides `bg1a.PIV`). The file must have the same name as on the disk; there is no alias table.
* **6-plane files are refused for now**: a picture whose BE16 plane count at offset 0 is 6, or a cel with any frame whose `plane_bits` (byte 9 of a 10-byte frame record) has
  `$20` set, is not served; the original is used and the log says `art override is 6-plane, needs MS_ENHANCED (4.8a), using original`. The 5-plane game
  cannot show them. Pictures with 4 or 5 planes and everything else (not recognised as a picture/cel, e.g. audio or tables) are served as they are.
  A cel whose frame table is larger than the 32 KB read buffer is not checked and falls back to the original (logged).
* The `.pal` sidecar is not read yet (it is only meaningful at 6 planes, section 4).
* The file is not validated beyond that: a truncated or wrong-format replacement behaves like a corrupt disk file. Keep the dimensions the game expects for
  pictures (320x200); cel frame sizes may change but the game's own layout code may not expect it.

The checks are `ms::artNameIsPlain/artNameEqual/artClassify` in `src/engine/artcheck.cpp` (pure, host-tested by `tests/test_art_override.py`).

## 4. What the game needs before 6-plane art shows up

Nothing in the shipped asm or C++ reads 6-plane files yet; the template/import side is ready ahead of the runtime. (5-plane replacements already load, via 5.2a.)

* **5.2a** asset override: done for 5-plane files (see "Trying your art in the game"); 6-plane files are detected and refused until 4.8a, and `<name>.pal` is not read yet.
* **4.4a** renderer depth: `ms::planCel/runCel` and the copy paths handle 5 planes (`CEL_MAX_PLANES`, 5 gather ops, temp planes of stride
  `0x12C0` with plane 5 = the mask plane, mask built from planes 0-4 by two blits, 5 `dest` plane pointers). Six planes need a 6th destination
  plane, a 7th temp plane (the mask moves to index 6) and the mask blit must OR plane 5 in as well.
* **Picture loader** (`LAB_03FC`/`LAB_0402`, mog twins): accept plane count 6, read `1 << planes` palette words, decode 48000 bytes into six
  plane buffers (the original decodes straight into `LAB_04D9..`), and read the `.pal` if present. The temp read buffer (`LAB_052A`, 32-bit
  copy loop) must hold the largest packed file.
* **4.8a** display: 6 bitplanes (BPLCON0 plane count, bitplane pointers in the copper list, 48000 bytes per screen buffer instead of 40000, so the
  two arenas and chip memory budget need rechecking), 64 colour registers.
* **4.5a** palette path: the live palette, fade/ramp/cycle code and `LAB_01CB..` contexts are 32 words; they need 64 entries, and 24-bit values (AGA
  colour banks, `BPLCON3`) for the `.pal` colours.
* Code that recolours indices (knight/creature palette patches in `mog.asm`) must be re-pointed at the new palette layout.

## 5. Known limits

* Everything in `build/disks` that is a graphics file round-trips at 5 planes; nothing failed. Not covered because they are not pixels: the stile map is
  copied word for word, code palettes are left alone, and no format here is interleaved or RLE (`LAB_0448`/`LAB_0434` are unused by any shipped file).
* Dropping unreferenced bytes: every original cel body is exactly the concatenation of its frames in table order, so the importer lays frames out
  back to back (verified on all 51 files); a hand-edited sidecar with overlapping offsets is not supported.
* `hot_x` is the only hot spot the format has (an x offset); frame `flags` are preserved but their meaning is unknown.
