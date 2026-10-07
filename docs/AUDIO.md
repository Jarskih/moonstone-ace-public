# Audio: entry points, ownership, and ptplayer (ROADMAP 4.6, 7.1b, 7.1g, 2.6, 2.12)

Citations are `file:line` in `../moonshard/moonstone-main/amiga_asm/` (IRA listings, never edited).

## 1. Two different sound systems

| | program | mog |
|---|---|---|
| What it is | ST/NT-style **module player** (ProTracker layout), CHIP code in S_1 (program.asm:1081-1560) | **Synth/sequencer driver**: 4 voice structs, ADSR/instrument tables, per-voice sequence scripts, CHIP code+data in S_44 (mog.asm:27734-28900) |
| Music source | `music.cmp` (pass 1: intro, prologue) / `vmusic.cmp` (pass 2: ending), RNC-packed, decoded in place to program `LAB_0124` (chip arena, loaded at program.asm:3473-3480 and 3603-3616, unpacked by `ms::rncDecodeInPlace`) | Sound banks loaded per area by `LAB_0AAA..LAB_0AB4` (`Re.a`, `He.a`, ...; sequences `LAB_1098` table, instruments `LAB_10A2`, S_45 waveforms) |
| Music and sfx | Music only. No sample is ever triggered besides the player (the only `AUDxLC/VOL` accesses are in S_1 and the fade, program.asm:1156-1254, 10681-10747; the four INT4 callback cells `LAB_036E..0371` stay at their default `RTS`) | Both, on the same four channels: tunes and effects are sequences started on a channel |
| Tick | VBL: `LAB_005C` -> `LAB_0065` is a job in the `LAB_0372` list that `SECSTRT_15` runs from the level-3 handler | VBL: `LAB_0F73` job (list `LAB_0B96`, installed by `LAB_0AA7`, mog.asm:19334) -> `LAB_0F8F` steps all voices; the INT4 handler `LAB_0F69` reloads loop/one-shot sample pointers |
| Replaceable by ptplayer | **Yes** (standard 4-channel module) | **No**: not a module; ptplayer cannot play these sequences without a converter. Left as the original asm |

So "switch to ptplayer" means program's music only. mog's music and all its sound effects keep the original driver.

## 2. Entry-point map

### program (module player)

| Entry | Label / line | Contract | Callers |
|---|---|---|---|
| start music | `SECSTRT_1` (1081): if `LAB_0060` != 0 return; else `LAB_0061` init, add `LAB_005C` to the job list `LAB_0372`, `LAB_0060` = slot | no args; idempotent while `LAB_0060` is set | `LAB_05A5` (inside it, 10896, pass 1, when `LAB_05B8+2 == 4`) and `LAB_0036` (605, ending scene, pass 2) |
| init | `LAB_0061` (1124): find the pattern count (arrangement at `+$3B8`), sample start table `LAB_009C`, speed 6, zero the four `AUDxVOL`, row/pos counters | reads module base `LAB_0124` | `SECSTRT_1` |
| stop | `LAB_005B` (1095): clears the job slot only. **Does not silence**: the channels keep looping their last sample until mog's init | no args | the scene stepper (`prg_LAB_005B()` from `src/engine/intro.cpp`, program.asm:152) and the intro skip |
| tick | `LAB_005C` (1100) -> `LAB_0065` (1170): speed counter `LAB_0099`, new row `LAB_006D/006E/0071/0072`, effects `LAB_0083/0067/007A/007E/008C`, DMACON waits `LAB_0072` (patched to `rt_audio_wait`, 2.8) | per VBL, from the level-3 handler (private stack, rt/irq) | job list |
| volume fade | `LAB_0598` / `LAB_0592` (10663-10759), called from the palette hook `LAB_057D` when `SECSTRT_32 != 0`; writes `AUDxVOL` directly from the four words at `LAB_059D` (all zero at start, so a fade step mutes). **`SECSTRT_32` is never written anywhere in program: the fade is dead today.** | now `ms::volumeFade` via `rt_palette_tick_prg` | palette tick |
| position/state | `LAB_0096` speed/pos/row, `LAB_0099` counters: read only inside S_1. No scene polls them, so nothing syncs to the music | | |

Effects the original implements: 0 (arpeggio), 1, 2, 3, 4, A, B (jump), C, D (always to row 0), F (low 5 bits = speed, 0 ignored). Everything else is ignored, E included. No tempo (CIA) support. Finetune is ignored.

