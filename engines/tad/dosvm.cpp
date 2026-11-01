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
#include "common/system.h"
#include "common/textconsole.h"

#include "tad/detection.h"
#include "tad/dosvm.h"

namespace Tad {

static const uint16 kPsp = 0x0ff0;              // load segment 0x1000 - 0x10
static const uint16 kLoadSeg = 0x1000;
static const uint16 kDefaultSeg = 0xf100;       // f100:nn holds IRET for every vector

DosVm::DosVm(FileProvider *files) : _files(files), _skipTrap(0xffffffff), _yielded(kYieldNone),
	_exitCode(0), _unpacking(false), _entryReached(false), _nextHandle(5), _memTop(0xa000),
	_progEnd(0xa000), _psp(kPsp), _dtaSeg(kPsp), _dtaOff(0x80), _dacW(0), _dacR(0), _retrace(0),
	_videoMode(3), _pitDiv(0x10000), _pitLo(-1), _pitLatch(-1), _pitHi(false), _pitReads(0),
	_ips(10e6), _clock(0), _nextIrq(0), _biosClock(0), _biosTicks(0), _pendingIrq(0), _kbdByte(0) {
	memset(_dac, 0, sizeof(_dac));
	mouseVars.ds = mouseVars.x = mouseVars.y = mouseVars.flags = 0;
}

bool DosVm::load(const byte *f, uint32 size) {
	if (size < 0x1c || READ_LE_UINT16(f) != 0x5a4d)
		return false;
	const uint16 cblp = READ_LE_UINT16(f + 2), cp = READ_LE_UINT16(f + 4), crlc = READ_LE_UINT16(f + 6);
	const uint16 cparhdr = READ_LE_UINT16(f + 8);
	const uint16 ss0 = READ_LE_UINT16(f + 0x0e), sp0 = READ_LE_UINT16(f + 0x10);
	const uint16 ip0 = READ_LE_UINT16(f + 0x14), cs0 = READ_LE_UINT16(f + 0x16), lfarlc = READ_LE_UINT16(f + 0x18);
	const uint32 hdr = cparhdr * 16;
	const uint32 end = MIN<uint32>(size, cp * 512 - (cblp ? 512 - cblp : 0));
	if (hdr >= end)
		return false;
	memcpy(_mem + kLoadSeg * 16, f + hdr, end - hdr);
	for (uint i = 0; i < crlc; i++) {
		const uint16 off = READ_LE_UINT16(f + lfarlc + 4 * i), seg = READ_LE_UINT16(f + lfarlc + 4 * i + 2);
		const uint32 a = (kLoadSeg + seg) * 16 + off;
		WRITE_LE_UINT16(_mem + a, READ_LE_UINT16(_mem + a) + kLoadSeg);
	}
	const uint32 psp = (kLoadSeg - 0x10) * 16;
	_mem[psp] = 0xcd;
	_mem[psp + 1] = 0x20;
	_mem[psp + 2] = 0xff;
	_mem[psp + 3] = 0x9f;
	_mem[psp + 0x2c] = 0x80;
	_mem[psp + 0x2d] = 0x00;
	static const char env[] = "PATH=C:\\\0\0\x01\0C:\\TK2\\XP01.EXE";
	memcpy(_mem + 0x800, env, sizeof(env));
	sreg[kES] = kLoadSeg - 0x10;
	sreg[kCS] = kLoadSeg + cs0;
	sreg[kSS] = kLoadSeg + ss0;
	sreg[kDS] = kLoadSeg - 0x10;
	r16[kSP] = sp0;
	ip = ip0;

	// the unpacking stub
	_unpacking = true;
	_entryReached = false;
	for (steps = 0; steps < 50000000 && !_entryReached; steps++) {
		byte rep;
		const byte op = fetchOpcode(rep);
		if (exec(op, rep) != kExecOk && !_entryReached) {
			warning("TAD: unpacking stopped at %04x:%04x", sreg[kCS], ip);
			return false;
		}
	}
	_unpacking = false;
	if (!_entryReached)
		return false;

	r16[kAX] = 0x0005;                           // INT 21h AH=30h: DOS 5.0
	steps = 0;
	ww(0x40, 0x1a, 0x1e);
	ww(0x40, 0x1c, 0x1e);
	ww(0x40, 0x80, 0x1e);
	ww(0x40, 0x82, 0x3e);
	for (uint n = 0; n < 256; n++) {
		ww(0, n * 4, n);
		ww(0, n * 4 + 2, kDefaultSeg);
		_mem[0xf1000 + n] = 0xcf;
	}
	_progEnd = _memTop;
	_blocks[kPsp] = _progEnd - kPsp;
	return true;
}

Common::String DosVm::str(uint16 seg, uint16 off, uint max) const {
	Common::String s;
	for (uint i = 0; i < max; i++) {
		const byte c = rb(seg, off + i);
		if (!c)
			break;
		s += (char)c;
	}
	return s;
}

Common::String DosVm::fileName(const Common::String &path) {
	Common::String p = path;
	for (uint i = 0; i < p.size(); i++)
		if (p[i] == '/')
			p.setChar('\\', i);
	const size_t k = p.findLastOf('\\');
	if (k != Common::String::npos)
		p = p.substr(k + 1);
	p.toUppercase();
	return p;
}

DosVm::RunResult DosVm::run(uint32 maxSteps) {
	const uint32 end = steps + maxSteps;
	_yielded = kYieldNone;
	if (!keepPit)
		_pitReads = 0;
	while (steps < end) {
		if (_stopping)
			return kRunReturn;
		const uint32 k = ((uint32)sreg[kCS] << 16) | ip;
		if (k == 0xf0000000 || k == nearRet)
			return kRunReturn;
		if (sreg[kCS] == kDefaultSeg) {
			// default handler reached by chaining: BIOS/DOS, then IRET
			const byte n = ip & 0xff;
			if (n != 8 && n != 0x1c)
				hle(n);
			const uint16 f = getFlags() & 1;
			ip = pop();
			sreg[kCS] = pop();
			const uint16 fl = pop();
			setFlags((fl & ~1) | ((n == 8 || n == 0x1c) ? (fl & 1) : f));
			continue;
		}
		if (k != _skipTrap && _traps.contains(k)) {
			const TrapResult r = onTrap(_traps[k]);
			_skipTrap = 0xffffffff;
			if (r == kTrapBefore) {
				_skipTrap = k;
				return _yielded != kYieldNone ? kRunYield : kRunStop;
			}
			if (r == kTrapPass) {
				_skipTrap = k;
				continue;
			}
			if (_yielded != kYieldNone)
				return kRunYield;
			if (r == kTrapStop)
				return kRunStop;
			continue;
		}
		_skipTrap = 0xffffffff;

		steps++;
		const uint16 ip0 = ip;
		byte rep;
		const byte op = fetchOpcode(rep);
		const ExecResult res = exec(op, rep);
		if (res == kExecInvalid) {
			// the CPU tests of the Borland runtime rely on INT 6 if the program installed a handler
			if (rw(0, 0x1a) == kDefaultSeg) {
				warning("TAD: invalid opcode %02x at %04x:%04x", op, sreg[kCS], ip0);
				return kRunError;
			}
			ip = ip0;
			irq(6);
		} else if (res == kExecError) {
			return kRunError;
		}
		if (_yielded != kYieldNone)
			return kRunYield;
		if ((steps & 0x3ff) == 0 && !noStepTime)
			addTime(1024 * 1193182.0 / _ips);
		if (!_kbdQueue.empty() && iflag && !_pendingIrq) {
			_kbdByte = _kbdQueue.pop();
			irq(9);
		}
		if (_pendingIrq && iflag) {
			_pendingIrq--;
			irq(rw(0, 0x22) != kDefaultSeg ? 8 : 0x1c);
		}
	}
	return kRunBudget;
}

void DosVm::serviceIrqs() {
	while (_pendingIrq && iflag) {
		const uint16 sip = ip, scs = sreg[kCS];
		const uint32 skip = _skipTrap;
		sreg[kCS] = 0xf000;
		ip = 0;
		_pendingIrq--;
		irq(rw(0, 0x22) != kDefaultSeg ? 8 : 0x1c);
		if (sreg[kCS] != 0xf000 || ip != 0) {
			if (run(1000000) != kRunReturn)
				warning("TAD: timer interrupt did not return");
		}
		sreg[kCS] = scs;
		ip = sip;
		_skipTrap = skip;
	}
}

void DosVm::addTime(double clocks) {
	_clock += clocks;
	while (_clock >= _nextIrq) {
		_nextIrq += _pitDiv;
		_biosClock += _pitDiv;
		while (_biosClock >= 0x10000) {
			_biosClock -= 0x10000;
			_biosTicks++;
			ww(0x40, 0x6c, _biosTicks & 0xffff);
			ww(0x40, 0x6e, _biosTicks >> 16);
		}
		_pendingIrq = MIN(32, _pendingIrq + 1);
	}
}

void DosVm::irq(byte n) {
	const uint16 o = rw(0, n * 4), sg = rw(0, n * 4 + 2);
	if (sg == kDefaultSeg) {
		if (n != 8 && n != 0x1c)
			interrupt(n);
		return;
	}
	push(getFlags());
	push(sreg[kCS]);
	push(ip);
	iflag = 0;
	tf = 0;
	sreg[kCS] = sg;
	ip = o;
}

Cpu::ExecResult DosVm::interrupt(byte n) {
	if (_unpacking) {
		const byte ah = r16[kAX] >> 8;
		if (n == 0x21) {
			if (ah == 0x30) {
				_entryReached = true;
				return kExecError;
			}
			if (ah == 0x4a || ah == 0x48)
				cf = 0;
			if (ah == 0x4c)
				return kExecError;
		}
		return kExecOk;
	}
	// a vector the program installed itself runs as code, the default ones are handled here
	if (rw(0, n * 4 + 2) != kDefaultSeg) {
		irq(n);
		return kExecOk;
	}
	hle(n);
	return kExecOk;
}

uint16 DosVm::in(uint16 port, bool word) {
	if (port == 0x3da) {
		// vertical retrace toggles with every read; with frameOnRetrace the start of a retrace
		// ends the run (one picture per host frame)
		_retrace ^= 9;
		if (_retrace && frameOnRetrace)
			_yielded = kYieldFrame;
		return _retrace;
	}
	if (port == 0x40) {
		// a program that keeps reading the counter waits for time to pass: end the run there
		if (++_pitReads > 400 && yieldOnWait) {
			_pitReads = 0;
			_yielded = kYieldWait;
		}
		if (_pitLatch < 0) {
			const double left = MAX(0.0, floor(_nextIrq - _clock));
			_pitLatch = (int)fmod(left, (double)_pitDiv) & 0xffff;
			_pitHi = false;
		}
		const uint16 v = _pitHi ? _pitLatch >> 8 : _pitLatch & 0xff;
		if (_pitHi)
			_pitLatch = -1;
		_pitHi = !_pitHi;
		return v;
	}
	if (port == 0x60)
		return _kbdByte;
	if (port == 0x64)
		return _kbdQueue.empty() ? 0 : 1;
	return 0xff;
}

void DosVm::out(uint16 port, uint16 v, bool word) {
	if (port == 0x3c8) {
		_dacW = (v & 0xff) * 3;
		if (word)
			out(0x3c9, v >> 8, false);
	} else if (port == 0x3c9) {
		_dac[_dacW % 768] = v & 0x3f;
		_dacW = (_dacW + 1) % 768;
	} else if (port == 0x3c7) {
		_dacR = (v & 0xff) * 3;
	} else if (port == 0x43) {
		_pitLo = -1;
		_pitLatch = -1;
	} else if (port == 0x40) {
		if (_pitLo < 0) {
			_pitLo = v & 0xff;
		} else {
			_pitDiv = (((v & 0xff) << 8) | _pitLo);
			if (!_pitDiv)
				_pitDiv = 0x10000;
			_pitLo = -1;
		}
	}
}

void DosVm::biosPut(uint16 code) {
	const uint16 tail = rw(0x40, 0x1c), next = tail + 2 >= 0x3e ? 0x1e : tail + 2;
	if (next == rw(0x40, 0x1a))
		return;                                     // buffer full
	ww(0x40, tail, code);
	ww(0x40, 0x1c, next);
}

void DosVm::hle(byte n) {
	uint16 *R = r16;
	const byte ah = R[kAX] >> 8;
	switch (n) {
	case 0x21:
		hleDos();
		return;
	case 0x33:
		hleMouse();
		return;
	case 0x10:
		hleVideo();
		return;
	case 0x16: {
		const uint16 head = rw(0x40, 0x1a), tail = rw(0x40, 0x1c);
		if (ah == 1 || ah == 0x11) {
			if (head != tail) {
				R[kAX] = rw(0x40, head);
				zf = 0;
			} else {
				zf = 1;
			}
		} else if (ah == 0 || ah == 0x10) {
			if (head != tail) {
				R[kAX] = rw(0x40, head);
				ww(0x40, 0x1a, head + 2 >= 0x3e ? 0x1e : head + 2);
			} else {
				R[kAX] = 0;
			}
		} else if (ah == 2) {
			s8(0, 0);
		}
		return;
	}
	case 0x1a:
		R[kCX] = 0;
		R[kDX] = 0;
		s8(0, 0);
		return;
	case 9: {
		// BIOS keyboard handler: the key into the buffer at 0040:001E
		const byte sc = _kbdByte;
		if (!(sc & 0x80))
			biosPut((sc << 8) | (asciiOf ? asciiOf(sc) : 0));
		return;
	}
	case 0x11:
		R[kAX] = 0x0026;
		return;
	case 0x12:
		R[kAX] = 640;
		return;
	default:
		return;
	}
}

void DosVm::hleDos() {
	uint16 *R = r16;
	const byte ah = R[kAX] >> 8, al = R[kAX] & 0xff;
	auto ok = [&]() { cf = 0; };
	auto okv = [&](uint16 ax) { R[kAX] = ax; cf = 0; };
	auto err = [&](uint16 code) { R[kAX] = code; cf = 1; };
	debug(2, "TAD: INT 21h AX=%04x BX=%04x CX=%04x DX=%04x at %04x:%04x", R[kAX], R[kBX], R[kCX], R[kDX], sreg[kCS], ip);

	switch (ah) {
	case 0x30:
		R[kAX] = 0x0005;
		return;
	case 0x25:
		ww(0, al * 4, R[kDX]);
		ww(0, al * 4 + 2, sreg[kDS]);
		return;
	case 0x35:
		sreg[kES] = rw(0, al * 4 + 2);
		R[kBX] = rw(0, al * 4);
		return;
	case 0x2a: {
		TimeDate t;
		g_system->getTimeAndDate(t);
		R[kCX] = t.tm_year + 1900;
		R[kDX] = ((t.tm_mon + 1) << 8) | t.tm_mday;
		s8(0, t.tm_wday);
		return;
	}
	case 0x2c: {
		TimeDate t;
		g_system->getTimeAndDate(t);
		R[kCX] = (t.tm_hour << 8) | t.tm_min;
		R[kDX] = t.tm_sec << 8;
		return;
	}
	case 0x19:
		s8(0, 2);                                   // drive C:
		return;
	case 0x0e:
		s8(0, 26);
		return;
	case 0x47:
		wb(sreg[kDS], R[kSI], 0);
		okv(0x100);
		return;
	case 0x44:
		if (al == 0)
			R[kDX] = R[kBX] < 3 ? 0x80 : 0;
		ok();
		return;
	case 0x48: {
		const uint16 s = alloc(R[kBX]);
		if (s) {
			okv(s);
		} else {
			R[kBX] = largest();
			err(8);
		}
		return;
	}
	case 0x49:
		_blocks.erase(sreg[kES]);
		ok();
		return;
	case 0x4a:
		if (resize(sreg[kES], R[kBX])) {
			ok();
		} else {
			R[kBX] = maxResize(sreg[kES]);
			err(8);
		}
		return;
	case 0x3d: {
		const Common::String name = fileName(str(sreg[kDS], R[kDX]));
		if (!_files->has(name)) {
			debug(1, "TAD: file missing: %s", name.c_str());
			err(2);
			return;
		}
		Handle h;
		h.name = name;
		h.data = _files->get(name);
		h.size = h.data->size();
		const int id = _nextHandle++;
		_handles[id] = h;
		okv(id);
		return;
	}
	case 0x3c: {
		Handle h;
		h.name = fileName(str(sreg[kDS], R[kDX]));
		h.data = ByteArrayPtr(new ByteArray());
		h.write = true;
		const int id = _nextHandle++;
		_handles[id] = h;
		okv(id);
		return;
	}
	case 0x3e: {
		if (_handles.contains(R[kBX])) {
			Handle &h = _handles[R[kBX]];
			if (h.write) {
				ByteArray d(h.data->data(), h.size);
				_files->put(h.name, d);
			}
			_handles.erase(R[kBX]);
		}
		okv(0);
		return;
	}
	case 0x3f: {
		if (!_handles.contains(R[kBX])) {
			err(6);
			return;
		}
		Handle &h = _handles[R[kBX]];
		const uint32 n = h.pos < h.size ? MIN<uint32>(R[kCX], h.size - h.pos) : 0;
		for (uint32 i = 0; i < n; i++)
			wb(sreg[kDS], R[kDX] + i, (*h.data)[h.pos + i]);
		h.pos += n;
		okv(n);
		return;
	}
	case 0x40: {
		if (R[kBX] < 5) {
			debug(1, "TAD: output: %s", str(sreg[kDS], R[kDX], R[kCX]).c_str());
			okv(R[kCX]);
			return;
		}
		if (!_handles.contains(R[kBX])) {
			err(6);
			return;
		}
		Handle &h = _handles[R[kBX]];
		const uint32 n = R[kCX];
		if (h.data->size() < h.pos + n)
			h.data->resize(h.pos + n);
		for (uint32 i = 0; i < n; i++)
			(*h.data)[h.pos + i] = rb(sreg[kDS], R[kDX] + i);
		h.pos += n;
		h.size = MAX(h.size, h.pos);
		okv(n);
		return;
	}
	case 0x42: {
		if (!_handles.contains(R[kBX])) {
			err(6);
			return;
		}
		Handle &h = _handles[R[kBX]];
		int32 o = (int32)(((uint32)R[kCX] << 16) | R[kDX]);
		if (al == 1)
			o += h.pos;
		else if (al == 2)
			o += h.size;
		h.pos = MAX<int32>(0, o);
		R[kAX] = h.pos & 0xffff;
		R[kDX] = h.pos >> 16;
		cf = 0;
		return;
	}
	case 0x4e: {                                    // find first: exact names only
		const Common::String name = fileName(str(sreg[kDS], R[kDX]));
		if (name.contains('*') || name.contains('?') || !_files->has(name)) {
			err(18);
			return;
		}
		const uint32 size = _files->get(name)->size();
		wb(_dtaSeg, _dtaOff + 0x15, 0x20);
		ww(_dtaSeg, _dtaOff + 0x16, 0);
		ww(_dtaSeg, _dtaOff + 0x18, 0);
		ww(_dtaSeg, _dtaOff + 0x1a, size & 0xffff);
		ww(_dtaSeg, _dtaOff + 0x1c, size >> 16);
		for (uint i = 0; i < 13; i++)
			wb(_dtaSeg, _dtaOff + 0x1e + i, i < name.size() ? name[i] : 0);
		okv(0);
		return;
	}
	case 0x4f:
		err(18);
		return;
	case 0x41: {                                    // delete a file (New Game deletes the savegame)
		const Common::String name = fileName(str(sreg[kDS], R[kDX]));
		if (!_files->has(name)) {
			err(2);
			return;
		}
		_files->remove(name);
		okv(0);
		return;
	}
	case 0x3a: {                                    // rmdir; on a file "access denied" as DOS
		const Common::String name = fileName(str(sreg[kDS], R[kDX]));
		if (_files->has(name))
			err(5);
		else
			okv(0);
		return;
	}
	case 0x39: case 0x3b:
		okv(0);
		return;
	case 0x4c:
		_exitCode = al;
		_yielded = kYieldExit;
		return;
	case 0x09: case 0x02: case 0x06:
		return;
	case 0x1a:
		_dtaSeg = sreg[kDS];
		_dtaOff = R[kDX];
		return;
	case 0x2f:
		sreg[kES] = _dtaSeg;
		R[kBX] = _dtaOff;
		return;
	case 0x33:
		R[kDX] = 0;
		return;
	case 0x34:
		sreg[kES] = kDefaultSeg;                    // InDOS flag (address of a zero byte)
		R[kBX] = 0;
		return;
	case 0x62:
		R[kBX] = _psp;
		return;
	case 0x58:
		okv(0);
		return;
	default:
		debug(1, "TAD: INT 21h AH=%02x not implemented", ah);
		cf = 0;
		return;
	}
}

void DosVm::hleMouse() {
	uint16 *R = r16;
	switch (R[kAX]) {
	case 0:
		R[kAX] = 0xffff;
		R[kBX] = 2;
		return;
	case 3:
		R[kBX] = mouse.buttons;
		R[kCX] = mouse.x * 2;
		R[kDX] = mouse.y;
		return;
	case 5: case 6: {
		const int was = mouse.pressed;
		mouse.pressed = 0;
		R[kBX] = was;
		R[kAX] = mouse.buttons;
		R[kCX] = mouse.x * 2;
		R[kDX] = mouse.y;
		return;
	}
	case 0x0b:
		// motion counters: the engine accelerates mickeys itself; instead the target position
		// is written into its cursor variables (x, y, moved flags)
		R[kCX] = 0;
		R[kDX] = 0;
		if (mouseVars.ds && (rw(mouseVars.ds, mouseVars.x) != mouse.x || rw(mouseVars.ds, mouseVars.y) != mouse.y)) {
			ww(mouseVars.ds, mouseVars.x, mouse.x);
			ww(mouseVars.ds, mouseVars.y, mouse.y);
			ww(mouseVars.ds, mouseVars.flags, rw(mouseVars.ds, mouseVars.flags) | 3);
		}
		return;
	default:
		return;
	}
}

void DosVm::hleVideo() {
	uint16 *R = r16;
	const byte ah = R[kAX] >> 8;
	if (R[kAX] == 0x1012) {
		for (uint i = 0; i < (uint)R[kCX] * 3; i++)
			_dac[(R[kBX] * 3 + i) % 768] = rb(sreg[kES], R[kDX] + i) & 0x3f;
	} else if (R[kAX] == 0x1010) {
		// a single register: nothing (as the reference)
	} else if (R[kAX] == 0x1a00) {
		R[kAX] = 0x001a;
		R[kBX] = 0x0008;
	} else if (ah == 0x12 && (R[kBX] & 0xff) == 0x10) {
		R[kBX] = 0x0003;
		R[kCX] = 0x0009;
	} else if (ah == 0x0f) {
		R[kAX] = (0x28 << 8) | _videoMode;
	} else if (ah == 0x00) {
		_videoMode = R[kAX] & 0x7f;
	} else if (ah == 0x1b) {                        // state information (VGA, 256 colours)
		const uint16 es = sreg[kES], di = R[kDI];
		for (uint i = 0; i < 64; i++)
			wb(es, di + i, 0);
		ww(es, di, 0xe000);
		ww(es, di + 2, 0xf000);                     // static functionality table
		for (uint i = 0; i < 16; i++)
			wb(0xf000, 0xe000 + i, 0xff);
		wb(es, di + 0x2d, 0x32);
		wb(es, di + 4, _videoMode);
		ww(es, di + 5, 40);
		wb(es, di + 0x22, 25);
		wb(es, di + 0x25, 8);
		ww(es, di + 0x27, 256);
		wb(es, di + 0x29, 1);
		s8(0, 0x1b);
	}
}

uint16 DosVm::alloc(uint16 paras) {
	// first fit above the program block
	uint32 cand = _progEnd;
	Common::Array<uint16> segs;
	for (Common::HashMap<uint16, uint16>::const_iterator it = _blocks.begin(); it != _blocks.end(); ++it)
		segs.push_back(it->_key);
	Common::sort(segs.begin(), segs.end());
	for (uint i = 0; i < segs.size(); i++) {
		const uint16 bs = segs[i];
		if (bs < _progEnd)
			continue;
		if (cand + paras <= bs)
			break;
		cand = MAX<uint32>(cand, bs + _blocks[bs]);
	}
	if (cand + paras > _memTop)
		return 0;
	_blocks[cand] = paras;
	return cand;
}

uint16 DosVm::largest() const {
	uint32 cand = _progEnd, best = 0;
	Common::Array<uint16> segs;
	for (Common::HashMap<uint16, uint16>::const_iterator it = _blocks.begin(); it != _blocks.end(); ++it)
		segs.push_back(it->_key);
	Common::sort(segs.begin(), segs.end());
	for (uint i = 0; i < segs.size(); i++) {
		const uint16 bs = segs[i];
		if (bs < _progEnd)
			continue;
		if (bs > cand)
			best = MAX<uint32>(best, bs - cand);
		cand = MAX<uint32>(cand, bs + _blocks.getValOrDefault(bs));
	}
	return MAX<uint32>(best, _memTop > cand ? _memTop - cand : 0);
}

uint16 DosVm::maxResize(uint16 seg) const {
	uint16 next = _memTop;
	for (Common::HashMap<uint16, uint16>::const_iterator it = _blocks.begin(); it != _blocks.end(); ++it)
		if (it->_key > seg && it->_key < next)
			next = it->_key;
	return next - seg;
}

bool DosVm::resize(uint16 seg, uint16 paras) {
	if (!_blocks.contains(seg) && seg != _psp)
		return false;
	if (paras > maxResize(seg))
		return false;
	_blocks[seg] = paras;
	if (seg == _psp)
		_progEnd = seg + paras;
	return true;
}

} // End of namespace Tad
