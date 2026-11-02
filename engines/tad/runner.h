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

#ifndef TAD_RUNNER_H
#define TAD_RUNNER_H

#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"

#include "tad/dosvm.h"

namespace Tad {

/** How one program of a second-generation game runs (from the web engine's game profiles) */
struct ProgramDesc {
	const char *exe;
	uint16 frameSeg, frameOff;    ///< input function at the end of a frame (0: frames at the vertical retrace)
	bool stopOnError;             ///< an exit code other than 0 ends the game (title program: quit)
	DosVm::MouseVars mouse;
};

/** Files of the game folder plus the files the programs write (savegames in the save folder) */
class GameFiles : public FileProvider {
public:
	explicit GameFiles(const Common::String &target) : _target(target), _keepWrites(true) {}
	/** Tests: what the programs write is dropped (as in the reference runs) */
	void setKeepWrites(bool keep) { _keepWrites = keep; }
	bool has(const Common::String &name) override;
	ByteArrayPtr get(const Common::String &name) override;
	void put(const Common::String &name, const ByteArray &data) override;
	void remove(const Common::String &name) override;

private:
	bool isSaveFile(const Common::String &name) const;
	Common::String saveName(const Common::String &name) const;

	Common::String _target;
	bool _keepWrites;
	Common::HashMap<Common::String, ByteArrayPtr, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _cache;
};

class ScriptRuntime;
struct ScriptFunction;
struct PrimAddress;
class ProgramVm;

/** What happens between two frames: picture, input and time (the game, or a test) */
class FrameHost {
public:
	virtual ~FrameHost() {}
	/** Before frame n runs: input and the time that passed */
	virtual void frameStart(ProgramVm &vm, int n) = 0;
	/** Frame n is complete; false stops the program */
	virtual bool frameDone(ProgramVm &vm, int n) = 0;
};

/**
 * One run of an original program with the traps of the runner (frame end, sound driver) and,
 * optionally, its translated functions. The program runs until it ends; whenever it ends a
 * frame (also inside translated code) the host shows the picture and gives input and time.
 */
class ProgramVm : public DosVm {
public:
	enum {
		kTrapFrame = 1,
		kTrapSoundOk,
		kTrapSoundStub,
		kTrapScript,
		kTrapPrim         ///< kTrapPrim + n: entry n of the routines in C++
	};

	ProgramVm(FileProvider *files, const ProgramDesc &desc, FrameHost *host);
	~ProgramVm() override;
	bool start(const ByteArray &exe);
	/** Translated functions that replace the original code (data segment ds) */
	void installScript(const ScriptFunction *list, uint16 ds);
	uint scriptCount() const;
	/** Assembler routines done in C++ that replace the original code */
	void installPrims(const PrimAddress *list);
	uint primCount() const;

	/** Runs the program until it ends; returns false on an error */
	bool execute();
	/** Ends the current frame (picture, input, time) and starts the next one */
	void endFrame();
	/** Runs original code until the call made by translated code returns (to f000:0000 or seg:FFFE) */
	void runToReturn(uint16 seg);
	/** Start and end of a translated function: as the reference, the frame ends here when its
	 *  budget (8086 steps plus 8 per translated block) is used up */
	void checkBudget();

	int frame() const { return _frame; }
	uint32 budget;

protected:
	TrapResult onTrap(int id) override;

private:
	void installSound();
	void startFrame();
	uint32 remaining() const;

	ProgramDesc _desc;
	FrameHost *_host;
	ScriptRuntime *_script;
	const PrimAddress *_prims;
	int _frame;
	uint32 _frameSteps;
	uint32 _frameBlocks;
	bool _error;
};

} // End of namespace Tad

#endif // TAD_RUNNER_H