### mog (sequencer)

| Entry | Label / line | Notes |
|---|---|---|
| init | `LAB_0AA7` (19328): `LAB_0F89` (voice structs, `AUDxLC/LEN/PER/VOL`, `INTENA`), job `LAB_0F73` into `LAB_0B96`, **`AUTO_INT4 = LAB_0F69`** (patch `int4-vector` -> `rt_irq_set_int4`), loads bank `Re.a` ($D924 bytes) and relocates it (`LAB_0FD4`) | called from `LAB_012C` (2789) |
| start sequence | `LAB_0F8C` (D0 = sequence index into `LAB_1098`, D1 = channel 0-3): resets the voice, sets `54/58(A4)` | many sites: 2925-2934 (`LAB_0133`: sequences $6E-$71 on channels 0-3 = area music), 9283.., 9630.., 9784.., and the helpers below |
| start on fixed channel | `SECSTRT_16`, `LAB_0A9B`, `LAB_0A9C`, `LAB_0A9D` (19277-19289) mark the channel busy in `LAB_0AA6` | |
| play on a free channel | `LAB_0AA2` (D0 = sequence, round-robin over channels not in `LAB_0AA6`) | ~60 callers, the sound effects |
| release channel | `LAB_0A9E..LAB_0AA1` (sequence $A7 = silence on that channel), stop all `LAB_0AA9` (625, 8553) | |
| tick | `LAB_0F73` (checks lock `LAB_0FCA`) -> `LAB_0F8F` (voice loop), `LAB_0F7B` (envelopes), `LAB_0F75` (period/volume registers) | per VBL job |
| INT4 | `LAB_0F69` (27888): per AUD0..3 pending bit: ack, `LAB_0F6F` writes the next `AUDxLC/LEN`, `INTENA` | level 4 via `rt_irq_h4` |
| fade | `LAB_0FC2` (26039): called on every fade step from the palette hook; sets a per-voice attenuation (field 142, 16 or 0) from `LAB_0FC4` (set to 1 around the fade in `LAB_03F1`, 8548-8552) | not a master volume; C++ since 7.1g (`ms::synthFade`, section 8) |
| state location | the four voice structs (148 bytes each, `SECSTRT_44`, `LAB_0F66..0F68`), `LAB_0FC4`, `LAB_0FC5..`, `LAB_0FCA` live in the **CODE** section S_44 (this is the "self-modifying" state of 2.12; `LAB_0F76/0F7C/0FC2` merely write into it). `image_tab` does not reset it; `LAB_0F89` re-initialises most fields on every `LAB_0AA7` | 2.12 closed by 7.1g: the state is `ms::Synth` (BSS) in C++ and `synthInit` clears it on every mog entry (section 8) |

## 3. Who owns what today

| Resource | program (original player) | mog | ACE |
|---|---|---|---|
| Audio DMA bits, `AUDx*`, `ADKCON` | player writes them directly (M2 ownership rule) | sequencer writes them directly | not touched |
| INT4 / AUD0-3 INTENA | masked at install (`irqInstall` restores the original masking); `rt_irq_h4` = default `LAB_0337` (acks) | `LAB_0F69` through `rt_irq_set_int4`; the driver enables the bits itself | `systemSetInt(INTB_AUDx, tramp4)` registered, so `systemUnuse` re-enables the bits after a file read (handler acks; harmless, see docs/IRQ.md) |
| CIA-A | ICR/PRA game; filter/LED bit (PRA bit 1) never touched | same | keyboard SDR |
| CIA-B timers, INT6/EXTER | unused (CIA-B accesses are dead trackdisk code) | unused | ACE allocates timer A and B exclusively (`ciaIcrHandlerAdd`), enables EXTER, dispatches `systemSetCiaInt` handlers |
| Tick source | VBL (level 3) | VBL (level 3) | VBL `timerOnInterrupt` |

## 4. ACE ptplayer: what it takes, and coexistence

`ace/managers/ptplayer.h`, a C port of ptplayer 6.3 (CIA mode; the VBL and audio-interrupt variants are `#define`d out as "buggy").

