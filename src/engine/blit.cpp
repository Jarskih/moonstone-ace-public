// engine/blit - see blit.hpp. Transcribed from program.asm S_23 LAB_04B5..LAB_04D6 (mog.asm S_28, identical
// code with labels +0x826) and S_25 LAB_04E1 / LAB_04E2. Word arithmetic is kept as the 68000 does it.
#include "engine/blit.hpp"

namespace ms {

namespace {

uint16_t rd16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t rd32(const uint8_t *p) {
	return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
	       (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

BlitOp makeOp(uint16_t con0, uint16_t con1, uint8_t channels) {
	BlitOp op{};
	op.con0 = con0;
	op.con1 = con1;
	op.afwm = 0xFFFF;
	op.alwm = 0xFFFF;
	op.channels = channels;
	return op;
}

}  // namespace

bool celFrame(const uint8_t *cel, int16_t frame, CelFrame &out) {
	// TST.W D0 / BLT, CMP.W (A0),D0 / BGE  (signed word compares)
	if(frame < 0 || frame >= static_cast<int16_t>(rd16(cel))) {
		return false;
	}
	// D0 = frame * 10 as a word, then LEA 0(A0,D0.W): the offset is sign-extended
	const int16_t rec = static_cast<int16_t>(static_cast<uint16_t>(frame * 10));
	const uint8_t *r = cel + 10 + rec;
	out.data = rd32(cel + 2) + rd32(r);
	out.width = rd16(r + 4);
	out.height = rd16(r + 6);
	out.hot = r[8];
	out.planeBits = r[9];
	return true;
}

bool planCel(const CelFrame &f, int16_t x, int16_t y, const CelView &v, CelJob &j) {
	j = CelJob{};

	// LAB_04B5: words per row (width rounded up to 16 px), hot-spot, plane flags, source plane size
	uint16_t d5 = static_cast<uint16_t>(static_cast<uint16_t>(f.width + 15) & 0xFFF0) >> 4;
	uint16_t d4 = f.height;
	int16_t d1 = static_cast<int16_t>(x - (f.hot >> 4));
	int16_t d2 = y;
	j.planeBits = f.planeBits;
	j.planeBytes = static_cast<uint32_t>(static_cast<uint16_t>(d5 << 1)) * d4;  // LSL.W #1,D5 / MULU D4,D5
	uint32_t src = f.data;

	// top clip: rows above the viewport are skipped in the source
	if(d2 < 0) {
		const int32_t t = static_cast<int32_t>(static_cast<int16_t>(d4)) + d2;  // ADD.W D2,D4 / BLE
		d4 = static_cast<uint16_t>(t);
		if(t <= 0) {
			return false;
		}
		const uint16_t negY = static_cast<uint16_t>(-d2);
		const uint32_t prod = static_cast<uint32_t>(d5) * negY;  // MULU D2,D6 (D6 = D5)
		// LSL.W #1,D6 / LEA 0(A2,D6.W): word shift, sign-extended offset
		src += static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(prod << 1))));
		d2 = 0;
	}
	// LAB_04B6: bottom clip
	if(d2 >= v.clipH) {
		return false;
	}
	{
		const int32_t t = static_cast<int32_t>(static_cast<int16_t>(d2 + static_cast<int16_t>(d4))) - v.clipH;
		if(t > 0) {
			d4 = static_cast<uint16_t>(d4 - static_cast<uint16_t>(t));
		}
	}
	// LAB_04B7: split x into a pixel shift and a byte offset (ASR.W #3 of the 16-px aligned value)
	j.shift = static_cast<uint16_t>(d1) & 0x0F;
	d1 = static_cast<int16_t>(static_cast<int16_t>(static_cast<uint16_t>(d1) & 0xFFF0) >> 3);
	if(static_cast<int32_t>(static_cast<int16_t>(d5 << 1)) + d1 < 0) {
		return false;  // wholly left of the viewport
	}
	if(d1 >= v.clipW) {
		return false;
	}
	uint16_t c4FD = 0, c4FE = 0;
	if(d1 < 0) {
		// left clip: the blit starts one word left of the viewport, the dropped source columns are skipped
		j.clipLeft = true;
		d1 = static_cast<int16_t>(d1 + 2);
		c4FD = static_cast<uint16_t>(d1);
		d1 = static_cast<int16_t>(d1 >> 1);
		d5 = static_cast<uint16_t>(d5 + d1);
		d1 = -2;
	} else {
		const int32_t t = static_cast<int32_t>(static_cast<int16_t>(static_cast<uint16_t>(d5 << 1) + d1)) - v.clipW;
		if(t >= 0) {
			j.clipRight = true;
			c4FE = static_cast<uint16_t>(t);
			d5 = static_cast<uint16_t>(d5 - static_cast<uint16_t>(static_cast<int16_t>(t) >> 1));
		}
	}
	// LAB_04B9: row offset into the plane
	const uint32_t rowOff = static_cast<uint32_t>(static_cast<uint16_t>(d2)) * v.stride;  // MULU
	const uint16_t off = static_cast<uint16_t>(static_cast<uint16_t>(d1) + static_cast<uint16_t>(rowOff));  // ADD.W D2,D1
	if(d4 == 0 || d5 == 0) {
		return false;
	}
	j.height = d4;
	j.words = d5;

