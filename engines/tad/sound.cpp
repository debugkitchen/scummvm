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

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "audio/fmopl.h"
#include "audio/mixer.h"
#include "common/debug.h"
#include "common/func.h"
#include "common/textconsole.h"

#include "tad/detection.h"
#include "tad/dosvm.h"
#include "tad/sound.h"

namespace Tad {

enum {
	kPitRate = 1193182,
	kTimerFrequency = 1000      ///< row timing of the music (callbacks per second)
};

namespace {

/** Bytes of a module; reads past its end give 0 */
struct Bytes {
	const byte *data;
	uint32 size;

	byte operator[](uint32 i) const { return i < size ? data[i] : 0; }
	uint16 u16(uint32 o) const { return (*this)[o] | ((*this)[o + 1] << 8); }
	uint32 u32(uint32 o) const { return u16(o) | ((uint32)u16(o + 2) << 16); }
	bool is(uint32 o, const char *s) const {
		for (uint i = 0; s[i]; i++)
			if ((*this)[o + i] != (byte)s[i])
				return false;
		return true;
	}
};

} // End of anonymous namespace

SamplePtr decodeSm8(const byte *data, uint32 size) {
	const Bytes b = { data, size };
	if (!b.is(0, "SM8"))
		return SamplePtr();
	const uint32 n = b.u16(6), div = b.u16(8);
	if (!div)
		return SamplePtr();
	SamplePtr s(new Sample());
	s->rate = kPitRate / div;
	s->data.resize(n);
	for (uint32 i = 0; i < n; i++)
		s->data[i] = b[10 + i];
	return s;
}

SongPtr compileSong(const byte *data, uint32 size, const byte *drvData, uint32 drvSize, double maxSeconds) {
	const Bytes mod = { data, size }, drv = { drvData, drvSize };
	SongPtr song(new Song());
	int64 p = -1;
	if (mod.is(0, "PLX"))
		p = 0;                                   // bare PLX block
	else if (!mod.is(0, "MTCVTS PSM 2.00"))
		return SongPtr();
	for (int i = 0; i < 16 && p != 0; i++) {
		const uint32 o = mod.u32(0x10 + 4 * i);
		if (!o)
			continue;
		if (mod.is(o, "PLX") && p < 0)
			p = o;
		if (mod.is(o, "SM8") && i < 8)
			song->samples.push_back(o < size ? decodeSm8(data + o, size - o) : SamplePtr());
	}
	if (p < 0)
		return SongPtr();
	const uint32 P = (uint32)p;
	// tables of the driver: OPL register shadow, operators of the channels, notes, volumes
	byte cache[256];
	for (int r = 0; r < 256; r++)
		cache[r] = drv[0x5c4 + r];
	byte opMod[9], opCar[9];
	uint16 chanvol[9];
	for (int c = 0; c < 9; c++) {
		opMod[c] = drv[0x6c4 + c];
		opCar[c] = drv[0x6cd + c];
		chanvol[c] = drv.u16(0x58c + 2 * c);
		song->carrier[c] = opCar[c];
	}
	const uint32 noteTab = 0x6d6;
	for (int r = 0; r < 256; r++) {
		if (cache[r] != 0xff) {
			const Song::Write w = { (byte)r, cache[r] };
			song->init.push_back(w);
		}
	}
	Common::Array<Song::Write> *cur = nullptr;
	auto w = [&](uint reg, uint val) {
		reg &= 0xff;
		val &= 0xff;
		if (cache[reg] != val) {
			cache[reg] = val;
			const Song::Write wr = { (byte)reg, (byte)val };
			cur->push_back(wr);
		}
	};
	auto keyoff = [&](int c) {
		const byte b0 = cache[0xb0 + c];
		if (b0 & 0x20)
			w(0xb0 + c, b0 & 0xdf);
	};
	const byte speed = mod[P + 4];
	uint16 tempo = mod.u16(P + 5);
	uint32 track[9];
	uint32 nexttime[9];
	byte carvol[9];
	uint16 freq[9];
	for (int c = 0; c < 9; c++) {
		const uint16 o = mod.u16(P + 7 + 2 * c);
		track[c] = o ? P + o : 0;
		nexttime[c] = 0;
		carvol[c] = 0;
		freq[c] = 0;
	}
	auto anyTrack = [&]() {
		for (int c = 0; c < 9; c++)
			if (track[c])
				return true;
		return false;
	};
	uint32 row = 0;
	double t = 0;
	while (t < maxSeconds && anyTrack()) {
		Song::Row r;
		cur = &r.writes;
		for (int c = 8; c >= 0; c--) {
			uint32 si = track[c];
			if (!si || row < nexttime[c])
				continue;
			const byte f = mod[si++];
			if (f == 0) {
				track[c] = 0;
				keyoff(c);
				continue;
			}
			if (f != 0x80) {
				if (f & 0x01) {
					uint32 ins = P + mod.u16(si) + 1;
					si += 2;
					w(0xc0 + c, mod[ins++]);
					static const byte kModRegs[5] = { 0x20, 0x40, 0x60, 0x80, 0xe0 };
					for (int k = 0; k < 5; k++)
						w(kModRegs[k] + opMod[c], mod[ins++]);
					w(0x20 + opCar[c], mod[ins++]);
					carvol[c] = mod[ins++];
					static const byte kCarRegs[3] = { 0x60, 0x80, 0xe0 };
					for (int k = 0; k < 3; k++)
						w(kCarRegs[k] + opCar[c], mod[ins++]);
				}
				if (f & 0x02)
					carvol[c] = mod[si++];
				if (f & 0x04)
					keyoff(c);
				if (f & 0x38) {
					const uint on = (cache[0xb0 + c] | f) & 0x20;
					uint16 val;
					if (f & 0x08) {
						val = drv.u16(noteTab + mod[si++]);
					} else if (f & 0x10) {
						val = mod.u16(si);
						si += 2;
					} else {
						val = freq[c];
					}
					val = (val & 0xdfff) | (on << 8);
					freq[c] = val;
					w(0xa0 + c, val & 0xff);
					w(0xb0 + c, val >> 8);
				}
				if (f & 0x40) {
					tempo = mod.u16(si);
					si += 2;
				}
			}
			nexttime[c] += mod[si++];
			track[c] = si;
		}
		// volume pass after each row (driver 0C61)
		for (int c = 0; c < 9; c++) {
			if (!track[c])
				continue;
			uint tl = carvol[c];
			if (chanvol[c] < 0x100) {
				const uint lvl = (((tl & 0x3f) ^ 0x3f) * chanvol[c]) >> 8;
				tl = (tl & 0xc0) | (lvl ^ 0x3f);
			}
			w(0x40 + opCar[c], tl);
		}
		r.dt = (double)speed * MAX<uint>(tempo, 0x200) / kPitRate;
		t += r.dt;
		song->rows.push_back(r);
		row++;
	}
	// MDH drum track: samples of the module on the rows
	int64 m = -1;
	if (P != 0) {
		for (uint32 i = 0; i + 4 <= size; i++) {
			if (mod.is(i, "MDH") && mod[i + 3] == 0) {
				m = i;
				break;
			}
		}
	}
	if (m >= 0) {
		const uint32 M = (uint32)m;
		const uint nv = mod.u16(M + 8);
		uint32 si = M + mod.u16(M + 6), r = 0;
		int state = 1, guard = 0;
		while (r < song->rows.size() && guard++ < 200000) {
			const byte b = mod[si++];
			if (state == 1) {
				if (b & 0x80) {
					r += (b & 0x40) ? (((b & 0x3f) << 8) | mod[si++]) : 2 * (b & 0x3f);
					continue;
				}
				if (b & 0x40) {
					si--;
					state = 2;
					r++;
					continue;
				}
			}
			state = 1;
			const uint n = b & 0x3f;
			if (n == 0x20) {
				const byte a = mod[si++];
				if (a == 0) {
					si = M + mod.u16(M + 6);
					continue;
				}
				break;
			}
			if (n > 0x20)
				break;
			const uint v = (n >> 2) & 7, vel = n & 3;
			if (v < nv && r < song->rows.size()) {
				const Song::Drum d = { mod.u16(M + 0xc + 8 * v), mod[M + 0xe + 8 * v + vel] };
				song->rows[r].drums.push_back(d);
			}
		}
	}
	return song;
}

/**
 * The drum samples of the music, mixed into one endless stream of the music type (started from
 * the OPL timer, which must not call the mixer)
 */
class DrumStream : public Audio::AudioStream {
public:
	DrumStream(Common::Mutex &mutex, int rate) : _mutex(mutex), _rate(rate) {}

