// rt/amiga/gtext_entries - the 68k register-ABI entry shims of src/rt/gtext.cpp (ROADMAP 11.1d).  The C++ bodies they front stay in the
// platform-neutral gtext.cpp; the shims are Amiga-only (the PC build calls the bodies directly), are linked into the game image
// for the callers that still reach a body through its label, and are what the unicorn tests (tests/emu_lib.py) run against the
// original asm: they list this file next to gtext.cpp.

asm(R"(
	.text
	.globl rt_prg_text_list
rt_prg_text_list:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtPrgTextList
	addq.l #4,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_mog_text_list
rt_mog_text_list:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogTextList
	addq.l #4,%sp
	bra.s rtGtextExtent

	.globl rt_mog_text_string
rt_mog_text_string:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	andi.l #0xFFFF,%d2
	move.l %d2,-(%sp)
	andi.l #0xFFFF,%d1
	move.l %d1,-(%sp)
	andi.l #0xFFFF,%d0
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtMogTextString
	lea 16(%sp),%sp
rtGtextExtent:
	move.l %d0,%d1
	swap %d1
	andi.l #0xFFFF,%d1
	andi.l #0xFFFF,%d0
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl rt_mog_add_record
rt_mog_add_record:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	jsr rtMogAddRecord
	tst.l %d0
	beq.s 1f
	movea.l %d0,%a0
	lea 24(%a0),%a0
	lea mogRecordTemplate+24,%a1
	move.l #0xFFFF,%d0
	bra.s 2f
1:
	movea.l mogRecordTable,%a0
	lea 2352(%a0),%a0
	moveq #1,%d0
2:
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
)");
