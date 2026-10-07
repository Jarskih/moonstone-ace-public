# Interrupts and input (ROADMAP 2.2, 2.3)

Citations are `file:line` in `../moonshard/moonstone-main/amiga_asm/` (IRA listings). program and mog carry
byte-identical copies of the engine; mog labels are given second (`prg / mog`).

## 1. The original handlers

Installed at init by `LAB_0325` / `LAB_0B49` (program.asm:6205-6226, mog.asm:20386-20402): `AUTO_INT1..6` vector pokes,
`CIAA_ICR $17` clear then `$88` set (serial port interrupt only), `CIAB_ICR $1f` (all off), `INTENA $1f95` clear
(TBE, SOFT, COPER, AUD0-3, RBF, DSKSYN off), `$a06a` set (DSKBLK, PORTS, VERTB, BLIT, EXTER on). It is bracketed by
`LAB_0556` (`INTENA $4000`, master off) and `LAB_0557` (`INTENA $c000`, master on) (program.asm:10325-10330).

| Level | Handler (prg / mog) | INTREQ bits serviced and acked | Game variables / work |
|---|---|---|---|
| 1 | `LAB_0326` / `LAB_0B4A` (6227) | DSKBLK(1): `LAB_0364 = 0`; SOFT(2), TBE(0): ack only | disk-DMA-done flag (trackdisk, replaced by rt/files) |
| 2 | `LAB_032A` / `LAB_0B4E` (6245) | PORTS(3): acks, reads CIAA ICR; if SP: reads `CIAA_SDR`, keyboard handshake (CRA bit 6 output, TA one-shot 2, SDR=0, wait ICR SP, ack), then `ROR.B #1` on the raw byte: bit 7 = pressed, low 7 = inverted raw code; `LAB_036C[code]` translates; a press stores the translated key in `LAB_0362` (also the low byte of word `SECSTRT_16`, which `LAB_0322` polls for "any key"); `LAB_036D[xlat] = 1/0` (6264-6279) | `LAB_0362`, `LAB_036D[128]` key-down flags |
| 3 | `LAB_0331` / `LAB_0B55` (6312) | BLIT(6): `LAB_0367 = 0` (blit-done flag, polled by `LAB_0324`); VERTB(5): `LAB_0379 += 1` while `LAB_0363 != 0` (frame counter), `JSR LAB_034D` (mouse delta from `JOY0DAT` + `EXT_0035`, fire from `CIAA_PRA` bit 6, pointer position `LAB_0375`/`SECSTRT_17`), `POTGO = 1`, `JSR SECSTRT_15` (sound/music/job tick), ack; COPER(4): ack only | frame counter `LAB_0379`, pointer, blit-done |
| 4 | `LAB_0337` / `LAB_0B5B` (6351) | AUD3..AUD0 (bits 10..7): ack, `JSR` through the cells `LAB_0371..LAB_036E` (default `RTS` = `LAB_0360`) | sound callbacks |
| 4 (mog, replaces the above) | `LAB_0F69` (mog.asm:27888), installed by `LAB_0AA7` (19335, patch `int4-vector`) | loops on INTREQR/INTENAR per audio channel: acks, writes the ST/NT player state to `AUDxLC/LEN` and `INTENA` | audio player |
| 5 | `LAB_033C` / `LAB_0B60` (6380) | RBF(11): ack; DSKSYN(12): `LAB_0366 = 0`, ack | disk |
| 6 | `LAB_033F` / `LAB_0B63` (6394) | EXTER(13): reads `CIAB_ICR`, FLG (bit 4) -> `LAB_0365 = 0`, ack | disk index |

Dead code: the unlabeled block after `LAB_032D`'s RTE (program.asm:6285-6310 + `LAB_0342`, mog `LAB_0B66`) is an
alternative INT2 handler with a pause key (code `$ff`, `MOVE (A7),SR` + spin). Nothing references it. The stack-switch
prologue of `LAB_0331` (6313-6325, `LAB_0B55`) is skipped because the DATA word `LAB_0373` / `LAB_0B97` is initialised to 1
(and restored to 1 on every overlay entry, docs/BOOT_CHAIN.md section 4); it must never run under ACE.

Entry/exit contract: raw 68000 autovector handlers. Each saves exactly the registers it uses (`MOVEM.L ...,-(A7)`) and
ends in `RTE`; the frame on the stack is the exception frame. Handlers do their own `INTREQ` acks (a single write; ACE
acks again afterwards, which is harmless). The level 3 and level 4 handlers call subroutines (`LAB_034D`, `SECSTRT_15`,
audio callbacks) and so need a normal stack.