	void add(const SamplePtr &s, double gain) {
		Voice v;
		v.s = s;
		v.pos = 0;
		v.step = (double)s->rate / _rate;
		v.gain = gain;
		_voices.push_back(v);
	}
	void clear() { _voices.clear(); }
	void scale(double f) {
		for (uint i = 0; i < _voices.size(); i++)
			_voices[i].gain *= f;
	}

	int readBuffer(int16 *buffer, const int numSamples) override {
		Common::StackLock lock(_mutex);
		for (int i = 0; i < numSamples; i++) {
			double v = 0;
			for (int k = (int)_voices.size() - 1; k >= 0; k--) {
				Voice &vo = _voices[k];
				const uint32 p = (uint32)vo.pos;
				if (p + 1 >= vo.s->data.size()) {
					_voices.remove_at(k);
					continue;
				}
				const double f = vo.pos - p;
				const double a = vo.s->data[p] - 128.0, b = vo.s->data[p + 1] - 128.0;
				v += (a + (b - a) * f) * vo.gain;
				vo.pos += vo.step;
			}
			buffer[i] = (int16)CLIP<double>(v, -32768, 32767);
		}
		return numSamples;
	}
	bool isStereo() const override { return false; }
	int getRate() const override { return _rate; }
	bool endOfData() const override { return false; }

private:
	struct Voice {
		SamplePtr s;
		double pos, step, gain;
	};
	Common::Mutex &_mutex;
	int _rate;
	Common::Array<Voice> _voices;
};

Sound::Sound(Audio::Mixer *mixer, const ByteArrayPtr &driver) : _mixer(mixer), _driver(driver), _opl(nullptr),
	_drums(nullptr), _row(0), _rowLeft(0), _loop(false), _ended(false), _tail(0), _fadeGain(1), _fadeStep(0),
	_attenuation(0), _haveMdh(false), _mdhSeg(0), _mdhOff(0) {
	memset(_regs, 0, sizeof(_regs));
	if (!_mixer || !_driver)
		return;
	_opl = OPL::Config::create();
	if (!_opl || !_opl->init()) {
		warning("TAD: no OPL emulator, music is off");
		delete _opl;
		_opl = nullptr;
		return;
	}
	_drums = new DrumStream(_mutex, _mixer->getOutputRate());
	_mixer->playStream(Audio::Mixer::kMusicSoundType, &_drumHandle, _drums, -1, Audio::Mixer::kMaxChannelVolume, 0,
					   DisposeAfterUse::YES);
	_opl->start(new Common::Functor0Mem<void, Sound>(this, &Sound::onTimer), kTimerFrequency);
}

Sound::~Sound() {
	if (_opl) {
		_opl->stop();
		delete _opl;
	}
	if (_drums)
		_mixer->stopHandle(_drumHandle);
}

void Sound::writeOpl(byte reg, byte val) {
	_regs[reg] = val;
	if (_attenuation && _song && reg >= 0x40 && reg < 0x56) {
		for (int c = 0; c < 9; c++) {
			if (reg == 0x40 + _song->carrier[c]) {
				val = (val & 0xc0) | MIN<int>(0x3f, (val & 0x3f) + _attenuation);
				break;
			}
		}
	}
	_opl->writeReg(reg, val);
}

void Sound::applyFade() {
	// amplitude to the attenuation of the carriers (0.75 dB per step)
	const int att = _fadeGain <= 0.001 ? 0x3f : MIN<int>(0x3f, (int)(-20.0 * log10(_fadeGain) / 0.75 + 0.5));
	if (att == _attenuation)
		return;
	_attenuation = att;
	for (int c = 0; c < 9; c++)
		writeOpl(0x40 + _song->carrier[c], _regs[0x40 + _song->carrier[c]]);
}

void Sound::playSong(const SongPtr &song, bool loop) {
	if (!_opl || !song)
		return;
	Common::StackLock lock(_mutex);
	_opl->reset();
	_song = song;
	_attenuation = 0;
	for (uint i = 0; i < song->init.size(); i++)
		writeOpl(song->init[i].reg, song->init[i].val);
	_row = 0;
	_rowLeft = 0;
	_loop = loop;
	_ended = false;
	_fadeGain = 1;
	_fadeStep = 0;
}

void Sound::stopMusic() {
	if (!_opl)
		return;
	Common::StackLock lock(_mutex);
	_song.reset();
	for (int c = 0; c < 9; c++)
		_opl->writeReg(0xb0 + c, 0);
	_drums->clear();
}

void Sound::fadeOut(uint ms) {
	if (!_opl)
		return;
	if (!_song || !ms) {
		stopMusic();
		return;
	}
	Common::StackLock lock(_mutex);
	_fadeStep = 1000.0 / ms;
}

bool Sound::nextRow() {
	if (_row >= _song->rows.size()) {
		// a song played once ends without a key-off: the last notes die away in the release
		if (!_loop) {
			_ended = true;
			_tail = 3.0;
			return false;
		}
		_row = 0;
	}
	const Song::Row &r = _song->rows[_row++];
	for (uint i = 0; i < r.writes.size(); i++)
		writeOpl(r.writes[i].reg, r.writes[i].val);
	for (uint i = 0; i < r.drums.size(); i++) {
		const uint16 k = r.drums[i].sample;
		if (k < _song->samples.size() && _song->samples[k]) {
			// level 40 is the full volume of a drum, mixed as the driver does against the AdLib
			_drums->add(_song->samples[k], r.drums[i].level / 40.0 * 0.2747 / 4.0 * 256.0 * _fadeGain);
		}
	}
	_rowLeft += r.dt;
	return true;
}

void Sound::onTimer() {
	Common::StackLock lock(_mutex);
	if (!_song)
		return;
	const double dt = 1.0 / kTimerFrequency;
	bool ok = !_ended;
	while (ok && _rowLeft <= 0)
		ok = nextRow();
	_rowLeft -= dt;
	if (_ended) {
		_tail -= dt;
		if (_tail <= 0) {
			_song.reset();
			for (int c = 0; c < 9; c++)
				_opl->writeReg(0xb0 + c, 0);
			return;
		}
	}
	if (_fadeStep) {
		const double old = _fadeGain;
		_fadeGain -= _fadeStep * dt;
		if (_fadeGain <= 0) {
			_song.reset();
			for (int c = 0; c < 9; c++)
				_opl->writeReg(0xb0 + c, 0);
			_drums->clear();
			return;
		}
		_drums->scale(_fadeGain / old);
		applyFade();
	}
}

void Sound::playSample(const SamplePtr &s, byte volume) {
	if (!_mixer || !s || s->data.empty())
		return;
	byte *copy = (byte *)malloc(s->data.size());
	memcpy(copy, s->data.data(), s->data.size());
	Audio::SeekableAudioStream *stream = Audio::makeRawStream(copy, s->data.size(), s->rate, Audio::FLAG_UNSIGNED,
															   DisposeAfterUse::YES);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, nullptr, stream, -1, volume);
}

