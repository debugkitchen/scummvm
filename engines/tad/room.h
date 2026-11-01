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

#ifndef TAD_ROOM_H
#define TAD_ROOM_H

#include "common/array.h"
#include "common/hashmap.h"

#include "tad/graphics.h"
#include "tad/resource.h"

namespace Tad {

/** An object of a room layer (record of 0x20 bytes) */
struct RoomObject {
	uint layer = 0;
	byte type = 0;
	byte flags = 0;           ///< bit 0 visible, bit 6 mirrored
	byte anchor = 0;          ///< 0 top left, 1 centre, 2 bottom centre
	int16 x = 0;
	int16 y = 0;
	byte frameList = 0;       ///< 0 frames of the room, 3 item icons
	uint16 frame = 0;
	uint16 scaleX = 0;
	uint16 scaleY = 0;
	byte id = 0;

	bool visible() const { return flags & 1; }
	bool mirrored() const { return flags & 0x40; }
};

struct RoomLayer {
	uint16 type = 0;
	Common::Array<byte> tilemap;          ///< 20 x 8 tiles of 16 x 16 (type 1), 1-based
	uint32 tileTable = 0;                 ///< frame pointers of the tiles
	Common::Array<RoomObject> objects;
};

/**
 * The graphics member of a room: a root record of 0x24 bytes at the end with
 * the size, the layers and the frame table (its first entry points to the
 * palette of the room).
 */
class Room {
public:
	bool load(const Span &graphics);

	/** A frame of the room's frame table (objects), 1 .. frameCount() - 1 */
	FramePtr frame(uint index);
	/** A tile of a layer, 1-based as in the tile map */
	FramePtr tile(const RoomLayer &layer, uint index);
	uint frameCount() const { return _frameCount; }

	/** Draws the layers with their tiles and visible room objects */
	void draw(Canvas &canvas, int clipBottom);

	uint16 width = 0;
	uint16 height = 0;
	PalettePart palette;
	Common::Array<RoomLayer> layers;

private:
	FramePtr frameAt(uint32 pointerPos);

	Span _gfx;
	uint32 _frameTable = 0;
	uint _frameCount = 0;
	Common::HashMap<uint32, FramePtr> _cache;
};

} // End of namespace Tad

#endif // TAD_ROOM_H
