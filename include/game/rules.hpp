// game/rules - the pure rules of mog.asm in C++ (ROADMAP 5.3): knight stats and HP, the settlement of a fight, the
// lunar clock with the daily upkeep, and the turn scheduler.  Pure on purpose: no ACE, no OS, no hardware, no globals;
// every function works on the struct references (include/game/{knight,world}.hpp) the caller passes, so it builds for
// the host tests as well as the Amiga.  The language is C++ (ROADMAP 5.3 says C11; the project moved to C++ for the
// typed state, see docs/GAME_STATE.md).  Authority: moonshard/moonstone-main/amiga_asm/mog.asm (labels cited per
// function).  tests/test_game_rules.py checks every function against a literal Python model of the asm, and
// LAB_0011/0013/0019/000E/0029/0030 additionally against the lifted C++ in src/lifted/mog.
//
// What is deliberately NOT here (the caller / the asm shim owns it):
//  * 68k addresses.  The asm keeps pointers (Knight +96 = Inventory*, ActiveKnights +0/+4, LAB_0633 = the current
//    knight).  The pure code takes the Inventory next to the Knight and never writes the pointer fields.
//  * Side effects in other subsystems.  They are reached through RuleHooks, so a host test can count them:
//    LAB_0E04 / LAB_0DC8 / LAB_012B / LAB_00EC / LAB_03EB / LAB_0E52 (screen and map work) and LAB_045E (the AI
//    knight's day, which does its own shopping and travelling).
//  * The copy LAB_05E2+4 -> LAB_05C4 at the head of LAB_0029 (two unrelated globals; the shim does it).
//
// Split (ROADMAP 9.3e) over include/game/rules/{hooks,stats,settle,clock,turns}.hpp and src/game/rules/*.cpp; this header
// only includes them all so older callers keep working.  New code includes the topic header it needs.
#pragma once
#include "game/rules/clock.hpp"
#include "game/rules/hooks.hpp"
#include "game/rules/settle.hpp"
#include "game/rules/stats.hpp"
#include "game/rules/turns.hpp"
