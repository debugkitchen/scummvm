/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Small assembler routines of the second engine generation (Bi-Fi 2, Dunkle Schatten). They are
 * hand-written in the original (string instructions, xlat, carry tricks), so the translator
 * cannot handle them. Each function here does to memory what the 8086 code does, with 16-bit
 * offsets inside their segments, and leaves AX, DX and the direction flag as the original.
 */

#include "common/array.h"

#include "tad/dosvm.h"
#include "tad/prims.h"
#include "tad/render.h"

namespace Tad {

static inline uint16 arg(DosVm &vm, int i) {
	return vm.rw(vm.sreg[kSS], (uint16)(vm.r16[kSP] + 4 + 2 * i));
}

static inline int16 s16(uint32 v) {
	return (int16)(uint16)v;
}

/** Forward copy of n bytes, offsets wrapping inside their segments (rep movsw + movsb) */
static void copyBytes(DosVm &vm, uint16 sseg, uint16 soff, uint16 dseg, uint16 doff, uint32 n) {
	for (uint32 i = 0; i < n; i++)
		vm.wb(dseg, (uint16)(doff + i), vm.rb(sseg, (uint16)(soff + i)));
}

static void fillBytes(DosVm &vm, uint16 dseg, uint16 doff, uint32 n, byte v) {
	for (uint32 i = 0; i < n; i++)
		vm.wb(dseg, (uint16)(doff + i), v);
}

/** Wait for the start of a vertical retrace, reading the port as often as the original */
static void waitRetrace(DosVm &vm) {
	while (vm.inPort(0x3da) & 8) {
	}
	while (!(vm.inPort(0x3da) & 8)) {
	}
}

// ---------------------------------------------------------------------------- 1DE4 picture effects

/** a + round((b - a) * t / 256) as 1DE4:001C / 0022 compute it (16-bit product, bits 8..23) */
static uint16 lerp(uint16 a, uint16 b, uint16 t, uint16 &dx, bool checkFull) {
	if (checkFull && t >= 0x100)
		return b;
	if (t == 0)
		return a;
	if (a <= b) {
		const uint32 p = (uint32)(b - a) * t;
		const uint c = (p >> 7) & 1;
		dx = c ? 0xffff : 0;
		return (uint16)(((p >> 8) & 0xffff) + c + a);
	}
	const uint32 p = (uint32)(a - b) * t;
	const uint c = (p >> 7) & 1;
	dx = c ? 0xffff : 0;
	return (uint16)(a - (((p >> 8) & 0xffff) + c));
}

/** 1DE4:004E fade step: eight words (two colour triples, two extra targets) and t */
static void fadeStep(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 off = arg(vm, 0), seg = arg(vm, 1);
	uint16 t = arg(vm, 2), dx = r.dx;
	uint16 w[8];
	for (int i = 0; i < 8; i++)
		w[i] = vm.rw(seg, (uint16)(off + 2 * i));
	vm.ww(seg, (uint16)(off + 0x10), lerp(w[0], w[3], t, dx, true));
	vm.ww(seg, (uint16)(off + 0x12), lerp(w[1], w[4], t, dx, true));
	t = lerp(w[2], w[5], t, dx, true);
	const uint16 bp = lerp(0, w[6], t, dx, false);
	vm.ww(seg, (uint16)(off + 0x14), bp);
	const uint16 last = lerp(0, w[7], t, dx, false);
	vm.ww(seg, (uint16)(off + 0x16), last);
	vm.df = 0;
	r.ax = last ? bp : 0;
	r.dx = dx;
}

/** 1DE4:0123 highlight: OR 0x80 into w x h pixels at x, y of a bitmap (+0 width, +4 pixels) */
static void markRect(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 x = arg(vm, 0), y = arg(vm, 1), w = arg(vm, 2), h = arg(vm, 3);
	const uint16 hoff = arg(vm, 4), hseg = arg(vm, 5);
	const uint16 stride = vm.rw(hseg, hoff);
	const uint16 poff = vm.rw(hseg, (uint16)(hoff + 4)), pseg = vm.rw(hseg, (uint16)(hoff + 6));
	uint16 di = (uint16)(poff + x + (uint16)(y * stride));
	const uint16 skip = (uint16)(stride - w);
	uint16 rows = h;
	do {
		uint16 n = w;
		do {
			vm.wb(pseg, di, vm.rb(pseg, di) | 0x80);
			di++;
			n--;
		} while (n);
		di += skip;
		rows--;
	} while (rows);
	vm.df = 0;
	r.ax = 0;
	r.dx = 0;
}

/** 1DE4:0158 copy an image through a colour table made from a palette; table on the stack */
static void remapCopy(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 poff = arg(vm, 0), pseg = arg(vm, 1);
	const byte bias = arg(vm, 2) & 0xff;
	const uint16 soff = arg(vm, 3), sseg = arg(vm, 4), doff = arg(vm, 5), dseg = arg(vm, 6);
	const uint16 ss = vm.sreg[kSS], tab = (uint16)(vm.r16[kSP] - 0x108);
	for (int i = 0; i < 256; i++) {
		const int red = (int8)vm.rb(pseg, (uint16)(poff + 3 * i));
		const int green = (int8)vm.rb(pseg, (uint16)(poff + 3 * i + 1));
		const int blue = (int8)vm.rb(pseg, (uint16)(poff + 3 * i + 2));
		const uint16 v = (uint16)((5 * red + 9 * green + 2 * blue) * 4);
		vm.wb(ss, (uint16)(tab + i), (v >> 8) + bias);
	}
	const uint16 w = vm.rw(sseg, soff), h = vm.rw(sseg, (uint16)(soff + 2));
	vm.ww(dseg, doff, w);
	vm.ww(dseg, (uint16)(doff + 2), h);
	const uint32 p = (uint32)w * h;
	const uint16 n0 = (uint16)p;
	uint16 n = n0, si = (uint16)(soff + 4), di = (uint16)(doff + 4);
	byte last = 0;
	do {
		last = vm.rb(ss, (uint16)(tab + vm.rb(sseg, si)));
		vm.wb(dseg, di, last);
		si++;
		di++;
		n--;
	} while (n);
	vm.df = 0;
	r.ax = (n0 & 0xff00) | last;
	r.dx = (p >> 16) & 0xffff;
}

/** 1DE4:01B7 mosaic: copy an image replacing each bw x bh block by its average */
static void mosaic(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 soff = arg(vm, 0), sseg = arg(vm, 1), doff = arg(vm, 2), dseg = arg(vm, 3);
	const uint16 bw = arg(vm, 4), bh = arg(vm, 5);
	const uint16 w = vm.rw(sseg, soff), h = vm.rw(sseg, (uint16)(soff + 2));
	vm.ww(dseg, doff, w);
	vm.ww(dseg, (uint16)(doff + 2), h);
	uint16 si = (uint16)(soff + 4), di = (uint16)(doff + 4);
	uint16 remX = w, remY = h;
	const uint16 rowSkip = (uint16)((bh - 1) * w);
	for (;;) {
		const uint16 cw = MIN(bw, remX), ch = MIN(bh, remY);
		const uint16 skip = (uint16)(w - cw);
		uint32 sum = 0;
		uint16 s = si;
		for (uint y = 0; y < ch; y++) {
			for (uint x = 0; x < cw; x++) {
				sum += vm.rb(sseg, s);
				s++;
			}
			s += skip;
		}
		const uint16 div = (uint16)(cw * ch);
		const byte avg = div ? (sum / div) & 0xff : 0;
		uint16 d = di;
		for (uint y = 0; y < ch; y++) {
			for (uint x = 0; x < cw; x++) {
				vm.wb(dseg, d, avg);
				d++;
			}
			d += skip;
		}
		di += cw;
		si += cw;
		const uint16 ox = remX;
		remX -= cw;
		if (ox > cw)
			continue;
		remX = w;
		di += rowSkip;
		si += rowSkip;
		const uint16 oy = remY;
		remY -= bh;
		if (oy > bh)
			continue;
		break;
	}
	vm.df = 0;
	r.ax = bh;
	r.dx = 0;
}

/** 1DE4:0289 copy 64 rows of w bytes with a stride of 640, after waiting for the retrace */
static void copyLines(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 soff = arg(vm, 0), sseg = arg(vm, 1), doff = arg(vm, 2), dseg = arg(vm, 3), w = arg(vm, 4);
	waitRetrace(vm);
	uint16 si = soff, di = doff;
	const uint16 skip = (uint16)(0x280 - w);
	for (int row = 0; row < 64; row++) {
		for (uint x = 0; x < w; x++) {
			vm.wb(dseg, di, vm.rb(sseg, si));
			si++;
			di++;
		}
		si += skip;
		di += skip;
	}
	vm.df = 0;
	r.ax = 0;
	r.dx = skip;
}

/** 1DE4:02EA clear 64 rows of w bytes with a stride of 640, after waiting for the retrace */
static void clearLines(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 doff = arg(vm, 0), dseg = arg(vm, 1), w = arg(vm, 2);
	waitRetrace(vm);
	uint16 di = doff;
	const uint16 skip = (uint16)(0x280 - w);
	for (int row = 0; row < 64; row++) {
		for (uint x = 0; x < w; x++) {
			vm.wb(dseg, di, 0);
			di++;
		}
		di += skip;
	}
	vm.df = 0;
	r.ax = 0;
	r.dx = skip;
}

/**
 * 2E84:0000 unpack RIB data in place: buffer (far), packed size, unpacked size (longs). The
 * packed bytes lie at the start of the buffer; unpacking runs backwards from the end.
 */
static void ribInPlace(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 off = arg(vm, 0), seg = arg(vm, 1);
	const uint32 packed = arg(vm, 2) | ((uint32)arg(vm, 3) << 16);
	const uint32 size = arg(vm, 4) | ((uint32)arg(vm, 5) << 16);
	if (arg(vm, 2) == arg(vm, 4) && arg(vm, 3) == arg(vm, 5))
		return;
	// the original normalises its pointer argument in place (offset 1..0x10); the caller does
	// not read it again, but the stack shall look the same
	const uint16 ss = vm.sreg[kSS], sp = vm.r16[kSP];
	if (off == 0) {
		vm.ww(ss, (uint16)(sp + 4), 0x10);
		vm.ww(ss, (uint16)(sp + 6), (uint16)(seg - 1));
	} else if (off > 0x10) {
		vm.ww(ss, (uint16)(sp + 4), off & 15);
		vm.ww(ss, (uint16)(sp + 6), (uint16)(seg + (off >> 4)));
	}
	byte *m = vm.mem();
	const int64 base = ((int64)seg << 4) + off;
	const int64 top = kMemorySize;
	Common::Array<byte> src(packed);
	for (uint32 i = 0; i < packed; i++)
		src[i] = base + i < top ? m[base + i] : 0;
	int64 s = (int64)packed - 1, d = (int64)size - 1;
	auto rd = [&]() -> uint { return s >= 0 ? src[s--] : (s--, 0); };
	auto put = [&](int64 at, byte v) {
		if (base + at >= 0 && base + at < top)
			m[base + at] = v;
	};
	auto get = [&](int64 at) -> byte { return base + at >= 0 && base + at < top ? m[base + at] : 0; };
	while (d >= 0) {
		if (s >= d)
			break;                       // the rest is already in place
		const uint b = rd(), hi = b >> 4;
		uint n = 0, v = 0, o = 0;
		int mode;
		if (hi == 0) {
			n = (b & 15) + 4;
			v = rd();
			mode = 0;
		} else if (hi == 1) {
			n = (((b & 15) << 8) | rd()) + 20;
			v = rd();
			mode = 0;
		} else if (hi == 2) {
			n = (b & 15) + 1;
			mode = 1;
		} else if (hi == 3) {
			n = (((b & 15) << 8) | rd()) + 17;
			mode = 1;
		} else if (hi == 4) {
			if (b == 0x40) {
				const uint x = rd();
				n = (x << 8) | rd();
				mode = 1;
			} else if (b == 0x41) {
				uint x = rd();
				o = (x << 8) | rd();
				x = rd();
				n = (x << 8) | rd();
				mode = 2;
			} else if (b == 0x42) {
				const uint x = rd();
				o = (x << 8) | rd();
				n = rd() + 17;
				mode = 2;
			} else {
				n = (b & 15) + 1;
				const uint x = rd();
				o = (x << 8) | rd();
				mode = 2;
			}
		} else if (hi == 5) {
			o = (((b & 15) << 8) | rd()) + 2;
			n = rd() + 14;
			mode = 2;
		} else {
			n = hi - 2;
			o = (((b & 15) << 8) | rd()) + 2;
			mode = 2;
		}
		if (mode == 0) {
			while (n--)
				put(d--, v);
		} else if (mode == 1) {
			while (n--)
				put(d--, rd());
		} else {
			while (n--) {
				put(d, get(d + o));
				d--;
			}
		}
	}
	vm.df = 1;
}

/**
 * 29C8:1965 timer watchdog, polled by the waiting loops of the screen transitions: if the timer
 * interrupt has not run since the last call (CS:019E) and the PIT counter has not moved, the
 * PIT is programmed again (divisor CS:0096) and the result is -1, otherwise 0. With CS:1964 set
 * it counts CS:1960 down instead of reading the PIT.
 */
static void timerPoll(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	r.ax = 0;
	if (vm.rw(cs, 0x86) == 0)
		return;
	const byte flag = vm.rb(cs, 0x19e);
	vm.wb(cs, 0x19e, 0);
	bool reprogram = false;
	if (vm.rb(cs, 0x1964) != 0) {
		if (flag) {
			const byte n = vm.rb(cs, 0x1964);
			vm.ww(cs, 0x1960, (vm.rw(cs, 0x96) >> ((n - 1) & 0x1f)) | 0x1f);
			vm.ww(cs, 0x1962, 0);
			return;
		}
		const uint16 c = (uint16)(vm.rw(cs, 0x1960) - 1);
		vm.ww(cs, 0x1960, c);
		reprogram = c == 0;
	} else {
		if (flag) {
			vm.ww(cs, 0x1962, 0);
			return;
		}
		vm.outPort(0x43, 0);
		const byte hi = vm.inPort(0x40), lo = vm.inPort(0x40);
		const uint16 v = (hi << 8) | lo;
		const uint16 old = vm.rw(cs, 0x1962);
		vm.ww(cs, 0x1962, v);
		reprogram = old == v;
	}
	if (reprogram) {
		vm.outPort(0x43, 0x34);
		const uint16 d = vm.rw(cs, 0x96);
		vm.outPort(0x40, d & 0xff);
		vm.outPort(0x40, d >> 8);
		r.ax = 0xffff;
	}
}

/** The VM ends a run after 400 reads of the PIT: where the original would be cut there, it runs */
static bool timerPollReady(DosVm &vm, uint16 cs) {
	return !(vm.rw(cs, 0x86) && !vm.rb(cs, 0x1964) && !vm.rb(cs, 0x19e) && vm.yieldOnWait &&
			 vm.pitReads() + 2 > 400);
}

// ---------------------------------------------------------------------------- 2C3C changed-area map

/** 2C3C:1FD6: a rectangle in cells of 4 x 4 pixels (map of 80 x 50 bytes) */
struct CellRect {
	bool fail;
	uint16 dx;   ///< DX as the original leaves it on failure
	uint16 at;   ///< offset of the first cell
	int w, h;
};

static CellRect cells(uint16 x, uint16 y, uint16 w, uint16 h) {
	CellRect c = { true, 0, 0, 0, 0 };
	uint16 ax = (uint16)(w + x);
	if (ax & 0x8000) {
		c.dx = h;
		return c;
	}
	uint16 dx = (uint16)(h + y);
	c.dx = dx;
	if (dx & 0x8000)
		return c;
	int si = s16(x) >> 2;
	if (si < 0)
		si = 0;
	int di = s16(y) >> 2;
	uint16 cx = y;
	if (di < 0) {
		di = 0;
		cx = 0;
	}
	if (ax > 0x140)
		ax = 0x140;
	ax--;
	if (ax & 0x8000)
		return c;
	const int width = s16((ax >> 2) + 1 - si);
	if (width <= 0)
		return c;
	if (dx > 0xc8)
		dx = 0xc8;
	dx--;
	c.dx = dx;
	if (dx & 0x8000)
		return c;
	const int height = s16((dx >> 2) + 1 - di);
	if (height <= 0) {
		c.dx = (uint16)height;
		return c;
	}
	cx = (uint16)((cx & 0xfffc) << 2);
	c.fail = false;
	c.at = (uint16)(cx * 4 + cx + si);
	c.w = width;
	c.h = height;
	return c;
}

static void cellRect(DosVm &vm, uint16 cs, PrimResult &r, byte v, bool orIn) {
	const CellRect c = cells(arg(vm, 0), arg(vm, 1), arg(vm, 2), arg(vm, 3));
	if (c.fail) {
		r.ax = 0;
		r.dx = c.dx;
		return;
	}
	uint16 di = c.at;
	for (int row = 0; row < c.h; row++) {
		for (int i = 0; i < c.w; i++, di++)
			vm.wb(cs, di, orIn ? (vm.rb(cs, di) | v) : v);
		di = (uint16)(di + 0x50 - c.w);
	}
	vm.df = 0;
	r.ax = (cs & 0xff00) | v;
	r.dx = 0;
}

static void cellsClear(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	fillBytes(vm, cs, 0, 4000, 0);
	vm.df = 0;
	r.ax = 0;
}

static void cellsAll(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	fillBytes(vm, cs, 0, 4000, 1);
	vm.df = 0;
	r.ax = 0x101;
}

static void cellsMark(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	cellRect(vm, cs, r, 1, false);
}

static void cellsOr(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	cellRect(vm, cs, r, 1, true);
}

static void cellsMark3(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	cellRect(vm, cs, r, 3, false);
}

/** 2C3C:20C1: the number of cell rows left from the first row with a mark, 0 if none */
static void cellsTest(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const CellRect c = cells(arg(vm, 0), arg(vm, 1), arg(vm, 2), arg(vm, 3));
	if (c.fail) {
		r.ax = 0;
		r.dx = c.dx;
		return;
	}
	uint16 di = c.at;
	vm.df = 0;
	for (int row = c.h; row > 0; row--) {
		for (int i = 0; i < c.w; i++) {
			if (vm.rb(cs, (uint16)(di + i))) {
				r.ax = r.dx = (uint16)row;
				return;
			}
		}
		di += 0x50;
	}
	r.ax = r.dx = 0;
}

/** 2C3C:21EA: run list of the marked cells at 2C3C:0FA0 */
static void cellsRuns(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	uint di = 0, bx = 0xfa0, si = 0;
	uint dh = 0;
	auto inc = [&](uint o) { vm.wb(cs, o, vm.rb(cs, o) + 1); };
	auto longRun = [&]() {
		if (dh >= 0x40) {
			inc(bx);
			vm.wb(cs, si, 0xfc);
			vm.wb(cs, si + 1, 0);
			si += 2;
			dh -= 0x3f;
		}
		dh = (dh << 2) & 0xff;
		vm.wb(cs, si++, dh);
	};
	for (int dl = 50; dl > 0; dl--) {
		uint cx = 80;
		vm.wb(cs, bx, 0);
		vm.wb(cs, bx + 1, 0);
		si = bx + 1;
		for (;;) {
			dh = cx;
			while (cx && vm.rb(cs, di) == 0) {
				cx--;
				di++;
			}
			if (cx == 0) {
				longRun();
				break;
			}
			dh = (dh - cx) & 0xff;
			longRun();
			dh = cx;
			while (cx && vm.rb(cs, di) != 0) {
				cx--;
				di++;
			}
			inc(bx);
			if (cx == 0) {
				dh = (dh << 1) & 0xff;
				vm.wb(cs, si++, dh);
				vm.wb(cs, si++, 0);
				break;
			}
			dh = ((dh - cx) << 1) & 0xff;
			vm.wb(cs, si++, dh);
		}
		bx = si;
	}
	r.ax = 0;
	r.dx = (uint16)(dh << 8);
}

/** 2C3C:226E: copy the cells of the run list from one picture to another */
static void cellsCopy(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	uint16 si = arg(vm, 0), di = arg(vm, 2);
	const uint16 ds = arg(vm, 1), es = arg(vm, 3);
	uint16 bp = 0xfa0, bx = bp;
	vm.df = 0;
	for (int dl = 50; dl > 0; dl--) {
		for (int line = 4; line > 0; line--) {
			bx = bp;
			uint n = vm.rb(cs, bx);
			if (!n) {
				r.ax = (uint16)line;
				r.dx = (uint16)dl;
				return;
			}
			bx++;
			uint c = vm.rb(cs, bx++);
			si += c;
			di += c;
			while (n--) {
				c = vm.rb(cs, bx);
				copyBytes(vm, ds, si, es, di, 2 * c);
				si += 2 * c;
				di += 2 * c;
				bx++;
				c = vm.rb(cs, bx++);
				si += c;
				di += c;
			}
		}
		bp = bx;
	}
	r.ax = r.dx = 0;
}

/** Offsets of the library variables in the layout of a program (given as in Bi-Fi 2) */
static inline uint16 frmVar(const GfxLayout *l, uint16 bifi) {
	return (uint16)(bifi >= 0xf6a ? l->frmFont + bifi - 0xf6e : l->frmWindow + bifi - 0xd4);
}

static inline uint16 blkVar(const GfxLayout *l, uint16 bifi) {
	return (uint16)(l->blkTarget + bifi - 0x2ee);
}

// ---------------------------------------------------------------------------- 2EA0 frames and text

static void frmWindowPtr(DosVm &, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	r.ax = frmVar(lay, 0xd4);
	r.dx = cs;
}

static void frmFontPtr(DosVm &, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	r.ax = frmVar(lay, 0xf6a);
	r.dx = cs;
}

/** 2EA0:0E24: clipping window x, y, w, h, target width and height, target far pointer */
static void frmWindow(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	vm.ww(cs, frmVar(lay, 0xe0), arg(vm, 6));
	vm.ww(cs, frmVar(lay, 0xe2), arg(vm, 7));
	vm.ww(cs, frmVar(lay, 0xd4), arg(vm, 0));
	vm.ww(cs, frmVar(lay, 0xd8), arg(vm, 0) + arg(vm, 2));
	vm.ww(cs, frmVar(lay, 0xd6), arg(vm, 1));
	vm.ww(cs, frmVar(lay, 0xda), arg(vm, 1) + arg(vm, 3));
	vm.ww(cs, frmVar(lay, 0xdc), arg(vm, 4));
	vm.ww(cs, frmVar(lay, 0xde), arg(vm, 5));
	r.ax = arg(vm, 5);
}

/** 2EA0:100D: width of a text: advances of the glyphs plus the extra spacing between them */
static void frmTextWidth(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 fo = vm.rw(cs, frmVar(lay, 0xf70)), fs = vm.rw(cs, frmVar(lay, 0xf72));
	const byte first = vm.rb(fs, (uint16)(fo + 2)), n = vm.rb(fs, (uint16)(fo + 4));
	uint16 cx = 0, dx = 0;
	for (uint16 i = 0;; i++) {
		const byte ch = vm.rb(arg(vm, 1), (uint16)(arg(vm, 0) + i));
		if (!ch)
			break;
		const byte k = (byte)(ch - first);
		if (ch < first || k >= n)
			continue;
		const uint16 e = (uint16)(fo + 10 + k * 8);
		if (vm.rb(fs, e) == 0)
			continue;
		cx += vm.rw(fs, (uint16)(e + 2));
		dx++;
	}
	vm.df = 0;
	dx--;
	if (s16(dx) <= 0) {
		r.ax = cx;
		r.dx = dx;
		return;
	}
	const int32 p = (int32)(int8)vm.rb(cs, frmVar(lay, 0xf6f)) * s16(dx);
	r.ax = (uint16)(p + cx);
	r.dx = (uint16)(p >> 16);
}

/** Advance of one character in the current font (2EA0:1062), 0 if it has none */
static uint16 glyphWidth(DosVm &vm, const GfxLayout *lay, byte ch) {
	const uint16 fo = vm.rw(lay->frm, frmVar(lay, 0xf70)), fs = vm.rw(lay->frm, frmVar(lay, 0xf72));
	const uint16 k = (uint16)(ch - vm.rw(fs, (uint16)(fo + 2)));
	if (ch < vm.rw(fs, (uint16)(fo + 2)) || k >= vm.rw(fs, (uint16)(fo + 4)))
		return 0;
	return vm.rw(fs, (uint16)(fo + 0xc + (uint16)(k << 3)));
}

/** 2EA0:1062: advance of one character; DX = DS */
static void frmGlyphWidth(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	vm.df = 0;
	r.ax = glyphWidth(vm, lay, arg(vm, 0) & 0xff);
	r.dx = vm.sreg[kDS];
}

/** 2EA0:0E5F: frame at x, y; AX 1, but 0 for a visible frame of format 2 and above */
static void frmDraw(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 off = arg(vm, 2), seg = arg(vm, 3);
	const byte fmt = vm.rb(seg, (uint16)(off + 4));
	Gfx gfx(vm, *lay);
	const bool shown = gfx.frame(s16(arg(vm, 0)), s16(arg(vm, 1)), seg, off);
	vm.df = 0;
	r.ax = shown && fmt >= 2 ? 0 : 1;
}

/** 2EA0:0F8F: zero-terminated text at x, y in the current colour; advances its x argument */
static void frmText(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	Common::Array<byte> codes;
	for (uint16 i = 0;; i++) {
		const byte c = vm.rb(arg(vm, 3), (uint16)(arg(vm, 2) + i));
		codes.push_back(c);
		if (!c)
			break;
	}
	Gfx gfx(vm, *lay);
	const int x = gfx.text(s16(arg(vm, 0)), s16(arg(vm, 1)), &codes[0], codes.size());
	vm.ww(vm.sreg[kSS], (uint16)(vm.r16[kSP] + 4), (uint16)x);
	vm.df = 0;
	r.ax = 0;
}

/** 2EA0:0F74: one character (the argument word with its high byte cleared, as a text) */
static void frmChar(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 ss = vm.sreg[kSS], sp = vm.r16[kSP];
	vm.wb(ss, (uint16)(sp + 9), 0);
	const byte codes[2] = { vm.rb(ss, (uint16)(sp + 8)), 0 };
	Gfx gfx(vm, *lay);
	gfx.text(s16(arg(vm, 0)), s16(arg(vm, 1)), codes, 2);
	vm.df = 0;
	r.ax = 0;
}

/** 2C3C:23C0: tiles touching marked cells; AX = the result byte, sign extended */
static void cellsTiles(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	DirtyMap dirty(vm, cs);
	const int res = dirty.tiles(arg(vm, 1), arg(vm, 0), arg(vm, 2), arg(vm, 3), arg(vm, 4), arg(vm, 5));
	// the original counts its rows argument down
	vm.ww(vm.sreg[kSS], (uint16)(vm.r16[kSP] + 14), 0);
	vm.df = 0;
	r.ax = (uint16)res;
}

// ---------------------------------------------------------------------------- 2B72 blocks
// the target picture: width at 2B72:02F2, height 02F4, pixels 02EE (far)

/** 2B72:0008: copy w x h at x, y of the target into a picture (u16 w, u16 h, pixels) */
static void blkGrab(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 tw = vm.rw(cs, blkVar(lay, 0x2f2)), ts = vm.rw(cs, blkVar(lay, 0x2f0));
	uint16 si = (uint16)(vm.rw(cs, blkVar(lay, 0x2ee)) + (uint16)(arg(vm, 1) * tw + arg(vm, 0)));
	const uint16 w = arg(vm, 2), h = arg(vm, 3), es = arg(vm, 5);
	uint16 di = arg(vm, 4);
	vm.ww(es, di, w);
	vm.ww(es, (uint16)(di + 2), h);
	di += 4;
	for (uint32 row = h ? h : 0x10000; row > 0; row--) {
		copyBytes(vm, ts, si, es, di, w);
		si += tw;
		di += w;
	}
	vm.df = 0;
	r.ax = 0;
	r.dx = vm.sreg[kDS];
}

/** 2B72:0048: picture at x, y of the target */
static void blkImage(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 tw = vm.rw(cs, blkVar(lay, 0x2f2)), es = vm.rw(cs, blkVar(lay, 0x2f0));
	uint16 di = (uint16)(vm.rw(cs, blkVar(lay, 0x2ee)) + (uint16)(arg(vm, 1) * tw + arg(vm, 0)));
	const uint16 ds = arg(vm, 3);
	uint16 si = arg(vm, 2);
	const uint16 w = vm.rw(ds, si), h = vm.rw(ds, (uint16)(si + 2));
	si += 4;
	for (uint32 row = h ? h : 0x10000; row > 0; row--) {
		copyBytes(vm, ds, si, es, di, w);
		si += w;
		di += tw;
	}
	vm.df = 0;
	r.ax = 0;
	r.dx = vm.sreg[kDS];
}

/** 2B72:0082: part (sx, sy, w, h) of a picture at x, y of the target */
static void blkPart(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 tw = vm.rw(cs, blkVar(lay, 0x2f2)), es = vm.rw(cs, blkVar(lay, 0x2f0));
	uint16 di = (uint16)(vm.rw(cs, blkVar(lay, 0x2ee)) + (uint16)(arg(vm, 1) * tw + arg(vm, 0)));
	const uint16 ds = arg(vm, 3);
	const uint16 iw = vm.rw(ds, arg(vm, 2));
	uint16 si = (uint16)(arg(vm, 2) + 4 + arg(vm, 4) + (uint16)(arg(vm, 5) * iw));
	const uint16 w = arg(vm, 6), h = arg(vm, 7);
	for (uint32 row = h ? h : 0x10000; row > 0; row--) {
		copyBytes(vm, ds, si, es, di, w);
		si += iw;
		di += tw;
	}
	vm.df = 0;
	r.ax = w;
	r.dx = (uint16)(iw - w);
}

/** 2B72:00D2: fill w x h at x, y */
static void blkFill(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 tw = vm.rw(cs, blkVar(lay, 0x2f2)), es = vm.rw(cs, blkVar(lay, 0x2f0));
	uint16 di = (uint16)(vm.rw(cs, blkVar(lay, 0x2ee)) + (uint16)(arg(vm, 1) * tw + arg(vm, 0)));
	const uint16 w = arg(vm, 2), h = arg(vm, 3);
	const byte c = arg(vm, 4) & 0xff;
	for (uint32 row = h ? h : 0x10000; row > 0; row--) {
		fillBytes(vm, es, di, w, c);
		di += tw;
	}
	vm.df = 0;
	r.ax = c * 0x101;
	r.dx = w;
}

/** 2B72:010A: fill the whole target */
static void blkClear(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 n = (uint16)(vm.rw(cs, blkVar(lay, 0x2f4)) * vm.rw(cs, blkVar(lay, 0x2f2)));
	const byte c = arg(vm, 0) & 0xff;
	fillBytes(vm, vm.rw(cs, blkVar(lay, 0x2f0)), vm.rw(cs, blkVar(lay, 0x2ee)), n, c);
	vm.df = 0;
	r.ax = c * 0x101;
	r.dx = vm.r16[kDI];
}

/** 2B72:0130: wait for the start of a vertical retrace */
static void blkRetrace(DosVm &vm, uint16, const GfxLayout *lay, PrimResult &r) {
	byte v;
	do {
		v = vm.inPort(0x3da);
	} while (v & 8);
	do {
		v = vm.inPort(0x3da);
	} while (!(v & 8));
	r.ax = 0x800 | v;
	r.dx = 0x3da;
}

/** 2B72:0140: horizontal line of len pixels at x, y */
static void blkHLine(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint32 p = (uint32)arg(vm, 1) * vm.rw(cs, blkVar(lay, 0x2f2));
	const byte c = arg(vm, 3) & 0xff;
	fillBytes(vm, vm.rw(cs, blkVar(lay, 0x2f0)), (uint16)(vm.rw(cs, blkVar(lay, 0x2ee)) + (uint16)(p + arg(vm, 0))), arg(vm, 2), c);
	vm.df = 0;
	r.ax = c * 0x101;
	r.dx = ((p >> 16) & 0xff00) | c;
}

/** 2B72:0170: vertical line of len pixels at x, y */
static void blkVLine(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 tw = vm.rw(cs, blkVar(lay, 0x2f2)), es = vm.rw(cs, blkVar(lay, 0x2f0));
	const uint16 ax = (uint16)(arg(vm, 1) * tw + arg(vm, 0));
	const byte c = arg(vm, 3) & 0xff;
	uint16 di = (uint16)(vm.rw(cs, blkVar(lay, 0x2ee)) + ax);
	for (uint32 n = arg(vm, 2) ? arg(vm, 2) : 0x10000; n > 0; n--) {
		vm.wb(es, di, c);
		di += tw;
	}
	vm.df = 0;
	r.ax = (ax & 0xff00) | c;
	r.dx = vm.r16[kDI];
}

/** 2B72:019A: copy the target description (width, height, far pixels) to a far pointer */
static void blkGetTarget(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 off = arg(vm, 0), seg = arg(vm, 1);
	vm.ww(seg, off, vm.rw(cs, blkVar(lay, 0x2f2)));
	vm.ww(seg, (uint16)(off + 2), vm.rw(cs, blkVar(lay, 0x2f4)));
	vm.ww(seg, (uint16)(off + 4), vm.rw(cs, blkVar(lay, 0x2ee)));
	vm.ww(seg, (uint16)(off + 6), vm.rw(cs, blkVar(lay, 0x2f0)));
	vm.df = 0;
	r.ax = vm.rw(cs, blkVar(lay, 0x2f0));
}

/** 2B72:0224: set the target from a description (null = the screen, 320 x 200 at A000:0000) */
static void blkSetTarget(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r) {
	const uint16 off = arg(vm, 0), seg = arg(vm, 1);
	if (!off && !seg) {
		vm.ww(cs, blkVar(lay, 0x2f2), 0x140);
		vm.ww(cs, blkVar(lay, 0x2f4), 0xc8);
		vm.ww(cs, blkVar(lay, 0x2ee), 0);
		vm.ww(cs, blkVar(lay, 0x2f0), 0xa000);
		r.ax = 0;
		return;
	}
	vm.ww(cs, blkVar(lay, 0x2f2), vm.rw(seg, off));
	vm.ww(cs, blkVar(lay, 0x2f4), vm.rw(seg, (uint16)(off + 2)));
	const uint16 po = vm.rw(seg, (uint16)(off + 4)), ps = vm.rw(seg, (uint16)(off + 6));
	vm.ww(cs, blkVar(lay, 0x2ee), po);
	vm.ww(cs, blkVar(lay, 0x2f0), ps);
	r.ax = po;
}

// ---------------------------------------------------------------------------- 10D7 palette and pointers

/** 10D7:0008: n colours from index i to the DAC */
static void palSet(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 n = arg(vm, 3);
	const byte i0 = arg(vm, 2) & 0xff;
	vm.df = 0;
	const uint16 cx = (uint16)(3 * n);
	if (!cx) {
		r.ax = n;
		return;
	}
	vm.outPort(0x3c8, i0);
	for (uint i = 0; i < cx; i++)
		vm.outPort(0x3c9, vm.rb(arg(vm, 1), (uint16)(arg(vm, 0) + i)));
	r.ax = (n & 0xff00) | i0;
	r.dx = 0x3c9;
}

/** 10D7:002B: n colours scaled by f/256 (at most 63) from src to dst */
static void palScale(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	uint16 si = arg(vm, 0), di = arg(vm, 2);
	const uint16 ds = arg(vm, 1), es = arg(vm, 3), bx = arg(vm, 5);
	uint16 cx = (uint16)(3 * arg(vm, 4));
	vm.df = 0;
	if (!bx) {
		fillBytes(vm, es, di, cx, 0);
		r.ax = 0;
		return;
	}
	uint16 ax = 0, dx = 0;
	do {
		ax = (ax & 0xff00) | vm.rb(ds, si);
		si++;
		const uint32 p = (uint32)ax * bx;
		ax = p & 0xffff;
		dx = (p >> 16) & 0xffff;
		dx = dx ? 0xffff : 0;
		ax |= dx;
		ax = (uint16)(((ax & 0xff) << 8) | (ax >> 8));
		const uint cf = (ax >> 8) >> 7;
		ax = (uint16)(((dx & 0xff) << 8) | (ax & 0xff));
		ax = (uint16)(ax + dx + cf);
		if (ax >= 0x3f)
			ax = 0x3f;
		vm.wb(es, di, ax & 0xff);
		di++;
		cx--;
	} while (cx);
	r.ax = ax;
	r.dx = dx;
}

/** Normalised segment of a far pointer seg:off made absolute (as the library computes it) */
static uint16 normSeg(uint16 base, uint16 off, uint16 seg) {
	uint16 v = (uint16)((off & 0xfff0) | seg);
	v = (uint16)((v >> 4) | (v << 12));
	return (uint16)(base + v);
}

/** 10D7:0147: zero-terminated list of far offsets (relative to the list) made absolute in place */
static void palRelocList(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 si = arg(vm, 0), ds = arg(vm, 1);
	uint16 di = si, bp = 0, dx = r.dx;
	vm.df = 0;
	for (;;) {
		uint16 bx = vm.rw(ds, di), cx = vm.rw(ds, (uint16)(di + 2));
		if (!(bx | cx))
			break;
		const uint32 sum = (uint32)bx + si;
		bx = (uint16)sum;
		cx = (uint16)(cx + (sum >> 16));
		dx = normSeg(ds, bx, cx);
		vm.ww(ds, di, bx & 0xf);
		vm.ww(ds, (uint16)(di + 2), dx);
		di += 4;
		bp++;
	}
	r.ax = bp;
	r.dx = dx;
}

/** 10D7:01C9: n far offsets relative to base made absolute (null stays null) */
static void palRelocN(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 si = arg(vm, 0), ds = arg(vm, 1), es = arg(vm, 3);
	uint16 di = arg(vm, 2), n = arg(vm, 4), ax = r.ax, dx = r.dx;
	vm.df = 0;
	while (n) {
		uint16 bx = vm.rw(es, di), bp = vm.rw(es, (uint16)(di + 2));
		if (!(bx | bp)) {
			ax = 0;
			dx = 0;
		} else {
			const uint32 sum = (uint32)bx + si;
			bx = (uint16)sum;
			bp = (uint16)(bp + (sum >> 16));
			ax = bx & 0xf;
			dx = normSeg(ds, bx, bp);
		}
		vm.ww(es, di, ax);
		vm.ww(es, (uint16)(di + 2), dx);
		di += 4;
		ax = dx;
		n--;
	}
	r.ax = ax;
	r.dx = dx;
}

/** 10D7:0220: far pointer + long, normalised */
static void palHugeAdd(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint32 sum = (uint32)arg(vm, 2) + arg(vm, 0);
	const uint16 bx = (uint16)sum, cx = (uint16)(arg(vm, 3) + (sum >> 16));
	r.ax = bx & 0xf;
	r.dx = normSeg(arg(vm, 1), bx, cx);
}

/** 10D7:0243: copy n (long) bytes between huge pointers, in 32 KB steps */
static void palHugeCopy(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	uint16 ds = (uint16)(arg(vm, 1) + (arg(vm, 0) >> 4)), si = arg(vm, 0) & 0xf;
	uint16 es = (uint16)(arg(vm, 3) + (arg(vm, 2) >> 4)), di = arg(vm, 2) & 0xf;
	uint16 bx = arg(vm, 4), cx = arg(vm, 5);
	cx = (uint16)((cx << 1) | (bx >> 15));
	bx &= 0x7fff;
	uint16 ax = arg(vm, 2) >> 4, dx = es;
	vm.df = 0;
	auto words = [&](uint n) {
		for (uint i = 0; i < n; i++) {
			vm.wb(es, di, vm.rb(ds, si));
			vm.wb(es, (uint16)(di + 1), vm.rb(ds, (uint16)(si + 1)));
			si += 2;
			di += 2;
		}
	};
	if (cx) {
		ax = cx;
		do {
			words(0x4000);
			si &= 0xf;
			di &= 0xf;
			ds += 0x800;
			es += 0x800;
			ax--;
		} while (ax);
		dx = ds;
	}
	words(bx >> 1);
	if (bx & 1)
		vm.wb(es, di, vm.rb(ds, si));
	r.ax = ax;
	r.dx = dx;
}

// ---------------------------------------------------------------------------- 1E17 text with control codes
// In the game texts, bytes 1..3 are codes followed by a parameter byte (1 and 3: colour and
// similar, 2: insert the text with that number), 0 ends a text.

/** 1E17:000A: length of a text as the original counts it (a code and its parameter count 2) */
static uint16 codeLength(DosVm &vm, uint16 seg, uint16 off) {
	uint16 si = off, bx = 0xffff;
	for (;;) {
		bx++;
		const byte al = vm.rb(seg, si);
		si++;
		if (al > 3)
			continue;
		if (al == 0)
			break;
		si++;
		bx++;
	}
	return bx;
}

enum LineStop {
	kStopCR,      ///< 0 and CR
	kStopLine,    ///< 0, LF and CR
	kStopWord     ///< 0, space, LF and CR
};

/** The common loop of 1E17:00B5 / 0100 / 014F: copy until a stop byte or the length runs out */
static void copyLine(DosVm &vm, PrimResult &r, byte limit, LineStop mode) {
	uint16 si = arg(vm, 0), di = arg(vm, 2);
	const uint16 ds = arg(vm, 1), es = arg(vm, 3);
	uint16 dx = arg(vm, 4), cx = 0;
	vm.df = 0;
	while (dx) {
		const byte al = vm.rb(ds, si);
		si++;
		if (al > limit) {
			vm.wb(es, di, al);
			di++;
			cx++;
			dx--;
			continue;
		}
		bool stop = al == 0 || al == 0xd;
		if (mode != kStopCR)
			stop = stop || al == 0xa;
		if (mode == kStopWord)
			stop = stop || al == 0x20;
		if (stop)
			break;
		if (al > 3) {
			vm.wb(es, di, al);
			di++;
			cx++;
			dx--;
			continue;
		}
		if (dx < 2)
			break;
		vm.wb(es, di, al);
		vm.wb(es, (uint16)(di + 1), vm.rb(ds, si));
		si++;
		di += 2;
		cx += 2;
		dx -= 2;
	}
	r.ax = cx;
	r.dx = (uint16)(arg(vm, 4) - dx);
}

/** 1E17:002D: copy a text, replacing code 2 by the text it names (table of far pointers) */
static void txtExpand(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	uint16 si = arg(vm, 2), di = arg(vm, 4);
	const uint16 ds = arg(vm, 3), es = arg(vm, 5), tab = arg(vm, 0), tabs = arg(vm, 1);
	uint16 dx = arg(vm, 6), cx = 0;
	vm.df = 0;
	while (dx) {
		const byte al = vm.rb(ds, si);
		si++;
		if (al > 3) {
			vm.wb(es, di, al);
			di++;
			cx++;
			dx--;
			continue;
		}
		if (al == 0)
			break;
		if (al == 2) {
			const byte k = vm.rb(ds, si);
			si++;
			const uint16 so = vm.rw(tabs, (uint16)(tab + k * 4)), sseg = vm.rw(tabs, (uint16)(tab + k * 4 + 2));
			const uint16 n = codeLength(vm, sseg, so);
			if (dx < n)
				break;
			for (uint i = 0; i < n; i++)
				vm.wb(es, (uint16)(di + i), vm.rb(sseg, (uint16)(so + i)));
			di += n;
			dx -= n;
			cx += 2;
			continue;
		}
		if (dx < 2)
			break;
		vm.wb(es, di, al);
		vm.wb(es, (uint16)(di + 1), vm.rb(ds, si));
		si++;
		di += 2;
		cx += 2;
		dx -= 2;
	}
	r.ax = cx;
	r.dx = (uint16)(arg(vm, 6) - dx);
}

static void txtLineCR(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	copyLine(vm, r, 0x1f, kStopCR);
}

static void txtLine(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	copyLine(vm, r, 0x1f, kStopLine);
}

static void txtWord(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	copyLine(vm, r, 0x20, kStopWord);
}

/**
 * 1E17:01A0: width of at most n bytes of a text in the current font plus the
 * spacing between the visible characters; AX = the bytes used, DX = the width. Counts n down on
 * the stack.
 */
static void txtWidth(DosVm &vm, uint16, const GfxLayout *lay, PrimResult &r) {
	const uint16 ss = vm.sreg[kSS], sp = vm.r16[kSP];
	const uint16 ds = arg(vm, 1), spacing = arg(vm, 3);
	uint16 si = arg(vm, 0), n = arg(vm, 2), cx = 0, dx = 0, bx = 0;
	vm.df = 0;
	bool check = true;
	for (;;) {
		if (check && n == 0)
			break;
		check = true;
		const byte al = vm.rb(ds, si);
		si++;
		if (al > 0x1f) {
			cx++;
			const uint16 w = glyphWidth(vm, lay, al);
			if (w) {
				dx += w;
				bx = spacing;
				dx += bx;
			}
			n--;
			continue;
		}
		if (al != 1 && al != 3)
			break;
		si++;
		cx += 2;
		const bool borrow = n < 2;
		n -= 2;
		if (borrow) {
			cx -= 2;
			break;
		}
		check = false;
	}
	vm.ww(ss, (uint16)(sp + 8), n);
	r.ax = cx;
	r.dx = (uint16)(dx - bx);
}

/** 1E17:01FB: in place: code 3 and line breaks become spaces, single spaces, none at the ends */
static void txtTidy(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 ds = arg(vm, 1), s0 = arg(vm, 0);
	vm.df = 0;
	if (vm.rb(ds, s0) == 0) {
		r.ax = 0x2000 | (ds & 0xff);
		return;
	}
	uint16 si = s0, di = s0;
	for (;;) {
		const byte al = vm.rb(ds, si);
		si++;
		if (al > 0x1f) {
			di++;
			continue;
		}
		if (al == 0)
			break;
		if (al <= 3) {
			si++;
			if (al == 3) {
				vm.wb(ds, di, 0x20);
				vm.wb(ds, (uint16)(di + 1), 0x20);
			}
			di += 2;
			continue;
		}
		if (al == 0xa || al == 0xd)
			vm.wb(ds, di, 0x20);
		di++;
	}
	si = s0;
	di = s0;
	byte al;
	do {
		al = vm.rb(ds, si);
		si++;
	} while (al == 0x20);
	if (al != 0) {
		for (;;) {
			if (al > 0x20 || (al > 3 && al < 0x20)) {
				vm.wb(ds, di, al);
				di++;
				al = vm.rb(ds, si);
				si++;
				continue;
			}
			if (al == 0x20) {
				vm.wb(ds, di, al);
				di++;
				al = vm.rb(ds, si);
				si++;
				while (al == 0x20) {
					al = vm.rb(ds, si);
					si++;
				}
				if (al == 0) {
					di--;
					break;
				}
				continue;
			}
			if (al == 0)
				break;
			vm.wb(ds, di, al);
			di++;
			al = vm.rb(ds, si);
			si++;
			vm.wb(ds, di, al);
			di++;
			al = vm.rb(ds, si);
			si++;
		}
	}
	vm.wb(ds, di, 0);
	r.ax = 0x2000;
}

/** 1E17:027B: length of a text as 1E17:000A counts it */
static void txtLen(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	vm.df = 0;
	r.ax = codeLength(vm, arg(vm, 1), arg(vm, 0));
}

// ---------------------------------------------------------------------------- 12AF palette shading

/** 12AF:0000: make the offsets of a walk-area list absolute (once; +2 marks it done) */
static void shdReloc(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 si = arg(vm, 0), ds = arg(vm, 1);
	vm.df = 0;
	const uint16 ax = ds | si;
	uint16 bx = ax;
	if (ax && vm.rw(ds, (uint16)(si + 2)) != ax) {
		vm.ww(ds, (uint16)(si + 2), ax);
		uint16 cx = vm.rw(ds, si);
		bx = (uint16)(si + 4);
		do {
			vm.ww(ds, (uint16)(bx + 6), ds);
			vm.ww(ds, (uint16)(bx + 0xa), ds);
			vm.ww(ds, (uint16)(bx + 4), vm.rw(ds, (uint16)(bx + 4)) + si);
			vm.ww(ds, (uint16)(bx + 8), vm.rw(ds, (uint16)(bx + 8)) + si);
			bx += 0x2e;
			cx--;
		} while (cx);
	}
	r.ax = ax ^ bx;
}

/** 12AF:0035: scale of a figure: step n along its line (fixed point 1.15) */
static void shdScale(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const int32 n = s16(arg(vm, 2));
	r.ax = 0;
	if (!n)
		return;
	const uint16 bx = arg(vm, 0), es = arg(vm, 1);
	if (!(bx | es))
		return;
	const uint16 si = vm.rw(es, (uint16)(bx + 0x24)), ds = vm.rw(es, (uint16)(bx + 0x26));
	if (!(si | ds))
		return;
	const int32 p = n * s16(vm.rw(ds, (uint16)(si + 0x18)));
	if (p != s16(p)) {
		r.ax = 1;
		r.dx = (uint16)(p >> 16);
		return;
	}
	const uint16 ax = (uint16)(p + vm.rw(ds, (uint16)(si + 0x1a)));
	if (ax & 0x8000) {
		r.ax = 1;
		r.dx = (uint16)(p >> 16);
		return;
	}
	vm.ww(ds, (uint16)(si + 0x1a), ax);
	const int32 cx = s16(ax);
	static const byte kTerms[3][3] = { { 0x1e, 0x1c, 0x28 }, { 0x22, 0x20, 0x2a }, { 0x26, 0x24, 0x2c } };
	uint16 dx = 0;
	for (int i = 0; i < 3; i++) {
		const int32 q = s16(vm.rw(ds, (uint16)(si + kTerms[i][0]))) * cx;
		const uint16 lo = (uint16)q, hi = (uint16)(q >> 16);
		dx = (uint16)(hi + vm.rw(ds, (uint16)(si + kTerms[i][1])) + (lo >> 15));
		vm.ww(ds, (uint16)(si + kTerms[i][2]), dx);
	}
	r.ax = 0xffff;
	r.dx = dx;
}

/** 12AF:01A0: shading row (0..3) of 64 entries from colour v towards 63 in k steps */
static void shdRow(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const uint16 row = arg(vm, 0), v = arg(vm, 1), k = arg(vm, 2);
	if (row > 3)
		return;
	if (v > 0x3f || !k || k > 0x40) {
		r.ax = v;
		return;
	}
	uint16 di = (uint16)((row << 6) + 0xa0), dx = 0;
	byte al = v & 0xff, ah = v >> 8, bh = al;
	vm.df = 0;
	for (int cx = 0x40; cx > 0; cx--) {
		vm.wb(cs, di++, al);
		ah--;
		if (ah & 0x80) {
			bh++;
			ah = (dx / k) & 0xff;
			al = bh;
			if (al >= 0x3f)
				al = 0x3f;
		}
		dx++;
	}
	r.ax = (ah << 8) | al;
	r.dx = dx;
}

/** 12AF:01EF: n colours (3 bytes each) through shading row 0 */
static void shdMap(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const uint16 n = arg(vm, 4);
	const uint16 ax0 = (uint16)(n << 1);
	vm.df = 0;
	if (!ax0) {
		r.ax = 0;
		return;
	}
	uint16 si = arg(vm, 0), di = arg(vm, 2);
	byte al = 0;
	for (uint16 cx = (uint16)(n * 3); cx > 0; cx--) {
		al = vm.rb(cs, (uint16)(0xa0 + vm.rb(arg(vm, 1), si)));
		si++;
		vm.wb(arg(vm, 3), di, al);
		di++;
	}
	r.ax = (ax0 & 0xff00) | al;
}

/** 12AF:0217: n colours, red, green and blue each through its own shading row */
static void shdMap3(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const uint16 n = arg(vm, 7);
	if (!n)
		return;
	const uint16 tab[3] = {
		(uint16)((uint16)(arg(vm, 4) << 6) + 0xa0),
		(uint16)((uint16)(arg(vm, 5) << 6) + 0xa0),
		(uint16)((uint16)(arg(vm, 6) << 6) + 0xa0)
	};
	uint16 si = arg(vm, 0), di = arg(vm, 2);
	byte al = 0;
	vm.df = 0;
	for (uint16 cx = n; cx > 0; cx--) {
		for (int i = 0; i < 3; i++) {
			al = vm.rb(cs, (uint16)(tab[i] + vm.rb(arg(vm, 1), si)));
			si++;
			vm.wb(arg(vm, 3), di, al);
			di++;
		}
	}
	r.ax = ((uint16)(arg(vm, 4) << 6) & 0xff00) | al;
	r.dx = tab[1];
}

// ---------------------------------------------------------------------------- 1724 colour table (Dunkle Schatten)

/**
 * 1724:01E1: colour table entries start..start+n-1 at CS:00A6 from a palette (signed 6-bit
 * triples): brightness 5r + 9g + 2b, scaled by the word at CS:00A4 (high word of the product),
 * plus the byte at CS:00A0. Leaves ES = CS.
 */
static void shdTable(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	uint16 si = arg(vm, 0);
	const uint16 ds = arg(vm, 1);
	uint16 di = (uint16)(0xa6 + arg(vm, 2)), cx = arg(vm, 3);
	const uint16 k = vm.rw(cs, 0xa4), base = vm.rw(cs, 0xa0);
	uint16 ax = r.ax, dx = r.dx;
	vm.df = 0;
	do {
		const int16 red = (int8)vm.rb(ds, si), green = (int8)vm.rb(ds, (uint16)(si + 1));
		const int16 blue = (int8)vm.rb(ds, (uint16)(si + 2));
		si += 3;
		const uint16 sum = (uint16)(red * 5 + green * 9 + blue * 2);
		const uint32 p = (uint32)sum * k;
		dx = (uint16)(p >> 16);
		ax = (uint16)(((p & 0xff00) | (dx & 0xff)) + base);
		vm.wb(cs, di, ax & 0xff);
		di++;
		cx--;
	} while (cx);
	vm.sreg[kES] = cs;
	r.ax = ax;
	r.dx = dx;
}

/**
 * 1724:0221: pixels of a rectangle (x, y, w, h) of a picture buffer structure (far) through the
 * colour table at CS:00A6. Leaves ES = the segment of the pixels.
 */
static void shdRect(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const uint16 bo = arg(vm, 4), bs = arg(vm, 5);
	const uint16 bw = vm.rw(bs, bo);
	const uint16 es = vm.rw(bs, (uint16)(bo + 6));
	uint16 di = (uint16)(vm.rw(bs, (uint16)(bo + 4)) + arg(vm, 0) + (uint16)(bw * arg(vm, 1)));
	const uint16 w = arg(vm, 2), skip = (uint16)(bw - w);
	uint16 dx = arg(vm, 3);
	const int step = vm.df ? -1 : 1;
	byte al = 0;
	do {
		uint16 cx = w;
		do {
			al = vm.rb(cs, (uint16)(0xa6 + vm.rb(es, di)));
			vm.wb(es, di, al);
			di += step;
			cx--;
		} while (cx);
		di += skip;
		dx--;
	} while (dx);
	vm.sreg[kES] = es;
	r.ax = al;
	r.dx = 0;
}

// ---------------------------------------------------------------------------- 2750 scaler (Dunkle Schatten)

/**
 * 2750:0430: scaled copy of a raw frame (source far, target far, target size, sx, sy in 1/256,
 * negative mirrors); AX = its size, 0 if it fails. The library keeps the sizes at CS:0000..0007.
 */
static void scaleFrameAt(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const uint16 fo = arg(vm, 0), fs = arg(vm, 1), bo = arg(vm, 2), bs = arg(vm, 3), size = arg(vm, 4);
	const int16 sx = (int16)arg(vm, 5), sy = (int16)arg(vm, 6);
	const uint16 w = vm.rw(fs, fo), h = vm.rw(fs, (uint16)(fo + 2));
	vm.ww(cs, 0, w);
	vm.ww(cs, 4, w);
	vm.ww(cs, 6, h);
	const uint16 dw = (uint16)(((int64)ABS((int)sx) * w + 0x7f) >> 8);
	if (((int64)ABS((int)sx) * w + 0x7f) >> 24 == 0)
		vm.ww(cs, 2, dw);
	const uint16 n = scaleRawFrame(vm, fs, fo, sx, sy, bs, bo, size);
	vm.df = 0;
	r.ax = n;
	r.dx = n ? 0xffff : 0;
}

// ---------------------------------------------------------------------------- Turbo C library

/** 303D:0006: set the random seed, AX:DX = the old one */
static void rtlSeed(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	r.ax = vm.rw(cs, 2);
	r.dx = vm.rw(cs, 4);
	vm.ww(cs, 2, arg(vm, 0));
	vm.ww(cs, 4, arg(vm, 1));
}

/** 303D:0024: random number lo..hi (lo if lo >= hi) */
static void rtlRandom(DosVm &vm, uint16 cs, const GfxLayout *, PrimResult &r) {
	const uint16 lo = arg(vm, 0), hi = arg(vm, 1);
	if (lo >= hi) {
		r.ax = lo;
		return;
	}
	const uint16 lo16 = vm.rw(cs, 2), hi16 = vm.rw(cs, 4);
	const uint32 p = (uint32)lo16 * 0x660d;
	const uint16 nh = (uint16)((uint16)(hi16 * 0x660d) + (uint16)(lo16 * 0x19) + (p >> 16));
	const uint32 s = (((uint32)nh << 16) | (p & 0xffff)) + 0x36191149;
	vm.ww(cs, 2, s & 0xffff);
	vm.ww(cs, 4, s >> 16);
	const uint16 d = (uint16)(hi - lo + 1);
	const uint16 rem = (s >> 16) % d;
	r.ax = (uint16)(rem + lo);
	r.dx = rem;
}

/** A range of 65536 divides by zero in the original: that is left to the 8086 code */
static bool rtlRandomReady(DosVm &vm, uint16) {
	const uint16 lo = arg(vm, 0), hi = arg(vm, 1);
	return (uint16)(hi - lo + 1) != 0 || lo >= hi;
}

/** 306F:0000 memset */
static void rtlMemset(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const byte c = arg(vm, 2) & 0xff;
	fillBytes(vm, arg(vm, 1), arg(vm, 0), arg(vm, 3), c);
	vm.df = 0;
	r.ax = c * 0x101;
	r.dx = vm.r16[kDI];
}

/** 306F:001E memcpy (forward, word by word) */
static void rtlMemcpy(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	uint16 si = arg(vm, 0), di = arg(vm, 2);
	const uint16 ds = arg(vm, 1), es = arg(vm, 3), n = arg(vm, 4);
	for (uint i = n >> 1; i > 0; i--) {
		const byte b0 = vm.rb(ds, si), b1 = vm.rb(ds, (uint16)(si + 1));
		vm.wb(es, di, b0);
		vm.wb(es, (uint16)(di + 1), b1);
		si += 2;
		di += 2;
	}
	if (n & 1)
		vm.wb(es, di, vm.rb(ds, si));
	vm.df = 0;
	r.ax = vm.sreg[kDS];
	r.dx = vm.r16[kDI];
}

/** 306F:0042 memcmp as the original returns it (the position after the compared bytes) */
static void rtlMemcmp(DosVm &vm, uint16, const GfxLayout *, PrimResult &r) {
	const uint16 n = arg(vm, 4);
	uint16 si = arg(vm, 0), di = arg(vm, 2), cx = (uint16)-n;
	bool down = true, zf = cx == 0;
	if (cx & 0x8000) {
		down = false;
		cx = n;
		zf = n == 0;
	}
	const uint16 dx = cx;
	while (cx) {
		const byte x = vm.rb(arg(vm, 1), si), y = vm.rb(arg(vm, 3), di);
		si += down ? 0xffff : 1;
		di += down ? 0xffff : 1;
		cx--;
		zf = x == y;
		if (!zf)
			break;
	}
	cx -= zf ? 1 : 0;
	const uint16 d = (uint16)(dx - cx);
	vm.df = 0;
	r.ax = (uint16)(d - 1);
	r.dx = d;
}

// ---------------------------------------------------------------------------- profiles

const PrimAddress bifi2XP01Prims[] = {
	{ 0x1de4, 0x004e, fadeStep, nullptr, nullptr },
	{ 0x1de4, 0x0123, markRect, nullptr, nullptr },
	{ 0x1de4, 0x0158, remapCopy, nullptr, nullptr },
	{ 0x1de4, 0x01b7, mosaic, nullptr, nullptr },
	{ 0x1de4, 0x0289, copyLines, nullptr, nullptr },
	{ 0x1de4, 0x02ea, clearLines, nullptr, nullptr },
	{ 0x2e84, 0x0000, ribInPlace, nullptr, nullptr },
	{ 0x29c8, 0x1965, timerPoll, timerPollReady, nullptr },
	{ 0x2c3c, 0x1fa4, cellsClear, nullptr, nullptr },
	{ 0x2c3c, 0x1fc4, cellsAll, nullptr, nullptr },
	{ 0x2c3c, 0x2036, cellsMark, nullptr, nullptr },
	{ 0x2c3c, 0x2063, cellsOr, nullptr, nullptr },
	{ 0x2c3c, 0x2094, cellsMark3, nullptr, nullptr },
	{ 0x2c3c, 0x20c1, cellsTest, nullptr, nullptr },
	{ 0x2c3c, 0x21ea, cellsRuns, nullptr, nullptr },
	{ 0x2c3c, 0x226e, cellsCopy, nullptr, nullptr },
	{ 0x2c3c, 0x23c0, cellsTiles, nullptr, nullptr },
	{ 0x2ea0, 0x0e1e, frmWindowPtr, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x108c, frmFontPtr, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x0e24, frmWindow, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x0e5f, frmDraw, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x0f8f, frmText, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x0f74, frmChar, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x100d, frmTextWidth, nullptr, &bifi2XP01Layout },
	{ 0x2ea0, 0x1062, frmGlyphWidth, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x0008, blkGrab, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x0048, blkImage, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x0082, blkPart, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x00d2, blkFill, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x010a, blkClear, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x0130, blkRetrace, nullptr, nullptr },
	{ 0x2b72, 0x0140, blkHLine, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x0170, blkVLine, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x019a, blkGetTarget, nullptr, &bifi2XP01Layout },
	{ 0x2b72, 0x0224, blkSetTarget, nullptr, &bifi2XP01Layout },
	{ 0x10d7, 0x0008, palSet, nullptr, nullptr },
	{ 0x10d7, 0x002b, palScale, nullptr, nullptr },
	{ 0x10d7, 0x0147, palRelocList, nullptr, nullptr },
	{ 0x10d7, 0x01c9, palRelocN, nullptr, nullptr },
	{ 0x10d7, 0x0220, palHugeAdd, nullptr, nullptr },
	{ 0x10d7, 0x0243, palHugeCopy, nullptr, nullptr },
	{ 0x1e17, 0x002d, txtExpand, nullptr, nullptr },
	{ 0x1e17, 0x00b5, txtLineCR, nullptr, nullptr },
	{ 0x1e17, 0x0100, txtLine, nullptr, nullptr },
	{ 0x1e17, 0x014f, txtWord, nullptr, nullptr },
	{ 0x1e17, 0x01a0, txtWidth, nullptr, &bifi2XP01Layout },
	{ 0x1e17, 0x01fb, txtTidy, nullptr, nullptr },
	{ 0x1e17, 0x027b, txtLen, nullptr, nullptr },
	{ 0x12af, 0x0000, shdReloc, nullptr, nullptr },
	{ 0x12af, 0x0035, shdScale, nullptr, nullptr },
	{ 0x12af, 0x01a0, shdRow, nullptr, nullptr },
	{ 0x12af, 0x01ef, shdMap, nullptr, nullptr },
	{ 0x12af, 0x0217, shdMap3, nullptr, nullptr },
	{ 0x303d, 0x0006, rtlSeed, nullptr, nullptr },
	{ 0x303d, 0x0024, rtlRandom, rtlRandomReady, nullptr },
	{ 0x306f, 0x0000, rtlMemset, nullptr, nullptr },
	{ 0x306f, 0x001e, rtlMemcpy, nullptr, nullptr },
	{ 0x306f, 0x0042, rtlMemcmp, nullptr, nullptr },
	{ 0, 0, nullptr, nullptr, nullptr }
};

// the routines that are byte for byte the same as in Bi-Fi 2, at their addresses in this program
const PrimAddress schattenXP01Prims[] = {
	{ 0x2373, 0x015e, remapCopy, nullptr, nullptr },
	{ 0x2373, 0x01bd, mosaic, nullptr, nullptr },
	{ 0x2373, 0x028f, copyLines, nullptr, nullptr },
	{ 0x2373, 0x02f0, clearLines, nullptr, nullptr },
	{ 0x2950, 0x1965, timerPoll, timerPollReady, nullptr },
	{ 0x2afa, 0x00f0, blkRetrace, nullptr, nullptr },
	{ 0x2bbd, 0x1fa4, cellsClear, nullptr, nullptr },
	{ 0x2bbd, 0x1fc4, cellsAll, nullptr, nullptr },
	{ 0x2bbd, 0x2036, cellsMark, nullptr, nullptr },
	{ 0x2bbd, 0x2094, cellsMark3, nullptr, nullptr },
	{ 0x2bbd, 0x20c1, cellsTest, nullptr, nullptr },
	{ 0x2bbd, 0x21ea, cellsRuns, nullptr, nullptr },
	{ 0x2bbd, 0x226e, cellsCopy, nullptr, nullptr },
	{ 0x2bbd, 0x23c0, cellsTiles, nullptr, nullptr },
	{ 0x2e05, 0x0000, ribInPlace, nullptr, nullptr },
	{ 0x1563, 0x0008, palSet, nullptr, nullptr },
	{ 0x1563, 0x002b, palScale, nullptr, nullptr },
	{ 0x1563, 0x0147, palRelocList, nullptr, nullptr },
	{ 0x1563, 0x01c9, palRelocN, nullptr, nullptr },
	{ 0x1563, 0x0220, palHugeAdd, nullptr, nullptr },
	{ 0x1563, 0x0243, palHugeCopy, nullptr, nullptr },
	{ 0x23a7, 0x0023, txtExpand, nullptr, nullptr },
	{ 0x23a7, 0x0145, txtWord, nullptr, nullptr },
	{ 0x23a7, 0x01f1, txtTidy, nullptr, nullptr },
	{ 0x1724, 0x0000, shdReloc, nullptr, nullptr },
	{ 0x1724, 0x0035, shdScale, nullptr, nullptr },
	{ 0x1724, 0x01e1, shdTable, nullptr, nullptr },
	{ 0x1724, 0x0221, shdRect, nullptr, nullptr },
	{ 0x2601, 0x109e, frmWindowPtr, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x14e4, frmFontPtr, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x10a4, frmWindow, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x10df, frmDraw, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x13e7, frmText, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x13cc, frmChar, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x1465, frmTextWidth, nullptr, &schattenXP01Layout },
	{ 0x2601, 0x14ba, frmGlyphWidth, nullptr, &schattenXP01Layout },
	{ 0x2750, 0x0430, scaleFrameAt, nullptr, nullptr },
	{ 0x2afa, 0x0008, blkImage, nullptr, &schattenXP01Layout },
	{ 0x2afa, 0x0042, blkPart, nullptr, &schattenXP01Layout },
	{ 0x2afa, 0x0092, blkFill, nullptr, &schattenXP01Layout },
	{ 0x2afa, 0x00ca, blkClear, nullptr, &schattenXP01Layout },
	{ 0x2afa, 0x0100, blkHLine, nullptr, &schattenXP01Layout },
	{ 0x2afa, 0x0130, blkGetTarget, nullptr, &schattenXP01Layout },
	{ 0x2afa, 0x01ba, blkSetTarget, nullptr, &schattenXP01Layout },
	{ 0x2ee7, 0x0004, rtlMemset, nullptr, nullptr },
	{ 0x2ee7, 0x0022, rtlMemcpy, nullptr, nullptr },
	{ 0x2ee7, 0x0046, rtlMemcmp, nullptr, nullptr },
	{ 0, 0, nullptr, nullptr, nullptr }
};

// Telekommando 2: the routines that are byte for byte the same as in Bi-Fi 2
const PrimAddress tk2XP01Prims[] = {
	{ 0x1a1f, 0x0046, fadeStep, nullptr, nullptr },
	{ 0x1a1f, 0x013f, remapCopy, nullptr, nullptr },
	{ 0x1a1f, 0x019e, mosaic, nullptr, nullptr },
	{ 0x26a0, 0x0000, ribInPlace, nullptr, nullptr },
	{ 0x2458, 0x1fa4, cellsClear, nullptr, nullptr },
	{ 0x2458, 0x1fc4, cellsAll, nullptr, nullptr },
	{ 0x2458, 0x2036, cellsMark, nullptr, nullptr },
	{ 0x2458, 0x2063, cellsOr, nullptr, nullptr },
	{ 0x2458, 0x2094, cellsMark3, nullptr, nullptr },
	{ 0x2458, 0x20c1, cellsTest, nullptr, nullptr },
	{ 0x2458, 0x21ea, cellsRuns, nullptr, nullptr },
	{ 0x2458, 0x226e, cellsCopy, nullptr, nullptr },
	{ 0x2458, 0x23c0, cellsTiles, nullptr, nullptr },
	{ 0x26bc, 0x0e1e, frmWindowPtr, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x108c, frmFontPtr, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x0e24, frmWindow, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x0e5f, frmDraw, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x0f8f, frmText, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x0f74, frmChar, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x100d, frmTextWidth, nullptr, &tk2XP01Layout },
	{ 0x26bc, 0x1062, frmGlyphWidth, nullptr, &tk2XP01Layout },
	{ 0x2383, 0x019a, blkRetrace, nullptr, nullptr },
	{ 0x1085, 0x0008, palSet, nullptr, nullptr },
	{ 0x1085, 0x002b, palScale, nullptr, nullptr },
	{ 0x1085, 0x0147, palRelocList, nullptr, nullptr },
	{ 0x1085, 0x01c9, palRelocN, nullptr, nullptr },
	{ 0x1085, 0x0220, palHugeAdd, nullptr, nullptr },
	{ 0x1085, 0x0243, palHugeCopy, nullptr, nullptr },
	{ 0x2872, 0x0004, rtlMemset, nullptr, nullptr },
	{ 0x2872, 0x0022, rtlMemcpy, nullptr, nullptr },
	{ 0, 0, nullptr, nullptr, nullptr }
};

} // End of namespace Tad
