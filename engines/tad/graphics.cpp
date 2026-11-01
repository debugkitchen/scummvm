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

#include "common/textconsole.h"

#include "tad/graphics.h"

namespace Tad {

FramePtr decodeFrame(const Span &span, uint32 pos) {
	if (pos + 8 > span.size)
		return FramePtr();
	FramePtr f(new Frame());
	f->w = span.u16(pos);
	f->h = span.u16(pos + 2);
	const byte mode = span[pos + 4];
	const uint32 n = f->w * f->h;
	f->pixels.resize(n);
	f->mask.resize(n);
	for (uint32 i = 0; i < n; i++) {
		f->pixels[i] = 0;
		f->mask[i] = 0;
	}
	uint32 q = pos + 8;

	if (mode == 0 || mode == 2) {
		if (q + n > span.size)
			return FramePtr();
		for (uint32 i = 0; i < n; i++) {
			const byte c = span[q + i];
			f->pixels[i] = c;
			f->mask[i] = (mode == 2 && c == 0) ? 0 : 1;
		}
		return f;
	}

	for (uint row = 0; row < f->h; row++) {
		if (q + 2 > span.size)
			return FramePtr();
		const byte op = span[q + 1];
		q += 2;
		uint x = 0;
		// copies n pixels of the row from the data; pixels beyond the width are skipped
		auto copy = [&](uint count) {
			for (uint i = 0; i < count; i++) {
				if (x < f->w && q < span.size) {
					const uint32 k = row * f->w + x;
					f->pixels[k] = span[q];
					f->mask[k] = 1;
				}
				q++;
				x++;
			}
		};
		if (op == 0) {
			break;
		} else if (op == 2) {
			// empty row
		} else if (op == 4) {
			copy(f->w);
		} else if (op == 6) {
			const byte cnt = span[q + 1];
			q += 2;
			copy(cnt);
		} else if (op == 8) {
			const byte skip = span[q], cnt = span[q + 1];
			q += 2;
			x += skip;
			copy(cnt);
		} else if (op == 0x0a) {
			const byte skip = span[q], cnt = span[q + 1];
			q += 2;
			copy(cnt);
			x += skip;
			const byte cnt2 = span[q++];
			copy(cnt2);
		} else if (op == 0x0c) {
			const byte skip = span[q++];
			x += skip;
			const byte cnt = span[q + 1];
			q += 2;
			copy(cnt);
		} else if (op == 0x0e) {
			const byte skip = span[q], runs = span[q + 1];
			q += 2;
			x += skip;
			for (uint i = 0; i < runs; i++) {
				const byte gap = span[q], cnt = span[q + 1];
				q += 2;
				copy(cnt);
				x += gap;
			}
		} else {
			return FramePtr();
		}
	}
	return f;
}

PalettePart decodePalette(const Span &span, uint32 pos) {
	PalettePart p;
	if (pos + 2 > span.size)
		return p;
	p.start = span[pos];
	const uint n = span[pos + 1];
	for (uint i = 0; i < 3 * n && pos + 2 + i < span.size; i++)
		p.rgb.push_back(span[pos + 2 + i] * 255 / 63);
	return p;
}

Canvas::Canvas() {
	clear();
	memset(palette, 0, sizeof(palette));
}

void Canvas::clear(byte c) {
	memset(pixels, c, sizeof(pixels));
}

void Canvas::blit(const Frame &f, int x, int y, int clipBottom, bool mirror, int scale) {
	if (!f.w || !f.h)
		return;
	const int w = MAX(1, (int)((f.w * scale + 128) / 256));
	const int h = MAX(1, (int)((f.h * scale + 128) / 256));
	for (int yy = 0; yy < h; yy++) {
		const int dy = y + yy;
		if (dy < 0 || dy >= clipBottom || dy >= kScreenHeight)
			continue;
		const int sy = MIN<int>(f.h - 1, yy * 256 / scale);
		for (int xx = 0; xx < w; xx++) {
			const int dx = x + xx;
			if (dx < 0 || dx >= kScreenWidth)
				continue;
			int sx = MIN<int>(f.w - 1, xx * 256 / scale);
			if (mirror)
				sx = f.w - 1 - sx;
			const uint32 k = sy * f.w + sx;
			if (f.mask[k])
				pixels[dy * kScreenWidth + dx] = f.pixels[k];
		}
	}
}

void Canvas::applyPalette(const PalettePart &p) {
	for (uint i = 0; i < p.rgb.size() && p.start * 3 + i < sizeof(palette); i++)
		palette[p.start * 3 + i] = p.rgb[i];
}

} // End of namespace Tad
