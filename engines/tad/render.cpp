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

#include "common/array.h"

#include "tad/render.h"
#include "tad/runner.h"

namespace Tad {

namespace {

inline int s16(int v) {
	return (int16)(uint16)v;
}

inline int s8(int v) {
	return (int8)(byte)v;
}

/** Linear address of seg:off (the offset is not wrapped, as the drawing code computes it) */
inline int64 lin(uint16 seg, int64 off) {
	return ((int64)seg << 4) + off;
}

inline bool inMemory(int64 a) {
	return a >= 0 && a < kMemorySize;
}

inline byte readMem(const byte *m, int64 a) {
	return inMemory(a) ? m[a] : 0;
}

inline void writeMem(byte *m, int64 a, byte v) {
	if (inMemory(a))
		m[a] = v;
}

/** n bytes from src to dst as if through a buffer (overlapping areas allowed) */
void moveMem(byte *m, int64 dst, int64 src, int64 n) {
	if (n <= 0)
		return;
	if (inMemory(dst) && inMemory(src) && inMemory(dst + n - 1) && inMemory(src + n - 1)) {
		memmove(m + dst, m + src, n);
		return;
	}
	Common::Array<byte> tmp((uint)n);
	for (int64 i = 0; i < n; i++)
		tmp[(uint)i] = readMem(m, src + i);
	for (int64 i = 0; i < n; i++)
		writeMem(m, dst + i, tmp[(uint)i]);
}

void fillMem(byte *m, int64 dst, int64 n, byte v) {
	for (int64 i = 0; i < n; i++)
		writeMem(m, dst + i, v);
}

/** Floor division as the JavaScript reference computes it */
inline int64 floorDiv(int64 a, int64 b) {
	int64 q = a / b;
	if ((a % b) != 0 && ((a < 0) != (b < 0)))
		q--;
	return q;
}

} // End of anonymous namespace

// ---------------------------------------------------------------------------- Gfx

uint32 Gfx::blkTarget(uint16 &width) const {
	const uint16 b = _lay.blk, t = _lay.blkTarget;
	width = _vm.rw(b, (uint16)(t + 4));
	return (uint32)lin(_vm.rw(b, (uint16)(t + 2)), _vm.rw(b, t));
}

void Gfx::setTarget(uint16 seg, uint16 off) {
	const uint16 b = _lay.blk, t = _lay.blkTarget;
	if (!seg && !off) {
		_vm.ww(b, (uint16)(t + 4), 0x140);
		_vm.ww(b, (uint16)(t + 6), 0xc8);
		_vm.ww(b, t, 0);
		_vm.ww(b, (uint16)(t + 2), 0xa000);
		return;
	}
	_vm.ww(b, (uint16)(t + 4), _vm.rw(seg, off));
	_vm.ww(b, (uint16)(t + 6), _vm.rw(seg, (uint16)(off + 2)));
	_vm.ww(b, t, _vm.rw(seg, (uint16)(off + 4)));
	_vm.ww(b, (uint16)(t + 2), _vm.rw(seg, (uint16)(off + 6)));
}

void Gfx::image(int x, int y, uint16 seg, uint16 off) {
	byte *m = _vm.mem();
	uint16 tw;
	const int64 t = blkTarget(tw);
	const int w = _vm.rw(seg, off), h = _vm.rw(seg, (uint16)(off + 2));
	int64 src = lin(seg, off + 4), dst = t + (int64)y * tw + x;
	for (int r = 0; r < h; r++, src += w, dst += tw)
		moveMem(m, dst, src, w);
}

void Gfx::imagePart(int x, int y, uint16 seg, uint16 off, int sx, int sy, int w, int h) {
	byte *m = _vm.mem();
	uint16 tw;
	const int64 t = blkTarget(tw);
	const int iw = _vm.rw(seg, off);
	int64 src = lin(seg, off + 4) + (int64)sy * iw + sx, dst = t + (int64)y * tw + x;
	for (int r = 0; r < h; r++, src += iw, dst += tw)
		moveMem(m, dst, src, w);
}

void Gfx::fill(int x, int y, int w, int h, byte c) {
	byte *m = _vm.mem();
	uint16 tw;
	int64 dst = blkTarget(tw) + (int64)y * tw + x;
	for (int r = 0; r < h; r++, dst += tw)
		fillMem(m, dst, w, c);
}

void Gfx::setHigh(int x, int y, int w, int h, uint16 seg, uint16 off) {
	byte *m = _vm.mem();
	const int bw = _vm.rw(seg, off);
	int64 dst = lin(_vm.rw(seg, (uint16)(off + 6)), _vm.rw(seg, (uint16)(off + 4))) + (int64)y * bw + x;
	for (int r = 0; r < h; r++, dst += bw)
		for (int i = 0; i < w; i++)
			writeMem(m, dst + i, readMem(m, dst + i) | 0x80);
}

/** 2EA0:00F0: as the original, only one side is checked per axis; results stay in memory */
bool Gfx::clip(int x, int y, int w, int h, Clip &c) {
	const uint16 s = _lay.frm;
	// the variables relative to the window: x0 at +0 (Bi-Fi 2: 00D4)
	auto at = [&](int bifiOffset) { return (uint16)(_lay.frmWindow + bifiOffset - 0xd4); };
	const int x0 = s16(_vm.rw(s, at(0xd4))), y0 = s16(_vm.rw(s, at(0xd6)));
	const int x1 = s16(_vm.rw(s, at(0xd8))), y1 = s16(_vm.rw(s, at(0xda)));
	int ce = 0;
	c.skipX = c.skipY = 0;
	_vm.ww(s, at(0xce), 0);
	_vm.ww(s, at(0xd0), w);
	_vm.ww(s, at(0xd2), h);
	if (x < x0) {
		c.skipX = x0 - x;
		c.cw = w - c.skipX;
		_vm.ww(s, at(0xc2), x0);
		if (c.cw <= 0)
			return false;
		_vm.ww(s, at(0xc6), c.skipX);
		_vm.ww(s, at(0xca), c.cw);
		ce = 8;
		c.cx = x0;
	} else {
		c.cx = x;
		_vm.ww(s, at(0xc2), x);
		const int bp = x1 - x - w;
		if (bp < 0) {
			c.cw = w + bp;
			if (c.cw <= 0)
				return false;
			ce = 0x10;
		} else {
			c.cw = w;
		}
		_vm.ww(s, at(0xca), c.cw);
	}
	_vm.ww(s, at(0xce), ce);
	if (y < y0) {
		c.skipY = y0 - y;
		c.ch = h - c.skipY;
		_vm.ww(s, at(0xc4), y0);
		if (c.ch <= 0)
			return false;
		_vm.ww(s, at(0xc8), c.skipY);
		_vm.ww(s, at(0xcc), c.ch);
		ce += 2;
		c.cy = y0;
	} else {
		c.cy = y;
		_vm.ww(s, at(0xc4), y);
		const int bp = y1 - y - h;
		if (bp < 0) {
			c.ch = h + bp;
			if (c.ch <= 0)
				return false;
			ce += 4;
		} else {
			c.ch = h;
		}
		_vm.ww(s, at(0xcc), c.ch);
	}
	_vm.ww(s, at(0xce), ce);
	return true;
}

bool Gfx::frame(int x, int y, uint16 seg, uint16 off) {
	byte *m = _vm.mem();
	const int64 a = lin(seg, off);
	if (!inMemory(a))
		return false;
	FramePtr f = decodeFrame(m, kMemorySize, (uint32)a);
	if (!f)
		return false;
	Clip c;
	if (!clip(x, y, f->w, f->h, c))
		return false;
	const uint16 win = _lay.frmWindow;
	const int tw = _vm.rw(_lay.frm, (uint16)(win + 8));
	const int64 t = lin(_vm.rw(_lay.frm, (uint16)(win + 0xe)), _vm.rw(_lay.frm, (uint16)(win + 0xc)));
	for (int r = 0; r < c.ch; r++) {
		const int fr = (c.skipY + r) * f->w + c.skipX;
		const int64 d = t + (int64)(c.cy + r) * tw + c.cx;
		for (int i = 0; i < c.cw; i++) {
			const uint k = fr + i;
			if (k < f->mask.size() && f->mask[k])
				writeMem(m, d + i, f->pixels[k]);
		}
	}
	return true;
}

/** 2EA0:1176: add d to the colours of a frame in place (raw: all bytes, run-length: literals) */
void Gfx::recolor(uint16 seg, uint16 off, byte d) {
	byte *m = _vm.mem();
	const int64 a = lin(seg, off);
	const int w = readMem(m, a) | (readMem(m, a + 1) << 8), h = readMem(m, a + 2) | (readMem(m, a + 3) << 8);
	int64 q = a + 8;
	auto add = [&](int n) {
		for (int i = 0; i < n; i++, q++)
			writeMem(m, q, readMem(m, q) + d);
	};
	if (readMem(m, a + 4) == 0) {
		add(w * h);
		return;
	}
	for (;;) {
		const byte op = readMem(m, q + 1);
		q += 2;
		if (op == 0) {
			return;
		} else if (op == 2) {
			continue;
		} else if (op == 4) {
			add(w);
		} else if (op == 6 || op == 8) {
			q++;
			add(readMem(m, q++));
		} else if (op == 0x0a) {
			q++;
			add(readMem(m, q++));
			add(readMem(m, q++));
		} else if (op == 0x0c) {
			q += 2;
			add(readMem(m, q++));
		} else if (op == 0x0e) {
			q++;
			int k = readMem(m, q++);
			while (k--) {
				q++;
				add(readMem(m, q++));
			}
		} else {
			return;
		}
	}
}

uint16 Gfx::glyphWidth(uint ch) {
	const uint16 fs = _vm.rw(_lay.frm, (uint16)(_lay.frmFont + 4)), fo = _vm.rw(_lay.frm, (uint16)(_lay.frmFont + 2));
	const uint first = _vm.rw(fs, (uint16)(fo + 2)), n = _vm.rw(fs, (uint16)(fo + 4));
	if (ch < first || ch - first >= n)
		return 0;
	return _vm.rw(fs, (uint16)(fo + 0xc + (ch - first) * 8));
}

int Gfx::text(int x, int y, const byte *codes, uint n) {
	const uint16 fs = _vm.rw(_lay.frm, (uint16)(_lay.frmFont + 4)), fo = _vm.rw(_lay.frm, (uint16)(_lay.frmFont + 2));
	const byte first = _vm.rb(fs, (uint16)(fo + 2)), count = _vm.rb(fs, (uint16)(fo + 4));
	for (uint i = 0; i < n; i++) {
		const byte ch = codes[i];
		if (!ch)
			break;
		const byte k = ch - first;
		if (ch < first || k >= count)
			continue;
		const uint16 e = (uint16)(fo + 10 + k * 8);
		if (!_vm.rb(fs, e))
			continue;
		const byte cur = _vm.rb(_lay.frm, _lay.frmFont), old = _vm.rb(fs, (uint16)(e + 1));
		_vm.wb(fs, (uint16)(e + 1), cur);
		const uint16 go = (uint16)(_vm.rw(fs, (uint16)(e + 6)) + fo + 10);
		if (old != cur)
			recolor(fs, go, cur - old);
		const int adv = s8(_vm.rb(_lay.frm, (uint16)(_lay.frmFont + 1))) + _vm.rw(fs, go);
		frame(x, y, fs, go);
		x += adv;
	}
	return x;
}

// ---------------------------------------------------------------------------- DirtyMap

enum {
	kCellCols = 80,
	kCellRows = 50
};

byte &DirtyMap::cell(uint32 o) {
	return _vm.mem()[(((uint32)_seg << 4) + o) & 0xfffff];
}

bool DirtyMap::cells(int x, int y, int w, int h, Cells &c) const {
	int ax = s16(x + w), dx = s16(y + h);
	if (ax < 0 || dx < 0)
		return false;
	int si = s16(x) >> 2;
	if (si < 0)
		si = 0;
	int di = s16(y) >> 2;
	if (di < 0)
		di = 0;
	if ((ax & 0xffff) > 0x140)
		ax = 0x140;
	ax -= 1;
	if (ax < 0)
		return false;
	ax = (ax >> 2) + 1 - si;
	if (ax <= 0)
		return false;
	if ((dx & 0xffff) > 0xc8)
		dx = 0xc8;
	dx -= 1;
	if (dx < 0)
		return false;
	dx = (dx >> 2) + 1 - di;
	if (dx <= 0)
		return false;
	c.at = di * kCellCols + si;
	c.w = ax;
	c.h = dx;
	return true;
}

void DirtyMap::mark(int x, int y, int w, int h) {
	Cells c;
	if (!cells(x, y, w, h, c))
		return;
	for (int r = 0; r < c.h; r++)
		for (int i = 0; i < c.w; i++)
			cell(c.at + r * kCellCols + i) = 1;
}

int DirtyMap::test(int x, int y, int w, int h) {
	Cells c;
	if (!cells(x, y, w, h, c))
		return 0;
	for (int r = 0; r < c.h; r++)
		for (int i = 0; i < c.w; i++)
			if (cell(c.at + r * kCellCols + i))
				return c.h - r;
	return 0;
}

int DirtyMap::tiles(uint16 bufSeg, uint16 bufOff, uint16 tw, uint16 th, uint16 cols, uint16 rows) {
	byte *m = _vm.mem();
	const int64 a = (int64)bufSeg << 4;
	auto word = [&](uint32 d) { return cell(d & 0xffff) | (cell((d + 1) & 0xffff) << 8); };
	byte res = 0;
	auto setRes = [&](byte v) {
		res = v;
		cell(0x23bf) = v;
	};
	(void)tw;
	setRes(0);
	fillMem(m, a + bufOff, (uint16)(cols * rows), 0);
	uint32 di = 0;
	uint16 si = bufOff;
	enum Entry { kL23fd, kL2400, kL2437, kL240c };
	auto row = [&]() {
		byte dl = th & 0xff;
		uint cx = 0, bx = 0, dh = 0;
		Entry entry = kL23fd;
		for (;;) {
			if (entry == kL23fd) {
				cx = 0x28;
				entry = kL2400;
			}
			if (entry == kL2400) {
				bx = cx;
				// repe scasw with ax = 0
				int zf = -1;
				while (cx) {
					cx--;
					const uint w = word(di);
					di += 2;
					zf = w == 0;
					if (!zf)
						break;
				}
				if (zf == 0) {
					entry = kL2437;
					continue;
				}
				dl -= 4;
				if ((int8)dl > 0) {
					entry = kL23fd;
					continue;
				}
				return;
			}
			if (entry == kL2437) {
				dh = cx;
				setRes(bx & 0xff);
				for (;;) {
					while (cx) {
						cx--;
						const uint w = word(di);
						di += 2;
						if (w == 0)
							break;
					}
					if (cx == 0) {
						entry = kL240c;
						break;
					}
					if (word(di) != 0)
						continue;
					if (cx & 1) {
						di -= 2;
						cx++;
					}
					bx = ((0x27 - dh) & 0xff) >> 1;
					const uint span = (dh - cx) & 0xff;
					dh = cx;
					const uint n = (span >> 1) + 1;
					fillMem(m, a + ((bx + si) & 0xffff), n, 0xff);
					cx = dh;
					entry = kL2400;
					break;
				}
				continue;
			}
			// kL240c
			const int n = (int)(dh >> 1) + 1;
			const int b = 0x14 - n;
			fillMem(m, a + ((b + si) & 0xffff), n, 0xff);
			if (b == 0) {
				// skip the rest of the tile row
				dl -= 4;
				while ((int8)dl > 0) {
					di += 0x50;
					dl -= 4;
				}
				return;
			}
			dl -= 4;
			if ((int8)dl > 0) {
				entry = kL23fd;
				continue;
			}
			return;
		}
	};
	for (int r = rows; r > 0; r--) {
		row();
		si += cols;
	}
	return (int8)res;
}

void DirtyMap::buildRuns() {
	uint di = 0, bx = 0xfa0, si = 0;
	for (int dl = kCellRows; dl > 0; dl--) {
		uint cx = kCellCols;
		cell(bx) = 0;
		cell(bx + 1) = 0;
		si = bx + 1;
		for (;;) {
			// repe scasb: clean cells
			uint dh = cx;
			while (cx && cell(di) == 0) {
				cx--;
				di++;
			}
			if (cx == 0) {
				if (dh >= 0x40) {
					cell(bx)++;
					cell(si) = 0xfc;
					cell(si + 1) = 0;
					si += 2;
					dh -= 0x3f;
				}
				cell(si++) = (dh << 2) & 0xff;
				break;
			}
			dh = (dh - cx) & 0xff;
			if (dh >= 0x40) {
				cell(bx)++;
				cell(si) = 0xfc;
				cell(si + 1) = 0;
				si += 2;
				dh -= 0x3f;
			}
			cell(si++) = (dh << 2) & 0xff;
			// repne scasb: marked run
			dh = cx;
			while (cx && cell(di) != 0) {
				cx--;
				di++;
			}
			cell(bx)++;
			if (cx == 0) {
				cell(si++) = (dh << 1) & 0xff;
				cell(si++) = 0;
				break;
			}
			dh = (dh - cx) & 0xff;
			cell(si++) = (dh << 1) & 0xff;
		}
		bx = si;
	}
}

void DirtyMap::copyRuns(uint32 src, uint32 dst) {
	byte *m = _vm.mem();
	uint bp = 0xfa0, bx = bp;
	int64 si = src, di = dst;
	for (int dl = kCellRows; dl > 0; dl--) {
		for (int line = 4; line > 0; line--) {
			bx = bp;
			int n = cell(bx);
			if (!n)
				return;
			bx++;
			uint c = cell(bx++);
			si += c;
			di += c;
			while (n--) {
				c = cell(bx);
				moveMem(m, di, si, 2 * c);
				si += 2 * c;
				di += 2 * c;
				bx++;
				c = cell(bx++);
				si += c;
				di += c;
			}
		}
		bp = bx;
	}
}

// ---------------------------------------------------------------------------- scaler
// After XP01.EXE of Bi-Fi 2 segment 27C7 (0470 sets up, 0038 picks the column and row
// stepping); the original works with 16-bit fixed point and carries, and this choice of source
// pixels is the same.

static void scaleColumns(int srcW, int dstW, Common::Array<int> &idx) {
	idx.resize(dstW);
	if (srcW == dstW) {
		for (int i = 0; i < dstW; i++)
			idx[i] = i;
		return;
	}
	if (srcW < dstW) {
		const uint32 r = (uint32)(((int64)srcW * 65536 / dstW) & 0xffff);
		uint32 acc = r >> 1;
		int i = 0;
		for (int k = 0; k < dstW; k++) {
			idx[k] = i;
			acc += r;
			if (acc > 0xffff) {
				acc &= 0xffff;
				i++;
			}
		}
		return;
	}
	if (srcW < 2 * dstW) {
		const uint32 r = (uint32)((((int64)srcW * 65536 / (2 * dstW)) << 1) & 0xffff);
		uint32 acc = 0x8000 | (r >> 1);
		int i = 0;
		for (int k = 0; k < dstW; k++) {
			idx[k] = i;
			i++;
			acc += r;
			if (acc > 0xffff) {
				acc &= 0xffff;
				i++;
			}
		}
		return;
	}
	// step >= 2 in 8.8 fixed point; the fraction accumulator starts with half the integer part
	const uint32 q = (int64)srcW * 256 >= (int64)dstW * 65536 ? 0xffff : (uint32)((int64)srcW * 256 / dstW);
	const int whole = q >> 8, frac = q & 0xff;
	idx[0] = 0;
	int i = whole >> 1;
	int acc = whole >> 1;
	for (int k = 1; k < dstW; k++) {
		idx[k] = i;
		acc += frac;
		i += whole + (acc > 0xff ? 1 : 0);
		acc &= 0xff;
	}
}

static void scaleRows(int srcH, int dstH, Common::Array<int> &out) {
	out.resize(dstH);
	for (int k = 0; k < dstH; k++)
		out[k] = 0;
	int n = 0;
	auto emit = [&](int v) {
		out[n++] = v;
		return n >= dstH;
	};
	if (srcH == dstH) {
		for (int k = 0; k < dstH; k++)
			out[k] = k;
		return;
	}
	if (srcH < dstH) {
		// grow: rows are repeated
		const uint32 e = (uint32)(((int64)srcH * 65536 / dstH) & 0xffff);
		uint32 acc = e >> 1;
		int s = 0, r = 0, cnt = dstH;
		bool c = false;
		auto add = [&]() {
			acc += e;
			c = acc > 0xffff;
			acc &= 0xffff;
		};
		auto put = [&](int at, int v) {
			if (at >= 0 && at < dstH)
				out[at] = v;
		};
		auto get = [&](int at) { return at >= 0 && at < dstH ? out[at] : 0; };
		put(r, s);
		s++;
		add();
		bool dup = !c;
		if (!dup) {
			r++;
			if (--cnt == 0)
				return;
		}
		for (;;) {
			if (dup) {
				if (--cnt == 0)
					return;
				do {
					put(r + 1, get(r));
					r++;
					if (--cnt == 0)
						return;
					add();
				} while (!c);
				r++;
			}
			put(r, s);
			s++;
			add();
			dup = !c;
			if (!dup) {
				r++;
				if (--cnt == 0)
					return;
			}
		}
	}
	if (srcH < 2 * dstH) {
		const uint32 ax = (uint32)(((int64)srcH * 65536 / (2 * dstH)) & 0xffff);
		uint32 acc = ax;
		const uint32 e = (ax << 1) & 0xffff;
		int s = 0;
		for (;;) {
			if (emit(s))
				return;
			s++;
			acc += e;
			if (acc > 0xffff) {
				acc &= 0xffff;
				s++;
			}
		}
	}
	const uint32 q = (int64)srcH * 256 >= (int64)dstH * 65536 ? 0xffff : (uint32)((int64)srcH * 256 / dstH);
	uint32 acc = 0;
	int s = 0;
	if (emit(0))
		return;
	for (;;) {
		const uint32 t = acc + q;
		acc = t & 0xff;
		s += t >> 8;
		if (emit(s))
			return;
	}
}

/** Scaled (and with a negative sx mirrored) copy of a raw frame; nullptr where the original fails */
static FramePtr scaleFrame(const Frame &f, int sx, int sy, int limit) {
	const int64 dw = ((int64)ABS(sx) * f.w + 0x7f) >> 8, dh = ((int64)ABS(sy) * f.h + 0x7f) >> 8;
	if (dw > 0xffff || dh > 0xffff || !dw || !dh || dw * dh + 8 > limit)
		return FramePtr();
	Common::Array<int> cols, rs;
	scaleColumns(f.w, (int)dw, cols);
	scaleRows(f.h, (int)dh, rs);
	FramePtr out(new Frame());
	out->w = (uint16)dw;
	out->h = (uint16)dh;
	out->pixels.resize((uint)(dw * dh));
	out->mask.resize((uint)(dw * dh));
	for (int y = 0; y < dh; y++) {
		const int ty = sy < 0 ? (int)dh - 1 - y : y;
		const int64 sr = (int64)rs[y] * f.w;
		for (int x = 0; x < dw; x++) {
			const int tx = sx < 0 ? (int)dw - 1 - x : x;
			const int64 k = sr + cols[x];
			const bool in = k >= 0 && k < (int64)f.pixels.size();
			out->pixels[ty * dw + tx] = in ? f.pixels[(uint)k] : 0;
			out->mask[ty * dw + tx] = in ? f.mask[(uint)k] : 0;
		}
	}
	return out;
}

uint16 scaleRawFrame(DosVm &vm, uint16 fs, uint16 fo, int sx, int sy, uint16 bs, uint16 bo, uint16 size) {
	byte *m = vm.mem();
	vm.ww(bs, bo, 0xffff);
	vm.ww(bs, (uint16)(bo + 2), 0xffff);
	Frame src;
	src.w = vm.rw(fs, fo);
	src.h = vm.rw(fs, (uint16)(fo + 2));
	const int64 dw = ((int64)ABS(sx) * src.w + 0x7f) >> 8;
	if (dw > 0xffff)
		return 0;
	vm.ww(bs, bo, (uint16)dw);
	const int64 dh = ((int64)ABS(sy) * src.h + 0x7f) >> 8;
	if (dh > 0xffff)
		return 0;
	vm.ww(bs, (uint16)(bo + 2), (uint16)dh);
	if (!dw || !dh || dw * dh + 8 > size)
		return 0;
	// the original reads the pixels as they are (raw frames only)
	const uint32 n = (uint32)src.w * src.h;
	src.pixels.resize(n);
	src.mask.resize(n);
	const int64 p = lin(fs, (int64)fo + 8);
	for (uint32 i = 0; i < n; i++) {
		src.pixels[i] = readMem(m, p + i);
		src.mask[i] = 1;
	}
	FramePtr f = scaleFrame(src, sx, sy, size);
	if (!f)
		return 0;
	vm.ww(bs, (uint16)(bo + 4), vm.rw(fs, (uint16)(fo + 4)));
	vm.ww(bs, (uint16)(bo + 6), vm.rw(fs, (uint16)(fo + 6)));
	const int64 d = lin(bs, (int64)bo + 8);
	for (uint i = 0; i < f->pixels.size(); i++)
		writeMem(m, d + i, f->pixels[i]);
	return (uint16)(dw * dh + 8);
}

// ---------------------------------------------------------------------------- EngineGraphics

const GfxLayout bifi2XP01Layout = { 0x2b72, 0x02ee, 0x2ea0, 0x00d4, 0x0f6e };
const GfxLayout schattenXP01Layout = { 0x2afa, 0x0284, 0x2601, 0x011c, 0x13c6 };

const GraphicsDesc bifi2XP01Graphics = {
	0x350a,
	&bifi2XP01Layout, 0x2c3c, 0x330b, 0x2819,
	0x2cfc, 0x2d24, 0x2cf4, 0x2cf2, 0x2d1c, 0x165c, 0x2cee,
	0x16e3, 0x01e1,
	0x1751, 0x0067,
	0x180c, 0x15f2,
	0x1b7e, 0x000e
};

EngineGraphics::EngineGraphics(ProgramVm &vm, const GraphicsDesc &desc) : _vm(vm), _desc(desc),
	_gfx(vm, *desc.layout), _dirty(vm, desc.dirty) {
}

uint16 EngineGraphics::w(uint16 off) const {
	return _vm.rw(_desc.ds, off);
}

void EngineGraphics::setW(uint16 off, uint v) {
	_vm.ww(_desc.ds, off, v);
}

// 16E3:01E1
void EngineGraphics::renderRoom() {
	const uint16 ds = _desc.ds;
	const uint16 rs = _vm.rw(ds, (uint16)(_desc.room + 2)), ro = _vm.rw(ds, _desc.room);
	auto rw = [&](int o) { return _vm.rw(rs, (uint16)(ro + o)); };
	const uint16 ls = _vm.rw(rs, (uint16)(ro + 0x1a));
	uint16 lo = _vm.rw(rs, (uint16)(ro + 0x18));
	const uint16 cs = _vm.rw(ds, (uint16)(_desc.clip + 2)), co = _vm.rw(ds, _desc.clip);
	_vm.ww(cs, (uint16)(co + 6), rw(2));
	const uint16 bs = _vm.rw(ds, (uint16)(_desc.tileBuf + 2)), bo = _vm.rw(ds, _desc.tileBuf);
	for (int n = rw(0xc); n > 0; n--, lo += 0x14) {
		const int tw = rw(4), th = rw(6), cols = rw(8), rows = rw(0xa);
		if (_dirty.tiles(bs, bo, tw, th, cols, rows)) {
			const uint16 kind = _vm.rw(ls, lo);
			const uint16 ts = _vm.rw(ls, (uint16)(lo + 0xa)), to = _vm.rw(ls, (uint16)(lo + 8));
			const uint16 ms = _vm.rw(ls, (uint16)(lo + 6)), mo = _vm.rw(ls, (uint16)(lo + 4));
			if (kind == 1 || kind == 2) {
				const int64 d = lin(bs, bo);
				const byte *m = _vm.mem();
				int k = 0;
				for (int r = 0, y = 0; r < rows; r++, y += th) {
					for (int c = 0, x = 0; c < cols; c++, x += tw, k++) {
						const byte db = readMem(m, d + k);
						const uint16 t = kind == 1 ? (db & _vm.rb(ms, (uint16)(mo + k)))
							: (uint16)(s8(db) & _vm.rw(ms, (uint16)(mo + 2 * k)));
						if (!t)
							continue;
						const uint16 e = (uint16)(to + t * 4 - 4);
						_gfx.frame(s16(x), s16(y), _vm.rw(ts, (uint16)(e + 2)), _vm.rw(ts, e));
						_dirty.mark(x, y, tw, th);
					}
				}
			}
		}
		const uint16 cnt = _vm.rw(ls, (uint16)(lo + 2));
		const uint16 os = _vm.rw(ls, (uint16)(lo + 0xe)), oo = _vm.rw(ls, (uint16)(lo + 0xc));
		for (uint j = 0; j < cnt; j++)
			roomObject(os, (uint16)(oo + j * 0x20));
		uint16 as = _vm.rw(ls, (uint16)(lo + 0x12)), ao = _vm.rw(ls, (uint16)(lo + 0x10));
		while (as || ao) {
			roomObject(as, ao);
			const uint16 nao = _vm.rw(as, (uint16)(ao + 0x20)), nas = _vm.rw(as, (uint16)(ao + 0x22));
			as = nas;
			ao = nao;
		}
	}
}

/** Pictures through 16E3:0005, objects with an own drawing routine through the hook DS:2D1C */
void EngineGraphics::roomObject(uint16 s, uint16 o) {
	const byte flags = _vm.rb(s, (uint16)(o + 2));
	if ((flags & 3) != 1) {
		_vm.wb(s, (uint16)(o + 2), flags & 0x7f);
		return;
	}
	if (flags & 0x10) {
		const uint16 hs = w((uint16)(_desc.drawHook + 2)), ho = w(_desc.drawHook);
		if (hs || ho) {
			const uint16 args[3] = { o, s, 1 };
			_vm.callSync(hs, ho, args, 3);
			_vm.wb(s, (uint16)(o + 2), _vm.rb(s, (uint16)(o + 2)) & 0x7f);
		}
		return;
	}
	roomPicture(s, o);
}

// 16E3:0005
void EngineGraphics::roomPicture(uint16 s, uint16 o) {
	const uint16 ds = _desc.ds;
	const byte flags = _vm.rb(s, (uint16)(o + 2));
	int mode = flags & 0x40;
	int x = s16(_vm.rw(s, (uint16)(o + 4))), y = s16(_vm.rw(s, (uint16)(o + 6)));
	const uint16 lt = (uint16)(_desc.frameLists + 4 * _vm.rb(s, (uint16)(o + 8)));
	const uint16 ts = _vm.rw(ds, (uint16)(lt + 2)), to = _vm.rw(ds, lt);
	const uint16 e = (uint16)(to + 4 * _vm.rw(s, (uint16)(o + 0xa)));
	uint16 fs = _vm.rw(ts, (uint16)(e + 2)), fo = _vm.rw(ts, e);
	int fw = s16(_vm.rw(fs, fo)), fh = s16(_vm.rw(fs, (uint16)(fo + 2)));
	const uint16 sx = _vm.rw(s, (uint16)(o + 0xe)), sy = _vm.rw(s, (uint16)(o + 0x10));
	if (sx && sx != 0x100) {
		mode++;
		fw = s16((int)floorDiv((int64)fw * sx, 256));
		if (fw >= 0x100)
			fw = 0xff;
	}
	if (sy && sy != 0x100) {
		mode++;
		fh = s16((int)floorDiv((int64)fh * sy, 256));
		if (fh >= 0x100)
			fh = 0xff;
	}
	const byte anchor = _vm.rb(s, (uint16)(o + 3));
	if (anchor == 1) {
		x -= fw >> 1;
		y -= fh >> 1;
	} else if (anchor == 2) {
		x -= fw >> 1;
		y -= fh;
	}
	int vis;
	if (!(flags & 0x80)) {
		vis = _dirty.test(x, y, fw, fh);
	} else {
		_vm.wb(s, (uint16)(o + 2), flags & 0x7f);
		vis = 1;
	}
	if (!vis)
		return;
	_dirty.mark(x, y, fw, fh);
	// run-length frames (mode word 1) are never scaled or mirrored
	if (mode && (_vm.rw(fs, (uint16)(fo + 6)) != 0 || _vm.rw(fs, (uint16)(fo + 4)) != 1)) {
		const uint16 bs = w((uint16)(_desc.scaleBuf + 2)), bo = w(_desc.scaleBuf);
		if (!scaleInto(fs, fo, mode < 3 ? s16(sx) : -s16(sx), s16(sy), bs, bo, w(_desc.scaleBufSize)))
			return;
		fs = bs;
		fo = bo;
	}
	_gfx.frame(x, y, fs, fo);
}

/** 27C7:0470: scaled copy of a raw frame into the work buffer (header w, h, mode words) */
bool EngineGraphics::scaleInto(uint16 fs, uint16 fo, int sx, int sy, uint16 bs, uint16 bo, uint16 size) {
	byte *m = _vm.mem();
	_vm.ww(bs, bo, 0xffff);
	_vm.ww(bs, (uint16)(bo + 2), 0xffff);
	const int64 a = lin(fs, fo);
	if (!inMemory(a))
		return false;
	FramePtr src = decodeFrame(m, kMemorySize, (uint32)a);
	if (!src)
		return false;
	const int64 dw = ((int64)ABS(sx) * src->w + 0x7f) >> 8;
	if (dw > 0xffff)
		return false;
	_vm.ww(bs, bo, (uint16)dw);
	const int64 dh = ((int64)ABS(sy) * src->h + 0x7f) >> 8;
	if (dh > 0xffff)
		return false;
	_vm.ww(bs, (uint16)(bo + 2), (uint16)dh);
	FramePtr f = scaleFrame(*src, sx, sy, size);
	if (!f)
		return false;
	_vm.ww(bs, (uint16)(bo + 4), _vm.rw(fs, (uint16)(fo + 4)));
	_vm.ww(bs, (uint16)(bo + 6), _vm.rw(fs, (uint16)(fo + 6)));
	const int64 d = lin(bs, (int64)bo + 8);
	for (uint i = 0; i < f->pixels.size(); i++)
		writeMem(m, d + i, f->pixels[i]);
	return true;
}

// ---------------------------------------------------------------------------- panel (1751:0067)
// DS state: 057A what to redraw (0x100 whole panel, 0xFF all slots or bit 0x80 >> k for slot k,
// 0x200 status line), 057C mask, 057E..0588 greying of the panel (timed), 0586 panel off, 0578 x
// of the status line, 1662 number of items, 1663 first visible item, 1664 item list, 169C far
// pointer of the frame per item, 2CEA far pointer to the panel picture, 1D24 far pointer to the
// picture buffer structure. The status line text is at TXT:0132.

static int slotX(int k) {
	return (k & 3) * 0x1e + 0xb5;
}

static int slotY(int k) {
	return k < 4 ? 0x91 : 0xae;
}

void EngineGraphics::slotItem(int k, int i) {
	const uint t = _vm.rb(_desc.ds, (uint16)(0x1664 + i)) * 4;
	_gfx.frame(slotX(k), slotY(k), w((uint16)(0x169e + t)), w((uint16)(0x169c + t)));
}

/** The picture buffer structure describes the whole screen and becomes the drawing target */
void EngineGraphics::target(uint16 bottom, uint16 &ss, uint16 &so) {
	ss = w(0x1d26);
	so = w(0x1d24);
	_vm.ww(ss, so, 0x140);
	_vm.ww(ss, (uint16)(so + 2), 200);
	_vm.ww(ss, (uint16)(so + 6), ss);
	_vm.ww(ss, (uint16)(so + 4), so + 8);
	_gfx.setTarget(ss, so);
	_vm.ww(w(0x2cf0), (uint16)(w(0x2cee) + 6), bottom);
}

void EngineGraphics::drawPanel() {
	const uint16 ds = _desc.ds;
	if (w(0x586))
		return;
	if (w(0x588)) {
		setW(0x588, w(0x588) - 1);
		if (!w(0x588)) {
			if (w(0x584)) {
				setW(0x582, w(0x582) & ~w(0x584));
				setW(0x584, 0);
			}
			setW(0x57e, w(0x57e) & ~w(0x580));
			setW(0x580, 0);
			if (!w(0x582))
				setW(0x57c, 0xffff);
			setW(0x57a, 0xffff);
		}
	}
	if (!(w(0x57a) & w(0x57c)))
		return;
	uint16 ss, so;
	target(200, ss, so);
	const uint16 flags = w(0x57a);
	// DS:2CEA points to the far pointer of the panel picture
	const uint16 ps = _vm.rw(w(0x2cec), (uint16)(w(0x2cea) + 2)), po = _vm.rw(w(0x2cec), w(0x2cea));
	const int count = _vm.rb(ds, 0x1662);
	auto allItems = [&]() {
		int idx = _vm.rb(ds, 0x1663) + 7;
		for (int k = 7; k >= 0; k--, idx--)
			if (idx < count)
				slotItem(k, idx);
	};
	if (flags & 0x100) {
		_gfx.image(0, 0x8c, ps, po);
		allItems();
	} else if (flags & 0xff) {
		if ((flags & 0xff) == 0xff) {
			_gfx.imagePart(0xb5, 0x91, ps, po, 0xb5, 5, 0x72, 0x33);
			allItems();
		} else {
			int idx = _vm.rb(ds, 0x1663) + 7;
			for (int k = 7; k >= 0; k--, idx--) {
				if (!(flags & (0x80 >> k)))
					continue;
				if (idx < count)
					slotItem(k, idx);
				else
					_gfx.imagePart(slotX(k), slotY(k), ps, po, slotX(k), slotY(k) - 0x8c, 0x18, 0x16);
			}
		}
	}
	if (flags & 0x200) {
		_gfx.fill(0, 0x80, 0x140, 0xc, 0);
		if (_vm.rb(_desc.txt, 0x132)) {
			const uint16 fs = w(0x19e2), fo = w(0x19e0);
			const int space = s8(_vm.rb(fs, (uint16)(fo + 5)));
			int x = w(0x578);
			_vm.wb(fs, (uint16)(fo + 4), 0x74);
			for (uint16 i = 0;; i++) {
				const byte c = _vm.rb(_desc.txt, (uint16)(0x132 + i));
				if (c == 9) {
					_vm.wb(fs, (uint16)(fo + 4), 0x7a);
				} else if (c == 0xa) {
					_vm.wb(fs, (uint16)(fo + 4), 0x7c);
				} else if (c == 0xd) {
					_vm.wb(fs, (uint16)(fo + 4), 0x74);
				} else if (c) {
					const uint16 gw = _gfx.glyphWidth(c);
					if (gw) {
						_gfx.text(x, 0x80, &c, 1);
						x += space + gw;
					}
				}
				if (!c)
					break;
			}
		}
	}
	if (flags & 0x100) {
		_dirty.mark(0, 0x8c, 0x140, 0x3c);
	} else if ((flags & 0xff) == 0xff) {
		_dirty.mark(0xb5, 0x91, 0x72, 0x33);
	} else if (flags & 0xff) {
		for (int k = 7; k >= 0; k--)
			if (flags & (0x80 >> k))
				_dirty.mark(slotX(k), slotY(k), 0x18, 0x16);
	}
	if (flags & 0x200)
		_dirty.mark(0, 0x80, 0x140, 0xc);
	if (w(0x57e))
		_gfx.setHigh(0, 0x80, 0x140, 0x48, ss, so);
	setW(0x57a, 0);
}

// ---------------------------------------------------------------------------- texts (180C:15F2)
// Two text slots of 0x4A bytes at DS:1E1A. Slot: +00 kind (0 none, 5 answers, else text),
// +08 busy, +12 hidden, +14 redraw flags, +18 bit 0x8000 off, +2E text window: +02 x, +04 y,
// +06 w, +08 h, +0A far pointer to the colours, +0E far pointer to the text. Text codes: 1 n
// colour n of the window, 3 x skipped, 10 new line.

void EngineGraphics::setColor(byte c) {
	_vm.wb(w(0x19e2), (uint16)(w(0x19e0) + 4), c);
}

int EngineGraphics::spacing() {
	return s8(_vm.rb(w(0x19e2), (uint16)(w(0x19e0) + 5)));
}

uint16 EngineGraphics::drawTexts() {
	uint16 ret = 0;
	for (int i = 1; i >= 0; i--) {
		const uint16 b = (uint16)(0x1e1a + i * 0x4a);
		if (w((uint16)(b + 8)))
			continue;
		const uint16 kind = w(b);
		if (kind == 0) {
			ret = 0;
		} else if (kind == 5) {
			if (i == 0) {
				answers();
				ret = 2;
			}
		} else {
			textSlot(i);
			ret = kind == 2 ? 0xffff : 1;
		}
	}
	return ret;
}

// 180C:1482
void EngineGraphics::textSlot(int i) {
	const uint16 b = (uint16)(0x1e1a + i * 0x4a);
	auto W = [&](int o) { return w((uint16)(b + o)); };
	if (W(0x18) & 0x8000)
		return;
	if (W(0x12) == 0) {
		if ((W(0x14) & 1) || _dirty.test(W(0x30), W(0x32), W(0x34), W(0x36))) {
			uint16 ss, so;
			target(0x80, ss, so);
			textWindow(_desc.ds, (uint16)(b + 0x2e), ss, so);
		}
		if (W(0x14) & 1)
			_dirty.mark(W(0x30), W(0x32), W(0x34), W(0x36));
	}
	setW((uint16)(b + 0x14), 0);
}

// 1E17:01A0: characters of the next line and its width
void EngineGraphics::measure(uint16 s, uint16 o, uint16 limit, int space, int &count, uint16 &width) {
	int wsum = 0, last = 0;
	count = 0;
	limit++;
	for (;;) {
		limit--;
		if (!limit)
			break;
		byte c = _vm.rb(s, o++);
		bool stop = false;
		while (c <= 0x1f) {
			if (c != 1 && c != 3) {
				stop = true;
				break;
			}
			o++;
			count += 2;
			if (limit < 2) {
				count -= 2;
				stop = true;
				break;
			}
			limit -= 2;
			c = _vm.rb(s, o++);
		}
		if (stop)
			break;
		count++;
		const uint16 gw = _gfx.glyphWidth(c);
		if (gw) {
			wsum += gw + space;
			last = space;
		}
	}
	width = (uint16)(wsum - last);
}

// 1D22:01AF
void EngineGraphics::textWindow(uint16 s, uint16 o, uint16 bs, uint16 bo) {
	const uint16 ts = _vm.rw(s, (uint16)(o + 0x10));
	uint16 to = _vm.rw(s, (uint16)(o + 0xe));
	int x = _vm.rw(s, (uint16)(o + 2)), y = _vm.rw(s, (uint16)(o + 4)), wd = _vm.rw(s, (uint16)(o + 6));
	const int space = spacing();
	_gfx.setHigh(x, y, wd, _vm.rw(s, (uint16)(o + 8)), bs, bo);
	x += 4;
	y += 3;
	wd -= 8;
	const uint16 ps = _vm.rw(s, (uint16)(o + 0xc)), po = _vm.rw(s, (uint16)(o + 0xa));
	setColor(_vm.rb(ps, po));
	const uint16 dseg = w(0x19e2), doff = w(0x19e0);
	for (;;) {
		int cnt;
		uint16 width;
		measure(ts, to, 0xffff, space, cnt, width);
		uint16 cx = (uint16)(x + (s16(wd - width) >> 1));
		int si = cnt;
		for (;;) {
			const int ax = si;
			si--;
			if (s16(ax) <= 0)
				break;
			const byte c = _vm.rb(ts, to);
			if (c >= 0x20) {
				_gfx.text(s16(cx), s16(y), &c, 1);
				const uint16 gw = _gfx.glyphWidth(c);
				if (gw)
					cx = (uint16)(cx + space + gw);
			} else if (c == 1) {
				si--;
				to++;
				setColor(_vm.rb(ps, (uint16)(po + _vm.rb(ts, to))));
			} else if (c == 3) {
				si--;
				to++;
			} else {
				to = (uint16)(to + si);
				break;
			}
			to++;
		}
		y = (y + _vm.rw(dseg, (uint16)(doff + 2))) & 0xffff;
		const byte c = _vm.rb(ts, to);
		to++;
		if (c != 0xa)
			break;
	}
}

// 180C:154C
void EngineGraphics::answers() {
	const uint16 f = w(0x1e2e);
	if (f & 2) {
		panelGrey();
		uint16 ss, so;
		target(200, ss, so);
		answerLines(_desc.ds, 0x58a);
		_dirty.mark(0, 0x80, 0x140, 0x48);
	} else if (f & 4) {
		uint16 ss, so;
		target(200, ss, so);
		answerLines(_desc.ds, 0x58a);
		// 180C:004D: mark the rows up to the last used entry of DS:003C
		int k = 5, n;
		do {
			n = k;
			k--;
			if (n == 0)
				break;
		} while (w((uint16)(0x3c + 2 * k)) == 0);
		if (k >= 0)
			_dirty.mark(w(0x58e), w(0x590), w(0x592), (uint16)(w(0x594) * n));
	}
	setW(0x1e2e, 0);
}

// 1751:04A2: panel picture with items, empty status line, all greyed
void EngineGraphics::panelGrey() {
	const uint16 ds = _desc.ds;
	uint16 ss, so;
	target(200, ss, so);
	const uint16 ps = _vm.rw(w(0x2cec), (uint16)(w(0x2cea) + 2)), po = _vm.rw(w(0x2cec), w(0x2cea));
	_gfx.image(0, 0x8c, ps, po);
	const int count = _vm.rb(ds, 0x1662);
	int idx = _vm.rb(ds, 0x1663) + 7;
	for (int k = 7; k >= 0; k--, idx--) {
		if (idx >= count)
			continue;
		slotItem(k, idx);
	}
	_gfx.fill(0, 0x80, 0x140, 0xc, 0);
	_gfx.setHigh(0, 0x80, 0x140, 0x48, ss, so);
	setW(0x57a, 0xffff);
}

// 1D22:0345: answer lines; the selected one (+02) in the second colour set
void EngineGraphics::answerLines(uint16 s, uint16 o) {
	int n = s16(_vm.rw(s, o));
	const uint16 hot = _vm.rw(s, (uint16)(o + 2)), x0 = _vm.rw(s, (uint16)(o + 4)), dy = _vm.rw(s, (uint16)(o + 0xa));
	uint16 y = _vm.rw(s, (uint16)(o + 6));
	const uint16 is = _vm.rw(s, (uint16)(o + 0xe));
	uint16 io = _vm.rw(s, (uint16)(o + 0xc));
	const uint16 pS = _vm.rw(s, (uint16)(o + 0x12)), pO = _vm.rw(s, (uint16)(o + 0x10));
	const uint16 ls = _vm.rw(s, (uint16)(o + 0x16));
	uint16 lo = _vm.rw(s, (uint16)(o + 0x14));
	const int space = spacing();
	uint16 cur = 0;
	int pal = pO;
	while (n >= 1 && _vm.rw(is, io) != 0) {
		const uint16 ss = _vm.rw(ls, (uint16)(lo + 2));
		uint16 so = _vm.rw(ls, lo);
		lo += 4;
		uint16 x = x0;
		const uint16 id = _vm.rw(is, io);
		if (id != cur) {
			pal = id == hot ? pO + 3 : pO;
			setColor(_vm.rb(pS, (uint16)pal));
			cur = id;
		}
		for (;;) {
			const byte c = _vm.rb(ss, so);
			so++;
			if (c == 0)
				break;
			if (c < 0x20) {
				if (c != 1)
					break;
				setColor(_vm.rb(pS, (uint16)(pal + _vm.rb(ss, so))));
				so++;
			} else {
				_gfx.text(s16(x), s16(y), &c, 1);
				x = (uint16)(x + _gfx.glyphWidth(c) + space);
			}
		}
		n--;
		y += dy;
		io += 2;
	}
}

// ---------------------------------------------------------------------------- screen (1B7E:000E)
// The marked cells of the picture buffer go to the screen, with the mouse pointer drawn into the
// buffer for that moment. Pointer variables of the mouse library (DS): 03B6 flags (bit 0
// shown), 03CC far pointer to the pointer frame, 048E/0490 pointer x, y, 0492 its offset in the
// picture, 0458 index of the save buffer, 045A + n far pointers to the save buffers, 039A/039C
// width/height limit, 039E row length, 03A6 base.

// 2819:02B6: clip a pointer-format frame (byte w at +0, byte h at +2) at x, y
bool EngineGraphics::pointerClip(uint16 fs, uint16 fo, uint16 x, uint16 y, PointerClip &c) {
	int bl = _vm.rb(fs, fo), bh = _vm.rb(fs, (uint16)(fo + 2));
	uint16 si = fo;
	int dx = s16(y), cx = s16(x);
	if (dx < 0) {
		const int d = -dx;
		if (d >= _vm.rw(fs, (uint16)(fo + 2)))
			return false;
		bh = (bh - d) & 0xff;
		si = (uint16)(si + (uint16)(bl * (d & 0xff)));
		dx = 0;
	}
	si += 8;
	int ax = bh + dx - w(0x39c);
	if (ax >= 0) {
		ax &= 0xffff;
		if (ax >> 8)
			return false;
		const int nb = bh - (ax & 0xff);
		if (nb <= 0)
			return false;
		bh = nb;
	}
	int skip = 0;
	if (cx < 0) {
		const int d = -cx;
		if (d >> 8)
			return false;
		const int nb = bl - d;
		if (nb <= 0)
			return false;
		bl = nb;
		si = (uint16)(si + d);
		skip = d;
		cx = 0;
	}
	if (cx - w(0x39a) >= 0)
		return false;
	const int over = cx - w(0x39a) + bl;
	if (over >= 0) {
		bl -= over;
		skip += over;
	}
	const uint16 stride = w(0x39e);
	c.si = si;
	c.w = bl & 0xff;
	c.h = bh & 0xff;
	c.skip = skip;
	c.di = (uint16)(cx + (uint16)(stride * dx) + w(0x3a6));
	c.advance = (uint16)(stride - (bl & 0xff));
	return true;
}

// 2819:0409: frame at the pointer position into a picture; frames with a mode word draw colour
// 0 transparent
void EngineGraphics::drawAtPointer(uint16 fs, uint16 fo, uint16 ts, uint16 to) {
	byte *m = _vm.mem();
	PointerClip c;
	if (!pointerClip(fs, fo, w(0x48e), w(0x490), c))
		return;
	const bool transparent = _vm.rw(fs, (uint16)(fo + 4)) != 0;
	int64 s = lin(fs, c.si), d = lin(ts, (uint16)(c.di + to));
	for (int r = 0; r < c.h; r++) {
		for (int i = 0; i < c.w; i++) {
			const byte v = readMem(m, s + i);
			if (!transparent || v)
				writeMem(m, d + i, v);
		}
		s += c.w + c.skip;
		d += c.w + c.advance;
	}
}

// 2819:03CA: save the picture under the pointer (frame header w, h, 0, 0)
void EngineGraphics::savePointer(uint16 bs, uint16 bo, uint16 ps, uint16 po) {
	byte *m = _vm.mem();
	const uint16 cs = w(0x3ce), co = w(0x3cc);
	const uint16 pw = _vm.rw(cs, co), ph = _vm.rw(cs, (uint16)(co + 2));
	_vm.ww(bs, bo, pw);
	_vm.ww(bs, (uint16)(bo + 2), ph);
	_vm.ww(bs, (uint16)(bo + 4), 0);
	_vm.ww(bs, (uint16)(bo + 6), 0);
	int64 d = lin(bs, (int64)bo + 8), s = lin(ps, (uint16)(po + w(0x492)));
	const uint16 adv = (uint16)(w(0x39e) - pw);
	for (int r = 0; r < ph; r++) {
		moveMem(m, d, s, pw);
		d += pw;
		s += pw + adv;
	}
}

void EngineGraphics::present() {
	_dirty.buildRuns();
	const uint16 ss = w(0x1d26), so = w(0x1d24);
	const uint16 ps = _vm.rw(ss, (uint16)(so + 6)), po = _vm.rw(ss, (uint16)(so + 4));
	const uint32 src = (uint32)lin(ps, po);
	if (!(w(0x3b6) & 1)) {
		_dirty.copyRuns(src, 0xa0000);
		return;
	}
	// lock and unlock of the mouse library (its timer handler may move the pointer meanwhile;
	// the unlock draws such a pending move)
	_vm.callNested(_desc.mouseLib, 0x0b8a, nullptr, 0);
	const uint16 cs = w(0x3ce), co = w(0x3cc);
	if (_dirty.test(w(0x48e), w(0x490), _vm.rw(cs, co), _vm.rw(cs, (uint16)(co + 2)))) {
		const uint16 k = w(0x458);
		const uint16 bs = w((uint16)(0x45c + k)), bo = w((uint16)(0x45a + k));
		savePointer(bs, bo, ps, po);
		drawAtPointer(cs, co, ps, po);
		_dirty.copyRuns(src, 0xa0000);
		drawAtPointer(bs, bo, ps, po);
	} else {
		_dirty.copyRuns(src, 0xa0000);
	}
	_vm.callNested(_desc.mouseLib, 0x0b9a, nullptr, 0);
}

} // End of namespace Tad
