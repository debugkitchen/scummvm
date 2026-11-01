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

#ifndef TAD_GRAPHICS_H
#define TAD_GRAPHICS_H

#include "common/array.h"
#include "common/ptr.h"

#include "tad/resource.h"

namespace Tad {

enum {
	kScreenWidth = 320,
	kScreenHeight = 200
};

/**
 * A decoded picture of the original engine: pixels and a mask of the pixels
 * that are drawn.
 */
struct Frame {
	uint16 w = 0;
	uint16 h = 0;
	Common::Array<byte> pixels;
	Common::Array<byte> mask;
};
typedef Common::SharedPtr<Frame> FramePtr;

/**
 * Decodes a frame: u16 width, u16 height, u8 mode, three unused bytes, data.
 * Modes 0 and 2 store the pixels unpacked (2: colour 0 is transparent), the
 * others store each row as a sequence of skip and copy runs.
 * Returns nullptr for data that is not a valid frame.
 */
FramePtr decodeFrame(const Span &span, uint32 pos);

/** A palette part: first colour, count, then 6-bit RGB triplets */
struct PalettePart {
	uint start = 0;
	Common::Array<byte> rgb;     ///< 8-bit RGB
};
PalettePart decodePalette(const Span &span, uint32 pos = 0);

/** A 320x200 picture of colour indices with the palette it is shown in */
class Canvas {
public:
	Canvas();
	void clear(byte c = 0);
	/**
	 * Draws the masked pixels of a frame at x, y (clipped to the screen and to
	 * clipBottom), mirrored and scaled by scale / 256 (nearest pixel)
	 */
	void blit(const Frame &f, int x, int y, int clipBottom = kScreenHeight, bool mirror = false, int scale = 256);
	void applyPalette(const PalettePart &p);

	byte pixels[kScreenWidth * kScreenHeight];
	byte palette[256 * 3];
};

} // End of namespace Tad

#endif // TAD_GRAPHICS_H
