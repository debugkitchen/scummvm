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
#include "common/file.h"
#include "common/savefile.h"
#include "common/system.h"

#include "tad/runner.h"

namespace Tad {

// default sound configuration (written by PSMCFG4.EXE): music AdLib 388h, samples SB 220h
static const byte kPsmCfg[] = {
	'P', 'S', 'M', '2', 'C', 'F', 'G', ',', ' ', 'V', 'E', 'R', 'S', 'I', 'O', 'N', ' ', '4', '.', '0', '0',
	0, 1, 0, 0x88, 3, 1, 0, 0x20, 2, 0x28, 0, 1, 0, 2, 0
};

bool GameFiles::isSaveFile(const Common::String &name) const {
	return name.hasPrefixIgnoreCase("SAVEGAME.");
}

Common::String GameFiles::saveName(const Common::String &name) const {
	return _target + "." + name;
}

bool GameFiles::has(const Common::String &name) {
	if (isSaveFile(name)) {
		Common::InSaveFile *f = g_system->getSavefileManager()->openForLoading(saveName(name));
		const bool ok = f != nullptr;
		delete f;
		return ok;
	}
	if (name.equalsIgnoreCase("PSMCFG4.DAT"))
		return true;
	return Common::File::exists(Common::Path(name));
}

ByteArrayPtr GameFiles::get(const Common::String &name) {
	ByteArrayPtr data(new ByteArray());
	if (isSaveFile(name)) {
		Common::InSaveFile *f = g_system->getSavefileManager()->openForLoading(saveName(name));
		if (f) {
			data->resize(f->size());
			f->read(data->data(), data->size());
			delete f;
		}
		return data;
	}
	if (_cache.contains(name))
		return _cache[name];
	Common::File f;
	if (f.open(Common::Path(name))) {
		data->resize(f.size());
		f.read(data->data(), data->size());
	} else if (name.equalsIgnoreCase("PSMCFG4.DAT")) {
		data->assign(kPsmCfg, kPsmCfg + sizeof(kPsmCfg));
	}
	_cache[name] = data;
	return data;
}

void GameFiles::put(const Common::String &name, const ByteArray &data) {
	if (!_keepWrites)
		return;
	if (!isSaveFile(name)) {
		debug(1, "TAD: the program wrote %s (not kept)", name.c_str());
		return;
	}
	Common::OutSaveFile *f = g_system->getSavefileManager()->openForSaving(saveName(name), false);
	if (f) {
		f->write(data.data(), data.size());
		f->finalize();
		delete f;
	}
}

void GameFiles::remove(const Common::String &name) {
	if (_keepWrites && isSaveFile(name))
		g_system->getSavefileManager()->removeSavefile(saveName(name));
}

// the stub table of the sound driver interface sits at the same offsets in every program of the
// engine: functions that answer "ok" (1) and those that do nothing (0)
static const uint16 kSoundOk[] = { 0x194, 0x1e5, 0x283, 0x2d4, 0x358, 0x3a9, 0x469, 0x4ba, 0x50b, 0x343 };
static const uint16 kSoundStubs[] = {
	0x12c, 0x131, 0x136, 0x13b, 0x140, 0x145, 0x14a, 0x14f, 0x154, 0x159, 0x15e, 0x163, 0x168, 0x16d, 0x172, 0x177,
	0x21c, 0x221, 0x226, 0x22b, 0x230, 0x235, 0x23a, 0x23f, 0x244, 0x249, 0x24e, 0x253, 0x258, 0x25d, 0x262,
	0x30c, 0x311, 0x316, 0x31b, 0x320, 0x325, 0x32a, 0x32f, 0x334, 0x339, 0x33e,
	0x3dc, 0x3e1, 0x3e6, 0x3eb, 0x3f0, 0x3f5, 0x3fa, 0x3ff, 0x404, 0x409, 0x40e, 0x413, 0x418, 0x41d, 0x422, 0x427,
	0x42c, 0x431, 0x436, 0x43b, 0x440, 0x445, 0x44a, 0x44f, 0x454
};

ProgramVm::ProgramVm(FileProvider *files, const ProgramDesc &desc) : DosVm(files), _desc(desc) {
}

bool ProgramVm::start(const ByteArray &exe) {
	if (!load(exe.data(), exe.size()))
		return false;
	installSound();
	frameOnRetrace = _desc.frameSeg == 0;
	yieldOnWait = true;
	mouseVars = _desc.mouse;
	if (_desc.frameSeg)
		setTrap(_desc.frameSeg, _desc.frameOff, kTrapFrame);
	return true;
}

void ProgramVm::installSound() {
	// the segment of the stub table: 16 jumps of five bytes ("nop nop jmp") from offset 0x12c
	uint16 seg = 0;
	for (uint32 a = 0x10000; a < 0x50000 && !seg; a++) {
		if (_mem[a] != 0x90 || _mem[a + 1] != 0x90 || _mem[a + 2] != 0xe9)
			continue;
		bool ok = true;
		for (uint k = 1; k < 16 && ok; k++)
			ok = _mem[a + 5 * k] == 0x90 && _mem[a + 5 * k + 1] == 0x90 && _mem[a + 5 * k + 2] == 0xe9;
		if (!ok || (a - 0x12c) % 16)
			continue;
		seg = (a - 0x12c) >> 4;
	}
	if (!seg) {
		warning("TAD: sound driver interface not found");
		return;
	}
	for (uint i = 0; i < ARRAYSIZE(kSoundOk); i++)
		setTrap(seg, kSoundOk[i], kTrapSoundOk);
	for (uint i = 0; i < ARRAYSIZE(kSoundStubs); i++)
		setTrap(seg, kSoundStubs[i], kTrapSoundStub);
}

DosVm::TrapResult ProgramVm::onTrap(int id) {
	switch (id) {
	case kTrapFrame:
		setYield(kYieldFrame);
		return kTrapBefore;
	case kTrapSoundOk:
		retf(1, 0);
		return kTrapContinue;
	case kTrapSoundStub:
		retf(0, 0);
		return kTrapContinue;
	default:
		return kTrapPass;
	}
}

DosVm::RunResult ProgramVm::runFrame(uint32 budget) {
	keepPit = true;
	resetPitReads();
	const uint32 s0 = steps;
	serviceIrqs();
	return run(MAX<uint32>(1000, budget - MIN(budget, steps - s0)));
}

} // End of namespace Tad
