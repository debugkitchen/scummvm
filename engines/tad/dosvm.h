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

#ifndef TAD_DOSVM_H
#define TAD_DOSVM_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/queue.h"
#include "common/str.h"

#include "tad/cpu.h"
#include "tad/resource.h"

namespace Tad {

/** The files a program sees: the game folder plus files written by the program */
class FileProvider {
public:
	virtual ~FileProvider() {}
	virtual bool has(const Common::String &name) = 0;
	virtual ByteArrayPtr get(const Common::String &name) = 0;
	/** A file the program wrote (closed) */
	virtual void put(const Common::String &name, const ByteArray &data) = 0;
	virtual void remove(const Common::String &name) = 0;
};

/**
 * Runs an original program of the second engine generation in the 8086
 * interpreter with a small high-level DOS: files, memory blocks, mouse,
 * keyboard, the timer and the VGA ports. Functions can be replaced or watched
 * through traps (onTrap).
 */
class DosVm : public Cpu {
public:
	enum RunResult {
		kRunReturn,       ///< the far call of call() returned
		kRunYield,        ///< a trap or the program asked to end the frame (yielded())
		kRunStop,         ///< a trap stopped the run
		kRunBudget,       ///< the instruction budget is used up
		kRunError         ///< invalid instruction or other end of the program
	};
	enum Yield {
		kYieldNone,
		kYieldFrame,      ///< the picture is complete
		kYieldWait,       ///< the program waits for time to pass
		kYieldExit        ///< the program ended (exitCode())
	};
	enum TrapResult {
		kTrapContinue,    ///< the trap replaced the function (it returned with retf)
		kTrapBefore,      ///< end the run here; the function runs on the next run()
		kTrapPass,        ///< observer: the function runs normally
		kTrapStop         ///< stop the run after the replaced function
	};

	DosVm(FileProvider *files);

	/** Loads the MZ program and runs its unpacking stub up to the C startup */
	bool load(const byte *exe, uint32 size);

	RunResult run(uint32 maxSteps);
	/** A new frame: the wait detection of the timer counter starts again */
	void resetPitReads() { _pitReads = 0; }
	uint pitReads() const { return _pitReads; }
	/** Port access for routines that replace original code (same effects as IN and OUT) */
	byte inPort(uint16 port) { return in(port, false) & 0xff; }
	void outPort(uint16 port, byte v) { out(port, v, false); }
	/** Delivers pending timer interrupts now */
	void serviceIrqs();
	/** Host time in milliseconds (once per displayed frame) */
	void elapse(double ms) { addTime(ms * 1193.182); }
	/** Key press (scan code) or release (scan code | 0x80) */
	void keyEvent(byte code) { _kbdQueue.push(code); }

	/** Registers a trap at seg:off with an id that is passed to onTrap() */
	void setTrap(uint16 seg, uint16 off, int id) { _traps[((uint32)seg << 16) | off] = id; }
	uint16 arg(int i) const { return rw(sreg[kSS], r16[kSP] + 4 + 2 * i); }
	void retf(uint16 ax, uint16 dx) {
		r16[kAX] = ax;
		r16[kDX] = dx;
		ip = pop();
		sreg[kCS] = pop();
	}

	Yield yielded() const { return _yielded; }
	void setYield(Yield y) { _yielded = y; }
	int exitCode() const { return _exitCode; }
	const byte *dac() const { return _dac; }

	struct MouseState {
		int x = 160;
		int y = 100;
		int buttons = 0;
		int pressed = 0;
	} mouse;

	/** Cursor variables of the program (DS, x, y, moved flags), set from the game profile */
	struct MouseVars {
		uint16 ds, x, y, flags;
	} mouseVars;

	/** Return address of the innermost near call from translated code (seg:FFFE) */
	uint32 nearRet = 0xffffffff;
	/** The program is being stopped: run() returns at once */
	bool stopping() const { return _stopping; }
	void stop() { _stopping = true; }

	bool frameOnRetrace = false;
	bool yieldOnWait = true;
	bool noStepTime = false;
	bool keepPit = false;
	byte (*asciiOf)(byte scanCode) = nullptr;

protected:
	virtual TrapResult onTrap(int id) { return kTrapPass; }

	ExecResult interrupt(byte n) override;
	uint16 in(uint16 port, bool word) override;
	void out(uint16 port, uint16 v, bool word) override;

private:
	void addTime(double clocks);
	void irq(byte n);
	void hle(byte n);
	void hleDos();
	void hleMouse();
	void hleVideo();
	void biosPut(uint16 code);
	Common::String str(uint16 seg, uint16 off, uint max = 260) const;
	static Common::String fileName(const Common::String &path);

	uint16 alloc(uint16 paras);
	uint16 largest() const;
	uint16 maxResize(uint16 seg) const;
	bool resize(uint16 seg, uint16 paras);

	struct Handle {
		Common::String name;
		ByteArrayPtr data;
		uint32 size = 0;
		uint32 pos = 0;
		bool write = false;
	};

	FileProvider *_files;
	bool _stopping = false;
	Common::HashMap<uint32, int> _traps;
	uint32 _skipTrap;
	Yield _yielded;
	int _exitCode;
	bool _unpacking;
	bool _entryReached;

	Common::HashMap<int, Handle> _handles;
	int _nextHandle;
	Common::HashMap<uint16, uint16> _blocks;
	uint16 _memTop, _progEnd, _psp;
	uint16 _dtaSeg, _dtaOff;

	byte _dac[768];
	uint _dacW, _dacR;
	byte _retrace;
	byte _videoMode;

	uint32 _pitDiv;
	int _pitLo;
	int _pitLatch;
	bool _pitHi;
	uint _pitReads;
	double _ips, _clock, _nextIrq, _biosClock;
	uint32 _biosTicks;
	int _pendingIrq;

	Common::Queue<byte> _kbdQueue;
	byte _kbdByte;
};

} // End of namespace Tad

#endif // TAD_DOSVM_H
