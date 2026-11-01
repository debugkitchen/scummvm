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

#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/system.h"
#include "engines/util.h"
#include "graphics/framelimiter.h"
#include "graphics/paletteman.h"
#include "graphics/surface.h"
#include "image/bmp.h"

#include "tad/console.h"
#include "tad/tad.h"

namespace Tad {

TadEngine *g_engine;

/** Height of the room area above the interface panel (the same in all games) */
static const int kRoomHeight = 128;

TadEngine::TadEngine(OSystem *syst, const ADGameDescription *gameDesc) : Engine(syst),
	_gameDescription(gameDesc), _randomSource("Tad") {
	g_engine = this;
	_version = !strcmp(gameDesc->gameId, "telekommando2") ? kVersion1 : kVersion2;
	memset(_basePalette, 0, sizeof(_basePalette));

	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDataDir, "resource");
}

TadEngine::~TadEngine() {
}

uint32 TadEngine::getFeatures() const {
	return _gameDescription->flags;
}

Common::String TadEngine::getGameId() const {
	return _gameDescription->gameId;
}

/** The room archive of the second generation */
static const char *roomArchive(const Common::String &gameId) {
	return gameId == "bifi2" ? "BFROOM.DAT" : "DSROOM.DAT";
}

/** An empty member of the second generation reads "NULLPTR" (with line end) */
static bool isNull(const Span &m) {
	return m.size <= 9;
}

Span TadEngine::roomGraphics(uint n) {
	if (_version == kVersion1) {
		const Common::String name = Common::String::format("ROOM%02u.DAT", n);
		if (!_resources.exists(name))
			return Span();
		const Common::Array<Span> &members = _resources.group(name);
		return members.empty() ? Span() : members[0];
	}

	// archive member n: a RIB-packed group of [walk graph, animation scripts, ?,
	// texts and dialogs, hotspots, graphics]; some rooms share the graphics of another
	const Common::Array<Span> &archive = _resources.group(roomArchive(getGameId()));
	for (int pass = 0; pass < 2; pass++) {
		uint m = n;
		if (pass == 1) {
			if (getGameId() != "bifi2")
				break;
			static const uint shared[][2] = {
				{ 18, 37 }, { 19, 37 }, { 30, 37 }, { 31, 37 }, { 32, 37 },
				{ 33, 37 }, { 34, 37 }, { 35, 37 }, { 36, 39 }
			};
			m = 0;
			for (uint i = 0; i < ARRAYSIZE(shared); i++)
				if (shared[i][0] == n)
					m = shared[i][1];
			if (!m)
				break;
		}
		if (m >= archive.size() || isNull(archive[m]))
			continue;
		Span packed;
		packed.owner = loadRib(archive[m]);
		packed.size = packed.owner->size();
		const Common::Array<Span> members = fgroup(packed);
		if (members.size() > 5 && !isNull(members[5]))
			return members[5];
	}
	return Span();
}

bool TadEngine::loadRoom(uint n, Room &room) {
	const Span gfx = roomGraphics(n);
	return !gfx.empty() && room.load(gfx);
}

void TadEngine::initPalette() {
	// interface palette: PANELS member 1, then the palettes of CMBMOU
	Canvas c;
	const Common::Array<Span> &panels = _resources.group("PANELS.DAT");
	if (panels.size() > 1)
		c.applyPalette(decodePalette(panels[1]));
	const Common::Array<Span> &cmbmou = _resources.group("CMBMOU.DAT");
	static const uint v1Palettes[] = { 2, 0 };
	static const uint v2Palettes[] = { 0 };
	const uint *list = _version == kVersion1 ? v1Palettes : v2Palettes;
	const uint count = _version == kVersion1 ? ARRAYSIZE(v1Palettes) : ARRAYSIZE(v2Palettes);
	for (uint i = 0; i < count; i++)
		if (list[i] < cmbmou.size() && !cmbmou[list[i]].empty())
			c.applyPalette(decodePalette(cmbmou[list[i]]));
	memcpy(_basePalette, c.palette, sizeof(_basePalette));
}

bool TadEngine::renderRoom(uint n, Canvas &canvas) {
	Room room;
	if (!loadRoom(n, room))
		return false;
	// the second generation uses colour 0x61 as black outside the room
	canvas.clear(_version == kVersion1 ? 0 : 0x61);
	memcpy(canvas.palette, _basePalette, sizeof(_basePalette));
	canvas.applyPalette(room.palette);
	room.draw(canvas, kRoomHeight);
	return true;
}

void TadEngine::dumpRooms(const Common::Path &folder) {
	Graphics::Surface s;
	s.create(kScreenWidth, kScreenHeight, Graphics::PixelFormat::createFormatCLUT8());
	for (uint i = 0; i < _roomNumbers.size(); i++) {
		Canvas c;
		if (!renderRoom(_roomNumbers[i], c))
			continue;
		memcpy(s.getPixels(), c.pixels, sizeof(c.pixels));
		Common::DumpFile out;
		const Common::Path p = folder.appendComponent(Common::String::format("room%02u.bmp", _roomNumbers[i]));
		if (out.open(p, true))
			Image::writeBMP(out, s, c.palette);
	}
	s.free();
}

void TadEngine::showRoom(uint index) {
	if (_roomNumbers.empty())
		return;
	const uint n = _roomNumbers[index % _roomNumbers.size()];
	renderRoom(n, _canvas);
	g_system->getPaletteManager()->setPalette(_canvas.palette, 0, 256);
	g_system->copyRectToScreen(_canvas.pixels, kScreenWidth, 0, 0, kScreenWidth, kScreenHeight);
	g_system->updateScreen();
	debug(1, "TAD: room %u", n);
}

Common::Error TadEngine::run() {
	initGraphics(kScreenWidth, kScreenHeight);
	setDebugger(new Console());

	initPalette();
	const uint maxRoom = _version == kVersion1 ? 40 : 100;
	for (uint n = 0; n < maxRoom; n++) {
		Room room;
		if (loadRoom(n, room))
			_roomNumbers.push_back(n);
	}
	if (_roomNumbers.empty())
		return Common::Error(Common::kNoGameDataFoundError, "No rooms found");

	// tests: write a picture of every room and end
	if (ConfMan.hasKey("tad_dump_rooms")) {
		dumpRooms(ConfMan.getPath("tad_dump_rooms"));
		return Common::kNoError;
	}

	// room viewer until the game logic follows: arrow keys or a click change the room
	uint index = 0;
	showRoom(index);
	Graphics::FrameLimiter limiter(g_system, 60);
	while (!shouldQuit()) {
		Common::Event e;
		while (g_system->getEventManager()->pollEvent(e)) {
			if (e.type == Common::EVENT_LBUTTONDOWN ||
			    (e.type == Common::EVENT_KEYDOWN && e.kbd.keycode == Common::KEYCODE_RIGHT)) {
				index = (index + 1) % _roomNumbers.size();
				showRoom(index);
			} else if (e.type == Common::EVENT_RBUTTONDOWN ||
			           (e.type == Common::EVENT_KEYDOWN && e.kbd.keycode == Common::KEYCODE_LEFT)) {
				index = (index + _roomNumbers.size() - 1) % _roomNumbers.size();
				showRoom(index);
			}
		}
		limiter.delayBeforeSwap();
		g_system->updateScreen();
		limiter.startFrame();
	}
	return Common::kNoError;
}

} // End of namespace Tad
