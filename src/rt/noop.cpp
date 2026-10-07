// rt/noop - rtNoop, a bare RTS (ROADMAP 7.1q): the identity / entry of the original asm routines that were one (mog LAB_0100 the disk prompt,
// LAB_0166 the no-spawn routine).  Kept in a file of its own so the unicorn tests that link only part of the rt code can add it.

asm(R"(
	.text
	.globl rtNoop
rtNoop:
	rts
)");