* Needs: CIA-B timer A (continuous, `mt_timerval / tempo`, 50 Hz at tempo 125), CIA-B timer B (one-shot, 576 ticks DMA delay), INT6/EXTER through `systemSetCiaInt(CIA_B, ...)`, audio DMA through `systemSetDmaMask`, and direct `AUDxLC/LEN/PER/VOL`. `mt_reset` also sets CIA-A PRA bit 1 (audio filter **off**). No audio interrupts in the default mode: channel completion is polled from `INTREQR`, and only the sfx path (`isChannelDone`) uses that, so a game handler acking `AUDx` does not hurt music-only playback.
* API used: `ptplayerCreate(isPal)`, `ptplayerLoadMod(tPtplayerMod*, 0, 0)`, `ptplayerEnableMusic`, `ptplayerSetMasterVolume`, `ptplayerStop`, `ptplayerDestroy`. There is no "mod from memory" constructor; `tPtplayerMod` is the 1084-byte file header plus `pPatterns`/`pSampleStarts[31]` pointers, so `rt/audio` copies the header into a static struct and points the rest into the decoded image (no copy of patterns or samples; samples must be chip, which `LAB_0124` is).
* **vs. the M2 ownership rule** (the game owns Paula): fine, because the game and ptplayer never use Paula at the same time. In program only the player touched it, and with ptplayer on, the original player code is not run. At the program -> mog switch ptplayer is stopped (below) before mog's driver takes Paula over. ACE's own Paula use is limited to `systemSetDmaMask` bookkeeping.
* **vs. rt/irq**: no overlap. The game registers levels 3 and 4 with ACE; ptplayer uses level 6 (CIA-B), which the game never installs (docs/IRQ.md section 2). INT6 outranks the level-3 handler, so a music tick can interrupt a VBL job running on the 2 KB private trampoline stack; the extra depth is ACE's int6 handler plus `mt_music`/`mt_playvoice` (a few hundred bytes). `rt_irq_disable` (INTEN off) delays ticks, never loses them.
* **vs. systemUse/Unuse (rt/files)**: ACE keeps the CIA-B timer values and control registers it was told about (`systemSetTimer`/`systemSetCiaCr`) and restores them in `systemUnuse`, so music pauses during a DOS read and resumes. A tick that lands between ptplayer's "disable main handler, arm timer B" and timer B's interrupt is the one theoretical stall; not observed or tested here.

## 5. What changed (ROADMAP 4.6)

* `src/rt/audio.cpp` + `audio.hpp`. ptplayer is the only music path (ROADMAP 7.1b): the CMake option / compile definition `MS_MUSIC_PTPLAYER` and the "original player" shims (`tst.l LAB_0060; jmp SECSTRT_1+6` and friends, weak `prg_SECSTRT_1`/`prg_LAB_005B`/`prg_LAB_0060`) are gone. The ST/NT player body in program S_1 stays in the image as dead code behind the three entry patches until the hunk is dropped (7.1k-style data work); `rt_audio_wait` (the two DMACON waits of `LAB_0072`) was dead with it and is deleted (ROADMAP 7.1f2: `src/rt/timing.cpp`, `program.timing.json`).
* Patches (unconditional, `asm/patches/program.audio.json`, `mog.audio.json`) replace the first instruction of three routines with `JMP/JSR rt_*`:

| Patch | Replaced | Shim |
|---|---|---|
| `audio-music-start` | `SECSTRT_1`: `TST.L LAB_0060` | `rt::musicStart` |
| `audio-music-stop` | `LAB_005B`: `MOVEA.L LAB_0060,A0` | `rt::musicStop` |
| `audio-quiesce-mog` | mog `SECSTRT_0`: `JSR LAB_04A5` | `rt::musicStop`, `rt::sfxEntry` (section 7), then `jmp LAB_04A5` |

  Entry patches (not call-site patches) because the scene stepper in C++ calls `prg_LAB_005B()` directly and `SECSTRT_1` has two asm callers. All shims preserve D0/D1/A0/A1 (the original clobbered more).
