# Resources use RAII (ROADMAP 9.2b)

Owner rule (2026-10-07, CLAUDE.md): every acquire/release pair is a small local guard object whose destructor releases, so an
early `return` cannot leak. Guards live on the stack only (global/static constructors do not run in this program), and there
are no exceptions, so destructors run on scope exit and on every `return`. `tests/test_raii.py` (step 0b of
`py tools/check.py --quick`) flags new raw pairs.

## 1. Guards

Pure, generic ones live in `include/engine/guard.hpp` (namespace `ms`); OS and hardware ones in `src/rt/guards.hpp`
(namespace `rt`, layer L0). Game code that needs a guard for an op table defines a small private one (`OpenFile` in
`src/game/combat_load.cpp`) on top of `ms::GuardTraits` / `MS_GUARD_PINNED`.

| guard | acquires / releases | where |
|---|---|---|
| `ms::ScopeExit<F>` / `ms::scopeExit(f)` | runs `f` at scope end; `dismiss()` cancels (no `std::function`) | engine/guard.hpp |
| `ms::MemMark` | `memStackMark` / `memStackRelease` of a MemStack for scratch inside one function; `keep()` leaves the allocations | engine/guard.hpp |
| `rt::SystemAccess` | `systemUse()` / `systemUnuse()` (ACE gives the OS back) | rt/guards.hpp |
| `rt::OsAccess` | SystemAccess + `systemReleaseBlitterToOs()` / `systemGetBlitterFromOs()`: the bracket of every dos.library read | rt/guards.hpp |
| `rt::NoRequesters` | `pr_WindowPtr = -1` / restore: no "insert volume" requesters | rt/guards.hpp |
| `rt::DosHandle` | dos `Open` / `Close` (optional quiet open; `release()` hands the handle on) | rt/guards.hpp |
| `rt::DosLock` | dos `Lock` / `UnLock` | rt/guards.hpp |
| `rt::FileHandle` | the single file slot: `rt_file_open[_exact]` / `rt_file_close`; `close()` for an early close | rt/guards.hpp |
| `rt::MemBlock` | `memAlloc` / `memFree` (size remembered); `acquire()`, `release()` | rt/guards.hpp |
| `rt::IrqOff` | exec `Disable()` / `Enable()` | rt/guards.hpp |
| `rt::FileSession` | `rt_file_shutdown()` (open file + disk images) at the end of the run | rt/guards.hpp |
| `OpenFile` (private) | the combat loader's `fileOpen` / `fileClose` ops | game/combat_load.cpp |

### Guards: rule of zero / five

Plain data (records, defs, POD state) follow the rule of zero: no user-declared special members. Every resource-owning guard
declares all five explicitly: the destructor that releases, copy constructor and copy assignment `= delete`, move
constructor and move assignment `= delete` by default. Only a guard that must leave its scope gets a real move: `rt::FileHandle`
(returned by `openFile()` in `rt/loaders.cpp`) has a move constructor and assignment that transfer ownership and leave the source
empty, so its destructor then does nothing; the cast is written by hand (no `<utility>`). Return a guard with
`return Guard(...)` where possible (C++17 guaranteed copy elision). Compile-time: `MS_GUARD_PINNED(T)` /
`MS_GUARD_MOVABLE(T)` (`ms::GuardTraits`, built on `__is_constructible` / `__is_assignable`, no STL) sit under every guard,
so a guard that becomes copyable fails the build; `tests/test_raii.py` compiles the engine guards on the host, checks that a
copyable guard is rejected by the assert, and checks the spelling of the five members textually.

## 2. Survey: converted pairs

| site | resource | guard now | early-return risk before |
|---|---|---|---|
| `rt/files.cpp` rt_file_init, rt_file_open, rt_file_open_exact, rt_file_write_exact, rt_file_list, refill, rt_file_skip, rt_file_close, rt_file_shutdown | systemUse + blitter to OS (was the private `OsGuard`) | `OsAccess` (9 sites) | none (RAII already; now the shared type) |
| `rt/files.cpp` rt_file_open, rt_file_open_exact, rt_file_write_exact, rt_file_list; `rt/adfdisks.cpp` adfScan; `rt/system.cpp` bootLogRaw | `pr_WindowPtr` save / restore | `NoRequesters` | rt_file_list and write_exact restored by hand before each return |
| `rt/files.cpp` loadPalSidecars, tryOpenArt, rt_file_write_exact; `rt/adfdisks.cpp` tryImage; `rt/autoplay.cpp` autoplayInit; `rt/system.cpp` bootLogRaw; `rt/origload.cpp` origDataDump | dos `Open` / `Close` | `DosHandle` | tryImage and tryOpenArt closed on three separate paths |
| `rt/files.cpp` logOpenFile, rt_file_list; `rt/adfdisks.cpp` scanDir; `rt/system.cpp` bootLogRaw | dos `Lock` / `UnLock` | `DosLock` | early `return` after the lock |
| `rt/files.cpp` rt_file_list; `rt/adfdisks.cpp` scanDir | `AllocDosObject` / `FreeDosObject` | `scopeExit` | none today (straight line) |
| `rt/system.cpp` fatal | `OpenLibrary` / `CloseLibrary` intuition | `scopeExit` | none today |
| `rt/loaders.cpp` picFile, celLoad, celSize, rtLoadBlob_mog, rtHitOpen_mog | `rt_file_open` / `rt_file_close` | `FileHandle` (`openFile()` returns it, moved) | each had 1-2 `return` paths with the close by hand |
| `rt/modload.cpp` loadOne | file + the text buffer | `FileHandle` + `MemBlock` | three paths each closing / freeing by hand |
| `rt/modload.cpp` modsLoad | the 3 KB work block | `MemBlock` | single path |
| `rt/origload.cpp` origLoad (2 loops) | `rt_file_open` / `rt_file_close` | `FileHandle` | none today |
| `rt/soundbank.cpp` rtSbFile | file slot | `FileHandle` | none today |
| `game/combat_load.cpp` loadAssets (message.piv, ch.piv) | ops `fileOpen` / `fileClose` | `OpenFile` | none today; call order unchanged (test compares it) |
| `rt/game.cpp` rtGameRun | chip + fast arenas (`memAlloc*` / `memFree`), enhanced fallback frees both and retries | `MemBlock` x2 | the fatal / display-failure paths freed by hand at the tail |
| `rt/game.cpp` rtGameRun | `rt_file_shutdown` on both exits | `FileSession` (declared first, so it runs after the arenas) | two exits, each calling it |
| `rt/crash.cpp` crashInstall, crashRemove | `Disable()` / `Enable()` around the vector table | `IrqOff` | none today |
| `rt/display_ace.cpp` displayAceActivate | `systemUse` around the ACE view / bitmap allocations | `SystemAccess` | none today |
| `rt/autoplay.cpp` autoplayInit; `rt/system.cpp` bootLog, fatal | `systemUse` / `systemUnuse` | `SystemAccess` | fatal's alert path had one release at the tail |
| `rt/irq.cpp` traceFlush (MS_IRQ_TRACE); `rt/origload.cpp` origDataDump | systemUse + blitter to OS | `OsAccess` | none today |

