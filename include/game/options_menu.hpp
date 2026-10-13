// game/options_menu - the Options screen of the title menu (ROADMAP 9.9, docs/MODDING.md "Options screen"): the mod packs found at run time
// (folders of PROGDIR:mods_examples/ and PROGDIR:packs/) with ON / OFF, the two main settings of the base rules as left / right choices
// (DEXTERITY: OFF / ON, MONSTER SCALING: NONE / LIMITED / FULL), and BACK.  No original: new.  Pure on purpose (no ACE, no hardware, no OS), like game/net_menu:
//   * OptionsMenu   the state machine: input (joystick word, key code) -> cursor / toggles / settings -> a result.  Host-tested
//                   (tests/test_options_menu.py).
//   * SceneOptions  draws an OptionsMenu through MenuOps (game/scene_menu.hpp) on the title picture and runs it.
//   * OptionsSeam   what the screen asks of the machine: find the packs and read options.ini (pfnLoad), write options.ini and rebuild the
//                   live data (pfnSave).  src/rt/options_io.cpp provides it.
//
// Controls: joystick up / down moves the cursor (end stops, no wrap).  On a pack: left = OFF, right = ON, fire / Return = toggle.  On a
// setting: left / right = previous / next choice (wrapping), fire = next.  Fire on BACK or Esc leaves.  A fire press is taken on its rising
// edge only (the press that opened the screen does nothing).  In a network game the screen is read-only: the cursor moves, nothing changes.
//
// The list: packs in the order they are loaded (mods_examples first, then packs, each sorted by name).  A pack that sets only keys the
// settings already offer ([dexterity] / [waves] of rules.ini: no_scaling, limited_scaling, dexterity) is not listed (found by its keys, not
// its name); if an old options.ini still enables one, load turns it into the matching setting and a save drops it.  Switching a pack ON
// that sets a key another enabled pack sets switches that pack OFF and the status line says "<PACK> TURNED OFF": no conflict stays.
//
// The settings show the value in effect: what the player chose on the screen, else what the enabled packs and the loose files of mods/
// make of it, else the base rules (OFF, FULL).  Changing one stores it (options.ini) and it is applied after everything else.
#pragma once
#include <stdint.h>

#include "game/data/options_ini.hpp"
#include "game/scene_menu.hpp"

namespace ms { namespace game {

struct OptPack {
	char szName[OPT_NAME_MAX + 1];
	uint8_t ubSource;          // OptSource
	bool bOn;
	bool bTruncated;           // more keys than OPT_KEYS_MAX (conflicts may be missed)
	uint8_t ubFiles;           // data files the folder holds (0 = not a pack, dropped by finishPacks)
	uint8_t ubKeys;
	OptScan sc;                // rt: optionsScanClear, then optionsScanSettings for every data file (finishPacks hides the settings-only packs)
	uint32_t aulKey[OPT_KEYS_MAX];
};

// A pack the list hides because the screen offers its keys as settings: kept for the conversion of an old options.ini.
struct OptHidden {
	char szName[OPT_NAME_MAX + 1];
	uint8_t ubSource;
	uint8_t ubDex;             // what the pack sets (OPTDEX_UNSET = nothing)
	uint8_t ubWave;
};

enum OptRow : uint8_t { OPTROW_PACK = 0, OPTROW_DEX, OPTROW_SCALE, OPTROW_BACK };
enum OptResult : uint8_t { OPT_IDLE = 0, OPT_REDRAW, OPT_LEAVE };
enum OptKey : uint16_t { OPTKEY_ESC = 0x01, OPTKEY_RETURN = 0x1C };   // game key codes

struct OptInput {
	uint16_t uwJoy;            // joystick word (bit0 right, 1 left, 2 down, 3 up, 4 fire)
	uint16_t uwKey;            // game key code, 0 = none
};

// Layout.  The rows are drawn in the game's small font (Small.font, 5-6 px glyphs, 6 rows high, one colour: the title picture's colour 1), so
// the full pack names fit and eight packs are listed per page.  The big font stays for the title banner and the arrow (sprites).  Text y is
// the top of the glyph row; the arrow (38 x 19 sprite) is drawn OPT_ARROW_UP above the text y so its body is centred on the row, and the
// labels start right of it.  Packs: OPT_VISIBLE rows of OPT_PACK_STEP from OPT_PACK_Y.
enum { OPT_VISIBLE = 8 };
constexpr uint16_t OPT_PACK_Y = 70;
constexpr uint16_t OPT_PACK_STEP = 10;
constexpr uint16_t OPT_DEX_Y = 156;
constexpr uint16_t OPT_SCALE_Y = 166;
constexpr uint16_t OPT_BACK_Y = 178;
constexpr uint16_t OPT_STATUS_Y = 190;      // glyph rows end at y 195
// The settings follow the packs shown (owner 2026-10-11: no empty pack rows between them): one row for "NO PACKS FOUND" when there
// are none, at most OPT_VISIBLE.  With a full page they land on OPT_DEX_Y / OPT_SCALE_Y / OPT_BACK_Y, the lowest they ever go.
constexpr uint16_t optDexY(uint8_t ubPacks) {
	return (uint16_t)(OPT_DEX_Y - (OPT_VISIBLE - (ubPacks == 0 ? 1 : (ubPacks < OPT_VISIBLE ? ubPacks : OPT_VISIBLE))) * OPT_PACK_STEP);
}
constexpr uint16_t optScaleY(uint8_t ubPacks) { return (uint16_t)(optDexY(ubPacks) + (OPT_SCALE_Y - OPT_DEX_Y)); }
constexpr uint16_t optBackY(uint8_t ubPacks) { return (uint16_t)(optDexY(ubPacks) + (OPT_BACK_Y - OPT_DEX_Y)); }
constexpr uint16_t OPT_CURSOR_X = 0x32;     // the arrow, as the title menu's (x 50..87)
constexpr uint16_t OPT_ARROW_UP = 6;
constexpr uint16_t OPT_LABEL_X = 94;
constexpr uint16_t OPT_VALUE_X = 252;       // "LIMITED" (32 px) ends at x 284; "MONSTER SCALING" (73 px) ends at x 167
enum { OPT_LABEL_PX = 150 };                // pack names are drawn at most this wide: OPT_VALUE_X - OPT_LABEL_X - 8 px gap
enum { OPT_STATUS_MAX = 48 };               // status line buffer: a name of at most OPT_NAME_MAX + " TURNED OFF" (11) + NUL

// Advance of a character in Small.font (the cel width of its glyph through the game's character map: capitals and lower case share glyphs).
// Characters the font does not draw are blank and 5 px wide; the screen only prints capitals, digits, '.', ' ' and a few plain words.
uint16_t optGlyphWidth(char c);
uint16_t optTextWidth(const char *sz);

struct OptionsMenu {
	OptPack aPack[OPT_MAX_PACKS];
	uint8_t ubPacks;
	char aszGhost[OPT_GHOST_MAX][OPT_PATH_MAX];   // enabled packs of the file whose folder is gone: written back, never loaded
	uint8_t ubGhosts;
	OptHidden aHidden[OPT_MAX_PACKS];
	uint8_t ubHidden;
	uint8_t ubDex;             // OptDex the player chose (OPTDEX_UNSET = not set on the screen: the packs and files decide)
	uint8_t ubWave;            // OptWave likewise
	uint8_t ubLooseDex;        // rt: what the loose files of mods/ say (OPTDEX_UNSET = nothing); optionsScanSettings of them
	uint8_t ubLooseWave;
	char szNote[OPT_STATUS_MAX];   // "<PACK> TURNED OFF" until the next input that moves or changes something
	uint8_t ubCursor;          // 0..ubPacks+2: the packs, Dexterity, Monster scaling, BACK
	uint8_t ubFirst;           // first pack row shown
	bool bReadOnly;            // network game: nothing changes
	bool bChanged;             // a pack or a setting changed since init
	bool bFirePrev;