	// LAB_04BB: gather set-up. NEG.W / LEA moves the source past the dropped left columns; D0 = modulo
	const uint16_t negFD = static_cast<uint16_t>(-static_cast<int16_t>(c4FD));
	src += static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(negFD)));
	j.srcMod = static_cast<int16_t>(static_cast<uint16_t>(negFD + c4FE));
	const uint16_t size = static_cast<uint16_t>(static_cast<uint16_t>(d4 << 6) | d5);

	// shift masks of LAB_04CC / LAB_04CF: 0xFFFF << shift as a long, low half and high half
	const uint32_t m = 0xFFFFu << j.shift;
	j.lastMask = j.clipRight ? static_cast<uint16_t>(m) : 0xFFFF;
	j.firstMask = j.clipLeft ? static_cast<uint16_t>(m >> 16) : 0xFFFF;

	j.cpuCopy = v.cpuMask;
	// depth 6 (enhanced) or 5 (original; any other value is treated as the original)
	const int depth = v.depth == CEL_MAX_PLANES ? CEL_MAX_PLANES : CEL_DEPTH_5;
	j.depth = static_cast<uint8_t>(depth);
	j.tempCount = static_cast<uint8_t>(depth + 1);
	j.planes = static_cast<uint8_t>(v.planes > depth ? depth : v.planes);
	const int maskPlane = depth;  // temp plane holding the one-plane mask
	for(int i = 0; i <= depth; ++i) {
		j.tempPlane[i] = v.temp + static_cast<uint32_t>(i) * CEL_TEMP_STRIDE;
	}
	uint32_t a2 = src;
	for(int i = 0; i < j.planes; ++i) {
		if(!((j.planeBits >> i) & 1)) {
			continue;  // LSR.W #1,D6 / BCS: plane absent, source not advanced
		}
		j.gatherSrc[i] = a2;
		BlitOp &g = j.gather[i];
		g = makeOp(0x09F0, 0x0000, BLT_CH_A | BLT_CH_D);  // LAB_04CC: straight A to D
		g.ptA = a2;
		g.ptD = j.tempPlane[i];
		g.modA = j.srcMod;
		g.modD = 2;  // one pad word per temp row
		g.afwm = j.firstMask;
		g.alwm = j.lastMask;
		g.size = size;
		a2 += j.planeBytes;
	}

	// LAB_04BF: mask plane = f(planes 0..4) in two blits (planes 0-2 into temp 5, then 3-4 on top of it)
	{
		uint16_t con0 = 0x0100;
		if(j.planeBits & 0x01) con0 |= 0x08F0;
		if(j.planeBits & 0x02) con0 |= 0x04CC;
		if(j.planeBits & 0x04) con0 |= 0x02AA;
		BlitOp &a = j.mask[0];
		a = makeOp(con0, 0, BLT_CH_A | BLT_CH_B | BLT_CH_C | BLT_CH_D);
		a.modA = a.modB = a.modC = a.modD = 2;
		a.ptA = j.tempPlane[0];
		a.ptB = j.tempPlane[1];
		a.ptC = j.tempPlane[2];
		a.ptD = j.tempPlane[maskPlane];
		a.size = size;

		uint16_t con0b = 0x03AA;
		if(j.planeBits & 0x08) con0b |= 0x08F0;
		if(j.planeBits & 0x10) con0b |= 0x04CC;
		BlitOp &b = j.mask[1];
		b = makeOp(con0b, 0, BLT_CH_A | BLT_CH_B | BLT_CH_C | BLT_CH_D);  // BLTCON1 stays 0 from the first op
		b.modA = b.modB = b.modC = b.modD = 2;
		b.ptA = j.tempPlane[3];
		b.ptB = j.tempPlane[4];
		b.ptC = j.tempPlane[maskPlane];
		b.ptD = j.tempPlane[maskPlane];
		b.size = size;
	}
	j.maskOps = 2;
	if(depth == CEL_MAX_PLANES && (j.planeBits & 0x20)) {
		// 6 planes: OR plane 5 into the mask as well (A | C -> D, no B channel); BLTCON1 stays 0
		BlitOp &c = j.mask[2];
		c = makeOp(0x0BFA, 0, BLT_CH_A | BLT_CH_C | BLT_CH_D);
		c.modA = c.modC = c.modD = 2;
		c.ptA = j.tempPlane[5];
		c.ptC = j.tempPlane[maskPlane];
		c.ptD = j.tempPlane[maskPlane];
		c.size = size;
		j.maskOps = 3;
	}

	// LAB_04C5..LAB_04CA: cookie cut of every temp plane through the mask onto the screen planes
	const uint16_t words2 = j.clipRight ? d5 : static_cast<uint16_t>(d5 + 1);
	const uint16_t size2 = static_cast<uint16_t>(static_cast<uint16_t>(d4 << 6) | words2);
	const uint16_t bytes2 = static_cast<uint16_t>(words2 << 1);
	const int16_t modCD = static_cast<int16_t>(static_cast<uint16_t>(v.stride - bytes2));
	const int16_t modAB = j.clipRight ? 2 : 0;
	// ADD.W LAB_0503,D5 / LEA 0(A5,D5.W): word sum, sign-extended
	const int32_t scrOff = static_cast<int16_t>(static_cast<uint16_t>(off + v.origin));
	const uint16_t shiftBits = static_cast<uint16_t>(j.shift << 12);
	for(int i = 0; i < j.planes; ++i) {
		// planes without data take the 0x0722 minterm (clear under the mask, no A), others 0x0FF2
		BlitOp &p = j.paint[i];
		p = makeOp(static_cast<uint16_t>(shiftBits | (((j.planeBits >> i) & 1) ? 0x0FF2 : 0x0722)), shiftBits,
		           BLT_CH_A | BLT_CH_B | BLT_CH_C | BLT_CH_D);
		p.modA = p.modB = modAB;
		p.modC = p.modD = modCD;
		p.ptA = j.tempPlane[i];
		p.ptB = j.tempPlane[maskPlane];
		p.ptC = p.ptD = v.dest[i] + static_cast<uint32_t>(scrOff);
		p.size = size2;
	}
	j.visible = true;
	return true;
}

