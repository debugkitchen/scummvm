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

namespace Tad {

const PlainGameDescriptor tadGames[] = {
	{ "telekommando2", "Das Telekommando kehrt zur\xc3\xbc" "ck" },
	{ "bifi2", "Bi-Fi 2: Action in Hollywood" },
	{ "dunkleschatten", "Dunkle Schatten" },
	{ 0, 0 }
};

const ADGameDescription gameDescriptions[] = {
	// Das Telekommando kehrt zurueck (1993, freeware)
	{
		"telekommando2",
		nullptr,
		AD_ENTRY2s("XP01.EXE", "b0ac255b0ea74451a9425c243a20402f", 67959,
		           "ROOM13.DAT", "2aa1585711c855bdca93b0fd93fbe88a", 12916),
		Common::DE_DEU,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},

	// Bi-Fi 2: Action in Hollywood (1994)
	{
		"bifi2",
		nullptr,
		AD_ENTRY2s("XP01.EXE", "d73fd787583bb7223e726d4a7e52e6ab", 84727,
		           "BFROOM.DAT", "3965337e734e3a678ab1235d375c7675", 718078),
		Common::DE_DEU,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},

	// Dunkle Schatten (1994)
	{
		"dunkleschatten",
		nullptr,
		AD_ENTRY2s("XP01.EXE", "928b5d643c0f7499c2e44fae184dea68", 79159,
		           "DSROOM.DAT", "bf5caa2ccb9670baa8f06ae50c0b303e", 819473),
		Common::DE_DEU,
		Common::kPlatformDOS,
		ADGF_UNSTABLE,
		GUIO1(GUIO_NONE)
	},

	AD_TABLE_END_MARKER
};

} // End of namespace Tad