## 3. Exempt (kept raw, in `tests/raii_allow.json`)

The pair spans calls, callbacks or a non-local exit, so no scope holds it.

| site | resource | why it stays |
|---|---|---|
| `game/flow/machine.cpp` enter / leave / flowInit | per-level MemStack mark (`aulMark[level]`), scene slot owners (`slotsAcquire` / `slotsRelease`) | the scene stack: enter and leave are two calls of the machine (a scene lives across the main loop), the mark is data of the level, not of a C++ scope. The machine itself pairs them (leave releases what enter took; `tests/test_flow.py`) |
| `main.cpp` splashCreate / splashDestroy | `systemUnuse` / `systemUse` | two ACE state callbacks (create / destroy of the splash state) |
| `rt/game.cpp` rt_game_call / `rt_run_*` (asm) | overlay switches | the original's deliberate non-local exit: the overlay jumps back to `rt_game_leave`, which restores SP into `rtGameRun`'s frame, so the guards of `rtGameRun` itself (arenas, FileSession) survive and release at its end |
| `rt/game.cpp` imageEnter | overlay DATA snapshot `memAllocFast` / the loop of `memFree` after the overlay loop | static `OverlayDef`, allocated at the first entry of an overlay and used by later entries |
| `rt/crash.cpp` rt_crash_c | `systemUse`, log `Open` | the exception reporter never returns (loops in `Delay`) |
| `rt/enhanced.cpp` enhancedEnable / enhancedDisable | 7-plane temp buffer (`memAllocChipClear` / `memFree`) | owned by the display lifetime (`displayAceActivate` / `displayAceRelease`), static |
| `rt/display_ace.cpp` displayAceActivate / Release | ACE view, vport, buffer, bitmaps | `s_ace` state with its own create / destroy pair, released by `displayAceRelease` on every failure path |
| `rt/irq.cpp` irqInstall / irqRemove (`systemSetInt`) | interrupt vectors | installed at the overlay start, removed at the end, across the whole game; owner is the irq module state |
| `rt/files.cpp` s_fh, s_logFh, tryOpen | the single file slot, `files.log`, the stub handle of the kn1.ob search | the file layer owns the handles that outlive a call; `rt::FileHandle` is the typed view of the slot, `rt_file_shutdown` (FileSession) closes it |
| `rt/files.cpp`, `rt/files.hpp` | the C ABI `rt_file_open[_exact]` | what `FileHandle` wraps (the asm shims call it too) |
| `rt/adfdisks.cpp` s_aImg[].fh, adfShutdown | ADF image handles | open for the whole run, released by `DosHandle::release()` into the table, closed by adfShutdown |
| `rt/irq.cpp` s_bpLog | the trace log (MS_IRQ_TRACE only) | open for the whole run |
| `rt/combat_load.cpp` opFileOpen / opFileClose | file slot ops of the combat loader | the two ops are separate entries of the `Ops` table; the pairing is `OpenFile` in `game/combat_load.cpp` (and `packDone` closes the pack read) |
| `rt/sfx.cpp` sfxReserve / sfxRelease | sound channel busy bits | the original's channel lock: reserved when a sound starts, released by the sound's end script (`fighters.cpp` cases 0x0A9E / 0x0A9F), seconds later |

Not resources in this sense: blitter waits (`rt_blit_wait`, `waitBlit`) are a poll, not an ownership; the copper / DMA state of a file
read is covered by `OsAccess` (blitter to the OS) and the display handover (`displayHandoverToGame`).

## 4. Lint

`tests/test_raii.py` scans `src/game`, `src/engine`, `src/rt`, `src/main.cpp` (not `src/lifted`) for `rt_file_open*`,
`systemUse` / `systemUnuse`, the blitter hand-over, dos `Open` / `Close` / `Lock` / `UnLock`, `memAlloc*` / `memFree`,
`Disable` / `Enable`, `pr_WindowPtr`, `AllocDosObject` / `OpenLibrary` and `memStackMark` / `memStackRelease`. Every hit must be
covered by an entry (file, kind, count, reason) of `tests/raii_allow.json`; the entries above are those. The list can only
shrink (a stale entry fails). New code uses the guards; if a pair really spans calls, add the entry with the reason and a row to
section 3.