void runCel(const CelJob &job, const BlitSink &s) {
	if(!job.visible) {
		return;
	}
	for(int i = 0; i < job.planes; ++i) {
		if(!((job.planeBits >> i) & 1)) {
			continue;
		}
		if(job.cpuCopy) {
			s.cpuCopy(s.ctx, job, i);
		} else {
			s.blit(s.ctx, job.gather[i]);
		}
	}
	for(int i = 0; i < job.maskOps; ++i) {
		s.blit(s.ctx, job.mask[i]);
	}
	s.clearPads(s.ctx, job);  // runs while mask[1] is in flight, on words the blit never touches
	for(int i = 0; i < job.planes; ++i) {
		s.blit(s.ctx, job.paint[i]);
	}
}

void cpuGatherPlane(const CelJob &job, const uint8_t *src, uint8_t *dst) {
	const uint32_t rowBytes = static_cast<uint32_t>(job.words) * 2;
	const uint8_t *s = src;
	uint8_t *d = dst;
	for(uint16_t y = 0; y < job.height; ++y) {
		for(uint32_t x = 0; x < rowBytes; ++x) {
			d[x] = s[x];
		}
		s += static_cast<int32_t>(rowBytes) + job.srcMod;
		d += rowBytes + 2;
	}
	if(job.clipLeft || job.clipRight) {
		uint8_t *row = dst;
		for(uint16_t y = 0; y < job.height; ++y) {
			row[0] &= static_cast<uint8_t>(job.firstMask >> 8);
			row[1] &= static_cast<uint8_t>(job.firstMask);
			row[rowBytes - 2] &= static_cast<uint8_t>(job.lastMask >> 8);
			row[rowBytes - 1] &= static_cast<uint8_t>(job.lastMask);
			row += rowBytes + 2;
		}
	}
}

