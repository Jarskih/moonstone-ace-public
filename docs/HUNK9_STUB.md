# Hunk 9 protection stub (mog.asm 17804-18801) - investigation notes (task 1.4a)

STATUS: RESOLVED (2026-10-05). The return chain in step 6 ends at LAB_00B4+$c, the main menu; src/rt/hunk9.cpp
replays it, and the menu matches the original disks. The older notes below call LAB_03A7 "sabotage": it is only a
$FF fill that the menu itself also calls.

## Important facts
* The ADFs in D:\Amiga\moonshard are a Crystal CRACK (cracktro "Moonstone ... CRYSTAL crack"
  boots first, seen in WinUAE A500/KS1.3). They are not pristine originals; the crack may have
  altered the stub or the surrounding checks. "Genuine" behaviour cannot be proven from them.
* Entry: mog.asm:2849 `JMP SECSTRT_9` ends LAB_00B4 (1666: `JSR LAB_03F1; JMP LAB_012D`);
  called each pass of the main loop (mog.asm:167). No once-flag exists in the stub; the stub is
  self-restoring (re-encrypts each instruction after executing it), so it re-runs every pass.
  Cost in my emulation: ~10.8k instructions / 366 trace exceptions per pass.

## Step by step (static + unicorn emulation, tools in build/hunk9/run.py)
1. `JSR LAB_0D7B` = INTENA $4000 (interrupts master off). Nothing re-enables it inside the stub.
2. LAB_0A31: `MOVEM.L LAB_0A4B(PC),D0-D7/A0-A6` loads 13 relocated seed longs (HUNK_RELOC32 entries
   at hunk-9 offsets 0x92e..0x95e; value = base(hunk 0/9/34) + 0x7604xxxx style obfuscation).
   Pushed with MOVEM to the stack (they are address plumbing: encrypted code cannot carry relocs).
3. ILLEGAL vector ($10) := LAB_0A32; MOVEC CACR,D0 / MOVEC D0,CACR: illegal on 68000 (vector taken),
   executed on 68020+ (cache bit0 cleared). Both paths converge at LAB_0A32 (`A7:=A0`).
   CPU detection has no effect on the later decryption in my runs.
4. Registers reloaded from the opcode bytes at LAB_0A33 (PC-relative MOVEM); the next 10
   `MOVE.L #imm,-(A7)` build a trace handler on the stack:
   `[eor.l d6,(a6); eor.l d6,4(a6)]` + `movea.l 2(a7),a6; move.w (a7),d6; andi.w #$a71f,d6;
   add.w d6,d0; add.w d6,d1; eor.l d1,d6; eor.l d2,d6; eor.l d6,(a6); eor.l d6,4(a6);
   add.l d6,$10.l; rte`.  TRACE vector := A7+6, `ORI #$a71f,SR` turns T on.
   So: each instruction is decrypted after the previous one ran, key = f(CCR of the previous
   instruction, D1, D2) - the CCR makes the stream sensitive to exact CPU flag results,
   single-stepping, and any patch of the encrypted bytes. $10 accumulates the keys.
   Junk reg/flag-only instructions (cmp, tas, negx, exg...) and `move.l #imm,d(a7)` that patch the
   handler itself (key-schedule changes) fill the stream. The first handler pass skips the pre-EOR
   (vector A7+6, later A7), afterwards each pass re-encrypts the previous instruction (A6/D6 kept).
5. Stage 2 (0x39740..): handler replaced by a second one: key = long before PC + [$8] (=0),
   decrypts 8 bytes at PC, re-encrypts the previous instruction (pointer in [$c]), and
   `cmpi.w #$cf47,(pc)` -> if the decrypted word is `exg d7,d7` it does JSR (A2) (marker hook).
   Reads `$b8(a7)` (caller stack SR slot) and `$78(a7)` (saved relocated A2).
6. Stage 3 (0x39b5e): stores the constant $3d742cf1 into the saved-A2 stack slot ($78(a7)) and into
   vector $60.w, then decodes the relocated seeds with a shifting key: `sub.l d0,d1` into an
   address array at $9c(a7): in my run (hunk 0 at $20000) [$39c64 (the plaintext tail), LAB_02CE,
   LAB_00EE, LAB_03A7, $39c64, LAB_00B4+$c ...] i.e. a fake return chain. Final stage pushes a
   third handler, sets TRACE, then control reaches the plaintext tail at 0x39c64 with
   D0 = $3d742cf1.
7. Tail (mog.asm 18790-18801, plaintext): `cmp.l #$84d2501b,d0 / bne -> cmp.l #$3d742cf1,d0 /
   beq -> RTS ; else JMP LAB_03A7`; `$84d2501b` -> `JMP LAB_0328`.

## D0 derivation
D0 is not computed from disk data, memory checksums of game code, or the CPU type in the paths
I traced. $3d742cf1 is a constant planted by the decrypted plaintext (step 6); the $84d2501b
route (JMP LAB_0328) was never reached in my runs. Decryption correctness depends on exact 68000
CCR results of the junk stream (any patch of the encrypted bytes or wrong flags breaks it, which
then yields garbage/crash or the sabotage exit).

## Result of emulation (unicorn, M68000 model, hunk 0 at $20000, own trace-exception emulation)
Reached tail with D0=$3d742cf1 -> beq -> RTS. BUT the RTS popped the synthesized chain
(LAB_02CE -> LAB_00EE -> LAB_03A7), ending in sabotage, with my fake caller stack. I could not
tell whether that is an emulation artefact (stack layout vs the real caller, unicorn flag quirks;
I had to force flag flushing after each step) or the real behaviour of a cracked/unusual path.
Real caller return address sits at the top of the saved frame ($3efffc in my run, untouched).

## Open points / recommendation
* Not verified in WinUAE (no savestate was taken; the A500 run only reached the intro loading).
  Next check: savestate in the map, inspect $8/$c/$10/$24 and LAB_064D (are 0x2D0 bytes $FF?).
* Evidence on which exit is intended: RTS-by-constant ($3d742cf1) is the one the plaintext
  arranges; LAB_0328 would make LAB_0328 run twice per pass (it is already JSR'd from other
  places), the sabotage obviously is the tamper path.
* Provisional patch: replace `JMP SECSTRT_9` (mog.asm:2849) with `RTS` (current patch). Caller
  relies on nothing in registers (LAB_0001 loop re-loads all), but interrupts: the stub leaves
  INTENA master bit cleared ($4000 write) - the main loop re-enables it elsewhere (LAB_0D7C);
  check that dropping the stub does not leave INTEN differently set (the stub disabled it each pass).
* Scratch: build/hunk9/run.py (emulator), trace*.txt, a500.uae, key.ps1, shot.ps1.
