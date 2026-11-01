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

#include "tad/room.h"

namespace Tad {

/**
 * The offset a, given as num / den, by which a position moves when it is
 * rounded as round(pos - a) with halves rounded up: ceil(a - 1/2)
 */
static int roundedShift(int num, int den) {
	const int m = 2 * num - den;            // (a - 1/2) * 2 * den
	const int d = 2 * den;
	return m >= 0 ? (m + d - 1) / d : -((-m) / d);
}

bool Room::load(const Span &graphics) {
	_gfx = graphics;
	_cache.clear();
	layers.clear();
	if (graphics.size < 0x24)
		return false;
	const uint32 root = graphics.size - 0x24;
	width = graphics.u16(root);
	height = graphics.u16(root + 2);
	const uint layerCount = graphics.u16(root + 0xc);
	_frameCount = graphics.u16(root + 0x12);
	_frameTable = graphics.u32(root + 0x20);
	if (_frameTable + 4 > graphics.size)
		return false;
	palette = decodePalette(graphics, graphics.u32(_frameTable));

	const uint32 layerTable = graphics.u32(root + 0x18);
	for (uint li = 0; li < layerCount; li++) {
		const uint32 l = layerTable + li * 0x14;
		if (l + 0x14 > graphics.size)
			return false;
		RoomLayer layer;
		layer.type = graphics.u16(l);
		layer.tileTable = graphics.u32(l + 8);
		if (layer.type == 1) {
			const uint32 map = graphics.u32(l + 4);
			for (uint t = 0; t < 160 && map + t < graphics.size; t++)
				layer.tilemap.push_back(graphics[map + t]);
		}
		const uint objectCount = graphics.u16(l + 2);
		const uint32 objects = graphics.u32(l + 0xc);
		if (objects) {
			for (uint j = 0; j < objectCount; j++) {
				const uint32 e = objects + j * 0x20;
				if (e + 0x20 > graphics.size)
					break;
				RoomObject o;
				o.layer = li;
				o.type = graphics[e];
				o.flags = graphics[e + 2];
				o.anchor = graphics[e + 3];
				o.x = graphics.s16(e + 4);
				o.y = graphics.s16(e + 6);
				o.frameList = graphics[e + 8];
				o.frame = graphics.u16(e + 0xa);
				o.scaleX = graphics.u16(e + 0xe);
				o.scaleY = graphics.u16(e + 0x10);
				o.id = graphics[e + 0x13];
				layer.objects.push_back(o);
			}
		}
		layers.push_back(layer);
	}
	return true;
}

FramePtr Room::frameAt(uint32 pointerPos) {
	if (pointerPos + 4 > _gfx.size)
		return FramePtr();
	const uint32 fo = _gfx.u32(pointerPos);
	if (!fo || fo + 8 > _gfx.size)
		return FramePtr();
	if (!_cache.contains(fo))
		_cache[fo] = decodeFrame(_gfx, fo);
	return _cache[fo];
}

FramePtr Room::frame(uint index) {
	if (index == 0 || index >= _frameCount)
		return FramePtr();
	return frameAt(_frameTable + 4 * index);
}

FramePtr Room::tile(const RoomLayer &layer, uint index) {
	if (!index)
		return FramePtr();
	return frameAt(layer.tileTable + 4 * (index - 1));
}

void Room::draw(Canvas &canvas, int clipBottom) {
	for (uint li = 0; li < layers.size(); li++) {
		const RoomLayer &layer = layers[li];
		for (uint t = 0; t < layer.tilemap.size(); t++) {
			FramePtr f = tile(layer, layer.tilemap[t]);
			if (f)
				canvas.blit(*f, (t % 20) * 16, (t / 20) * 16, clipBottom);
		}
		for (uint j = 0; j < layer.objects.size(); j++) {
			const RoomObject &o = layer.objects[j];
			if (!o.visible() || o.frameList != 0)
				continue;
			FramePtr f = frame(o.frame);
			if (!f)
				continue;
			const int sx = o.scaleX && o.scaleX != 0x100 ? o.scaleX : 256;
			const int sy = o.scaleY && o.scaleY != 0x100 ? o.scaleY : 256;
			int x = o.x, y = o.y;
			if (o.anchor == 1) {
				x -= roundedShift(f->w * sx, 512);
				y -= roundedShift(f->h * sy, 512);
			} else if (o.anchor == 2) {
				x -= roundedShift(f->w * sx, 512);
				y -= roundedShift(f->h * sy, 256);
			}
			canvas.blit(*f, x, y, clipBottom, o.mirrored(), sx);
		}
	}
}

} // End of namespace Tad