	void init(bool bReadOnlyScreen);
	// Packs (rt, before the screen opens): add the folders, set ubFiles / the keys, then finishPacks.
	OptPack *addPack(const char *szName, uint8_t ubSource);
	void finishPacks();        // drops folders without data files, moves the settings-only packs to aHidden, sorts (examples first, by name)
	// Enabled flags, ghosts, settings from options.ini; an enabled hidden pack becomes the matching setting (unless the file sets it).
	void applySaved(const OptionsSaved &s);
	void toSaved(OptionsSaved &s) const;       // enabled packs in list order, then the ghosts, and the settings that are set

	OptResult update(const OptInput &in);

	uint8_t rowCount() const { return (uint8_t)(ubPacks + 3); }
	OptRow rowKind(uint8_t ubRow) const;
	uint8_t effDex() const;    // OPTDEX_ON / OPTDEX_OFF in effect
	uint8_t effWave() const;   // OPTWAVE_NONE / LIMITED / ORIGINAL in effect
	// Display text: label (the pack name in capitals, '_' / '-' and other odd characters as spaces, cut at OPT_LABEL_PX pixels and trailing
	// spaces dropped; pOut holds OPT_NAME_MAX + 1), the value and the note line.
	void packLabel(uint8_t ubPack, char *pOut) const;
	const char *packValue(uint8_t ubPack) const;       // "ON" or "OFF"
	const char *dexValue() const;                      // "OFF" / "ON" (in effect)
	const char *waveValue() const;                     // "NONE" / "LIMITED" / "FULL" (in effect)
	// The status line ("" = none): the "TURNED OFF" note, else the read-only note, else "PACK n OF m" when the list scrolls and the cursor
	// is on a pack.  pOut holds OPT_STATUS_MAX bytes; the pack name is cut at OPT_LABEL_PX like the rows.
	void statusText(char *pOut) const;

private:
	OptResult move(int8_t sbDelta);
	OptResult adjust(int8_t sbDelta, bool bFire);
	OptResult activate();
	OptResult togglePack(OptPack &p, bool bNew);
	void scroll();
};

// ---------------------------------------------------------------------------------------------------------

struct OptionsSeam {
	void (*pfnLoad)(OptionsMenu &m);           // find the packs and read options.ini into m (m is init()ed)
	void (*pfnSave)(const OptionsMenu &m);     // write options.ini and rebuild the live data
};

struct SceneOptions {
	OptionsMenu menu;
	OptResult eResult;

	// Clears stale keys, draws, fades the title palette in.  `menu` must be loaded.
	void enter(const MenuOps &ops);
	// One pass (joystick + key, then the menu).  Returns true when the screen is finished.
	bool update(const MenuOps &ops);
	void exit(const MenuOps &ops);

private:
	void draw(const MenuOps &ops);
};

}}  // namespace ms::game
