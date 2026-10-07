// game/combat_load - see include/game/combat_load.hpp.  Every function cites the mog.asm routine it replaces and keeps the order
// of the primitive calls of the original (the test compares the call sequence).  Word / byte widths follow the asm.
#include "game/combat_load.hpp"
#include "engine/guard.hpp"

namespace ms { namespace game { namespace cl {

namespace {

// DBF loop: D0 counts down from its low word to -1, so count + 1 passes of the low 16 bits.
inline uint32_t dbfBytes(uint32_t ulCount) { return (ulCount & 0xFFFFu) + 1u; }

// Kind tags of LAB_05DF.
enum : uint8_t {
	KIND_BE = 0x00, KIND_MUDMEN = 0x04, KIND_DEMON = 0x08, KIND_HE = 0x0C, KIND_DRAGON = 0x14, KIND_TROGG_AXE = 0x18,
	KIND_TROGG_SPEAR = 0x20, KIND_RATMEN = 0x24, KIND_BALOK = 0x30, KIND_WIZARD = 0x3C, KIND_TROLL = 0x40, KIND_NONE = 0xFF,
};

// The file slot of the asm side, open for the scope (ROADMAP 9.2b; the pack read closes inside packDone, see loadPack).
class OpenFile {
public:
	OpenFile(const Ops &o, uint16_t uwName) : m_o(o) { m_o.fileOpen(uwName); }
	~OpenFile() { m_o.fileClose(); }
	OpenFile(const OpenFile &) = delete;
	OpenFile &operator=(const OpenFile &) = delete;
	OpenFile(OpenFile &&) = delete;
	OpenFile &operator=(OpenFile &&) = delete;
private:
	const Ops &m_o;
};
MS_GUARD_PINNED(OpenFile);

struct Ctx {
	const Env &e;
	const Mem &m;
	const Ops &o;
	explicit Ctx(const Env &env) : e(env), m(env.m), o(env.o) {}

	uint32_t a(uint16_t l) const { return e.addr(l); }
	uint32_t l32(uint16_t l) const { return m.rd32(a(l)); }             // MOVE.L LAB_xxxx,Dn
	void s32(uint16_t l, uint32_t v) const { m.wr32(a(l), v); }
	uint16_t l16(uint16_t l) const { return m.rd16(a(l)); }
	void s16(uint16_t l, uint16_t v) const { m.wr16(a(l), v); }
	uint8_t l8(uint16_t l) const { return m.rd8(a(l)); }
	void s8(uint16_t l, uint8_t v) const { m.wr8(a(l), v); }
	// LEA tab,A0 ; MOVEA.L 4*i(A0),A1 : long i of a table / slot array.
	uint32_t at(uint16_t tab, uint32_t i) const { return m.rd32(a(tab) + 4 * i); }
	void put(uint16_t tab, uint32_t i, uint32_t v) const { m.wr32(a(tab) + 4 * i, v); }

