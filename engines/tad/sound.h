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

#ifndef TAD_SOUND_H
#define TAD_SOUND_H

#include "audio/mixer.h"
#include "common/array.h"
#include "common/mutex.h"
#include "common/ptr.h"

#include "tad/resource.h"

namespace OPL {
class OPL;
}

namespace Tad {

class DosVm;
class DrumStream;

/** SM8 sample: unsigned 8-bit PCM at the rate of a PIT divisor */
struct Sample {
	uint32 rate;
	ByteArray data;
};

typedef Common::SharedPtr<Sample> SamplePtr;

/** 'SM8\0', 00 01, u16 length, u16 PIT divisor, data; nullptr if it is none */
SamplePtr decodeSm8(const byte *data, uint32 size);

/**
 * A PSM module compiled to OPL register writes per row (as the PLX track of the PARSEC music
 * driver plays it), with the drum samples of its MDH track.
 */
struct Song {
	struct Write {
		byte reg, val;
	};
	struct Drum {
		uint16 sample;
		byte level;
	};
	struct Row {
		Common::Array<Write> writes;
		double dt;          ///< seconds until the next row
		Common::Array<Drum> drums;
	};
	Common::Array<Write> init;
	Common::Array<Row> rows;
	Common::Array<SamplePtr> samples;
	byte carrier[9];        ///< carrier operator of each channel (for fading)
};

typedef Common::SharedPtr<Song> SongPtr;

/**
 * "MTCVTS PSM 2.00" module or a bare PLX block, played with the tables of the music driver
 * (PSMDRV.DAT member 5: OPL register shadow, operators, note table, channel volumes); at most
 * maxSeconds. nullptr if it is no module.
 */
SongPtr compileSong(const byte *mod, uint32 size, const byte *drv, uint32 drvSize, double maxSeconds = 400);

/**
 * Music and sound effects of the second engine generation. The programs link the PSM drivers
 * (music on the AdLib, samples on the Sound Blaster) into a table of stubs; the engine answers
 * these calls (DosVm traps) and plays the modules and samples the program has in memory.
 */
class Sound {
public:
	Sound(Audio::Mixer *mixer, const ByteArrayPtr &driver);
	~Sound();

	/** A call of the stub at off of the driver interface (far call, arguments on the stack) */
	void stubCall(DosVm &vm, uint16 off);

	void playSong(const SongPtr &song, bool loop);
	void stopMusic();
	void fadeOut(uint ms);
	void playSample(const SamplePtr &s, byte volume);

private:
	void onTimer();
	bool nextRow();
	void writeOpl(byte reg, byte val);
	void applyFade();
	void playLinkedModule(DosVm &vm, uint16 seg, uint16 off, bool loop);

	Audio::Mixer *_mixer;
	ByteArrayPtr _driver;
	OPL::OPL *_opl;
	DrumStream *_drums;
	Audio::SoundHandle _drumHandle;
	Common::Mutex _mutex;

	SongPtr _song;
	uint _row;
	double _rowLeft;
	bool _loop;
	bool _ended;
	double _tail;
	double _fadeGain;
	double _fadeStep;       ///< gain per second
	int _attenuation;       ///< of the carriers while fading, in steps of 0.75 dB
	byte _regs[256];        ///< last values written

	bool _haveMdh;
	uint16 _mdhSeg, _mdhOff;
};

} // End of namespace Tad

#endif // TAD_SOUND_H
