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
 * The command tables of the current room (Bi-Fi 2: library 1E40, Dunkle Schatten: 23D0, the
 * same code with other variables). These assembler routines call back into the game, so they
 * run like translated functions, in a stack frame of the original, and the handlers they call
 * can be original or translated code alike.
 *
 * A table is a list of 14-byte records ending with a record whose mask (+4) is 0: +0 link to
 * the next record with the same object (set up here), +2 verb (0 = any), +4 mask of the
 * situations it applies to, +6 object (-1 = any), +8 second object, +0A far pointer to the
 * handler (null = nothing to do).
 */

#include "tad/dosvm.h"
#include "tad/prims.h"
#include "tad/script.h"

namespace Tad {

/** Library segment and the words of the current command in the data segment */
struct CommandVars {
	uint16 seg, verb, obj1, obj2, any1, any2;
};

static const CommandVars kBifi2Commands = { 0x1e40, 0x2d38, 0x2d3c, 0x2d3e, 0x2d40, 0x2d42 };
static const CommandVars kSchattenCommands = { 0x23d0, 0x288d, 0x2891, 0x2893, 0x2895, 0x2897 };

static uint16 argWord(ScriptRuntime &s, const ScriptFrame &fr, int i) {
	return s.rw(fr.ss, (uint16)(fr.ab + 2 * i));
}

/**
 * x:005A: link the records of a table; returns 1 + the offset of the record for any object
 * (the earliest one with object -1), 0 if there is none; DX as the original leaves it
 */
static CallResult linkTable(ScriptRuntime &s, uint16 seg, uint16 off, uint16 dx) {
	uint16 cx = 0xffff, si = off;
	const uint16 bx = off;
	s.ww(seg, si, 0);
	if (s.rw(seg, (uint16)(si + 4)) != 0) {
		do {
			si += 0xe;
			s.ww(seg, si, 0);
		} while (s.rw(seg, (uint16)(si + 4)) != 0);
		for (;;) {
			si -= 0xe;
			dx = si;
			const uint16 ax = s.rw(seg, (uint16)(si + 6));
			if (ax == 0xffff)
				cx = si;
			if (si == bx)
				break;
			while (si != bx) {
				si -= 0xe;
				if (ax == s.rw(seg, (uint16)(si + 6))) {
					s.ww(seg, si, dx);
					break;
				}
			}
			si = dx;
		}
	}
	CallResult r;
	r.ax = (uint16)(cx + 1);
	r.dx = dx;
	return r;
}

/** x:000C (general table, at 0, result 8) and x:0031 (room table, at 4, result 0A) */
static void setTable(ScriptRuntime &s, const CommandVars &v, uint16 at, uint16 res, bool early) {
	ScriptFrame fr;
	s.enter(fr, true, 0, false, v.seg, "sd");
	const uint16 off = argWord(s, fr, 0), seg = argWord(s, fr, 1);
	s.ww(v.seg, at, off);
	s.ww(v.seg, (uint16)(at + 2), seg);
	CallResult r;
	r.ax = seg | off;
	r.dx = s.vm->r16[kDX];
	if (early)
		s.ww(v.seg, res, r.ax);
	if (r.ax)
		r = linkTable(s, seg, off, fr.entry.dx);
	s.ww(v.seg, res, r.ax);
	s.leave(fr, r.ax, r.dx);
}

/**
 * x:0141: search the records from es:si for object bx (verb cx, situation dx), following the
 * links of matching records. False: not found; otherwise the handler's result in result.
 */
static bool searchTable(ScriptRuntime &s, const CommandVars &v, uint16 es, uint16 si, uint16 bx, uint16 cx,
						uint16 dx, uint16 &result) {
	for (;;) {
		if (!s.rw(es, (uint16)(si + 4)))
			return false;
		if (bx == s.rw(es, (uint16)(si + 6)))
			break;
		si += 0xe;
	}
	for (;;) {
		bool hit = false;
		if (s.rw(es, (uint16)(si + 4)) & dx) {
			if (dx & 1) {
				hit = true;
			} else {
				const uint16 verb = s.rw(es, (uint16)(si + 2));
				if (cx == verb || verb == 0) {
					if (!(dx & 0x30) || (dx & 0x2000)) {
						hit = true;
					} else {
						const uint16 o = s.rw(es, (uint16)(si + 8));
						hit = o == s.rw(s.ds, v.any1) || (uint16)~o == s.rw(s.ds, v.any2) || o == 0 || o == 0xffff;
					}
				}
			}
		}
		if (hit) {
			result = 0;
			if (dx & 0x3000)
				return true;
			const uint16 ho = s.rw(es, (uint16)(si + 0xa)), hs = s.rw(es, (uint16)(si + 0xc));
			if (!(ho | hs))
				return true;
			const CallResult r = s.call(hs, ho, false, &dx, 1);
			if (r.ax != 0 || s.vm->stopping()) {
				result = r.ax;
				return true;
			}
		}
		si = s.rw(es, si);
		if (!si)
			return false;
	}
}

/**
 * x:009C: run the handler for the current command in situation dx: the room table before the
 * general one, the first object, then the second, then the records for any object. AX = the
 * handler's result, -1 if none applies
 */
static void dispatchCommand(ScriptRuntime &s, const CommandVars &v) {
	ScriptFrame fr;
	s.enter(fr, true, 0, false, v.seg, "sd");
	const uint16 dx = argWord(s, fr, 0);
	uint16 ret = 0xffff;
	while (!s.vm->stopping()) {
		const uint16 cx = s.rw(s.ds, v.verb);
		const uint16 bOff = s.rw(v.seg, 4), bSeg = s.rw(v.seg, 6), aOff = s.rw(v.seg, 0), aSeg = s.rw(v.seg, 2);
		const uint16 o1 = s.rw(s.ds, v.obj1), o2 = s.rw(s.ds, v.obj2);
		struct Try {
			uint16 seg, off, obj;
		} tries[5];
		int n = 0;
		if (bOff | bSeg) {
			tries[n++] = { bSeg, bOff, o1 };
			if (o2)
				tries[n++] = { bSeg, bOff, (uint16)~o2 };
			tries[n++] = { bSeg, bOff, 0 };
		}
		if (aOff | aSeg) {
			tries[n++] = { aSeg, aOff, o1 };
			if (o2)
				tries[n++] = { aSeg, aOff, (uint16)~o2 };
		}
		uint16 r = 0;
		bool found = false;
		for (int i = 0; i < n && !found; i++)
			found = searchTable(s, v, tries[i].seg, tries[i].off, tries[i].obj, cx, dx, r);
		if (!found) {
			const uint16 bAny = s.rw(v.seg, 0xa), aAny = s.rw(v.seg, 8);
			if (bAny)
				found = searchTable(s, v, bSeg, (uint16)(bAny - 1), 0xffff, cx, dx, r);
			if (!found && aAny)
				found = searchTable(s, v, aSeg, (uint16)(aAny - 1), 0xffff, cx, dx, r);
		}
		if (!found) {
			ret = 0xffff;
			break;
		}
		if (r != 0xffff) {
			ret = r;
			break;
		}
	}
	s.leave(fr, ret, dx);
}

static void bifi2SetGeneral(ScriptRuntime &s) {
	setTable(s, kBifi2Commands, 0, 8, false);
}

static void bifi2SetRoom(ScriptRuntime &s) {
	setTable(s, kBifi2Commands, 4, 0xa, true);
}

static void bifi2Dispatch(ScriptRuntime &s) {
	dispatchCommand(s, kBifi2Commands);
}

static void schattenSetGeneral(ScriptRuntime &s) {
	setTable(s, kSchattenCommands, 0, 8, false);
}

static void schattenSetRoom(ScriptRuntime &s) {
	setTable(s, kSchattenCommands, 4, 0xa, true);
}

static void schattenDispatch(ScriptRuntime &s) {
	dispatchCommand(s, kSchattenCommands);
}

const ScriptFunction bifi2XP01Hand[] = {
	{ 0x1e40, 0x000c, bifi2SetGeneral },
	{ 0x1e40, 0x0031, bifi2SetRoom },
	{ 0x1e40, 0x009c, bifi2Dispatch },
	{ 0, 0, nullptr }
};

const ScriptFunction schattenXP01Hand[] = {
	{ 0x23d0, 0x000c, schattenSetGeneral },
	{ 0x23d0, 0x0031, schattenSetRoom },
	{ 0x23d0, 0x009c, schattenDispatch },
	{ 0, 0, nullptr }
};

} // End of namespace Tad
