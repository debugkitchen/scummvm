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

#include "tad/cpu.h"

namespace Tad {

Cpu::Cpu() : ip(0), cf(0), zf(0), sf(0), of(0), pf(0), af(0), df(0), iflag(1), tf(0), steps(0), _segOv(-1) {
	_mem = new byte[kMemorySize];
	memset(_mem, 0, kMemorySize);
	memset(r16, 0, sizeof(r16));
	memset(sreg, 0, sizeof(sreg));
}

Cpu::~Cpu() {
	delete[] _mem;
}

void Cpu::s8(int i, uint v) {
	v &= 0xff;
	if (i < 4)
		r16[i] = (r16[i] & 0xff00) | v;
	else
		r16[i - 4] = (r16[i - 4] & 0xff) | (v << 8);
}

uint16 Cpu::getFlags() const {
	return 0xf002 | cf | (pf << 2) | (af << 4) | (zf << 6) | (sf << 7) | (tf << 8) |
		(iflag << 9) | (df << 10) | (of << 11);
}

void Cpu::setFlags(uint16 v) {
	cf = v & 1;
	pf = (v >> 2) & 1;
	af = (v >> 4) & 1;
	zf = (v >> 6) & 1;
	sf = (v >> 7) & 1;
	tf = (v >> 8) & 1;
	iflag = (v >> 9) & 1;
	df = (v >> 10) & 1;
	of = (v >> 11) & 1;
}

Cpu::ModRM Cpu::modrm(bool w) {
	const byte b = fetch();
	const int mod = b >> 6;
	ModRM e;
	e.reg = (b >> 3) & 7;
	e.rm = b & 7;
	e.w = w;
	e.isReg = mod == 3;
	e.seg = 0;
	e.off = 0;
	if (e.isReg)
		return e;
	uint16 off = 0;
	int seg = kDS;
	switch (e.rm) {
	case 0: off = r16[kBX] + r16[kSI]; break;
	case 1: off = r16[kBX] + r16[kDI]; break;
	case 2: off = r16[kBP] + r16[kSI]; seg = kSS; break;
	case 3: off = r16[kBP] + r16[kDI]; seg = kSS; break;
	case 4: off = r16[kSI]; break;
	case 5: off = r16[kDI]; break;
	case 6:
		if (mod == 0) {
			off = fetchw();
		} else {
			off = r16[kBP];
			seg = kSS;
		}
		break;
	default: off = r16[kBX]; break;
	}
	if (mod == 1)
		off += (int8)fetch();
	else if (mod == 2)
		off += fetchw();
	if (_segOv >= 0)
		seg = _segOv;
	e.seg = sreg[seg];
	e.off = off;
	return e;
}

uint16 Cpu::getRM(const ModRM &e) {
	if (e.isReg)
		return e.w ? r16[e.rm] : r8(e.rm);
	return e.w ? rw(e.seg, e.off) : rb(e.seg, e.off);
}

void Cpu::setRM(const ModRM &e, uint v) {
	if (e.isReg) {
		if (e.w)
			r16[e.rm] = v;
		else
			s8(e.rm, v);
		return;
	}
	if (e.w)
		ww(e.seg, e.off, v);
	else
		wb(e.seg, e.off, v);
}

uint32 Cpu::szp(uint32 v, bool w) {
	v &= w ? 0xffff : 0xff;
	zf = v == 0;
	sf = (v >> (w ? 15 : 7)) & 1;
	uint p = v & 0xff;
	p ^= p >> 4;
	p ^= p >> 2;
	p ^= p >> 1;
	pf = (~p) & 1;
	return v;
}

uint32 Cpu::alu(int op, uint32 a, uint32 b, bool w) {
	const int32 mask = w ? 0xffff : 0xff;
	const uint32 sign = w ? 0x8000 : 0x80;
	int32 r = 0;
	switch (op) {
	case 0: // ADD
		r = a + b;
		cf = r > mask;
		of = ((~(a ^ b) & (a ^ r)) & sign) ? 1 : 0;
		af = ((a ^ b ^ r) >> 4) & 1;
		break;
	case 1: // OR
		r = a | b;
		cf = of = af = 0;
		break;
	case 2: { // ADC
		const int c = cf;
		r = a + b + c;
		cf = r > mask;
		of = ((~(a ^ b) & (a ^ r)) & sign) ? 1 : 0;
		af = ((a ^ b ^ r) >> 4) & 1;
		break;
	}
	case 3: { // SBB
		const int c = cf;
		r = (int32)a - (int32)b - c;
		cf = r < 0;
		of = (((a ^ b) & (a ^ r)) & sign) ? 1 : 0;
		af = ((a ^ b ^ r) >> 4) & 1;
		break;
	}
	case 4: // AND
		r = a & b;
		cf = of = af = 0;
		break;
	case 6: // XOR
		r = a ^ b;
		cf = of = af = 0;
		break;
	default: // SUB, CMP
		r = (int32)a - (int32)b;
		cf = r < 0;
		of = (((a ^ b) & (a ^ r)) & sign) ? 1 : 0;
		af = ((a ^ b ^ r) >> 4) & 1;
		break;
	}
	return szp(r, w);
}

uint32 Cpu::shift(int op, uint32 v, uint n, bool w) {
	const int bits = w ? 16 : 8;
	const uint32 mask = w ? 0xffff : 0xff;
	n &= 0x1f;
	if (!n)
		return v;
	for (uint i = 0; i < n; i++) {
		const uint32 msb = (v >> (bits - 1)) & 1, lsb = v & 1;
		switch (op) {
		case 0: v = ((v << 1) | msb) & mask; cf = msb; break;                  // ROL
		case 1: v = (v >> 1) | (lsb << (bits - 1)); cf = lsb; break;           // ROR
		case 2: v = ((v << 1) | cf) & mask; cf = msb; break;                   // RCL
		case 3: v = (v >> 1) | ((uint32)cf << (bits - 1)); cf = lsb; break;   // RCR
		case 5: v = v >> 1; cf = lsb; break;                                   // SHR
		case 7: v = (v >> 1) | (msb << (bits - 1)); cf = lsb; break;           // SAR
		default: v = (v << 1) & mask; cf = msb; break;                         // SHL, SAL
		}
	}
	const uint32 msb = (v >> (bits - 1)) & 1;
	if (op <= 3) {
		of = (op == 0 || op == 2) ? (msb ^ cf) : (msb ^ ((v >> (bits - 2)) & 1));
	} else {
		szp(v, w);
		of = (op == 4 || op == 6) ? (msb ^ cf) : 0;
	}
	return v;
}

bool Cpu::cond(int c) const {
	switch (c) {
	case 0: return of;
	case 1: return !of;
	case 2: return cf;
	case 3: return !cf;
	case 4: return zf;
	case 5: return !zf;
	case 6: return cf || zf;
	case 7: return !(cf || zf);
	case 8: return sf;
	case 9: return !sf;
	case 10: return pf;
	case 11: return !pf;
	case 12: return sf != of;
	case 13: return sf == of;
	case 14: return zf || sf != of;
	default: return !zf && sf == of;
	}
}

void Cpu::strop(byte op, byte rep) {
	const bool w = op & 1;
	const int step = df ? (w ? -2 : -1) : (w ? 2 : 1);
	const uint16 srcSeg = sreg[_segOv >= 0 ? _segOv : kDS];
	const bool cmpOp = op == 0xa6 || op == 0xa7 || op == 0xae || op == 0xaf;

	for (;;) {
		if (rep && r16[kCX] == 0)
			break;
		switch (op) {
		case 0xa4: case 0xa5: {
			const uint16 v = w ? rw(srcSeg, r16[kSI]) : rb(srcSeg, r16[kSI]);
			if (w)
				ww(sreg[kES], r16[kDI], v);
			else
				wb(sreg[kES], r16[kDI], v);
			r16[kSI] += step;
			r16[kDI] += step;
			break;
		}
		case 0xa6: case 0xa7: {
			const uint16 a = w ? rw(srcSeg, r16[kSI]) : rb(srcSeg, r16[kSI]);
			const uint16 b = w ? rw(sreg[kES], r16[kDI]) : rb(sreg[kES], r16[kDI]);
			alu(7, a, b, w);
			r16[kSI] += step;
			r16[kDI] += step;
			break;
		}
		case 0xaa: case 0xab:
			if (w)
				ww(sreg[kES], r16[kDI], r16[kAX]);
			else
				wb(sreg[kES], r16[kDI], r16[kAX]);
			r16[kDI] += step;
			break;
		case 0xac: case 0xad:
			if (w)
				r16[kAX] = rw(srcSeg, r16[kSI]);
			else
				s8(0, rb(srcSeg, r16[kSI]));
			r16[kSI] += step;
			break;
		case 0x6e: case 0x6f: {
			const uint16 v = w ? rw(srcSeg, r16[kSI]) : rb(srcSeg, r16[kSI]);
			out(r16[kDX], v, w);
			r16[kSI] += step;
			break;
		}
		case 0x6c: case 0x6d: {
			const uint16 v = in(r16[kDX], w);
			if (w)
				ww(sreg[kES], r16[kDI], v);
			else
				wb(sreg[kES], r16[kDI], v);
			r16[kDI] += step;
			break;
		}
		default: { // 0xae, 0xaf
			const uint16 b = w ? rw(sreg[kES], r16[kDI]) : rb(sreg[kES], r16[kDI]);
			alu(7, w ? r16[kAX] : r16[kAX] & 0xff, b, w);
			r16[kDI] += step;
			break;
		}
		}
		if (!rep)
			break;
		r16[kCX]--;
		if (cmpOp && ((rep == 0xf3 && !zf) || (rep == 0xf2 && zf)))
			break;
	}
}

Cpu::ExecResult Cpu::interrupt(byte n) {
	return kExecOk;
}

byte Cpu::fetchOpcode(byte &rep) {
	_segOv = -1;
	rep = 0;
	for (;;) {
		const byte op = fetch();
		if (op == 0x26 || op == 0x2e || op == 0x36 || op == 0x3e) {
			_segOv = (op >> 3) & 3;
			continue;
		}
		if (op == 0xf2 || op == 0xf3) {
			rep = op;
			continue;
		}
		if (op == 0xf0)
			continue;
		return op;
	}
}

Cpu::ExecResult Cpu::exec(byte op, byte rep) {
	uint16 *R = r16;
	if (op < 0x40 && (op & 7) < 6) {                     // ALU ops 00..3D
		const int a = op >> 3, form = op & 7;
		if (form < 4) {
			const bool w = form & 1;
			const ModRM e = modrm(w);
			if (form & 2) {
				const uint32 v = alu(a, getReg(e), getRM(e), w);
				if (a != 7)
					setReg(e, v);
			} else {
				const uint32 v = alu(a, getRM(e), getReg(e), w);
				if (a != 7)
					setRM(e, v);
			}
		} else if (form == 4) {
			const uint32 v = alu(a, R[kAX] & 0xff, fetch(), false);
			if (a != 7)
				s8(0, v);
		} else {
			const uint32 v = alu(a, R[kAX], fetchw(), true);
			if (a != 7)
				R[kAX] = v;
		}
		return kExecOk;
	}
	switch (op) {
	case 0x06: case 0x0e: case 0x16: case 0x1e:
		push(sreg[op >> 3]);
		return kExecOk;
	case 0x07: case 0x17: case 0x1f:
		sreg[op >> 3] = pop();
		return kExecOk;
	case 0x27: case 0x2f: case 0x37: case 0x3f:
		warning("TAD: BCD instruction %02x", op);
		return kExecError;
	default:
		break;
	}
	if (op >= 0x40 && op <= 0x4f) {                        // INC/DEC r16
		const int i = op & 7;
		const byte c = cf;
		R[i] = alu(op < 0x48 ? 0 : 5, R[i], 1, true);
		cf = c;
		return kExecOk;
	}
	if (op >= 0x50 && op <= 0x57) {
		push(R[op & 7]);
		return kExecOk;
	}
	if (op >= 0x58 && op <= 0x5f) {
		R[op & 7] = pop();
		return kExecOk;
	}
	if (op >= 0x70 && op <= 0x7f) {
		const int8 d = rel8();
		if (cond(op & 15))
			ip += d;
		return kExecOk;
	}
	if (op >= 0x91 && op <= 0x97) {
		const uint16 t = R[kAX];
		R[kAX] = R[op & 7];
		R[op & 7] = t;
		return kExecOk;
	}
	if (op >= 0xb0 && op <= 0xb7) {
		s8(op & 7, fetch());
		return kExecOk;
	}
	if (op >= 0xb8 && op <= 0xbf) {
		R[op & 7] = fetchw();
		return kExecOk;
	}

	switch (op) {
	case 0x0f: {                                           // 286: SMSW only (real mode)
		const byte op2 = fetch();
		if (op2 == 0x01) {
			const ModRM e = modrm(true);
			if (e.reg == 4) {
				setRM(e, 0xfff0);
				return kExecOk;
			}
		}
		break;
	}
	case 0x60: {
		const uint16 sp = R[kSP];
		for (int i = 0; i < 8; i++)
			push(i == 4 ? sp : R[i]);
		return kExecOk;
	}
	case 0x61:
		for (int i = 7; i >= 0; i--) {
			const uint16 v = pop();
			if (i != 4)
				R[i] = v;
		}
		return kExecOk;
	case 0x68:
		push(fetchw());
		return kExecOk;
	case 0x6a:
		push((uint16)(int16)(int8)fetch());
		return kExecOk;
	case 0x69: case 0x6b: {
		const ModRM e = modrm(true);
		const int32 imm = op == 0x69 ? (int16)fetchw() : (int8)fetch();
		const int32 r = (int16)getRM(e) * imm;
		R[e.reg] = r & 0xffff;
		cf = of = (r < -32768 || r > 32767) ? 1 : 0;
		return kExecOk;
	}
	case 0x80: case 0x81: case 0x82: case 0x83: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		uint32 imm = op == 0x81 ? fetchw() : fetch();
		if (op == 0x83 && (imm & 0x80))
			imm |= 0xff00;
		const uint32 v = alu(e.reg, getRM(e), imm, w);
		if (e.reg != 7)
			setRM(e, v);
		return kExecOk;
	}
	case 0x84: case 0x85: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		alu(4, getRM(e), getReg(e), w);
		return kExecOk;
	}
	case 0x86: case 0x87: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		const uint16 a = getRM(e);
		setRM(e, getReg(e));
		setReg(e, a);
		return kExecOk;
	}
	case 0x88: case 0x89: {
		const ModRM e = modrm(op & 1);
		setRM(e, getReg(e));
		return kExecOk;
	}
	case 0x8a: case 0x8b: {
		const ModRM e = modrm(op & 1);
		setReg(e, getRM(e));
		return kExecOk;
	}
	case 0x8c: {
		const ModRM e = modrm(true);
		setRM(e, sreg[e.reg & 3]);
		return kExecOk;
	}
	case 0x8d: {
		const ModRM e = modrm(true);
		R[e.reg] = e.off;
		return kExecOk;
	}
	case 0x8e: {
		const ModRM e = modrm(true);
		sreg[e.reg & 3] = getRM(e);
		return kExecOk;
	}
	case 0x8f: {
		const ModRM e = modrm(true);
		setRM(e, pop());
		return kExecOk;
	}
	case 0x90:
		return kExecOk;
	case 0x98:
		R[kAX] = (R[kAX] & 0x80) ? (R[kAX] | 0xff00) : (R[kAX] & 0xff);
		return kExecOk;
	case 0x99:
		R[kDX] = (R[kAX] & 0x8000) ? 0xffff : 0;
		return kExecOk;
	case 0x9a: {
		const uint16 nip = fetchw(), ncs = fetchw();
		push(sreg[kCS]);
		push(ip);
		sreg[kCS] = ncs;
		ip = nip;
		return kExecOk;
	}
	case 0x9c:
		push(getFlags());
		return kExecOk;
	case 0x9d:
		setFlags(pop());
		return kExecOk;
	case 0x9e:
		setFlags((getFlags() & 0xff00) | (R[kAX] >> 8));
		return kExecOk;
	case 0x9f:
		s8(4, getFlags() & 0xff);
		return kExecOk;
	case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
		const uint16 off = fetchw(), seg = sreg[_segOv >= 0 ? _segOv : kDS];
		if (op == 0xa0)
			s8(0, rb(seg, off));
		else if (op == 0xa1)
			R[kAX] = rw(seg, off);
		else if (op == 0xa2)
			wb(seg, off, R[kAX]);
		else
			ww(seg, off, R[kAX]);
		return kExecOk;
	}
	case 0x6c: case 0x6d: case 0x6e: case 0x6f:
	case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
		strop(op, rep);
		return kExecOk;
	case 0xa8:
		alu(4, R[kAX] & 0xff, fetch(), false);
		return kExecOk;
	case 0xa9:
		alu(4, R[kAX], fetchw(), true);
		return kExecOk;
	case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		const uint n = op <= 0xc1 ? fetch() : op <= 0xd1 ? 1 : R[kCX] & 0xff;
		setRM(e, shift(e.reg, getRM(e), n, w));
		return kExecOk;
	}
	case 0xc2: {
		const uint16 n = fetchw();
		ip = pop();
		R[kSP] += n;
		return kExecOk;
	}
	case 0xc3:
		ip = pop();
		return kExecOk;
	case 0xc4: case 0xc5: {
		const ModRM e = modrm(true);
		R[e.reg] = rw(e.seg, e.off);
		sreg[op == 0xc4 ? kES : kDS] = rw(e.seg, e.off + 2);
		return kExecOk;
	}
	case 0xc6: case 0xc7: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		setRM(e, w ? fetchw() : fetch());
		return kExecOk;
	}
	case 0xc8: {
		const uint16 n = fetchw();
		const byte level = fetch();
		push(R[kBP]);
		const uint16 fp = R[kSP];
		if (level) {
			warning("TAD: ENTER with nesting level");
			return kExecError;
		}
		R[kBP] = fp;
		R[kSP] -= n;
		return kExecOk;
	}
	case 0xc9:
		R[kSP] = R[kBP];
		R[kBP] = pop();
		return kExecOk;
	case 0xca: {
		const uint16 n = fetchw();
		ip = pop();
		sreg[kCS] = pop();
		R[kSP] += n;
		return kExecOk;
	}
	case 0xcb:
		ip = pop();
		sreg[kCS] = pop();
		return kExecOk;
	case 0xcc:
		return interrupt(3);
	case 0xcd:
		return interrupt(fetch());
	case 0xcf:
		ip = pop();
		sreg[kCS] = pop();
		setFlags(pop());
		return kExecOk;
	case 0xd7: {
		const uint16 seg = sreg[_segOv >= 0 ? _segOv : kDS];
		s8(0, rb(seg, R[kBX] + (R[kAX] & 0xff)));
		return kExecOk;
	}
	case 0xe0: case 0xe1: case 0xe2: {
		const int8 d = rel8();
		R[kCX]--;
		const bool go = R[kCX] != 0 && (op == 0xe2 || (op == 0xe1 ? zf : !zf));
		if (go)
			ip += d;
		return kExecOk;
	}
	case 0xe3: {
		const int8 d = rel8();
		if (R[kCX] == 0)
			ip += d;
		return kExecOk;
	}
	case 0xe4: case 0xe5: case 0xec: case 0xed: {
		const uint16 port = op <= 0xe5 ? fetch() : R[kDX];
		const uint16 v = in(port, op & 1);
		if (op & 1)
			R[kAX] = v;
		else
			s8(0, v);
		return kExecOk;
	}
	case 0xe6: case 0xe7: {
		const byte port = fetch();
		out(port, (op & 1) ? R[kAX] : R[kAX] & 0xff, op & 1);
		return kExecOk;
	}
	case 0xee: case 0xef:
		out(R[kDX], (op & 1) ? R[kAX] : R[kAX] & 0xff, op & 1);
		return kExecOk;
	case 0xe8: {
		const uint16 d = fetchw();
		push(ip);
		ip += d;
		return kExecOk;
	}
	case 0xe9: {
		const uint16 d = fetchw();
		ip += d;
		return kExecOk;
	}
	case 0xea: {
		const uint16 nip = fetchw(), ncs = fetchw();
		sreg[kCS] = ncs;
		ip = nip;
		return kExecOk;
	}
	case 0xeb: {
		const int8 d = rel8();
		ip += d;
		return kExecOk;
	}
	case 0xf4:
		warning("TAD: HLT");
		return kExecError;
	case 0xf5:
		cf ^= 1;
		return kExecOk;
	case 0xf6: case 0xf7: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		const uint16 v = getRM(e);
		switch (e.reg) {
		case 0: case 1:
			alu(4, v, w ? fetchw() : fetch(), w);
			return kExecOk;
		case 2:
			setRM(e, ~v);
			return kExecOk;
		case 3:
			setRM(e, alu(5, 0, v, w));
			cf = v ? 1 : 0;
			return kExecOk;
		case 4:
			if (w) {
				const uint32 r = (uint32)R[kAX] * v;
				R[kAX] = r & 0xffff;
				R[kDX] = r >> 16;
				cf = of = R[kDX] ? 1 : 0;
			} else {
				const uint32 r = (R[kAX] & 0xff) * v;
				R[kAX] = r & 0xffff;
				cf = of = (r >> 8) ? 1 : 0;
			}
			return kExecOk;
		case 5:
			if (w) {
				const int32 r = (int32)(int16)R[kAX] * (int16)v;
				R[kAX] = r & 0xffff;
				R[kDX] = (r >> 16) & 0xffff;
				cf = of = (r < -32768 || r > 32767) ? 1 : 0;
			} else {
				const int32 r = (int32)(int8)(R[kAX] & 0xff) * (int8)(v & 0xff);
				R[kAX] = r & 0xffff;
				cf = of = (r < -128 || r > 127) ? 1 : 0;
			}
			return kExecOk;
		case 6:
			if (!v) {
				warning("TAD: division by zero at %04x:%04x", sreg[kCS], ip);
				return kExecError;
			}
			if (w) {
				const uint32 n = ((uint32)R[kDX] << 16) | R[kAX];
				const uint32 q = n / v;
				if (q > 0xffff) {
					warning("TAD: division overflow");
					return kExecError;
				}
				R[kAX] = q;
				R[kDX] = n % v;
			} else {
				const uint16 n = R[kAX];
				const uint16 q = n / v;
				if (q > 0xff) {
					warning("TAD: division overflow");
					return kExecError;
				}
				R[kAX] = ((n % v) << 8) | q;
			}
			return kExecOk;
		default:
			if (!v) {
				warning("TAD: division by zero at %04x:%04x", sreg[kCS], ip);
				return kExecError;
			}
			if (w) {
				const int32 n = (int32)(((uint32)R[kDX] << 16) | R[kAX]);
				const int32 d = (int16)v;
				// the JavaScript reference truncates and keeps the low 16 bits, also on overflow
				const int64 q = (int64)n / d;
				R[kAX] = (uint16)(q & 0xffff);
				R[kDX] = d == -1 ? 0 : (uint16)((n % d) & 0xffff);
			} else {
				const int32 n = (int16)R[kAX];
				const int32 d = (int8)(v & 0xff);
				const int32 q = n / d;
				R[kAX] = (((n % d) & 0xff) << 8) | (q & 0xff);
			}
			return kExecOk;
		}
	}
	case 0xf8: cf = 0; return kExecOk;
	case 0xf9: cf = 1; return kExecOk;
	case 0xfa: iflag = 0; return kExecOk;
	case 0xfb: iflag = 1; return kExecOk;
	case 0xfc: df = 0; return kExecOk;
	case 0xfd: df = 1; return kExecOk;
	case 0xfe: case 0xff: {
		const bool w = op & 1;
		const ModRM e = modrm(w);
		switch (e.reg) {
		case 0: case 1: {
			const byte c = cf;
			setRM(e, alu(e.reg ? 5 : 0, getRM(e), 1, w));
			cf = c;
			return kExecOk;
		}
		case 2: {
			const uint16 t = getRM(e);
			push(ip);
			ip = t;
			return kExecOk;
		}
		case 3: {
			const uint16 nip = rw(e.seg, e.off), ncs = rw(e.seg, e.off + 2);
			push(sreg[kCS]);
			push(ip);
			sreg[kCS] = ncs;
			ip = nip;
			return kExecOk;
		}
		case 4:
			ip = getRM(e);
			return kExecOk;
		case 5:
			ip = rw(e.seg, e.off);
			sreg[kCS] = rw(e.seg, e.off + 2);
			return kExecOk;
		case 6:
			push(getRM(e));
			return kExecOk;
		default:
			break;
		}
		break;
	}
	default:
		break;
	}
	return kExecInvalid;
}

} // End of namespace Tad
