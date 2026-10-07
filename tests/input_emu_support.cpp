// Test-only: mog's joystick-port entry (LAB_00EA) that the game no longer links (ROADMAP 7.1 cleanup: its patch was dead, nothing calls
// it).  tests/test_input.py links this file next to src/rt/input.cpp and runs it in unicorn against the ORIGINAL routine.
//   rt_mog_joy_port  (LAB_00EA): A0 = record; D0.w = the port of the record's joystick (+11 == 1: port 0), D1.w = port 1.
asm(R"(
	.text
	.globl rt_mog_joy_port
rt_mog_joy_port:
	jsr rt_mog_joy_read
	cmpi.b #1,11(%a0)
	beq.s 1f
	move.w %d1,%d0
1:	rts
)");
