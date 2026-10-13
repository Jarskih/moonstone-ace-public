// rt/noop - rtNoop, a bare return (ROADMAP 7.1q): the identity / entry of the original asm routines that were one (mog LAB_0100 the disk prompt,
// LAB_0166 the no-spawn routine).  Kept in a file of its own so the unicorn tests that link only part of the rt code can add it.
// A C++ function since ROADMAP 11.1d (it was a one-instruction asm block): the compiler emits the same RTS on the Amiga, and the PC build links it.

extern "C" __attribute__((used, externally_visible)) void rtNoop(void) {}
