/*
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef h264fileparser_hpp
#define h264fileparser_hpp

#include "fileparser.hpp"
#include <boost/optional.hpp>

class H264FileParser: public FileParser {
    boost::optional<std::vector<nonstd::byte>> previousUnitType5 = boost::none;
    boost::optional<std::vector<nonstd::byte>> previousUnitType7 = boost::none;
    boost::optional<std::vector<nonstd::byte>> previousUnitType8 = boost::none;

public:
    H264FileParser(std::string directory, uint32_t fps, bool loop);
    void loadNextSample() override;
    std::vector<nonstd::byte> initialNALUS();
};

#endif /* h264fileparser_hpp */
