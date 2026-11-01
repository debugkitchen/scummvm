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

#ifndef TAD_CPU_H
#define TAD_CPU_H

#include "common/scummsys.h"

namespace Tad {

enum {
	kMemorySize = 0x110000
};

/** Register indices (Intel order) and segment register indices */
enum Reg16 { kAX = 0, kCX, kDX, kBX, kSP, kBP, kSI, kDI };
enum SegReg { kES = 0, kCS, kSS, kDS };

/**
 * Real-mode 8086/80186 interpreter for the original programs of the second
 * engine generation. Memory is a flat array of 1 MB plus the wrap area.
 */
class Cpu {
public:
	Cpu();
	virtual ~Cpu();

	byte *mem() { return _mem; }
	const byte *mem() const { return _mem; }

	byte rb(uint16 seg, uint16 off) const { return _mem[((seg << 4) + off) & 0xfffff]; }
	void wb(uint16 seg, uint16 off, uint v) { _mem[((seg << 4) + off) & 0xfffff] = (byte)v; }
	uint16 rw(uint16 seg, uint16 off) const { return rb(seg, off) | (rb(seg, (uint16)(off + 1)) << 8); }
	void ww(uint16 seg, uint16 off, uint v) { wb(seg, off, v); wb(seg, (uint16)(off + 1), v >> 8); }
	void push(uint16 v) { r16[kSP] -= 2; ww(sreg[kSS], r16[kSP], v); }
	uint16 pop() { const uint16 v = rw(sreg[kSS], r16[kSP]); r16[kSP] += 2; return v; }

	uint16 getFlags() const;
	void setFlags(uint16 v);

	uint16 r16[8];
	uint16 sreg[4];
	uint16 ip;
	byte cf, zf, sf, of, pf, af, df, iflag, tf;
	uint32 steps;

protected:
	/** Result of one instruction */
	enum ExecResult {
		kExecOk,
		kExecInvalid,     ///< invalid opcode (INT 6)
		kExecEntry,       ///< INT 21h AH=30h during unpacking
		kExecError        ///< division error, HLT and other ends
	};

	/** Executes the instruction with the opcode op (prefixes already read) */
	ExecResult exec(byte op, byte rep);
	/** Reads prefixes and the opcode; returns the opcode */
	byte fetchOpcode(byte &rep);

	virtual ExecResult interrupt(byte n);
	virtual uint16 in(uint16 port, bool word) { return 0xffff; }
	virtual void out(uint16 port, uint16 v, bool word) {}

	byte fetch() { const byte v = rb(sreg[kCS], ip); ip++; return v; }
	uint16 fetchw() { const uint16 v = rw(sreg[kCS], ip); ip += 2; return v; }

	byte r8(int i) const { return i < 4 ? r16[i] & 0xff : r16[i - 4] >> 8; }
	void s8(int i, uint v);

	int _segOv;
	byte *_mem;

private:
	struct ModRM {
		int reg;
		bool isReg;
		int rm;
		bool w;
		uint16 seg;
		uint16 off;
	};
	ModRM modrm(bool w);
	uint16 getRM(const ModRM &e);
	void setRM(const ModRM &e, uint v);
	uint16 getReg(const ModRM &e) const { return e.w ? r16[e.reg] : r8(e.reg); }
	void setReg(const ModRM &e, uint v) { if (e.w) r16[e.reg] = v; else s8(e.reg, v); }
	uint32 szp(uint32 v, bool w);
	uint32 alu(int op, uint32 a, uint32 b, bool w);
	uint32 shift(int op, uint32 v, uint n, bool w);
	bool cond(int c) const;
	int8 rel8() { return (int8)fetch(); }
	void strop(byte op, byte rep);
};

} // End of namespace Tad

#endif // TAD_CPU_H