## 2. Strategy under ACE (state after ROADMAP 7.1d)

ACE owns the CPU vectors (`systemUnuse` in `splashCreate`) and keeps its own level 2/3/4/6 dispatch with VBL
`timerOnInterrupt`, so the game must not poke `AUTO_INT*` (that would kill ACE's timer and keyboard). The original handlers are
therefore not installed any more. What they did is C++ (`src/rt/irq.cpp`, `src/rt/input.cpp`, `src/engine/input.cpp`):

* **Install.** The patches `irq2-init` (`asm/patches/{program,mog}.irq2.json`) replace the three lines after the beam wait in
  `LAB_0325` / `LAB_0B49` (first mouse counter read, master off) by `JMP rt_prg_irq_init` / `rt_mog_irq_init`. The shim
  (all registers kept) runs `rt::irqInstall(cells)`: bind the overlay's cells (`rt::IrqCells`, the twin labels of program S_16/S_17
  and mog S_21/S_22), first mouse counter reading, `systemSetInt` for VERTB/BLIT/COPER (level 3) and AUD0-3 (level 4), the keyboard
  callback, master interrupt on, and returns to the caller of `LAB_0325`. The older `irq-install` patch (PEA of the six handler labels,
  `rt_irq_install`) and the no-op symbol are gone (7.1 cleanup). INTENA: `systemSetInt`
  force-enables its bit, so install saves and restores the state of COPER and AUD0-3 (the original masks them, the sound player enables
  the channels itself).
* **Level 3 (VBL/BLIT/COPER)** is `rt::irqLevel3()`. `rt_irq_tramp3` (top-level asm) is a plain function for ACE: it saves all
  registers, switches to a private 4 KB stack and calls it. The body is the original `LAB_0331` in the original order: read INTREQR once;
  BLIT: blit-done word := 0, ack; VERTB: frame counter (while `LAB_0363` is non-zero), mouse pointer (`inputPointerTick`, `LAB_034D`),
  `POTGO` := 1, the hook list (`SECSTRT_15`), ack; COPER: ack. ACE calls the trampoline once per pending source; the later calls find
  nothing pending (idempotent). The motor-off timeout at the head of `SECSTRT_15` (cells `LAB_038C/038D`) is gone: nothing positive
  is ever stored there since the file layer is `rt/files`. The stack-switch prologue of `LAB_0331` (skipped by `LAB_0373 == 1`) is gone too.
* **VBL hook list** (`LAB_0372` / `LAB_0B96`, nine routine addresses, 0 terminated) stays an asm DATA array: asm code appends entries
  (palette tick, copper effects, the synth tick, the joystick cursor `LAB_057D`, now `rt_mog_cursor_on`) and removes them by clearing the slot.
  `runHooks` walks it, re-reading after every call (a hook may edit the list), and calls each routine through `rtIrqCallHook`, which saves
  the registers C expects preserved and passes the next slot in A0 like the original dispatcher.
* **Level 4 (audio).** `rt_irq_tramp4` reads the cell `rt_irq_h4`. Null (after install): the C++ default `rt::irqLevel4Default()`, which
  acks the four audio bits (the original handler called four callback cells that always held an RTS). The synth installs its own asm handler
  (mog `LAB_0F69`, patch `int4-vector`, `rt_irq_set_int4`; since ROADMAP 7.1g only with `MS_SYNTH_ASM`: the C++ synth replaces the cell by `rt_synth_int4` in its instrument relocation shim, docs/AUDIO.md section 8): then the trampoline pushes a faked 68020 format-0 frame (vector
  word, return PC, SR) and jumps to it, so its RTE lands in the trampoline and returns to ACE. Needs a 68010+ (frame format word); the target is 68020.
