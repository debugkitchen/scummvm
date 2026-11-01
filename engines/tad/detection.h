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

#ifndef TAD_DETECTION_H
#define TAD_DETECTION_H

#include "engines/advancedDetector.h"

namespace Tad {

enum TadDebugChannels {
	kDebugGraphics = 1,
	kDebugResources,
	kDebugScript,
};

extern const PlainGameDescriptor tadGames[];

extern const ADGameDescription gameDescriptions[];

} // End of namespace Tad

class TadMetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
	static const DebugChannelDef debugFlagList[];

public:
	TadMetaEngineDetection();
	~TadMetaEngineDetection() override {}

	const char *getName() const override {
		return "tad";
	}

	const char *getEngineName() const override {
		return "The Art Department";
	}

	const char *getOriginalCopyright() const override {
		return "(C) The Art Department / Freelance Development";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugFlagList;
	}
};

#endif // TAD_DETECTION_H