void clearTempPads(const CelJob &job, uint8_t *const *planes) {
	const uint32_t rowBytes = static_cast<uint32_t>(job.words) * 2 + 2;
	for(int p = 0; p < job.tempCount; ++p) {
		uint8_t *pad = planes[p] + static_cast<uint32_t>(job.words) * 2;
		for(uint16_t y = 0; y < job.height; ++y) {
			pad[0] = 0;
			pad[1] = 0;
			pad += rowBytes;
		}
	}
}

BlitOp planCopyRect(uint32_t src, uint32_t dst, uint16_t modA, uint16_t modD, uint16_t words, uint16_t rows) {
	BlitOp op = makeOp(0x09F0, 0x0000, BLT_CH_A | BLT_CH_D);
	op.ptA = src;
	op.ptD = dst;
	op.modA = static_cast<int16_t>(modA);
	op.modD = static_cast<int16_t>(modD);
	op.size = static_cast<uint16_t>(static_cast<uint16_t>(rows << 6) | words);
	return op;
}

BlitOp planCopyRectDesc(uint32_t src, uint32_t dst, uint16_t modA, uint16_t modD, uint16_t words, uint16_t rows) {
	BlitOp op = makeOp(0x09F0, 0x0002, BLT_CH_A | BLT_CH_D);  // BLTCON1 bit 1 = descending
	op.modA = static_cast<int16_t>(modA);
	op.modD = static_cast<int16_t>(modD);
	// D4 = 2*words*rows; D5 = modA*rows (the SUB.W only touches the low half), D6 likewise; both += D4
	const uint32_t d4 = static_cast<uint32_t>(static_cast<uint16_t>(words << 1)) * rows;
	uint32_t d5 = static_cast<uint32_t>(modA) * rows;
	d5 = (d5 & 0xFFFF0000u) | static_cast<uint16_t>(d5 - modA);
	uint32_t d6 = static_cast<uint32_t>(modD) * rows;
	d6 = (d6 & 0xFFFF0000u) | static_cast<uint16_t>(d6 - modD);
	op.ptA = src + (d5 + d4) - 2;
	op.ptD = dst + (d6 + d4) - 2;
	op.size = static_cast<uint16_t>(static_cast<uint16_t>(rows << 6) | words);
	return op;
}

int planCopyPlanes(BlitOp *ops, int depth, uint32_t src, uint32_t dst, uint32_t srcPlane, uint32_t dstPlane, uint16_t modA,
                   uint16_t modD, uint16_t words, uint16_t rows) {
	for(int p = 0; p < depth; ++p) {
		ops[p] = planCopyRect(src + p * srcPlane, dst + p * dstPlane, modA, modD, words, rows);
	}
	return depth;
}