* **Level 2 (keyboard)**: the game handler is not called (it would race ACE's own SDR handshake); `rt/input` replaces the CIA-A SERIAL
  callback (section 3). **Levels 1, 5, 6** (disk DMA, serial/disk sync, CIA-B index) only served trackdisk and are not installed.
* **Trackdisk residue.** The motor/step/CIA-B routines (program S_13, mog S_18) had no caller left after the VBL service went: program's are
  dead; in mog `irq2-td-motor` turns the drive-select routine `LAB_0B37` (reached by falling through the `LAB_0B35` data cell) into an RTS, which
  leaves only the probe stub `LAB_0B18` (a `MOVEQ #-1,D0 ; RTS` since `files-drive-detect`, called by the drive prompt code `LAB_00F8-00FD`, 7.1f).
* **`rt_irq_disable/enable`** (`INTENA $4000/$c000`): the two master-enable writes at the original `LAB_0556/0557` (`LAB_0D7B/0D7C`) sites (asm shims).
  Pending requests stay latched in INTREQ, so ACE's VBL timer and key interrupts are delayed, never lost, while the game is in a short
  critical section. ACE touches INTEN only in `systemUse/Unuse`, which does not run while the game runs.
* `rt_loader_park_irq`, `rt_trap15_*`: removed in ROADMAP 4.7 (dead loader code, now inert data, and dead TRAP #15 helper; docs/DEAD_RT.md).

## 3. Input (rt/input, engine/input)

* **Keyboard**: `inputInstall` registers `inputKeyIsr` with `systemSetCiaInt(CIA_A, CIAICRB_SERIAL)`, replacing ACE's `onKeyInterrupt`
  (installed by `keyCreate`). It reads `~SDR` (ACE format `(key<<1)|released`), calls ACE's `onKeyInterrupt` for the handshake and
  `g_sKeyManager`, then `ms::keyDecode` / `keyApply` write the game tables exactly as the original did (section 1, level 2): index
  `(~key)&0x7f` into the overlay's translation table (`LAB_036C` / `LAB_0B90`, 128 bytes, asm data, listed in `tools/tables.yaml` as
  `keymap_program` / `keymap_mog`), press -> `LAB_0362` / `LAB_0B86`, `LAB_036D[xlat]` / `LAB_0B91[xlat]` = 1/0. Because `LAB_0362` is the low
  byte of the "any key" word, `LAB_0322`-style waits work (`LAB_0B46` is `rt_mog_key_wait`, `LAB_035E` / `LAB_0B82` key reset are `rt_prg_key_reset` /
  `rt_mog_key_reset`).
* **Mouse pointer**: one sample per VBL inside `irqLevel3` (`ms::mouseStep`: counter deltas with the original's 255 - d wrap quirk, position
  clamp -7..319 / -7..199, the activity cells, the left button). A pointer movement writes the *long* 0x10 at `LAB_036B`, which also lands on the
  first word of the key table (indices 0/1 = raw $7F/$7E, never sent by a keyboard): kept as is.
* **Joystick** (mog only): `LAB_00EE` (both ports + fire into `LAB_062F/0630`, `ms::joyRead`, port 0 drops every bit when left+right or
  up+down are both set), `LAB_00EA` (the joystick of a record), `LAB_00EC` (wait for fire press and release) and the cursor on/off
  `LAB_0575` / `LAB_057B` are `rt_mog_joy_read`, `rt_mog_joy_port`, `rt_mog_wait_fire`, `rt_mog_cursor_on/off`. The shims keep the original
  register contracts (D0.w/D1.w results, everything else preserved). The cursor routines still call the sprite routines `LAB_0E75-0E78` (asm, 7.1e)
  through `rtInputAsm`.
* Facade: `rt::input*` is the only code touching keyboard hardware; the game tables are written only in `feedGame` and `inputPointerTick`.

## 4. Debug / verification aids (compile-time, off by default)

* `-DMS_IRQ_TRACE`: the ISRs queue events (VBL line every 50th frame, every key event, install); `rt_irq_disable/enable`
  (main context, called constantly) flush them to `PROGDIR:irq.log` through dos.library inside `systemUse()` (same pattern as
  rt/files). Stock WinUAE has no Bartman log trap and its TCP serial port never opened, so a file on the HD mount is the sink.
* `-DMS_IRQ_SELFTEST` (first install only): enables interrupts, polls the beam for 500 frames and logs beam frames vs ACE VBL
  count (VERTB services of `irqLevel3`) vs the game's own frame counter (`LAB_0379`), after a phase that validates the beam counter against INTREQ.
  Keys pressed in the window appear as `KEY` lines. Used because the game's main flow may stall before it calls the hooks.
* Note: `systemUse()/systemUnuse()` (rt/files OsGuard, the trace flush) re-enable INTEN and every INTENA bit registered
  with `systemSetInt` (COPER, AUD0-3 included) on the way out; harmless (their handlers ack) but not the original masking.

Tests: `tests/test_input.py` (pure decoding against models; the m68k code of `rt/irq`, `rt/input` and `engine/input` against the original handlers and
joystick/key routines in unicorn on a fake chip set), `tests/test_irq.py` (patch/symbol contracts).

Run: `cmake -S . -B build/irqtest/bld ... "-DCMAKE_CXX_FLAGS=-DMS_IRQ_TRACE -DMS_IRQ_SELFTEST"`, copy the exe to an HD
mount (chip RAM 4 MB, as moonstone-ace-hd.uae) and read `irq.log`.