// The programs build a module in memory: slot 0 of its header points to the PLX block, slots 1..8
// to the SM8 samples, the MDH track follows the header at +38h. For compileSong it is copied
// into one buffer with offsets.
void Sound::playLinkedModule(DosVm &vm, uint16 seg, uint16 off, bool loop) {
	if (!_opl || !_haveMdh)
		return;
	const byte *m = vm.mem();
	const int64 base = ((int64)_mdhSeg << 4) + _mdhOff - 0x38;
	if (base < 0 || base + 0x1000 > kMemorySize || memcmp(m + base, "MTCVTS", 6) != 0) {
		warning("TAD: no music module at the MDH track");
		return;
	}
	ByteArray out;
	Common::Array<uint32> slots;
	auto add = [&](int64 a, uint32 n) {
		slots.push_back(out.size());
		for (uint32 i = 0; i < n; i++)
			out.push_back(a + i < kMemorySize ? m[a + i] : 0);
	};
	for (uint32 i = 0; i < 0x1000; i++)
		out.push_back(m[base + i]);
	slots.clear();
	add(((int64)seg << 4) + off, 0x8000);
	for (int i = 1; i < 9; i++) {
		const uint16 o = READ_LE_UINT16(m + base + 0x10 + 4 * i), sg = READ_LE_UINT16(m + base + 0x12 + 4 * i);
		if ((!o && !sg) || sg == 0xffff)
			break;
		const int64 a = ((int64)sg << 4) + o;
		if (a + 10 > kMemorySize || m[a] != 'S' || m[a + 1] != 'M')
			break;
		add(a, READ_LE_UINT16(m + a + 6) + 10);
	}
	for (uint i = 0; i < 10; i++)
		WRITE_LE_UINT32(out.data() + 0x10 + 4 * i, i < slots.size() ? slots[i] : 0);
	SongPtr song = compileSong(out.data(), out.size(), _driver->data(), _driver->size());
	if (!song) {
		warning("TAD: music module could not be read");
		return;
	}
	debugC(1, kDebugResources, "TAD: music %04x:%04x, %u rows, loop %d", seg, off, song->rows.size(), loop);
	playSong(song, loop);
}

