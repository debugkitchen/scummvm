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

#ifndef TAD_RENDER_H
#define TAD_RENDER_H

#include "common/scummsys.h"

#include "tad/graphics.h"

namespace Tad {

class DosVm;
class ProgramVm;

/**
 * Drawing routines of the graphics library of the second engine generation, working on the
 * memory of the program: the picture buffer, the clipping window and the font are where the
 * original keeps them, so original and C++ code draw into the same picture.
 *
 * Library variables (offsets of Bi-Fi 2): blk:02EE far pointer to the target of the block
 * routines, blk:02F2 its width; frm:00E0 far pointer to the target of the frame routine,
 * frm:00DC its width, frm:00D4/D6/D8/DA clipping window x0, y0, x1, y1 (exclusive), frm:0F6E
 * text colour, frm:0F6F extra spacing, frm:0F70 far pointer to the font.
 */
/**
 * Where a version of the graphics library keeps its variables: the block routines (blk) and
 * the frame and font routines (frm). Dunkle Schatten uses the same code with other offsets.
 */
struct GfxLayout {
	uint16 blk;
	uint16 blkTarget;   ///< far pointer to the target, then its width and height (Bi-Fi 2: 02EE)
	uint16 frm;
	uint16 frmWindow;   ///< clipping window x0, y0, x1, y1, then target width, height, far pointer (00D4);
	                    ///< the results of the clipping are in the 18 bytes before it
	uint16 frmFont;     ///< text colour, extra spacing, far pointer to the font (0F6E)
};

extern const GfxLayout bifi2XP01Layout;
extern const GfxLayout schattenXP01Layout;

class Gfx {
public:
	Gfx(DosVm &vm, const GfxLayout &layout) : _vm(vm), _lay(layout) {}

	/** 2B72:0224: target of the block routines (null = the screen) */
	void setTarget(uint16 seg, uint16 off);
	/** 2B72:0048: picture (u16 w, u16 h, pixels) at x, y, unclipped */
	void image(int x, int y, uint16 seg, uint16 off);
	/** 2B72:0082: part (sx, sy, w, h) of such a picture at x, y */
	void imagePart(int x, int y, uint16 seg, uint16 off, int sx, int sy, int w, int h);
	/** 2B72:00D2 */
	void fill(int x, int y, int w, int h, byte c);
	/** 1DE4:0123: set bit 7 of the pixels of a rectangle of a picture buffer structure */
	void setHigh(int x, int y, int w, int h, uint16 seg, uint16 off);
	/** 2EA0:0E5F: frame at x, y in the clipping window; false if nothing is visible */
	bool frame(int x, int y, uint16 seg, uint16 off);
	/** 2EA0:1062: advance of a character, 0 if the font has no glyph */
	uint16 glyphWidth(uint ch);
	/** 2EA0:0F8F: characters (up to a 0) at x, y in the current colour; returns the end x */
	int text(int x, int y, const byte *codes, uint n);

private:
	struct Clip {
		int cx, cy, cw, ch, skipX, skipY;
	};
	bool clip(int x, int y, int w, int h, Clip &c);
	void recolor(uint16 seg, uint16 off, byte d);
	uint32 blkTarget(uint16 &width) const;

	DosVm &_vm;
	const GfxLayout &_lay;
};

/**
 * The changed areas of the screen (library segment 2C3C in Bi-Fi 2): cells of 4 x 4 pixels,
 * 80 x 50 bytes at the start of the segment, 0 = unchanged. Ported with the quirks of the
 * original scans, so the pictures match.
 */
class DirtyMap {
public:
	DirtyMap(DosVm &vm, uint16 seg) : _vm(vm), _seg(seg) {}

	/** 2C3C:2036 */
	void mark(int x, int y, int w, int h);
	/** 2C3C:20C1: rows of cells left from the first one with a mark, 0 if none */
	int test(int x, int y, int w, int h);
	/** 2C3C:23C0: tiles touching marked cells get 0xFF in the buffer; the result byte, signed */
	int tiles(uint16 bufSeg, uint16 bufOff, uint16 tw, uint16 th, uint16 cols, uint16 rows);
	/** 2C3C:21EA: run list of the marked cells at seg:0FA0 */
	void buildRuns();
	/** 2C3C:226E: copy the marked cells between two 320 x 200 pictures (linear addresses) */
	void copyRuns(uint32 src, uint32 dst);

private:
	struct Cells {
		int at, w, h;
	};
	bool cells(int x, int y, int w, int h, Cells &c) const;
	byte &cell(uint32 o);

	DosVm &_vm;
	uint16 _seg;
};

/**
 * 27C7:0470 (Bi-Fi 2) / 2750:0430 (Dunkle Schatten): scaled copy of a raw frame into a work
 * buffer of size bytes, mirrored for a negative scale; returns its size (w * h + 8), 0 where the
 * original fails. The header gets 0xFFFF, then the new width and height as far as they fit.
 */
uint16 scaleRawFrame(DosVm &vm, uint16 fs, uint16 fo, int sx, int sy, uint16 bs, uint16 bo, uint16 size);

/** Addresses of the drawing code of a program and of the state it works on */
struct GraphicsDesc {
	uint16 ds;
	const GfxLayout *layout;
	uint16 dirty, txt, mouseLib;              ///< library segments
	uint16 room, frameLists, scaleBuf, scaleBufSize, drawHook, tileBuf, clip;   ///< DS words of the room
	uint16 roomSeg, roomOff;                  ///< the original routines that are replaced
	uint16 panelSeg, panelOff;
	uint16 textSeg, textOff;
	uint16 presentSeg, presentOff;
};

extern const GraphicsDesc bifi2XP01Graphics;

/**
 * Room picture, control panel, texts and screen output in C++ (Bi-Fi 2 XP01: 16E3:01E1,
 * 1751:0067, 180C:15F2, 1B7E:000E). They replace the original routines and leave memory as
 * they do; whatever they draw marks its cells, so only changed parts are redrawn.
 */
class EngineGraphics {
public:
	EngineGraphics(ProgramVm &vm, const GraphicsDesc &desc);

	void renderRoom();
	void drawPanel();
	uint16 drawTexts();
	void present();

private:
	uint16 w(uint16 off) const;
	void setW(uint16 off, uint v);

	// room (16E3)
	void roomObject(uint16 s, uint16 o);
	void roomPicture(uint16 s, uint16 o);
	bool scaleInto(uint16 fs, uint16 fo, int sx, int sy, uint16 bs, uint16 bo, uint16 size);

	// panel (1751) and texts (180C, 1D22)
	void slotItem(int k, int i);
	void target(uint16 bottom, uint16 &ss, uint16 &so);
	void setColor(byte c);
	int spacing();
	void textSlot(int i);
	void measure(uint16 s, uint16 o, uint16 limit, int space, int &count, uint16 &width);
	void textWindow(uint16 s, uint16 o, uint16 bs, uint16 bo);
	void answers();
	void panelGrey();
	void answerLines(uint16 s, uint16 o);

	// screen output (1B7E, pointer of the mouse library)
	struct PointerClip {
		uint16 si, di, advance;
		int w, h, skip;
	};
	bool pointerClip(uint16 fs, uint16 fo, uint16 x, uint16 y, PointerClip &c);
	void drawAtPointer(uint16 fs, uint16 fo, uint16 ts, uint16 to);
	void savePointer(uint16 bs, uint16 bo, uint16 ps, uint16 po);

	ProgramVm &_vm;
	GraphicsDesc _desc;
	Gfx _gfx;
	DirtyMap _dirty;
};

} // End of namespace Tad

#endif // TAD_RENDER_H
