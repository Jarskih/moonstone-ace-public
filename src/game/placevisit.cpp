// game/placevisit - see placevisit.hpp.  Transcribed from mog.asm LAB_007B (1191) .. LAB_00B3 (1651), LAB_009B / LAB_009C (1429 / 1486) and LAB_0DCA (25038).
#include "game/placevisit.hpp"

#include "game/scene_places.hpp"

namespace ms { namespace game {

// ---------------------------------------------------------------------------------------------------------
// The visit of a location (LAB_007B .. LAB_00B3)

const TownLayout kTownA = {0x100, {0x1E, 0x42, 0x6A, 0x8C, 0xB7}, 0x1A};   // LAB_009B
const TownLayout kTownB = {0x000, {0x1A, 0x3F, 0x65, 0x86, 0xB6}, 0x1F};   // LAB_009C

namespace {

inline PlaceRet pcall(const PlaceOps &ops, PlaceCall e, uint32_t ulD0 = 0, uint32_t ulD1 = 0, uint32_t ulA0 = 0, uint32_t ulA1 = 0) {
	return ops.pfnCall(ops.pCtx, e, ulD0, ulD1, ulA0, ulA1);
}

constexpr uint32_t RESTART = 0xFFFF;     // D0 after SECSTRT_36: the exit value of LAB_0011's DBF loop

// LAB_00B3: the map scene set-up, then back to the node menu with a non-zero D0 (it restarts the map screen).
uint32_t exitSetup(const PlaceOps &ops) {
	ops.pfnSceneSetup(ops.pCtx);                                      // JSR SECSTRT_36
	return RESTART;                                                   // (SECSTRT_36 leaves D0.l = $FFFF) ; RTS
}

// LAB_00B2: the turn is used up, then LAB_00B3.
uint32_t exitSpent(const PlaceCells &c, const PlaceOps &ops) {
	*c.puwSpent = *c.puwBudget;                                       // MOVE.W LAB_0665,LAB_0655
	return exitSetup(ops);
}

// LAB_00B1: the temple screen (scene 9), then LAB_00B2.
uint32_t exitTemple(const PlaceCells &c, const PlaceOps &ops) {
	pcall(ops, PCALL_SCREEN, 9);                                      // MOVEQ #9,D0 ; JSR LAB_04CF
	return exitSpent(c, ops);
}

// LAB_008B / LAB_0094: what the town screen shows: the cursor, the buttons, the picture, the palette fade.
void townDraw(bool bA, const PlaceCells &c, const PlaceOps &ops) {
	*c.puwMenuX = bA ? 0x122 : 0x1E;                                  // MOVE.W #$122 / #$1e,LAB_097F
	*c.puwMenuY = 0x64;                                               // MOVE.W #$64,LAB_0980
	pcall(ops, PCALL_CURSOR_ON);                                      // JSR LAB_0575
	townButtonsBuild(bA ? kTownA : kTownB, c, ops);                   // JSR LAB_009B / LAB_009C
	pcall(ops, PCALL_BLIT_SCREEN, 0, 0, *c.pulPicture, *c.pulScreen); // MOVEA.L LAB_0704,A0 ; MOVEA.L LAB_05C0,A1 ; JSR LAB_0419
	pcall(ops, PCALL_BLIT_BOTH);                                      // JSR LAB_0418
	pcall(ops, PCALL_FADE_TO, 0, 0, c.ulPalette);                     // LEA LAB_05B7,A0 ; JSR LAB_03F2
	pcall(ops, PCALL_KEY_RESET);                                      // JSR LAB_0B82
}

enum TownAfter { TA_REDRAW, TA_EXIT };

// The action behind a town button: what LAB_0090..LAB_0092 / LAB_0097..LAB_009A do before the screen is drawn again.
TownAfter townAction(bool bA, TownButton eButton, const PlaceOps &ops) {
	switch (eButton) {
		case TB_SMITH:                                                // LAB_0090 / LAB_0098: the smith screen (scene 5)
			pcall(ops, PCALL_FADE_OUT);
			pcall(ops, PCALL_CURSOR_OFF);
			pcall(ops, PCALL_SCREEN, 5);
			return TA_REDRAW;
		case TB_DICE:                                                 // LAB_008F / LAB_0097
			pcall(ops, PCALL_CURSOR_OFF);
			pcall(ops, PCALL_DICE);
			pcall(ops, PCALL_FADE_OUT);
			return TA_REDRAW;
		case TB_HEALER:                                               // LAB_0091 / LAB_009A
			pcall(ops, PCALL_FADE_OUT);
			pcall(ops, PCALL_CURSOR_OFF);
			pcall(ops, PCALL_HEALER);
			pcall(ops, PCALL_FADE_OUT);
			return TA_REDRAW;
		case TB_THIRD:
			if (bA) {                                                 // LAB_0099: the market (scene 6)
				pcall(ops, PCALL_FADE_OUT);
				pcall(ops, PCALL_CURSOR_OFF);
				pcall(ops, PCALL_SCREEN, 6);
			} else {                                                  // LAB_008E: Mythral the mystic
				pcall(ops, PCALL_CURSOR_OFF);
				pcall(ops, PCALL_MYSTIC);
				pcall(ops, PCALL_FADE_OUT);
			}
			return TA_REDRAW;
		default:                                                      // TB_EXIT: LAB_0092
			pcall(ops, PCALL_CURSOR_OFF);
			return TA_EXIT;
	}
}

}  // namespace

// LAB_008A / LAB_0093: the town's picture and message screen (LAB_012F / LAB_012E).
void placeTownLoad(bool bA, const PlaceOps &ops) {
	pcall(ops, bA ? PCALL_LOAD_TOWN_A : PCALL_LOAD_TOWN_B);           // JSR LAB_012E / LAB_012F
}

// LAB_008B..LAB_0092 (town B) and LAB_0094..LAB_009A (town A): draw / poll / act until the exit button.
uint32_t placeTownRun(bool bA, const PlaceCells &c, const PlaceOps &ops) {
	for (;;) {
		townDraw(bA, c, ops);                                         // LAB_008B / LAB_0094
		bool bRedraw = false;
		while (!bRedraw) {                                            // LAB_008C / LAB_0095
			const PlaceRet h = pcall(ops, PCALL_HIT_TEST, *c.puwMenuX, *c.puwMenuY);   // MOVEQ #0,D0 / D1 ; MOVE.W ... ; JSR LAB_0451
			if ((uint16_t)h.ulD0 != 0) {                              // TST.W D0 ; BEQ.S LAB_008D
				const PlaceRet j = pcall(ops, PCALL_JOYSTICK);        // MOVE.L A0,-(A7) ; JSR LAB_00EE ; MOVEA.L (A7)+,A0
				if (j.ulD1 & 0x10) {                                  // BTST #4,D1 ; BEQ.S LAB_008D: fire
					const TownButton eButton = townButton(ops.pfnButtonId(ops.pCtx, h.ulA0));   // JSR rt_places_town_btn
					if (eButton != TB_NONE) {
						if (townAction(bA, eButton, ops) == TA_EXIT) return exitSpent(c, ops);   // LAB_0092: BRA.W LAB_00B2
						bRedraw = true;
						continue;
					}
				}
			}
			PlaceRet k = pcall(ops, PCALL_KEY_XLAT, *c.puwKeyCell);   // LAB_008D: MOVE.W SECSTRT_21,D0 ; JSR LAB_0D8D
			if ((uint16_t)k.ulD0 != 0x20) continue;                   // CMP.W #$20,D0 ; BNE.W LAB_008C: space only
			if (bA) {                                                 // LAB_0096: the temple screen
				pcall(ops, PCALL_FADE_OUT);
				pcall(ops, PCALL_CURSOR_OFF);
				pcall(ops, PCALL_SCREEN, 9);
			} else {                                                  // LAB_008D: QUIRK: no MOVEQ before LAB_04CF, the scene is whatever D0 holds
				uint32_t ulD0 = pcall(ops, PCALL_FADE_OUT, k.ulD0).ulD0;
				ulD0 = pcall(ops, PCALL_CURSOR_OFF, ulD0).ulD0;
				pcall(ops, PCALL_SCREEN, ulD0);
			}
			bRedraw = true;
		}
	}
}

// LAB_009B / LAB_009C (mog.asm 1429 / 1486)
void townButtonsBuild(const TownLayout &l, const PlaceCells &c, const PlaceOps &ops) {
	pcall(ops, PCALL_BUTTONS_CLEAR);                                  // JSR LAB_044E
	TownButtonRec &r = *c.pButton;
	r.uwX = l.uwX;                                                    // MOVE.W #x,12(A0)
	r.uwY = l.auwY[0];                                                // MOVE.W #y,14(A0)
	r.uwW = 0x40;                                                     // MOVE.W #$40,4(A0)
	r.uwH = 0x10;                                                     // MOVE.W #$10,6(A0)
	r.ul8 = 0;                                                        // MOVE.L #0,8(A0)
	r.ulId = 1;                                                       // MOVE.L #1,16(A0)
	r.uw20 = 1;                                                       // MOVE.W #1,20(A0)
	r.uw22 = 0x4A;                                                    // MOVE.W #$4a,22(A0)
	pcall(ops, PCALL_BUTTON_ADD, 0, 0, c.ulButtonAddr);               // LEA LAB_0A58,A0 ; JSR LAB_0448
	r.uwY = l.auwY[1];
	r.ulId = 2;
	pcall(ops, PCALL_BUTTON_ADD, 0, 0, c.ulButtonAddr);
	r.uwY = l.auwY[2];
	r.ulId = 3;
	pcall(ops, PCALL_BUTTON_ADD, 0, 0, c.ulButtonAddr);
	r.uwY = l.auwY[3];
	r.uwW = 0x40;
	r.uwH = l.uwH4;
	r.ulId = 4;
	pcall(ops, PCALL_BUTTON_ADD, 0, 0, c.ulButtonAddr);
	r.uwY = l.auwY[4];
	r.uwH = 0x0C;
	r.ulId = 5;
	pcall(ops, PCALL_BUTTON_ADD, 0, 0, c.ulButtonAddr);
}

// LAB_0DCA (mog.asm 25038)
uint32_t valleyTail(const PlaceOps &ops, uint32_t ulD0) {
	ulD0 = pcall(ops, PCALL_KEY_RESET, ulD0).ulD0;                    // JSR LAB_0B82
	ulD0 = pcall(ops, PCALL_SETUP_TURN, ulD0).ulD0;                   // JSR LAB_0DBD
	ops.pfnValleyMoonstone(ops.pCtx);                                 // JSR LAB_04A1 ; BSET D0,22(A0) (the patch: rt_places_valley_stone)
	return ulD0;
}

// LAB_007B (mog.asm 1191) up to the CMP.W chain: the prologue every visit runs.
void placeVisitBegin(const PlaceCells &c, const PlaceOps &ops) {
	ops.pfnColourStop(ops.pCtx);                                      // JSR LAB_0DC8
	pcall(ops, PCALL_CEL_INIT5);                                      // MOVEQ #5,D7 ; JSR SECSTRT_28
	*c.puwPictureFlag = 0;                                            // MOVE.W #0,LAB_0D4C
	*c.pulSpriteBank = *c.pulE3;                                      // MOVE.L 0(A1),10(A0) with A0 = LAB_05E4, A1 = LAB_05E3
	pcall(ops, PCALL_SETUP_TURN);                                     // BSR LAB_0113 (a JMP to LAB_0DBD)
	pcall(ops, PCALL_JOBS_RESET);                                     // BSR LAB_0065: the arena reset
	pcall(ops, PCALL_CREATURE_CLEAR);
	pcall(ops, PCALL_FIGHT_TABLES);
	pcall(ops, PCALL_FADE_OUT);
	pcall(ops, PCALL_KEY_RESET);
	*c.pulFightLink = 0;                                              // CLR.L LAB_05F4
	*c.puwDragonActive = 0;                                           // MOVE.W #0,LAB_0667
}

// LAB_00B0: a castle visit gives a life.
uint32_t placeVillage(const PlaceCells &c, const PlaceOps &ops) {
	ops.pfnCastle(ops.pCtx);
	return exitTemple(c, ops);
}

// LAB_00A1
uint32_t placeStonehenge(const PlaceCells &c, const PlaceOps &ops) {
	if (!ops.pfnStoneMatches(ops.pCtx)) {                             // no stone of this phase: Danu's offer, LAB_00A5
		pcall(ops, PCALL_TEXT_RECOLOURED, 0, 0, c.ulTextStoneNo);     // LEA LAB_06CD,A0 ; JSR LAB_0137
		pcall(ops, PCALL_WAIT_FIRE);
		pcall(ops, PCALL_FADE_OUT);
		*c.puwStoneAnswer = 0xFFFF;                                   // MOVE.W #$ffff,LAB_053B
		pcall(ops, PCALL_SCREEN, 3);                                  // MOVEQ #3,D0 ; JSR LAB_04CF
		if (*c.puwStoneAnswer != 0xFFFF) {                            // the item screen changed it: an item was offered
			pcall(ops, PCALL_RITUAL);                                 // JSR LAB_04BF
			ops.pfnDanu(ops.pCtx);                                    // +1 life, healed, curse off
		}
		pcall(ops, PCALL_FADE_OUT);                                   // LAB_00A7
		return exitTemple(c, ops);
	}
	uint16_t uwEnding = (uint16_t)ops.pfnStoneCode(ops.pCtx);         // LAB_00A8: the ending code (D7)
	pcall(ops, PCALL_TEXT, 0, 0, c.ulTextStoneCode);                  // LAB_00AF: MOVE.W D7,-(A7) ; LEA LAB_06D1,A0 ; JSR LAB_0136
	pcall(ops, PCALL_WAIT, 20);
	pcall(ops, PCALL_WAIT_FIRE);
	pcall(ops, PCALL_DISK_PROMPT, 1);                                 // MOVEQ #1,D0 ; JSR LAB_0100
	uwEnding = (uint16_t)(uwEnding | 0x0080);                         // ORI.W #$80,D7: bit 7 = the ending / diagnostics pass
	*c.puwBootFlags = uwEnding;                                       // MOVE.W D7,EXT_000e ($3E0)
	ops.pfnRunProgram(ops.pCtx);                                      // LEA LAB_06D9,A0 ; JMP SECSTRT_5 (the patch: rt_run_program)
	return 0;
}

// LAB_009D
uint32_t placeValley(const PlaceCells &c, const PlaceOps &ops) {
	if (!ops.pfnValleyKeys(ops.pCtx)) {                               // not all four keys
		pcall(ops, PCALL_TEXT, 0, 0, c.ulTextValleyKeys);             // LEA LAB_06E8,A0 ; JSR LAB_0136
		pcall(ops, PCALL_WAIT_FIRE);                                  // BSR LAB_00EC
		return exitSetup(ops);                                        // BRA.W LAB_00B3 (the turn is kept)
	}
	pcall(ops, PCALL_SETUP_TURN);                                     // LAB_009E
	pcall(ops, PCALL_GUARDIAN_SETUP);                                 // JSR LAB_01A0
	pcall(ops, PCALL_FIGHT_RUN);                                      // JSR LAB_0036
	*c.pulLocked = 0;                                                 // CLR.L LAB_0662
	if (*c.pubDefeat & 1) {                                           // BTST #0,LAB_05DC: the knight lost
		pcall(ops, PCALL_SETUP_TURN);
		ops.pfnValleyDefeat(ops.pCtx);                                // the life and the stat
		return exitTemple(c, ops);                                    // BRA.W LAB_00B1
	}
	ops.pfnValleyVictory(ops.pCtx);                                   // LAB_00A0: +3 progress, the keys are taken
	pcall(ops, PCALL_TEXT_RECOLOURED, 0, 0, c.ulTextValleyWin);       // LEA LAB_06E1,A0 ; JSR LAB_0137
	pcall(ops, PCALL_WAIT_FIRE);
	const uint32_t ulD0 = pcall(ops, PCALL_WAIT, 10).ulD0;            // MOVEQ #10,D0 ; JSR LAB_0D74
	return valleyTail(ops, ulD0);                                     // JMP LAB_0DCA
}

// LAB_007C
uint32_t placeWizard(const PlaceCells &c, const PlaceOps &ops) {
	pcall(ops, PCALL_MATH_SHOW);                                      // JSR LAB_0456
	pcall(ops, PCALL_SCREEN, 9);
	return exitSpent(c, ops);                                         // BRA.W LAB_00B2
}

// The CMP.W chain after the prologue: the place of the kind (the id for no place: it falls into RTS with D0 = the id).
uint32_t placeVisitKind(uint8_t ubKind, uint32_t ulId, const PlaceCells &c, const PlaceOps &ops) {
	switch (ubKind) {
		case PK_VILLAGE: return placeVillage(c, ops);
		case PK_TOWN_A: placeTownLoad(true, ops); return placeTownRun(true, c, ops);    // LAB_0093
		case PK_TOWN_B: placeTownLoad(false, ops); return placeTownRun(false, c, ops);  // LAB_008A
		case PK_STONEHENGE: return placeStonehenge(c, ops);
		case PK_VALLEY: return placeValley(c, ops);
		case PK_WIZARD: return placeWizard(c, ops);
		case PK_DUEL:                                                 // BEQ.W LAB_004F with the prologue's A0 / A1
			return pcall(ops, PCALL_FIGHT_MEET, 0, 0, c.ulActiveAddr, c.ulE3Addr).ulD0;
		default:
			return ulId;
	}
}

// LAB_007B (mog.asm 1191).  The game runs the pieces as scenes (src/game/scenes/{village,town_a,...}.cpp, ROADMAP 9.2a).
uint32_t placeVisit(uint32_t ulId, const PlaceCells &c, const PlaceOps &ops) {
	placeVisitBegin(c, ops);
	return placeVisitKind(placeClassify((uint16_t)ulId, ops.pData), ulId, c, ops);   // the CMP.W chain (rt_places_classify)
}

}}  // namespace ms::game
