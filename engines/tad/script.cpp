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

#include "common/debug.h"
#include "common/textconsole.h"

#include "tad/detection.h"
#include "tad/runner.h"
#include "tad/script.h"

namespace Tad {

CallResult longOp(LongOp op, const uint16 *a) {
	const int32 x = (int32)(((uint32)a[1] << 16) | a[0]);
	const uint32 ux = ((uint32)a[1] << 16) | a[0];
	int32 v = 0;
	switch (op) {
	case kLongMul:
		v = (int32)((uint32)x * (((uint32)a[3] << 16) | a[2]));
		break;
	case kLongDiv: case kLongMod: {
		const int32 d = (int32)(((uint32)a[3] << 16) | a[2]);
		if (!d || (d == -1 && x == (int32)0x80000000)) {
			warning("TAD: long division by zero");
			break;
		}
		v = op == kLongDiv ? x / d : x % d;
		break;
	}
	case kLongUDiv: case kLongUMod: {
		const uint32 d = ((uint32)a[3] << 16) | a[2];
		if (!d) {
			warning("TAD: long division by zero");
			break;
		}
		v = (int32)(op == kLongUDiv ? ux / d : ux % d);
		break;
	}
	case kLongShl:
		v = (a[2] & 0xff) >= 32 ? 0 : (int32)(ux << (a[2] & 31));
		break;
	case kLongSar:
		v = x >> MIN<int>(a[2] & 0xff, 31);
		break;
	default: // kLongShr
		v = (a[2] & 0xff) >= 32 ? 0 : (int32)(ux >> (a[2] & 31));
		break;
	}
	CallResult r;
	r.ax = (uint32)v & 0xffff;
	r.dx = ((uint32)v >> 16) & 0xffff;
	return r;
}

void ScriptRuntime::install(const ScriptFunction *list) {
	for (const ScriptFunction *f = list; f->fn; f++) {
		_funcs[((uint32)f->seg << 16) | f->off] = f->fn;
		vm->setTrap(f->seg, f->off, ProgramVm::kTrapScript);
	}
}

void ScriptRuntime::invoke(uint16 seg, uint16 off) {
	// the frame budget (8086 steps and translated blocks) is checked when a function starts and
	// when it returns, as the table interpreter does
	prog->checkBudget();
	if (vm->stopping())
		return;
	_funcs[((uint32)seg << 16) | off](*this);
	prog->checkBudget();
}

void ScriptRuntime::enter(ScriptFrame &fr, bool bpFrame, uint16 frameSize, bool nearFunc, uint16 seg, const char *saves) {
	uint16 *r = vm->r16;
	fr.sp0 = r[kSP];
	fr.bp = r[kBP];
	fr.bpFrame = bpFrame;
	fr.nearFunc = nearFunc;
	fr.seg = seg;
	if (bpFrame) {
		vm->push(r[kBP]);
		fr.bp = r[kSP];
		r[kBP] = fr.bp;
		r[kSP] = fr.bp - frameSize;
	}
	for (const char *p = saves; *p; p++)
		vm->push(r[*p == 's' ? kSI : kDI]);
	fr.ab = bpFrame ? fr.bp + (nearFunc ? 4 : 6) : fr.sp0 + (nearFunc ? 2 : 4);
	fr.ss = vm->sreg[kSS];
	fr.si = r[kSI];
	fr.di = r[kDI];
	fr.entry.ax = r[kAX];
	fr.entry.bx = r[kBX];
	fr.entry.cx = r[kCX];
	fr.entry.dx = r[kDX];
	fr.entry.si = r[kSI];
	fr.entry.di = r[kDI];
	fr.entry.es = vm->sreg[kES];
	fr.entry.bp = fr.bp;
}

void ScriptRuntime::leave(ScriptFrame &fr, uint16 ret, uint16 retd) {
	uint16 *r = vm->r16;
	if (fr.bpFrame) {
		r[kSP] = fr.bp;
		r[kBP] = vm->pop();
	} else {
		r[kSP] = fr.sp0;
	}
	vm->ip = vm->pop();
	vm->sreg[kCS] = fr.nearFunc ? fr.seg : vm->pop();
	r[kAX] = ret;
	r[kDX] = retd;
	r[kSI] = fr.si;
	r[kDI] = fr.di;
	vm->sreg[kDS] = ds;
}

CallResult ScriptRuntime::call(uint16 seg, uint16 off, bool nearCall, const uint16 *args, uint n) {
	uint16 *r = vm->r16;
	CallResult res;
	if (vm->stopping())
		return res;
	const uint16 sp0 = r[kSP];
	for (int i = (int)n - 1; i >= 0; i--)
		vm->push(args[i]);
	const uint32 saveNear = vm->nearRet;
	if (nearCall) {
		vm->nearRet = ((uint32)seg << 16) | 0xfffe;
		vm->push(0xfffe);
	} else {
		vm->push(0xf000);
		vm->push(0x0000);
	}
	vm->sreg[kCS] = seg;
	vm->ip = off;
	vm->sreg[kDS] = ds;
	if (!nearCall && has(seg, off)) {
		// a translated function: the trap the interpreter would reach first
		invoke(seg, off);
	} else {
		prog->runToReturn(seg);
	}
	vm->nearRet = saveNear;
	res.ax = r[kAX];
	res.dx = r[kDX];
	r[kSP] = sp0;
	vm->sreg[kDS] = ds;
	return res;
}

CallResult ScriptRuntime::hugeOp(bool add, const uint16 *a) {
	const int32 l = (int32)(((uint32)a[3] << 16) | a[2]);
	const uint16 off = vm->rw(a[1], a[0]), seg = vm->rw(a[1], a[0] + 2);
	const int64 lin = (int64)seg * 16 + off + (add ? l : -(int64)l);
	const uint32 n = (uint32)(((lin % 0x100000) + 0x100000) % 0x100000);
	const uint16 nseg = (n >> 4) & 0xffff, noff = n & 15;
	vm->ww(a[1], a[0], noff);
	vm->ww(a[1], a[0] + 2, nseg);
	CallResult r;
	r.ax = noff;
	r.dx = nseg;
	return r;
}

bool ScriptRuntime::blockStep(ScriptFrame &fr) {
	blocks++;
	// as the table interpreter: a loop that runs very long (waiting for memory that an interrupt
	// changes) lets the frame end
	if ((fr.steps & 0x3ffff) == 0x3ffff) {
		debugC(2, kDebugScript, "TAD: long loop in segment %04x, frame %d", fr.seg, prog->frame());
		prog->endFrame();
	}
	fr.steps++;
	return vm->stopping();
}

void ScriptRuntime::fail(const char *msg) {
	if (!_failed)
		warning("TAD: %s", msg);
	_failed = true;
	vm->stop();
}

} // End of namespace Tad
