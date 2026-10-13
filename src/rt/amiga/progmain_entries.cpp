// rt/amiga/progmain_entries - the 68k register entry of src/rt/progmain.cpp (ROADMAP 11.1d): rt_prg_main, SECSTRT_0 of the program overlay,
// entered by the overlay loader with A0/D0 = fast RAM start/size, A1/D1 = chip RAM start/size.  (rtPrgCall, the register-block call the boot
// steps used, is gone: they call the C++ bodies directly.)

asm(R"(
	.text

	.globl rt_prg_main
rt_prg_main:
	move.l %d1,-(%sp)
	move.l %a1,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	jsr rtPrgMain
	lea 16(%sp),%sp
	rts
)");

