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

#include "common/md5.h"
#include "common/memstream.h"
#include "common/savefile.h"

#include "tad/console.h"
#include "tad/runner.h"
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

	if (_version == kVersion2 && !ConfMan.hasKey("tad_dump_rooms") && !ConfMan.getBool("tad_room_viewer")) {
		if (ConfMan.hasKey("tad_test"))
			return runVersion2Test();
		return runVersion2();
	}
	return runRoomViewer();
}

Common::Error TadEngine::runRoomViewer() {
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

// ---------------------------------------------------------------------------- second generation

static const ProgramDesc kBifi2Programs[] = {
	{ "XP00.EXE", 0x187c, 0x01b0, true, { 0x1dd7, 0x270, 0x272, 0x180 } },
	{ "XP01.EXE", 0x2819, 0x01b0, false, { 0x350a, 0x486, 0x488, 0x396 } }
};
static const ProgramDesc kSchattenPrograms[] = {
	// the title program ends its frames at the vertical retrace
	{ "XP00.EXE", 0, 0, true, { 0x1e3a, 0x27a, 0x27c, 0x18a } },
	{ "XP01.EXE", 0x279e, 0x01b0, false, { 0x31ef, 0x3e6, 0x3e8, 0x2f6 } }
};

/** Scan code of a key (German keyboard as in the original); 0 for keys the games do not use */
static byte scanCode(const Common::KeyState &k) {
	switch (k.keycode) {
	case Common::KEYCODE_ESCAPE: return 0x01;
	case Common::KEYCODE_RETURN: case Common::KEYCODE_KP_ENTER: return 0x1c;
	case Common::KEYCODE_SPACE: return 0x39;
	case Common::KEYCODE_BACKSPACE: return 0x0e;
	case Common::KEYCODE_TAB: return 0x0f;
	case Common::KEYCODE_UP: return 0x48;
	case Common::KEYCODE_DOWN: return 0x50;
	case Common::KEYCODE_LEFT: return 0x4b;
	case Common::KEYCODE_RIGHT: return 0x4d;
	case Common::KEYCODE_LSHIFT: case Common::KEYCODE_RSHIFT: return 0x2a;
	case Common::KEYCODE_LCTRL: case Common::KEYCODE_RCTRL: return 0x1d;
	case Common::KEYCODE_LALT: case Common::KEYCODE_RALT: return 0x38;
	default: break;
	}
	if (k.keycode >= Common::KEYCODE_F1 && k.keycode <= Common::KEYCODE_F10)
		return 0x3b + (k.keycode - Common::KEYCODE_F1);
	const char c = (char)tolower(k.ascii);
	if (c >= '1' && c <= '9')
		return 2 + (c - '1');
	if (c == '0')
		return 0x0b;
	static const char *const rows[] = { "qwertzuiop", "asdfghjkl", "yxcvbnm" };
	static const byte rowStart[] = { 0x10, 0x1e, 0x2c };
	for (uint r = 0; r < 3; r++) {
		const char *p = c ? strchr(rows[r], c) : nullptr;
		if (p)
			return rowStart[r] + (p - rows[r]);
	}
	if (c == '-')
		return 0x35;
	if (c == '.')
		return 0x34;
	if (c == ',')
		return 0x33;
	return 0;
}

// ASCII code of the last key pressed for a scan code (the BIOS keyboard handler needs it)
static byte g_asciiOfScan[128];
static byte asciiOfScan(byte sc) {
	return g_asciiOfScan[sc & 0x7f];
}

static void presentVm(const DosVm &vm) {
	byte pal[768];
	for (uint i = 0; i < 768; i++)
		pal[i] = vm.dac()[i] * 255 / 63;
	g_system->getPaletteManager()->setPalette(pal, 0, 256);
	g_system->copyRectToScreen(vm.mem() + 0xa0000, kScreenWidth, 0, 0, kScreenWidth, kScreenHeight);
	g_system->updateScreen();
}

Common::Error TadEngine::runVersion2() {
	const ProgramDesc *programs = getGameId() == "bifi2" ? kBifi2Programs : kSchattenPrograms;
	GameFiles files(_targetName);
	memset(g_asciiOfScan, 0, sizeof(g_asciiOfScan));

	for (uint p = files.has("XP00.EXE") ? 0 : 1; p < 2 && !shouldQuit(); p++) {
		ProgramVm vm(&files, programs[p]);
		vm.asciiOf = asciiOfScan;
		if (!vm.start(*files.get(programs[p].exe)))
			return Common::Error(Common::kUnknownError, Common::String::format("Could not start %s", programs[p].exe));
		debug(1, "TAD: %s started", programs[p].exe);

		Common::Array<byte> held, releases;
		uint32 last = g_system->getMillis();
		Graphics::FrameLimiter limiter(g_system, 60);
		bool running = true;
		while (running && !shouldQuit()) {
			// a release reaches the program only after it ran a frame with the key held
			for (uint i = 0; i < releases.size(); i++) {
				vm.keyEvent(releases[i] | 0x80);
				for (uint j = 0; j < held.size(); j++)
					if (held[j] == releases[i]) {
						held.remove_at(j);
						break;
					}
			}
			releases.clear();

			Common::Event e;
			while (g_system->getEventManager()->pollEvent(e)) {
				switch (e.type) {
				case Common::EVENT_MOUSEMOVE:
					vm.mouse.x = CLIP<int>(e.mouse.x, 0, kScreenWidth - 1);
					vm.mouse.y = CLIP<int>(e.mouse.y, 0, kScreenHeight - 1);
					break;
				case Common::EVENT_LBUTTONDOWN:
				case Common::EVENT_RBUTTONDOWN:
					vm.mouse.buttons |= e.type == Common::EVENT_LBUTTONDOWN ? 1 : 2;
					vm.mouse.pressed = 1;
					break;
				case Common::EVENT_LBUTTONUP:
					vm.mouse.buttons &= ~1;
					break;
				case Common::EVENT_RBUTTONUP:
					vm.mouse.buttons &= ~2;
					break;
				case Common::EVENT_KEYDOWN: {
					const byte sc = scanCode(e.kbd);
					if (!sc)
						break;
					g_asciiOfScan[sc] = e.kbd.ascii < 0x100 ? (byte)e.kbd.ascii : 0;
					bool isHeld = false;
					for (uint j = 0; j < held.size(); j++)
						isHeld |= held[j] == sc;
					if (!isHeld) {
						held.push_back(sc);
						vm.keyEvent(sc);
					}
					break;
				}
				case Common::EVENT_KEYUP: {
					const byte sc = scanCode(e.kbd);
					if (sc)
						releases.push_back(sc);
					break;
				}
				default:
					break;
				}
			}

			const uint32 now = g_system->getMillis();
			vm.elapse(MIN<uint32>(100, now - last));
			last = now;
			const DosVm::RunResult r = vm.runFrame(3000000);
			if (r == DosVm::kRunError)
				return Common::Error(Common::kUnknownError, Common::String::format("%s stopped at %04x:%04x", programs[p].exe, vm.sreg[kCS], vm.ip));
			if (r == DosVm::kRunYield && vm.yielded() == DosVm::kYieldExit) {
				debug(1, "TAD: %s ended (%d)", programs[p].exe, vm.exitCode());
				if (vm.exitCode() && programs[p].stopOnError)
					return Common::kNoError;
				running = false;
			}
			presentVm(vm);
			limiter.delayBeforeSwap();
			limiter.startFrame();
		}
	}
	return Common::kNoError;
}

Common::Error TadEngine::runVersion2Test() {
	// tad_test = <program index 0/1>, tad_test_input = file with lines "frame x y buttons" or
	// "frame w seg off value" (poke), tad_test_frames = count, tad_test_output = file for the hashes
	const ProgramDesc *programs = getGameId() == "bifi2" ? kBifi2Programs : kSchattenPrograms;
	const uint p = ConfMan.getInt("tad_test");
	const int frames = ConfMan.getInt("tad_test_frames");
	const uint32 budget = ConfMan.hasKey("tad_test_budget") ? ConfMan.getInt("tad_test_budget") : 4000000;
	GameFiles files(_targetName);
	files.setKeepWrites(false);
	ProgramVm vm(&files, programs[p]);
	if (!vm.start(*files.get(programs[p].exe)))
		return Common::Error(Common::kUnknownError, "start failed");

	struct Input {
		int frame;
		bool poke;
		uint a, b, c;
	};
	Common::Array<Input> input;
	if (ConfMan.hasKey("tad_test_input")) {
		Common::File f;
		if (f.open(Common::FSNode(ConfMan.getPath("tad_test_input")))) {
			while (!f.eos()) {
				const Common::String line = f.readLine();
				Input in;
				if (sscanf(line.c_str(), "%d w %x %x %x", &in.frame, &in.a, &in.b, &in.c) == 4) {
					in.poke = true;
					input.push_back(in);
				} else if (sscanf(line.c_str(), "%d %u %u %u", &in.frame, &in.a, &in.b, &in.c) == 4) {
					in.poke = false;
					input.push_back(in);
				}
			}
		}
	}

	Common::String out;
	for (int f = 0; f < frames; f++) {
		for (uint i = 0; i < input.size(); i++) {
			const Input &in = input[i];
			if (in.frame != f)
				continue;
			if (in.poke) {
				vm.ww(in.a, in.b, in.c);
			} else {
				vm.mouse.x = in.a;
				vm.mouse.y = in.b;
				vm.mouse.buttons = in.c;
				if (in.c)
					vm.mouse.pressed = 1;
			}
		}
		if (f == 10)
			vm.noStepTime = true;
		vm.elapse(40);
		const DosVm::RunResult r = vm.runFrame(budget);
		if (r == DosVm::kRunError) {
			out += Common::String::format("error %04x:%04x\n", vm.sreg[kCS], vm.ip);
			break;
		}
		if (ConfMan.hasKey("tad_test_dump") && ConfMan.getInt("tad_test_dump") == f) {
			Common::DumpFile d;
			if (d.open(Common::FSNode(ConfMan.getPath("tad_test_output")).getParent().getChild("mem.bin")))
				d.write(vm.mem(), 0xa0000);
		}
		byte buf[64000 + 768];
		memcpy(buf, vm.mem() + 0xa0000, 64000);
		memcpy(buf + 64000, vm.dac(), 768);
		Common::MemoryReadStream s(buf, sizeof(buf));
		out += Common::computeStreamMD5AsString(s).substr(0, 10) +
			Common::String::format(" %u %04x:%04x %d\n", vm.steps, vm.sreg[kCS], vm.ip, (int)r);
		if (r == DosVm::kRunYield && vm.yielded() == DosVm::kYieldExit) {
			out += Common::String::format("exit %d\n", vm.exitCode());
			break;
		}
	}
	Common::DumpFile d;
	if (d.open(Common::FSNode(ConfMan.getPath("tad_test_output"))))
		d.writeString(out);
	return Common::kNoError;
}

} // End of namespace Tad
