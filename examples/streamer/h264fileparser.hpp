/**
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
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