	// The recurring pair of the loaders: JSR LAB_0CBB (load at `at`) ; JSR LAB_0CB6 (its size), result = the address after it.
	uint32_t celAdv(uint16_t name, uint32_t ulAt) const {
		o.celLoad(name, ulAt);
		return ulAt + o.celSize(name);
	}
	// JSR LAB_03CE (the hit set of the cel set at ulAt) ; JSR LAB_0CB6 : the size is that of the name's cel file.
	uint32_t hitAdv(uint16_t name, uint32_t ulAt) const {
		o.hitLoad(name, ulAt);
		return ulAt + o.celSize(name);
	}
	// LEA LAB_05B9,A0 ; MOVEA.L 4*n(A0),A0 : a picture of the pack.
	uint32_t pack(uint32_t n) const { return at(HEAP9, n); }
	// MOVE.L LAB_05E4+..: ActiveKnights +10 := LAB_05E3[4] (the moon / frame cel set), written as a long on a word boundary.
	void activeFromSlots3() const { m.wr32(a(ACTIVE) + 10, at(SLOTS3, 4)); }
	// `LEA tab,A0 ; MOVE.L (A0)+..` : the 32 palette words LAB_0D2B to a buffer (JSR LAB_0422).
	void copyPalette(uint16_t dst) const { m.copy(a(dst), a(PALETTE), 64); }
};

// ---- the arena pictures (copies out of the pack to LAB_05C2, then LAB_0C21) ----------------------------------------------------
// One call per picture: planes of the buffer `planesCell`, copy pack[packIx] with the DBF count sizes[k], unpack LAB_05C2.
void arenaPictureCopy(const Ctx &c, uint16_t planesCell, uint32_t ulPackIx, uint32_t ulCountSize) {
	c.o.planes(c.l32(planesCell));
	c.m.copy(c.l32(BUF_C), c.pack(ulPackIx), dbfBytes(ulCountSize));
	c.o.unpack(c.l32(BUF_C));
}

// LAB_0150: the shared tail of 014A / 014C / 014E: pack picture 1 ($5148) with the planes of LAB_0D92.
void pic1(const Ctx &c) { arenaPictureCopy(c, SCREEN_B, 3, c.e.sz.aulPic[1]); }
// LAB_014A: pack picture 0 ($5957) with LAB_05C1, then LAB_0150.
void pic0(const Ctx &c) {
	arenaPictureCopy(c, BUF_B, 2, c.e.sz.aulPic[0]);
	pic1(c);
}
// LAB_014C: pack picture 2 ($4657), then LAB_0150.
void pic2(const Ctx &c) {
	arenaPictureCopy(c, BUF_B, 5, c.e.sz.aulPic[2]);
	pic1(c);
}
// LAB_014E: pack picture 3 ($3A54); falls through into LAB_0150.
void pic3(const Ctx &c) {
	arenaPictureCopy(c, BUF_B, 4, c.e.sz.aulPic[3]);
	pic1(c);
}
// LAB_0142: pack picture 5 ($51C3) with the planes of LAB_05C0 (no tail).
void pic5(const Ctx &c) { arenaPictureCopy(c, BUF_A, 8, c.e.sz.aulPic[5]); }

// The backdrop script of an arena: when LAB_076D = 2 (a creature fight) the one in LAB_076E (and the cycle counter stays), else
// the caller takes the next entry of the arena's cycle table.
bool backdropFixed(const Ctx &c) {
	if(c.l32(SCENE_076D) == 2) {
		c.o.backdrop(c.l32(PARAM_076E));
		return true;
	}
	return false;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// LAB_00F8 (the drive probe LAB_0B18 is patched to "-1" = no extra drive; LAB_0BB4 to RTS; LAB_0100 to RTS)
void driveInit(const Env &e) {
	const Ctx c(e);
	c.s16(DRIVES, 1);                                       // LAB_05D0: drives found
	c.s16(DISK_UNIT0, 0);                                   // LAB_05CC..05CF: disk id per unit (A on unit 0)
	c.s16(DISK_UNIT1, 0xFFFF);
	c.s16(DISK_UNIT2, 0xFFFF);
	c.s16(DISK_UNIT3, 0xFFFF);
	c.s16(UNIT_DISK1, 1);                                   // LAB_06FE..0701: unit per disk id
	c.s16(UNIT_DISK2, 0xFFFF);
	c.s16(UNIT_DISK3, 0xFFFF);
	c.s16(UNIT_DISK4, 0xFFFF);
	c.s16(UNIT_SEL, 0);                                     // LAB_0105: CLR.W LAB_0B35 (selected unit)
	c.s16(DISK_FLAG, 1);                                    // MOVE.W #1,LAB_0D4D
}

// ---------------------------------------------------------------------------------------------------------------------------------
// LAB_0115
void loadKnights(const Env &e) {
	const Ctx c(e);
	uint32_t ulAt = c.at(HEAP8, 1);                         // MOVEA.L 4(LAB_05B8),A1
	c.put(SLOTS1, 0, ulAt);
	ulAt = c.celAdv(N_KN1, ulAt);
	c.put(SLOTS1, 1, ulAt);
	ulAt = c.celAdv(N_KN2, ulAt);
	c.put(SLOTS1, 2, ulAt);
	ulAt = c.celAdv(N_KN3, ulAt);
	c.put(SLOTS1, 3, ulAt);
	e.o.hitLoad(N_KN4, ulAt);                               // kn4: only its hit set
	c.s32(HIT_0A4F, c.l32(HIT_0A4D));
	c.s32(HIT_0A50, c.l32(HIT_0A4E));
	e.o.celLoad(N_BLO, c.l32(HEAP_05BB));
	e.o.music(MUSIC_KNIGHTS);
}

// LAB_0116: kind $0C
void loadHe(const Env &e) {
	const Ctx c(e);
	if(c.l8(MODE) == KIND_HE) {
		return;
	}
	c.s8(MODE, KIND_HE);
	uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.celAdv(N_HE1, ulAt);
	c.put(SLOTS0, 1, ulAt);
	ulAt = c.celAdv(N_HE2, ulAt);
	c.put(SLOTS0, 2, ulAt);
	ulAt = c.celAdv(N_HE3, ulAt);                           // the size is taken (and unused) like the original
	c.put(SLOTS0, 3, c.at(SLOTS1, 3));                      // the knights' slots 3 and 4
	c.put(SLOTS0, 4, c.at(SLOTS1, 4));
}

// LAB_0118: kind $20
void loadTroggSpear(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	if(c.l8(MODE) == KIND_TROGG_SPEAR) {
		return;
	}
	c.s8(MODE, KIND_TROGG_SPEAR);
	uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.celAdv(N_TROGGSPEAR1, ulAt);
	for(uint32_t i = 1; i <= 4; ++i) {                      // slots 1..4 all point behind the first set
		c.put(SLOTS0, i, ulAt);
	}
	e.o.hitLoad(N_TROGGSPEAR2, ulAt);
	e.o.music(uwMusic);
}

// LAB_011A: kind $18
void loadTroggAxe(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	if(c.l8(MODE) == KIND_TROGG_AXE) {
		return;
	}
	c.s8(MODE, KIND_TROGG_AXE);
	uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.celAdv(N_TROGGAXE1, ulAt);
	c.put(SLOTS0, 1, ulAt);
	e.o.hitLoad(N_TROGGAXE2, ulAt);
	e.o.music(uwMusic);
}

// LAB_011C: kind $24.  QUIRK: Ratmen1's hit set is registered and its size taken, but its cel set is not loaded here (the cels
// of slot 0 are the ones already at the start of the arena).
void loadRatmen(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	if(c.l8(MODE) == KIND_RATMEN) {
		return;
	}
	c.s8(MODE, KIND_RATMEN);
	uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.hitAdv(N_RATMEN1, ulAt);
	c.put(SLOTS0, 1, ulAt);
	e.o.celLoad(N_RATMEN2, ulAt);
	e.o.music(uwMusic);
}

// LAB_011E: kind $04, always
void loadMudmen(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	c.s8(MODE, KIND_MUDMEN);
	const uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	e.o.hitLoad(N_MUDMEN1, ulAt);
	const uint32_t ulSet2 = c.pack(11);                     // LAB_05B9[11]: the cel buffer after the pack
	c.put(SLOTS0, 1, ulSet2);
	e.o.celLoad(N_MUDMEN2, ulSet2);
	e.o.music(uwMusic);
}

namespace {
// LAB_0120 (tail of 011F / 0121): LAB_05E1[4] := LAB_05B9[11] and Kn5.ob there.
void loadKn5(const Ctx &c) {
	const uint32_t ulAt = c.pack(11);
	c.put(SLOTS1, 4, ulAt);
	c.o.celLoad(N_KN5, ulAt);
}
}  // namespace

// LAB_011F: kind $30
void loadBalok(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	if(c.l8(MODE) != KIND_BALOK) {
		c.s8(MODE, KIND_BALOK);
		uint32_t ulAt = c.at(HEAP8, 2);
		c.put(SLOTS0, 0, ulAt);
		ulAt = c.hitAdv(N_BALOK1, ulAt);
		c.put(SLOTS0, 1, ulAt);
		ulAt = c.celAdv(N_BALOK3, ulAt);                    // LAB_05E0[1] + size
		c.put(SLOTS0, 2, ulAt);
		e.o.celLoad(N_BALOK2, ulAt);
		e.o.music(uwMusic);
	}
	loadKn5(c);
}

// LAB_0121: kind $14, always
void loadDragon(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	c.s8(MODE, KIND_DRAGON);
	uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.hitAdv(N_DRAGON1, ulAt);
	c.put(SLOTS0, 1, ulAt);
	ulAt = c.hitAdv(N_DRAGON2, ulAt);
	c.put(SLOTS0, 4, ulAt);
	e.o.hitLoad(N_DRAGON5, ulAt);
	e.o.music(uwMusic);
	loadKn5(c);
}

// LAB_0123: kind $00 (the check is against zero: a fresh table has $00 only after LAB_013A set it to $FF)
void loadBe(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	if(c.l8(MODE) == KIND_BE) {
		return;
	}
	c.s8(MODE, KIND_BE);
	uint32_t ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.hitAdv(N_BE1, ulAt);
	c.put(SLOTS0, 1, ulAt);
	e.o.celLoad(N_BE2, ulAt);
	e.o.music(uwMusic);
}

// LAB_0125: kind $08, always; first the pack picture 5 (LAB_0142)
void loadDemon(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	pic5(c);
	c.s8(MODE, KIND_DEMON);
	uint32_t ulAt = c.pack(11);
	c.put(SLOTS0, 3, ulAt);
	ulAt = c.celAdv(N_DEMON4, ulAt);
	c.put(SLOTS0, 4, ulAt);
	e.o.celLoad(N_DEMON1, ulAt);
	ulAt = c.at(HEAP8, 2);
	c.put(SLOTS0, 0, ulAt);
	ulAt = c.hitAdv(N_DEMON2, ulAt);
	c.put(SLOTS0, 2, ulAt);
	e.o.hitLoad(N_DEMON3, ulAt);
	e.o.music(uwMusic);
}

// LAB_0126: kind $40
void loadTroll(const Env &e, uint16_t uwMusic) {
	const Ctx c(e);
	if(c.l8(MODE) != KIND_TROLL) {
		c.s8(MODE, KIND_TROLL);
		c.s32(TMP_0632, c.at(HEAP8, 2));                    // LAB_0632 is the running pointer
		c.put(SLOTS0, 0, c.l32(TMP_0632));
		e.o.hitLoad(N_TROLL1, c.l32(TMP_0632));
		c.s32(TMP_0632, c.l32(TMP_0632) + e.o.celSize(N_TROLL1));
		c.put(SLOTS0, 1, c.l32(TMP_0632));
		e.o.hitLoad(N_TROLL2, c.l32(TMP_0632));
	}
	const uint32_t ulAt = c.pack(11);
	c.put(SLOTS1, 4, ulAt);
	e.o.celLoad(N_KN5, ulAt);
	e.o.music(uwMusic);
}

// LAB_0128: LAB_05E2[0] = LAB_0664 (a cel buffer of the map code), [1..4] = LAB_05B9[12]
void loadKiMi(const Env &e) {
	const Ctx c(e);
	const uint32_t ulAt = c.pack(12);
	c.put(SLOTS2, 0, c.l32(CELS_0664));
	for(uint32_t i = 1; i <= 4; ++i) {
		c.put(SLOTS2, i, ulAt);
	}
	e.o.celLoad(N_KI, ulAt);
	e.o.celLoad(N_MI, c.l32(CELS_0664));
}

// LAB_0131: wizard screen (kind $3C): backdrop synths, two pictures, the cel set wi1.c
void loadWizard(const Env &e) {
	const Ctx c(e);
	messageNext(e);
	e.o.planes(c.l32(BUF_C));
	e.o.pictureLoad(N_WI2P, c.l32(SCREEN_B));
	c.copyPalette(PAL_05E5);
	e.o.planes(c.l32(BUF_B));
	e.o.pictureLoad(N_WI1P, c.l32(SCREEN_B));
	c.copyPalette(PAL_05E6);
	if(c.l8(MODE) != KIND_WIZARD) {
		c.s8(MODE, KIND_WIZARD);
		const uint32_t ulAt = c.at(HEAP8, 2);
		c.s32(TMP_0632, ulAt);
		for(uint32_t i = 0; i < 5; ++i) {
			c.put(SLOTS0, i, ulAt);
		}
		e.o.celLoad(N_WI1C, ulAt);
	}
	e.o.music(MUSIC_WIZARD);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// LAB_0129
void screenFromChar(const Env &e) {
	const Ctx c(e);
	e.o.blank();
	e.o.clear(c.l32(SCREEN_A));
	e.o.planes(c.l32(SCREEN_A));
	e.m.copy(c.l32(SCREEN_B), c.pack(14), dbfBytes(e.sz.ulRawChar));
	e.o.unpack(c.l32(SCREEN_B));
}

// LAB_0138
void screenFromMessage(const Env &e) {
	const Ctx c(e);
	e.o.blank();
	e.o.clear(c.l32(SCREEN_A));
	e.o.planes(c.l32(SCREEN_A));
	e.m.copy(c.l32(SCREEN_B), c.pack(13), dbfBytes(e.sz.ulRawMessage));
	e.o.unpack(c.l32(SCREEN_B));
}

// LAB_012B
void drawMoon(const Env &e) {
	const Ctx c(e);
	screenFromChar(e);
	c.activeFromSlots3();
	e.o.text(c.a(TEXT_0709));
	const uint16_t uwFrame = e.m.rd16(c.a(ACTIVE) + 18);     // ActiveKnights::uwMoonFrame
	c.s16(FLAG_0D05, 1);
	e.o.drawCel(c.at(SLOTS2, 1), uwFrame, 0x77, 0x0C);
	c.s16(FLAG_0D05, 0);
	e.o.palette(c.a(PALETTE));
}

// LAB_0133
void startSynths(const Env &e) {
	for(uint16_t i = 0; i < 4; ++i) {
		e.o.synth(0x6E + i, i);
	}
}

// LAB_012C combat_assets_load
void loadAssets(const Env &e) {
	const Ctx c(e);
	e.o.blank();
	{
		const OpenFile sMessage(e.o, N_MESSAGE);
		e.o.fileRead(c.pack(13), e.sz.ulRawMessage);
	}
	const uint32_t ulFonts = c.pack(10);
	c.put(SLOTS3, 4, ulFonts);
	e.o.celLoad(N_BOLD, ulFonts);
	e.o.music(MUSIC_CAMPAIGN);
	startSynths(e);
	const uint32_t ulSmall = c.at(SLOTS3, 4) + e.o.celSize(N_BOLD);
	c.put(SLOTS3, 0, ulSmall);
	e.o.celLoad(N_SMALL, ulSmall);
	{
		const OpenFile sChar(e.o, N_CH);
		e.o.fileRead(c.pack(14), e.sz.ulRawChar);
	}
	screenFromChar(e);
	c.activeFromSlots3();
	c.s32(CELBASE_0705, c.at(SLOTS3, 4));
	c.s16(FLAG_0D05, 1);
	e.o.drawCel(c.l32(CELBASE_0705), 0x49, 0x05, 0x14);
	e.o.drawCel(c.l32(CELBASE_0705), 0x4A, 0x16, 0xB5);
	e.o.drawCel(c.l32(CELBASE_0705), 0x4B, 0x6E, 0xBE);
	c.s16(FLAG_0D05, 0);
	e.o.text(c.a(TEXT_0706));
	e.o.palette(c.a(PALETTE));
}

// LAB_012D: no return in the original (JMP SECSTRT_9)
void selectScreen(const Env &e) {
	const Ctx c(e);
	e.o.celLoad(N_SEL, c.l32(BUF_B));
	c.activeFromSlots3();
	screenFromChar(e);
	e.o.copyScreen(c.l32(SCREEN_A), c.l32(SCREEN_B));
	e.o.copyScreen(c.l32(SCREEN_A), c.l32(BUF_A));
	e.o.hunk9();
}

namespace {
// LAB_0136: the message screen with a record list.
void messageScreen(const Ctx &c, uint32_t ulList) {
	c.s32(TMP_05E7, ulList);
	screenFromMessage(c.e);
	c.activeFromSlots3();
	c.o.text(c.l32(TMP_05E7));
}
// LAB_0130: a place picture (A0 = name) into LAB_05C2, its palette to LAB_05B7, the fade, two screen copies.
void placePicture(const Ctx &c, uint16_t name) {
	c.o.pictureLoad(name, c.l32(BUF_C));
	c.copyPalette(PAL_05B7);
	c.o.fadeOut();
	c.o.copyScreen(c.l32(PICBASE_0704), c.l32(BUF_A));
	c.o.copyScreens();
}
}  // namespace

// LAB_012E
void placeHighWood(const Env &e) {
	const Ctx c(e);
	c.s32(SCRIPT_0713, c.a(TEXT_071B));
	messageText(e, c.a(TEXT_070F));
	c.s32(PICBASE_0704, c.l32(BUF_B));
	e.o.planes(c.l32(BUF_B));
	placePicture(c, N_HIGHWOOD);
}

// LAB_012F
void placeWaterDeep(const Env &e) {
	const Ctx c(e);
	c.s32(SCRIPT_0713, c.a(TEXT_071C));
	messageText(e, c.a(TEXT_070F));
	c.s32(PICBASE_0704, c.l32(BUF_B));
	e.o.planes(c.l32(PICBASE_0704));
	placePicture(c, N_WATERDEEP);
}

// LAB_0134
void messageNext(const Env &e) {
	const Ctx c(e);
	startSynths(e);
	screenFromMessage(e);
	c.activeFromSlots3();
	const uint16_t uwIx = c.l16(PIC_NEXT);
	e.o.text(e.m.rd32(c.a(PIC_TABLE) + (uint16_t)(uwIx << 2)));   // MOVEA.L 0(A0,D0.W),A0 with D0.W = counter << 2
	const int16_t swNext = (int16_t)(c.l16(PIC_NEXT) + 1);  // ADDI.W #1 ; CMPI.W #$0E ; BLT
	c.s16(PIC_NEXT, (uint16_t)swNext);
	if(!(swNext < 0x0E)) {
		c.s16(PIC_NEXT, 0);
	}
	e.o.palette(c.a(PALETTE));
}

// LAB_0136
void messageText(const Env &e, uint32_t ulList) {
	const Ctx c(e);
	messageScreen(c, ulList);
	e.o.palette(c.a(PALETTE));
}

// LAB_0137: after the text the palette entries 1..6 are overwritten (the avoid-text screen's colours).
void messageTextRecoloured(const Env &e, uint32_t ulList) {
	const Ctx c(e);
	messageScreen(c, ulList);
	static const uint16_t kColours[6] = {0x0800, 0x0600, 0x0400, 0x0000, 0x0200, 0x0100};
	const uint32_t ulPal = c.a(PALETTE) + 2;
	for(uint32_t i = 0; i < 6; ++i) {
		e.m.wr16(ulPal + 2 * i, kColours[i]);
	}
	e.o.palette(c.a(PALETTE));
}

// ---------------------------------------------------------------------------------------------------------------------------------
// LAB_013A
void loadPack(const Env &e) {
	const Ctx c(e);
	c.s16(CYCLE_05EA, 0);
	c.s16(CYCLE_05EB, 0);
	c.s16(CYCLE_05E9, 0);
	c.s16(CYCLE_05E8, 0);
	c.s8(MODE_05DD, KIND_NONE);
	c.s8(MODE_05DE, KIND_NONE);
	c.s8(MODE, KIND_NONE);
	e.o.fileOpen(N_PACK);
	e.o.fileRead(c.pack(2), e.sz.ulRawPack);
	e.o.packDone();                                         // rt_mog_pack_done (the close LAB_0BFF + the 6-plane pack walk)
}

// LAB_013C
void arenaPicture(const Env &e) {
	const Ctx c(e);
	e.o.clear(c.l32(BUF_A));
	e.o.backdropReset();
	switch(c.l32(ARENA_08C4)) {
		case 8: {                                           // LAB_013D
			pic2(c);                                        // LAB_014C
			arenaPictureCopy(c, BUF_A, 9, e.sz.aulPic[6]);
			if(!backdropFixed(c)) {
				const uint16_t uwIx = c.l16(CYCLE_05E8);
				e.o.backdrop(e.m.rd32(c.a(CYCLE_TAB_B8) + (uint16_t)(uwIx << 2)));
				c.s16(CYCLE_05E8, (uint16_t)((c.l16(CYCLE_05E8) + 1) & 7));
			}
			break;
		}
		case 0xC: {                                         // LAB_0140
			pic3(c);                                        // LAB_014E
			pic5(c);                                        // LAB_0142
			if(!backdropFixed(c)) {
				const uint16_t uwIx = c.l16(CYCLE_05E9);
				e.o.backdrop(e.m.rd32(c.a(CYCLE_TAB_B6) + (uint16_t)(uwIx << 2)));
				c.s16(CYCLE_05E9, (uint16_t)((c.l16(CYCLE_05E9) + 1) & 7));
			}
			break;
		}
		case 0: {                                           // LAB_0144
			pic0(c);                                        // LAB_014A
			arenaPictureCopy(c, BUF_A, 7, e.sz.aulPic[7]);
			if(!backdropFixed(c)) {
				const uint16_t uwIx = c.l16(CYCLE_05EA);
				e.o.backdrop(e.m.rd32(c.a(CYCLE_TAB_B7) + (uint16_t)(uwIx << 2)));
				c.s16(CYCLE_05EA, (uint16_t)((c.l16(CYCLE_05EA) + 1) & 7));
			}
			break;
		}
		case 4: {                                           // LAB_0147
			pic0(c);                                        // LAB_014A
			arenaPictureCopy(c, BUF_A, 6, e.sz.aulPic[4]);
			if(!backdropFixed(c)) {
				const uint32_t ulIx = (uint32_t)c.l8(CYCLE_05EB);        // MOVE.B ; LSL.L #2
				e.o.backdrop(c.at(CYCLE_TAB_B9, ulIx));
				c.s8(CYCLE_05EB, (uint8_t)((c.l8(CYCLE_05EB) + 1) & 7));
			}
			break;
		}
		default:
			break;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// LAB_0152
void clearTables(const Env &e) {
	const Ctx c(e);
	e.m.fill(c.a(TBL_060C), 0, 0x2A0);                      // MOVE.L #$029F,D0 ; CLR.B (A0)+ ; DBF
	e.m.fill(c.a(TBL_05F5), 0, 0x33C);                      // MOVE.L #$033B,D0
	fillTables(e);                                          // falls into LAB_0155
}

// LAB_0155: the fighters' script tables (record of 9 longs each): handler / animation / script data addresses and counts.
void fillTables(const Env &e) {
	const Ctx c(e);
	// LAB_05F5: data addresses by creature type 0..8
	static const uint16_t k05F5[9] = {D_07DB, D_07EF, D_07ED, D_07EA, D_07F4, D_07E9, D_07F1, D_07F3, D_07EE};
	// LAB_05F6
	static const uint16_t k05F6[9] = {D_07DB, D_07F6, D_07F6, D_07F5, D_07DC, D_07F6, D_07F5, D_07DC, D_07F5};
	for(uint32_t i = 0; i < 9; ++i) {
		c.put(TBL_05F5, i, c.a(k05F5[i]));
		c.put(TBL_05F6, i, c.a(k05F6[i]));
	}
	// LAB_05F7: counts (only the listed types)
	c.put(TBL_05F7, 2, 4);
	c.put(TBL_05F7, 5, 2);
	c.put(TBL_05F7, 1, 3);
	c.put(TBL_05F7, 8, 4);
	c.put(TBL_05F7, 3, 3);
	c.put(TBL_05F7, 6, 3);
	// LAB_0610: three blocks of five (index 0..4, 8..12, 16..20): four addresses and a zero terminator each
	static const uint16_t k0610[3][4] = {
		{D_07DD, D_07DE, D_07DF, D_07E0}, {D_07E1, D_07E2, D_07E3, D_07E4}, {D_07E5, D_07E6, D_07E7, D_07E8}};
	for(uint32_t b = 0; b < 3; ++b) {
		for(uint32_t i = 0; i < 4; ++i) {
			c.put(TBL_0610, b * 8 + i, c.a(k0610[b][i]));
		}
		c.put(TBL_0610, b * 8 + 4, 0);
	}
	// LAB_05F8
	c.put(TBL_05F8, 8, 0x1C);
	c.put(TBL_05F8, 2, 0x10);
	c.put(TBL_05F8, 1, 0x1C);
	c.put(TBL_05F8, 5, 0x1C);
}

}}}  // namespace ms::game::cl
