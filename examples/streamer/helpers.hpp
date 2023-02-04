/**
 * libdatachannel streamer example
 * Copyright (c) 2020 Filip Klembara (in2core)
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
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

    uint32_t rtpStartTimestamp = 0;

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
