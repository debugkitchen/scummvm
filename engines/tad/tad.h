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

#ifndef TAD_H
#define TAD_H

#include "common/array.h"
#include "common/error.h"
#include "common/random.h"
#include "common/scummsys.h"
#include "common/system.h"
#include "engines/engine.h"

#include "tad/detection.h"
#include "tad/graphics.h"
#include "tad/resource.h"
#include "tad/room.h"

namespace Tad {

/**
 * Generations of the engine: the first one of "Das Telekommando kehrt
 * zurueck" (one data file per room) and the second one of Bi-Fi 2 and
 * Dunkle Schatten (all rooms in one archive, game logic in the program).
 */
enum EngineVersion {
	kVersion1 = 1,
	kVersion2 = 2
};

class Sound;

class TadEngine : public Engine {
public:
	TadEngine(OSystem *syst, const ADGameDescription *gameDesc);
	~TadEngine() override;

	uint32 getFeatures() const;
	Common::String getGameId() const;
	EngineVersion getVersion() const { return _version; }

	uint32 getRandomNumber(uint maxNum) {
		return _randomSource.getRandomNumber(maxNum);
	}

	bool hasFeature(EngineFeature f) const override {
		return f == kSupportsReturnToLauncher;
	}

	Resources &resources() { return _resources; }

	/** Numbers of the rooms that have graphics */
	const Common::Array<uint> &roomNumbers() const { return _roomNumbers; }
	/** Loads the graphics of a room; false if it has none */
	bool loadRoom(uint n, Room &room);
	/** Draws a room into a canvas with the interface palette and the room palette */
	bool renderRoom(uint n, Canvas &canvas);
	/** Writes a picture of every room as BMP files into the folder (for tests) */
	void dumpRooms(const Common::Path &folder);

protected:
	Common::Error run() override;

private:
	Common::Error runRoomViewer();
	/** Second generation: the original programs in the interpreter (title program, then the game) */
	Common::Error runVersion2();
	Sound *createSound();
	/** Test mode of runVersion2: fixed input and time, a hash of every picture into a file */
	Common::Error runVersion2Test();
	void initPalette();
	Span roomGraphics(uint n);
	void showRoom(uint index);

	const ADGameDescription *_gameDescription;
	Common::RandomSource _randomSource;
	EngineVersion _version;
	Resources _resources;
	Common::Array<uint> _roomNumbers;
	byte _basePalette[256 * 3];
	Canvas _canvas;
};

extern TadEngine *g_engine;
#define SHOULD_QUIT ::Tad::g_engine->shouldQuit()

} // End of namespace Tad

#endif // TAD_H
