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

#include "common/endian.h"
#include "common/file.h"
#include "common/textconsole.h"

#include "tad/resource.h"

namespace Tad {

ByteArrayPtr Span::copy() const {
	ByteArrayPtr out(new ByteArray());
	if (!empty())
		out->assign(data(), data() + size);
	return out;
}

void ribUnpack(const byte *src, uint32 srcSize, byte *dst, uint32 dstSize) {
	int32 s = (int32)srcSize - 1;
	int32 d = (int32)dstSize - 1;

	while (d >= 0) {
		if (s >= d) {
			// the rest is stored unpacked at the start of the stream
			memcpy(dst, src, d + 1);
			break;
		}
		if (s < 0)
			error("ribUnpack: packed data ends early");

		const byte b = src[s--];
		const int hi = b >> 4;
		int32 n = 0, off = 0;
		byte value = 0;
		enum { kFill, kCopy, kRepeat } mode;

		if (hi == 0) {
			n = (b & 15) + 4;
			value = src[s--];
			mode = kFill;
		} else if (hi == 1) {
			n = (((b & 15) << 8) | src[s--]) + 20;
			value = src[s--];
			mode = kFill;
		} else if (hi == 2) {
			n = (b & 15) + 1;
			mode = kCopy;
		} else if (hi == 3) {
			n = (((b & 15) << 8) | src[s--]) + 17;
			mode = kCopy;
		} else if (hi == 4) {
			if (b == 0x40) {
				const byte h = src[s--];
				n = (h << 8) | src[s--];
				mode = kCopy;
			} else if (b == 0x41) {
				byte h = src[s--];
				off = (h << 8) | src[s--];
				h = src[s--];
				n = (h << 8) | src[s--];
				mode = kRepeat;
			} else if (b == 0x42) {
				const byte h = src[s--];
				off = (h << 8) | src[s--];
				n = src[s--] + 17;
				mode = kRepeat;
			} else {
				n = (b & 15) + 1;
				const byte h = src[s--];
				off = (h << 8) | src[s--];
				mode = kRepeat;
			}
		} else if (hi == 5) {
			off = (((b & 15) << 8) | src[s--]) + 2;
			n = src[s--] + 14;
			mode = kRepeat;
		} else {
			n = hi - 2;
			off = (((b & 15) << 8) | src[s--]) + 2;
			mode = kRepeat;
		}

		if (n > d + 1)
			error("ribUnpack: run of %d bytes beyond the output", n);
		while (n--) {
			if (mode == kFill)
				dst[d] = value;
			else if (mode == kCopy)
				dst[d] = src[s--];
			else
				dst[d] = dst[d + off];
			d--;
		}
	}
}

ByteArrayPtr loadRib(const Span &span) {
	if (span.size >= 8 && !memcmp(span.data(), "RIB\0", 4)) {
		const uint32 size = span.u32(4);
		ByteArrayPtr out(new ByteArray(size));
		ribUnpack(span.data() + 8, span.size - 8, out->data(), size);
		return out;
	}
	return span.copy();
}

Common::Array<Span> fgroup(const Span &span) {
	Common::Array<Span> out;
	if (span.size < 4)
		return out;
	const uint32 n = span.u32(0) / 4;
	Common::Array<uint32> vals;
	for (uint32 i = 0; i < n && 4 * i + 4 <= span.size; i++)
		vals.push_back(span.u32(4 * i));

	uint32 k = 0;
	while (k < vals.size() && vals[k] != 0)
		k++;
	// the size table follows the 0; older files have none, then the next offset ends a member
	const bool sizes = vals.size() >= 2 * k + 1;
	for (uint32 i = 0; i < k; i++) {
		uint32 size = sizes ? vals[k + 1 + i] : (i + 1 < k ? vals[i + 1] : span.size) - vals[i];
		if (vals[i] > span.size)
			size = 0;
		else
			size = MIN<uint32>(size, span.size - vals[i]);
		Span m;
		if (size && !(size == 7 && !memcmp(span.data() + vals[i], "NULLPTR", 7))) {
			m.owner = span.owner;
			m.offset = span.offset + vals[i];
			m.size = size;
		}
		out.push_back(m);
	}
	return out;
}

bool Resources::exists(const Common::String &name) const {
	return Common::File::exists(Common::Path(name));
}

Span Resources::file(const Common::String &name) {
	if (!_files.contains(name)) {
		Common::File f;
		if (!f.open(Common::Path(name)))
			error("Could not open %s", name.c_str());
		Span raw;
		raw.owner = ByteArrayPtr(new ByteArray(f.size()));
		raw.size = f.size();
		f.read(raw.owner->data(), raw.size);
		_files[name] = loadRib(raw);
	}
	Span s;
	s.owner = _files[name];
	s.size = s.owner->size();
	return s;
}

const Common::Array<Span> &Resources::group(const Common::String &name) {
	if (!_groups.contains(name))
		_groups[name] = fgroup(file(name));
	return _groups[name];
}

} // End of namespace Tad