static uint16 stubArg(DosVm &vm, int i) {
	return vm.rw(vm.sreg[kSS], (uint16)(vm.r16[kSP] + 4 + 2 * i));
}

void Sound::stubCall(DosVm &vm, uint16 off) {
	switch (off) {
	case 0x445:     // samples driver: start the MDH drum track (level, far pointer to "MDH")
		_haveMdh = true;
		_mdhOff = stubArg(vm, 2);
		_mdhSeg = stubArg(vm, 3);
		break;
	case 0x235:     // music driver: play a PLX block (far pointer, loop)
		playLinkedModule(vm, stubArg(vm, 1), stubArg(vm, 0), stubArg(vm, 2) != 0);
		break;
	case 0x244:     // fade the music out (milliseconds)
		fadeOut(stubArg(vm, 0));
		break;
	case 0x253:     // stop the music
		stopMusic();
		break;
	case 0x431: {   // play an SM8 sample (far pointer)
		const int64 a = ((int64)stubArg(vm, 1) << 4) + stubArg(vm, 0);
		if (a < kMemorySize) {
			SamplePtr smp = decodeSm8(vm.mem() + a, (uint32)(kMemorySize - a));
			debugC(2, kDebugResources, "TAD: sample %04x:%04x, %u bytes", stubArg(vm, 1), stubArg(vm, 0), smp ? smp->data.size() : 0);
			playSample(smp, 184);
		}
		break;
	}
	default:
		break;
	}
}

} // End of namespace Tad
