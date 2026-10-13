// rt/amiga/enhanced_entries - the 68k register-ABI entry shims of src/rt/enhanced.cpp (ROADMAP 11.10): rt_mog_carve, rt_mog_pack_done and the palette sites
// rt_prg_pal_copy_live / rt_mog_pal_copy_live (asm/patches/{program,mog}.enhanced.json).  The C++ bodies (rtEnhCarveMog, rtEnhPackDone,
// rtEnhPalSetLive) stay in the platform-neutral enhanced.cpp together with the mode cells.

#define PALETTE_SITE_SHIMS(PFX, LIVECELL) \
asm(".text\n" \
	".globl rt_" #PFX "_pal_copy_live\n" \
	"rt_" #PFX "_pal_copy_live:\n" \
	"	movea.l " LIVECELL ",%a1\n" \
	"	moveq #31,%d0\n" \
	"2:	move.w (%a0)+,(%a1)+\n" \
	"	dbf %d0,2b\n" \
	"	cmpi.w #6,rt_enh_planes\n" \
	"	bne.s 3f\n" \
	"	movem.l %d0-%d1/%a0-%a1,-(%sp)\n" \
	"	lea -64(%a0),%a0\n" \
	"	move.l %a0,-(%sp)\n" \
	"	jsr rtEnhPalSetLive\n" \
	"	addq.l #4,%sp\n" \
	"	movem.l (%sp)+,%d0-%d1/%a0-%a1\n" \
	"3:	rts\n");

PALETTE_SITE_SHIMS(prg, "prgLivePalette")
PALETTE_SITE_SHIMS(mog, "mogLivePalette")

asm(R"(
	.text
	.globl rt_mog_carve
rt_mog_carve:
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtEnhCarveMog
	movem.l (%sp)+,%d0-%d1/%a0-%a1
	rts

	.globl rt_mog_pack_done
rt_mog_pack_done:
	jsr rt_mog_file_close
	cmpi.w #6,rt_enh_planes
	bne.s 1f
	movem.l %d0-%d1/%a0-%a1,-(%sp)
	jsr rtEnhPackDone
	movem.l (%sp)+,%d0-%d1/%a0-%a1
1:	rts
)");

