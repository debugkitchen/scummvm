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

#ifndef TAD_RESOURCE_H
#define TAD_RESOURCE_H

#include "common/array.h"
#include "common/endian.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/ptr.h"
#include "common/str.h"

namespace Tad {

typedef Common::Array<byte> ByteArray;
typedef Common::SharedPtr<ByteArray> ByteArrayPtr;

/**
 * A view into a byte buffer that is kept alive by the shared pointer.
 */
struct Span {
	ByteArrayPtr owner;
	uint32 offset = 0;
	uint32 size = 0;

	bool empty() const { return !owner || size == 0; }
	const byte *data() const { return owner->data() + offset; }
	byte operator[](uint32 i) const { return (*owner)[offset + i]; }
	uint16 u16(uint32 o) const { return READ_LE_UINT16(data() + o); }
	int16 s16(uint32 o) const { return (int16)READ_LE_UINT16(data() + o); }
	uint32 u32(uint32 o) const { return READ_LE_UINT32(data() + o); }
	/** A copy of this span as a buffer of its own */
	ByteArrayPtr copy() const;
};

/**
 * Unpacks data compressed with the "RIB" method of the original engine: the
 * packed stream is read backwards from its end and the output is written from
 * its end towards its start.
 */
void ribUnpack(const byte *src, uint32 srcSize, byte *dst, uint32 dstSize);

/** A file whose data may start with the "RIB\0" header: returns the unpacked data. */
ByteArrayPtr loadRib(const Span &span);

/**
 * File group ("FGROUP"): a table of 32-bit offsets, a 0 and a table of
 * sizes, followed by the members. A member reading "NULLPTR" is empty.
 */
Common::Array<Span> fgroup(const Span &span);

/**
 * Access to the data files in the RESOURCE folder of a game.
 */
class Resources {
public:
	/** The whole file, unpacked if it is RIB-packed */
	Span file(const Common::String &name);
	/** The members of a file group */
	const Common::Array<Span> &group(const Common::String &name);
	bool exists(const Common::String &name) const;

private:
	Common::HashMap<Common::String, ByteArrayPtr, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _files;
	Common::HashMap<Common::String, Common::Array<Span>, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> _groups;
};

} // End of namespace Tad

#endif // TAD_RESOURCE_H
