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

#ifndef helpers_hpp
#define helpers_hpp

#include "rtc/rtc.hpp"

#include <boost/thread/lock_types.hpp>
#include <boost/thread/shared_mutex.hpp>

struct ClientTrackData {
	boost::shared_ptr<rtc::Track> track;
	boost::shared_ptr<rtc::RtcpSrReporter> sender;

	ClientTrackData(boost::shared_ptr<rtc::Track> track,
	                boost::shared_ptr<rtc::RtcpSrReporter> sender);
};

struct Client {
    enum class State {
        Waiting,
        WaitingForVideo,
        WaitingForAudio,
        Ready
    };
    const std::shared_ptr<rtc::PeerConnection> & peerConnection = _peerConnection;
    Client(std::shared_ptr<rtc::PeerConnection> pc) {
        _peerConnection = pc;
    }
    boost::optional<std::shared_ptr<ClientTrackData>> video;
    boost::optional<std::shared_ptr<ClientTrackData>> audio;
    boost::optional<boost::shared_ptr<rtc::DataChannel>> dataChannel{};
    void setState(State state);
    State getState();

private:
	boost::shared_mutex _mutex;
    State state = State::Waiting;
    std::string id;
    std::shared_ptr<rtc::PeerConnection> _peerConnection;
};

struct ClientTrack {
    std::string id;
    std::shared_ptr<ClientTrackData> trackData;
    ClientTrack(std::string id, std::shared_ptr<ClientTrackData> trackData);
};

uint64_t currentTimeInMicroSeconds();

#endif /* helpers_hpp */