int planCopyPlanesDesc(BlitOp *ops, int depth, uint32_t src, uint32_t dst, uint32_t srcPlane, uint32_t dstPlane, uint16_t modA,
                       uint16_t modD, uint16_t words, uint16_t rows) {
	for(int p = 0; p < depth; ++p) {
		ops[p] = planCopyRectDesc(src + p * srcPlane, dst + p * dstPlane, modA, modD, words, rows);
	}
	return depth;
}

CelClip celClip(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) {
	CelClip c;
	c.width = static_cast<int16_t>(static_cast<uint16_t>(right - left));   // SUB.W D0,D2
	c.height = static_cast<int16_t>(static_cast<uint16_t>(bottom - top));  // SUB.W D1,D3
	c.origin = static_cast<uint16_t>(static_cast<uint16_t>(top * 40u) + left);  // MULU #$28,D1 / ADD.W D0,D1
	return c;
}

uint8_t reverseBits(uint8_t b) {
	b = static_cast<uint8_t>((b >> 4) | (b << 4));
	b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
	return static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
}

bool mirrorCelHeader(uint8_t *cel, int16_t frame, MirrorJob &job) {
	CelFrame f;
	if(!celFrame(cel, frame, f)) {
		return false;
	}
	const int16_t rec = static_cast<int16_t>(static_cast<uint16_t>(frame * 10));
	uint8_t *hot = cel + 10 + rec + 8;
	// D5 = (width + 15) & $FFF0 (word), D7 = D5 - width = padding pixels, D5 >> 3 = bytes per row
	const uint16_t aligned = static_cast<uint16_t>(static_cast<uint16_t>(f.width + 15) & 0xFFF0);
	const uint16_t pad = static_cast<uint16_t>(aligned - f.width);
	if(*hot & 1) {
		*hot = static_cast<uint8_t>(pad << 4);  // LAB_04A9: LSL.W #4,D7 / MOVE.B D7,(A0)+
	} else {
		*hot = 0x01;
	}
	job.data = f.data;
	job.rowBytes = static_cast<uint16_t>(aligned >> 3);
	job.height = f.height;
	job.planeBits = f.planeBits;
	return true;
}

void mirrorCelPlanes(uint8_t *data, const MirrorJob &job, int planes) {
	if(job.rowBytes == 0 || job.height == 0) {
		return;
	}
	const int n = planes > CEL_MAX_PLANES ? CEL_MAX_PLANES : planes;
	uint8_t *p = data;
	for(int i = 0; i < n; ++i) {
		if(!((job.planeBits >> i) & 1)) {
			continue;  // absent plane: not stored, the source pointer does not move
		}
		for(uint16_t y = 0; y < job.height; ++y) {
			// the asm reads the row forward into the temp row from its end backwards, then copies it back: one reversal
			uint8_t *a = p;
			uint8_t *b = p + job.rowBytes - 1;
			while(a < b) {
				const uint8_t t = reverseBits(*a);
				*a++ = reverseBits(*b);
				*b-- = t;
			}
			if(a == b) {
				*a = reverseBits(*a);
			}
			p += job.rowBytes;
		}
	}
}

CelScratch carveCelScratch(uint32_t base, int depth, uint32_t chipTemp) {
	CelScratch s;
	s.block0 = base;
	s.block1 = s.block0 + 0x1000;
	s.block2 = s.block1 + 0x222E;
	s.temp = s.block2 + 0x1000;
	s.mask = s.temp + 5 * CEL_TEMP_STRIDE;
	if(depth == CEL_MAX_PLANES && chipTemp) {
		s.temp = chipTemp;
		s.mask = chipTemp + CEL_MAX_PLANES * CEL_TEMP_STRIDE;
	}
	return s;
}

}  // namespace ms