* Start: build the `tPtplayerMod` from `LAB_0124`, `ptplayerCreate(systemIsPal())`, `ptplayerLoadMod`, `ptplayerEnableMusic(1)`. Idempotent while playing (the original's `LAB_0060` guard).
* Stop (`LAB_005B`): music off, channel DMA off, ptplayer handlers off, CIA-B timers stopped, `AUDxVOL` 0. The original left the last notes ringing; this is silent immediately. Also done at mog's entry because the ending pass goes program -> mog without `LAB_005B` (program.asm:157-169) and mog's carve-out overlaps the arena that holds the module.
* Fade: `programFadeHook` (`src/rt/engine_palette.cpp`) calls `rt::musicFade` instead of `ms::volumeFade` (master volume -4 per step, mode 1 = mute). Dead today (see section 2); mog's `LAB_0FC2` fade is the sequencer's own attenuation and is unaffected.
* **Module fix-ups** (`kMimicOriginalEffects`, patterns rewritten in place): ptplayer is ProTracker, the original is not. Scanned with the reference RNC decoder: `music.cmp` has **seven `F00` at row 48 of patterns 0 and 3-8**, which ptplayer treats as "stop song" and the original ignores; one `D01` (original always breaks to row 0, ptplayer to row 1). `vmusic.cmp` has neither (F03/F06 only, `D00`). Both modules use effects 0, 2, 8, A, C, D, F only, no E, no finetune, no sample > 9491 words. The sanitiser mirrors the original generally: effects 5-9 and E removed, `Fxx` masked to 5 bits (0 removed), `Dxx` -> `D00`.
* Sfx: none to route. program plays no sample besides the music; mog's sfx stay on mog's driver. If a later task wants ptplayer sfx it would use `ptplayerReserveChannelsForMusic`/`ptplayerSfxPlay` (not wired).

## 6. Risks and what to listen for

1. **Tempo drift.** ptplayer ticks from CIA-B at exactly 50.00 Hz (PAL), the original from VBL (49.92 Hz PAL, 60 Hz NTSC). Over the 60 s intro the music runs ~0.1 s ahead of the original; scenes are timed on VBL, so any sync between picture and music moves slightly. On NTSC ptplayer is 17% slower than the original.
2. **Sound colour.** ptplayer turns the audio filter off (`mt_reset`: PRA bit 1); the original left it as the system had it. ptplayer follows PT period/finetune/sample-start rules (it clears one word at each sample start, the original one longword; no audible effect expected). Original quirks not reproduced: effect `C` does not update the volume that `A` slides from; arpeggio phase.
3. **Song end / loops.** ptplayer loops to position 0 at the arrangement length like the original; `music.cmp` plays 19 positions, `vmusic.cmp` 22. If a song stops early, a `F00` or `Bxx` was not neutralised.
4. **Stack.** Nested INT6 on the 2 KB `rt_irq_stk3`. Recommended: raise `rt_irq_stk3` to 4096 bytes in `src/rt/irq.cpp` (`.space 4096`, `lea \stk+4096`). Symptom if too small: random crash/garbage a few seconds into music with heavy VBL jobs.
5. **File reads during music.** `systemUse` stalls the music (the original's VBL tick stalled as well, but its channels kept looping; ptplayer's DMA is switched off by `systemUse` and restored after).
6. **INT4 ownership.** Unchanged: program never enables AUD0-3 INTENA; mog owns INT4 after `LAB_0AA7`. `systemUnuse` after a file read re-enables the AUD bits registered by `irqInstall`; with music-only ptplayer that only costs interrupt time.
7. **mog's synth** is C++ since 7.1g (section 8) and must sound exactly as the original (A/B with `-DMS_SYNTH_ASM=ON`): listen to the town/map music and effects after the knight select; if they differ, compare with the asm build before suspecting the quiesce hook.

Listen for (the original player is no longer buildable; compare with a recording of the original): the intro music start time and whether it ends/loops where the original does (watch for it stopping at row 48 of patterns 0 and 3-8 = the F00 fix failing), the bass/chant timbre, the victory music in the ending, silence (not hang/noise) at the intro -> knight-select transition, and normal mog sound afterwards.

## 7. mog sound-request front end in C++ (ROADMAP 7.1b)

mog S_16 (`SECSTRT_16`, `LAB_0A9B..0AA2`, `LAB_0AA9`) is a 4-bit busy mask plus a round-robin cursor; there is no priority queue
and no request queue. `include/engine/sfx.hpp` + `src/engine/sfx.cpp` (pure, `ms::SfxState`) transcribe it, `src/rt/sfx.cpp` owns the
state (the original's CODE-hunk cells `LAB_0AA6` busy byte and `LAB_0AA5` cursor word are dead now) and starts sequences on the synth.

| Original | Now | Behaviour (all as the asm, quirks kept) |
|---|---|---|
| `LAB_0AA2` D0.w = sequence | `rt::sfxRequest` / `ms::sfxPick` (called from C++; the asm entry `rt_sfx_request` went in the 7.1 cleanup, `tests/sfx_emu_support.cpp` keeps it for the unicorn test) | step the cursor to the next channel whose busy bit is clear and start the sequence there; all four busy: start nothing, D1.b = `$0F`. The channel is **not** marked busy, so effects take channels in turn |
| `SECSTRT_16`, `LAB_0A9B/9C/9D` (area music, channels 0-3) | `rt::sfxStartFixed` (entry `rt_sfx_start_fixed` test-only since the 7.1 cleanup; it ran after the original `MOVEQ #ch,D1`) | set the busy bit, start on that channel |
| `LAB_0A9E..0AA1` (also reached through script tables) | `rt::sfxRelease`, entries `rt_sfx_release0/1` (script tables; 2/3 test-only since the 7.1 cleanup) | clear the bit (the mask also drops bits 4-7), then request `$A7` like any effect: it lands on the next free channel in cursor order, not necessarily the released one |
| `LAB_0AA9` | `rt::sfxStopAll` / `ms::sfxStopAll` (entry `rt_sfx_stop_all` test-only since the 7.1 cleanup) | clear mask, release 0..3 (four `$A7` starts that cover every channel); D0.w = `$A7`, D1.w = last channel |
| `LAB_0AA7` tail `MOVE.W #0,LAB_0AA6` | `rt::sfxReset`, `rt_sfx_reset` | mask 0 (cursor kept) |
| mog image load | `rt::sfxEntry` from `rt_audio_quiesce_mog` | busy and cursor 0, as a freshly loaded CODE hunk |

The synth start `LAB_0F8C` (D0.w = sequence index into `LAB_1098`, D1.w = channel; saves/restores all registers, takes the lock `LAB_0FCA`)
is the synth entry `rt_synth_start` (7.1g, section 8); `rtSfxSynth(seq, channel)` in `src/rt/sfx.cpp` still reaches it by jumping to the (patched) `LAB_0F8C`. C++ callers (`rt/combat_script`, `rt/fighters`
sound pickers, `rt/loot` click `$9C`, `rt/combat` stop-all) call `rt::sfxRequest` / `rt::sfxStopAll` directly; loot reproduces the original D1
side effect (the channel, or `$xx0F` when full) for its wizard-path quirk. Asm callers keep `JSR LAB_0AA2` etc. through the entry patches
(`asm/patches/mog.sfx.json`); register contracts are in the comment above the shims in `src/rt/sfx.cpp`. `tests/test_sfx.py` checks the pure part
against a transcription of the asm and the m68k shims against the original routines in the unicorn harness.

## 8. mog synth in C++ (ROADMAP 7.1g, closes the synth part of 2.12)

The voice/sequence synth of mog S_44/S_45 (the thing that plays all mog music and every sound effect) is `ms::synth*` in `src/engine/synth.cpp` (pure, host
testable, all hardware through `ms::SynthHw`), its read-only data is generated into `src/engine/synth_data.cpp` (`tools/gen_synth_tables.py`, from the
original mog hunk), and the glue + entry shims are `src/rt/synth.cpp`. Five entry patches (`asm/patches/mog.synth.json`) replace the first instruction of:

| Original | Now | Notes |
|---|---|---|
| `LAB_0F89` init (from `LAB_0AA7`) | `rt_synth_init` -> `ms::synthInit` | Paula (DMACON/INTENA/INTREQ/ADKCON, VOL/LEN/PER/LC of 4 channels, same write order), voices cleared |
| `LAB_0FD4` instrument relocation (from `LAB_0AA7`, after the bank load) | `rt_synth_reloc` -> `ms::synthRelocate` | sample address = bank buffer (`LAB_05C7..05CB`) + offset per instrument; also `rt::irqSetInt4(rt_synth_int4)`: the INT4 handler becomes the C++ one (the `int4-vector` patch just installed `LAB_0F69`) |
| `LAB_0F8C` start (D0.w sequence, D1.w channel) | `rt_synth_start` -> `ms::synthStart` | all registers preserved; called from asm (`JSR`/`JMP`), `rtSfxSynth`, `rt/combat_ui` |
| `LAB_0F73` VBL job -> `LAB_0F8F` | `rt_synth_tick` -> `ms::synthTick` | D0 = 0; skips the frame while the lock (`LAB_0FCA`, set during a start) is non-zero |
| `LAB_0F69` INT4 -> `LAB_0F6F` | `rt_synth_int4` (RTE shim) -> `ms::synthInt4` | per pending enabled AUDx bit: ack, then the loop/one-shot reload (`AUDxLC/LEN`, `INTENA`) |
| `LAB_0FC2` fade step | `rt_synth_fade` -> `ms::synthFade` | per-voice attenuation 16 (flag `LAB_0FC4` set) or 0, subtracted from the volume at the register pass |

What the synth is: a sequence interpreter per voice (note bytes < $80, commands $80..$D4 in steps of 4: volume, restart, note length x tempo, end marker, tempo,
summed length, vibrato/tremolo row, flags, stop, call/return, transpose add/set, loop start/end, envelope on/off, instrument, jump), a tremolo/vibrato stage machine
(2 + 3 stages from a 15-byte row), an attack/decay/sustain/release envelope (8-byte rows) and the register pass (`AUDxPER`, `AUDxVOL`, and on a new note
`AUDxLC/LEN`, DMACON on, INTREQ clear, INTENA on). The game's 167 sequences only use notes, `$80 $88 $8C $90 $94 $9C $A8 $AC $BC $D0 $D4`
(a static decode of every sequence; no envelopes, loops, calls, sums or transpose adds) but all of it is ported and tested.

State: `ms::Synth` (4 `SynthVoice`, tempo, lock, the relocated sample table) in BSS. The original kept it in the CODE hunk (voice structs, the sequence
stacks as 4 x 128 bytes, the tempo word `LAB_0FC5+2`, the lock `LAB_0FCA`), which `image_tab` does not reset: a second mog entry (ending pass) would have
started with the first run's voices still playing. `synthInit` clears everything, which is what a fresh load of the hunk gave the original (2.12 for the synth).
`LAB_0FC4` (the fade flag, written by `rt/palette_glue`) stays an asm cell and is read at each fade step.

Data (`kSynthBlob`, `kSynthInst`, `kSynthWave`): refs are S_44 offsets (the raw pre-relocation values), so the sequence table, pitch tables, vibrato rows and
envelopes are the original bytes. Two deliberate differences: the word at `LAB_0FCA+2` is zero (the original's init wipes it with its stack clear) and the relocation
code that IRA left between the tables is zero-filled. The nine S_45 waveforms (and the 1-word silent buffer `LAB_10A2`) are a CHIP array.

Deliberate differences (never reachable with the game's data; the original would trap or fetch garbage): a sequence/instrument index out of range, a channel > 3,
a `$94` tempo argument of 0 and a command byte that is not in the table stop the voice or are ignored. Quirks kept: starting any sequence ends with DMACON <- $0002
(channel 1's DMA off), a start also clears the started voice's fade attenuation and transpose, `$C0 n ... $C4` runs the body n + 1 times.

**A/B switch:** CMake `-DMS_SYNTH_ASM=ON` (default OFF) keeps the original asm synth: the five shims then replay the replaced instruction and jump to the next
one of the original, so S_44 runs untouched (the original `LAB_0F69` is the INT4 handler, installed by the `int4-vector` patch). Build both and listen.

**Tests** (`tests/test_synth.py`, helpers `tests/synth_oracle.py`, `tests/synth_driver.cpp`): the original S_44 code runs in unicorn (68020) on a fake Paula; the C++
gets the same script (init + relocation, starts, VBL ticks, INT4 entries with random pending/enable masks, fades, lock) and must write the same chip registers, in the
same order, per operation. Covered: every sequence 0..167 on all four channels for 1500 ticks (> 1M register writes), random multi-voice scenarios, the INT4 handler against
random voice states, the instrument relocation for random bank buffers, and random synthetic sequences that use every command (envelopes, loops, calls, ...; installed with
`P`/`Q` operations in both implementations). Part 2 runs the compiled m68k code (engine + data + rt shims, `-m68020`) in unicorn through the original entry labels with
the five patches applied (and in the `MS_SYNTH_ASM` configuration), proving the shims, the calling contracts and the JMP encodings. Three deliberate source mutations are
caught. Unicorn cannot execute `RTE`, so the interrupt-handler runs patch the final `RTE` of the handler to `RTS` (the rest of the handler is unchanged).

**Listening** (`MS_SYNTH_ASM` OFF = C++ vs ON = original): the area music is sequences $6E..$71 on channels 0..3 (`LAB_0133`: 110, 111, 112, 113 in the boot log) and starts
once mog is up (menu / map / town); effects are the ~60 callers of `LAB_0AA2` (sword hits, clicks, spells, creature sounds) and the loot click `$9C`; the release / "silence"
sequence `$A7` (167) stops a channel (the boot log shows it right after the map). Fade-outs between screens go through `LAB_0FC2`.
