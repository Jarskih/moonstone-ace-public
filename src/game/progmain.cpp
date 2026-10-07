// game/progmain - see progmain.hpp.  Transcribed from program.asm SECSTRT_0 (108), LAB_0000 (153) and LAB_0001 (156).
#include "game/progmain.hpp"

namespace ms { namespace game {

namespace {

constexpr uint16_t BOOT_FLAG_DIAG = 0x0080;                   // ANDI.W #$80,D0 ; CMP.W #$80,D0

inline void run(const ProgOps &ops, ProgStep eStep) { ops.step(ops.pCtx, eStep); }

}  // namespace

// SECSTRT_0 (program.asm 108)
void progMain(const ProgEnv &env, const ProgOps &ops, const BootRegs &regs) {
	*env.pChipFree = regs.ulChipFree;                         // MOVE.L A1,LAB_00C2
	*env.pChipSize = regs.ulChipSize;                         // MOVE.L D1,LAB_00C3
	*env.pFastFree = regs.ulFastFree;                         // MOVE.L A0,LAB_00C4
	*env.pFastSize = regs.ulFastSize;                         // MOVE.L D0,LAB_00C5
	run(ops, PSTEP_LAB_038F);                                 // JSR LAB_038F
	*env.pFlagsCopy = *env.pBootFlags;                        // MOVE.W EXT_0007,LAB_0005
	run(ops, PSTEP_SECSTRT_29);
	run(ops, PSTEP_SECSTRT_25);
	run(ops, PSTEP_LAB_0006);
	run(ops, PSTEP_LAB_0044);
	run(ops, PSTEP_SECSTRT_10);
	run(ops, PSTEP_SECSTRT_31_0274);                          // LEA LAB_0274,A0 ; JSR SECSTRT_31
	*env.pHandler0 = env.ulHandlerStop;                       // LEA LAB_011B,A0 ; MOVE.L #LAB_0014,(A0)
	run(ops, PSTEP_LAB_0051);
	if((*env.pFlagsCopy & BOOT_FLAG_DIAG) == BOOT_FLAG_DIAG) {   // MOVE.W LAB_0005,D0 ; ANDI.W #$80,D0 ; CMP.W #$80,D0 ; BEQ.W LAB_0001
		ops.endingRun(ops.pCtx);                              // LAB_0001 (the whole ending, up to its BRA.W LAB_0000)
	} else {
		*env.pIntroState = 0;                                 // MOVE.L #0,LAB_0060
		*env.pSceneGap = 0;                                   // MOVE.L #0,LAB_0123
		ops.introBegin(ops.pCtx);                             // MOVE.L #2,LAB_00D0 + the skip arm
		ops.introRun(ops.pCtx);                               // LAB_025F .. LAB_005B
	}
	ops.runMog(ops.pCtx);                                     // LAB_0000: LEA LAB_0004,A0 ; JMP SECSTRT_4
}

}}  // namespace ms::game
