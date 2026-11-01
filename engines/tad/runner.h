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

/** One run of an original program with the traps of the runner (frame end, sound driver) */
class ProgramVm : public DosVm {
public:
	ProgramVm(FileProvider *files, const ProgramDesc &desc);
	bool start(const ByteArray &exe);

	/** One display frame: pending interrupts, then the program until it ends the frame */
	RunResult runFrame(uint32 budget);

protected:
	TrapResult onTrap(int id) override;

private:
	enum {
		kTrapFrame = 1,
		kTrapSoundOk,
		kTrapSoundStub
	};
	void installSound();
	ProgramDesc _desc;
};

} // End of namespace Tad

#endif // TAD_RUNNER_H
