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

#ifndef TAD_PRIMS_H
#define TAD_PRIMS_H

#include "common/scummsys.h"

namespace Tad {

class DosVm;
class ScriptRuntime;
struct GfxLayout;
struct ScriptFunction;

/** AX and DX of a routine that returns with RETF */
struct PrimResult {
	uint16 ax;
	uint16 dx;
};

/**
 * A hand-written assembler routine of the second engine generation done in C++. It gets the
 * machine at the entry of the far routine (return address at SS:SP, arguments after it), the
 * segment of its library and, for the graphics routines, where the library keeps its
 * variables; it changes memory, ports and the
 * direction flag exactly as the original does and sets AX and DX as the original leaves them
 * (r holds AX and DX at the entry).
 */
typedef void (*PrimFunc)(DosVm &vm, uint16 cs, const GfxLayout *lay, PrimResult &r);
/** False where the original would be interrupted in the middle: then the 8086 code runs */
typedef bool (*PrimReady)(DosVm &vm, uint16 cs);

/** A routine at its address in one program */
struct PrimAddress {
	uint16 seg;
	uint16 off;
	PrimFunc fn;
	PrimReady ready;   ///< nullptr: always
	const GfxLayout *layout;
};

extern const PrimAddress bifi2XP01Prims[];
extern const PrimAddress schattenXP01Prims[];

/** Routines that call back into the game (command tables), run as translated functions */
extern const ScriptFunction bifi2XP01Hand[];
extern const ScriptFunction schattenXP01Hand[];

} // End of namespace Tad

#endif // TAD_PRIMS_H
