/*
 * libdatachannel client example
 * Copyright (c) 2019-2020 Paul-Louis Ageneau
 * Copyright (c) 2019 Murat Dogan
 * Copyright (c) 2020 Will Munn
 * Copyright (c) 2020 Nico Chatzi
 * Copyright (c) 2020 Lara Mackey
 * Copyright (c) 2020 Erik Cota-Robles
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

#include "nlohmann/json.hpp"

#include "h264fileparser.hpp"
#include "opusfileparser.hpp"
#include "helpers.hpp"
#include "ArgParser.hpp"

using namespace rtc;
using namespace std;
using namespace std::chrono_literals;

using json = nlohmann::json;

template <class T> std::weak_ptr<T> make_weak_ptr(std::shared_ptr<T> ptr) { return ptr; }
template <class T> boost::weak_ptr<T> make_weak_ptr(boost::shared_ptr<T> ptr) { return ptr; }

/// all connected clients
unordered_map<string, std::shared_ptr<Client>> clients{};

/// Creates peer connection and client representation
/// @param config Configuration
/// @param wws Websocket for signaling
/// @param id Client ID
/// @returns Client
std::shared_ptr<Client> createPeerConnection(const Configuration &config,
                                        std::weak_ptr<WebSocket> wws,
                                        string id);

/// Creates stream
/// @param h264Samples Directory with H264 samples
/// @param fps Video FPS
/// @param opusSamples Directory with opus samples
/// @returns Stream object
std::shared_ptr<Stream> createStream(const string h264Samples, const unsigned fps,
                                     const string opusSamples);

/// Add client to stream
/// @param client Client
/// @param adding_video True if adding video
void addToStream(std::shared_ptr<Client> client, bool isAddingVideo);

/// Start stream
void startStream();

/// Main dispatch queue
DispatchQueue MainThread("Main");

/// Audio and video stream
boost::optional<std::shared_ptr<Stream>> avStream = boost::none;

const string defaultRootDirectory = "../../../examples/streamer/samples/";
const string defaultH264SamplesDirectory = defaultRootDirectory + "h264/";
string h264SamplesDirectory = defaultH264SamplesDirectory;
const string defaultOpusSamplesDirectory = defaultRootDirectory + "opus/";
string opusSamplesDirectory = defaultOpusSamplesDirectory;
const string defaultIPAddress = "127.0.0.1";
const uint16_t defaultPort = 8000;
string ip_address = defaultIPAddress;
uint16_t port = defaultPort;

/// Incomming message handler for websocket
/// @param message Incommint message
/// @param config Configuration
/// @param ws Websocket
void wsOnMessage(json message, Configuration config, std::shared_ptr<WebSocket> ws) {
    auto it = message.find("id");
    if (it == message.end())
        return;
    string id = it->get<string>();
    it = message.find("type");
    if (it == message.end())
        return;
    string type = it->get<string>();

    if (type == "streamRequest") {
		std::shared_ptr<Client> c = createPeerConnection(config, make_weak_ptr(ws), id);
        clients.emplace(id, c);
    } else if (type == "answer") {
		std::shared_ptr<Client> c;

		auto jt = clients.find(id);
        if (jt != clients.end()) {
            auto pc = clients.at(id)->peerConnection;
            auto sdp = message["sdp"].get<string>();
            auto description = Description(sdp, type);
            pc->setRemoteDescription(description);
        }
    }
}

int main(int argc, char **argv) try {
    bool enableDebugLogs = false;
    bool printHelp = false;
    int c = 0;
    auto parser = ArgParser({{"a", "audio"}, {"b", "video"}, {"d", "ip"}, {"p","port"}}, {{"h", "help"}, {"v", "verbose"}});
    auto parsingResult = parser.parse(argc, argv, [](string key, string value) {
        if (key == "audio") {
            opusSamplesDirectory = value + "/";
        } else if (key == "video") {
            h264SamplesDirectory = value + "/";
        } else if (key == "ip") {
            ip_address = value;
        } else if (key == "port") {
            port = atoi(value.data());
        } else {
            cerr << "Invalid option --" << key << " with value " << value << endl;
            return false;
        }
        return true;
    }, [&enableDebugLogs, &printHelp](string flag){
        if (flag == "verbose") {
            enableDebugLogs = true;
        } else if (flag == "help") {
            printHelp = true;
        } else {
            cerr << "Invalid flag --" << flag << endl;
            return false;
        }
        return true;
    });
    if (!parsingResult) {
        return 1;
    }

    if (printHelp) {
        cout << "usage: stream-h264 [-a opus_samples_folder] [-b h264_samples_folder] [-d ip_address] [-p port] [-v] [-h]" << endl
        << "Arguments:" << endl
        << "\t -a " << "Directory with opus samples (default: " << defaultOpusSamplesDirectory << ")." << endl
        << "\t -b " << "Directory with H264 samples (default: " << defaultH264SamplesDirectory << ")." << endl
        << "\t -d " << "Signaling server IP address (default: " << defaultIPAddress << ")." << endl
        << "\t -p " << "Signaling server port (default: " << defaultPort << ")." << endl
        << "\t -v " << "Enable debug logs." << endl
        << "\t -h " << "Print this help and exit." << endl;
        return 0;
    }
    if (enableDebugLogs) {
        InitLogger(LogLevel::Debug);
    }

    Configuration config;
    string stunServer = "stun:stun.l.google.com:19302";
    cout << "Stun server is " << stunServer << endl;
    config.iceServers.emplace_back(stunServer);
    config.disableAutoNegotiation = true;

    string localId = "server";
    cout << "The local ID is: " << localId << endl;

    auto ws = make_shared<WebSocket>();

    ws->onOpen([]() { cout << "WebSocket connected, signaling ready" << endl; });

    ws->onClosed([]() { cout << "WebSocket closed" << endl; });

    ws->onError([](const string &error) { cout << "WebSocket failed: " << error << endl; });

    ws->onMessage([&](variant<binary, string> data) {
        if (!workarounds::holds_alternative<string>(data))
            return;

        json message = json::parse(get<string>(data));
        MainThread.dispatch([message, config, ws]() {
            wsOnMessage(message, config, ws);
        });
    });

    const string url = "ws://" + ip_address + ":" + to_string(port) + "/" + localId;
    cout << "Url is " << url << endl;
    ws->open(url);

    cout << "Waiting for signaling to be connected..." << endl;
    while (!ws->isOpen()) {
        if (ws->isClosed())
            return 1;
        this_thread::sleep_for(100ms);
    }

    while (true) {
        string id;
        cout << "Enter to exit" << endl;
        cin >> id;
        cin.ignore();
        cout << "exiting" << endl;
        break;
    }

    cout << "Cleaning up..." << endl;
    return 0;

} catch (const std::exception &e) {
    std::cout << "Error: " << e.what() << std::endl;
    return -1;
}

std::shared_ptr<ClientTrackData> addVideo(const std::shared_ptr<PeerConnection> pc,
                                     const uint8_t payloadType, const uint32_t ssrc,
                                     const string cname, const string msid,
                                     const function<void(void)> onOpen) {
    auto video = Description::Video(cname);
    video.addH264Codec(payloadType);
    video.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(video);
    // create RTP configuration
	auto rtpConfig = boost::make_shared<RtpPacketizationConfig>(
	    ssrc, cname, payloadType, H264RtpPacketizer::defaultClockRate);
    // create packetizer
    auto packetizer = boost::make_shared<H264RtpPacketizer>(H264RtpPacketizer::Separator::Length, rtpConfig);
    // create H264 handler
	auto h264Handler = boost::make_shared<H264PacketizationHandler>(packetizer);
    // add RTCP SR handler
    auto srReporter = boost::make_shared<RtcpSrReporter>(rtpConfig);
    h264Handler->addToChain(srReporter);
    // add RTCP NACK handler
	auto nackResponder = boost::make_shared<RtcpNackResponder>();
    h264Handler->addToChain(nackResponder);
    // set handler
    track->setMediaHandler(h264Handler);
    track->onOpen(onOpen);
    auto trackData = std::make_shared<ClientTrackData>(track, srReporter);
    return trackData;
}

std::shared_ptr<ClientTrackData> addAudio(const std::shared_ptr<PeerConnection> pc,
                                          const uint8_t payloadType, const uint32_t ssrc,
                                          const string cname, const string msid,
                                          const function<void(void)> onOpen) {
    auto audio = Description::Audio(cname);
    audio.addOpusCodec(payloadType);
    audio.addSSRC(ssrc, cname, msid, cname);
    auto track = pc->addTrack(audio);
    // create RTP configuration
	auto rtpConfig = boost::make_shared<RtpPacketizationConfig>(
	    ssrc, cname, payloadType, OpusRtpPacketizer::defaultClockRate);
    // create packetizer
	auto packetizer = boost::make_shared<OpusRtpPacketizer>(rtpConfig);
    // create opus handler
	auto opusHandler = boost::make_shared<OpusPacketizationHandler>(packetizer);
    // add RTCP SR handler
	auto srReporter = boost::make_shared<RtcpSrReporter>(rtpConfig);
    opusHandler->addToChain(srReporter);
    // add RTCP NACK handler
	auto nackResponder = boost::make_shared<RtcpNackResponder>();
    opusHandler->addToChain(nackResponder);
    // set handler
    track->setMediaHandler(opusHandler);
    track->onOpen(onOpen);
    auto trackData = std::make_shared<ClientTrackData>(track, srReporter);
    return trackData;
}

// Create and setup a PeerConnection
std::shared_ptr<Client> createPeerConnection(const Configuration &config,
                                                std::weak_ptr<WebSocket> wws,
                                                string id) {
    auto pc = make_shared<PeerConnection>(config);
    auto client = make_shared<Client>(pc);

    pc->onStateChange([id](PeerConnection::State state) {
        cout << "State: " << state << endl;
        if (state == PeerConnection::State::Disconnected ||
            state == PeerConnection::State::Failed ||
            state == PeerConnection::State::Closed) {
            // remove disconnected client
            MainThread.dispatch([id]() {
                clients.erase(id);
            });
        }
    });

    pc->onGatheringStateChange(
        [wpc = make_weak_ptr(pc), id, wws](PeerConnection::GatheringState state) {
        cout << "Gathering State: " << state << endl;
        if (state == PeerConnection::GatheringState::Complete) {
            if(auto pc = wpc.lock()) {
                auto description = pc->localDescription();
                json message = {
                    {"id", id},
                    {"type", description->typeString()},
                    {"sdp", string(description.value())}
                };
                // Gathering complete, send answer
                if (auto ws = wws.lock()) {
                    ws->send(message.dump());
                }
            }
        }
    });

    client->video = addVideo(pc, 102, 1, "video-stream", "stream1", [id, wc = make_weak_ptr(client)]() {
        MainThread.dispatch([wc]() {
            if (auto c = wc.lock()) {
                addToStream(c, true);
            }
        });
        cout << "Video from " << id << " opened" << endl;
    });

    client->audio = addAudio(pc, 111, 2, "audio-stream", "stream1", [id, wc = make_weak_ptr(client)]() {
        MainThread.dispatch([wc]() {
            if (auto c = wc.lock()) {
                addToStream(c, false);
            }
        });
        cout << "Audio from " << id << " opened" << endl;
    });

    auto dc = pc->createDataChannel("ping-pong");
    dc->onOpen([id, wdc = make_weak_ptr(dc)]() {
        if (auto dc = wdc.lock()) {
            dc->send("Ping");
        }
    });

    dc->onMessage(nullptr, [id, wdc = make_weak_ptr(dc)](string msg) {
        cout << "Message from " << id << " received: " << msg << endl;
        if (auto dc = wdc.lock()) {
            dc->send("Ping");
        }
    });
    client->dataChannel = dc;

    pc->setLocalDescription();
    return client;
};

/// Create stream
std::shared_ptr<Stream> createStream(const string h264Samples, const unsigned fps, const string opusSamples) {
    // video source
    auto video = make_shared<H264FileParser>(h264Samples, fps, true);
    // audio source
    auto audio = make_shared<OPUSFileParser>(opusSamples, true);

    auto stream = make_shared<Stream>(video, audio);
    // set callback responsible for sample sending
    stream->onSample([ws = make_weak_ptr(stream)](Stream::StreamSourceType type, uint64_t sampleTime, rtc::binary sample) {
        vector<ClientTrack> tracks{};
        string streamType = type == Stream::StreamSourceType::Video ? "video" : "audio";
        // get track for given type
        function<boost::optional<std::shared_ptr<ClientTrackData>> (std::shared_ptr<Client>)> getTrackData = [type](std::shared_ptr<Client> client) {
            return type == Stream::StreamSourceType::Video ? client->video : client->audio;
        };
        // get all clients with Ready state
        for(auto id_client: clients) {
            auto id = id_client.first;
            auto client = id_client.second;
            auto optTrackData = getTrackData(client);
            if (client->getState() == Client::State::Ready && optTrackData.has_value()) {
                auto trackData = optTrackData.value();
                tracks.push_back(ClientTrack(id, trackData));
            }
        }
        if (!tracks.empty()) {
            for (auto clientTrack: tracks) {
                auto client = clientTrack.id;
                auto trackData = clientTrack.trackData;
                // sample time is in us, we need to convert it to seconds
                auto elapsedSeconds = double(sampleTime) / (1000 * 1000);
                auto rtpConfig = trackData->sender->rtpConfig;
                // get elapsed time in clock rate
                uint32_t elapsedTimestamp = rtpConfig->secondsToTimestamp(elapsedSeconds);

                // set new timestamp
                rtpConfig->timestamp = rtpConfig->startTimestamp + elapsedTimestamp;

                // get elapsed time in clock rate from last RTCP sender report
                auto reportElapsedTimestamp = rtpConfig->timestamp - trackData->sender->previousReportedTimestamp;
                // check if last report was at least 1 second ago
                if (rtpConfig->timestampToSeconds(reportElapsedTimestamp) > 1) {
                    trackData->sender->setNeedsToReport();
                }
                cout << "Sending " << streamType << " sample with size: " << to_string(sample.size()) << " to " << client << endl;
                bool send = false;
                try {
                    // send sample
                    send = trackData->track->send(sample);
                } catch (...) {
                    send = false;
                }
                if (!send) {
                    cerr << "Unable to send "<< streamType << " packet" << endl;
                    break;
                }
            }
        }
        MainThread.dispatch([ws]() {
            if (clients.empty()) {
                // we have no clients, stop the stream
                if (auto stream = ws.lock()) {
                    stream->stop();
                }
            }
        });
    });
    return stream;
}

/// Start stream
void startStream() {
    std::shared_ptr<Stream> stream;
    if (avStream.has_value()) {
        stream = avStream.value();
        if (stream->isRunning) {
            // stream is already running
            return;
        }
    } else {
        stream = createStream(h264SamplesDirectory, 30, opusSamplesDirectory);
        avStream = stream;
    }
    stream->start();
}

/// Send previous key frame so browser can show something to user
/// @param stream Stream
/// @param video Video track data
void sendInitialNalus(std::shared_ptr<Stream> stream, std::shared_ptr<ClientTrackData> video) {
    auto h264 = dynamic_cast<H264FileParser *>(stream->video.get());
    auto initialNalus = h264->initialNALUS();

    // send previous NALU key frame so users don't have to wait to see stream works
    if (!initialNalus.empty()) {
        const double frameDuration_s = double(h264->sampleDuration_us) / (1000 * 1000);
        const uint32_t frameTimestampDuration = video->sender->rtpConfig->secondsToTimestamp(frameDuration_s);
        video->sender->rtpConfig->timestamp = video->sender->rtpConfig->startTimestamp - frameTimestampDuration * 2;
        video->track->send(initialNalus);
        video->sender->rtpConfig->timestamp += frameTimestampDuration;
        // Send initial NAL units again to start stream in firefox browser
        video->track->send(initialNalus);
    }
}

/// Add client to stream
/// @param client Client
/// @param adding_video True if adding video
void addToStream(std::shared_ptr<Client> client, bool isAddingVideo) {
    if (client->getState() == Client::State::Waiting) {
        client->setState(isAddingVideo ? Client::State::WaitingForAudio : Client::State::WaitingForVideo);
    } else if ((client->getState() == Client::State::WaitingForAudio && !isAddingVideo)
               || (client->getState() == Client::State::WaitingForVideo && isAddingVideo)) {

        // Audio and video tracks are collected now
        assert(client->video.has_value() && client->audio.has_value());

        auto video = client->video.value();
        auto audio = client->audio.value();

        auto currentTime_us = double(currentTimeInMicroSeconds());
        auto currentTime_s = currentTime_us / (1000 * 1000);

        // set start time of stream
        video->sender->rtpConfig->setStartTime(currentTime_s, RtpPacketizationConfig::EpochStart::T1970);
        audio->sender->rtpConfig->setStartTime(currentTime_s, RtpPacketizationConfig::EpochStart::T1970);

        // start stat recording of RTCP SR
        video->sender->startRecording();
        audio->sender->startRecording();

        if (avStream.has_value()) {
            sendInitialNalus(avStream.value(), video);
        }

        client->setState(Client::State::Ready);
    }
    if (client->getState() == Client::State::Ready) {
        startStream();
    }
}
