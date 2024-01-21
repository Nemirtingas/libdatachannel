/**
 * Copyright (c) 2020-2021 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_IMPL_TRACK_H
#define RTC_IMPL_TRACK_H

#include "channel.hpp"
#include "common.hpp"
#include "description.hpp"
#include "mediahandler.hpp"
#include "queue.hpp"

#if RTC_ENABLE_MEDIA
#include "dtlssrtptransport.hpp"
#endif

#include <boost/atomic.hpp>
#include <boost/thread.hpp>

namespace rtc {
namespace impl {

struct PeerConnection;

class Track final : public std::enable_shared_from_this<Track>, public Channel {
public:
	Track(weak_ptr<PeerConnection> pc, Description::Media desc);
	~Track();

	void close();
	void incoming(message_ptr message);
	bool outgoing(message_ptr message);

	optional<message_variant> receive() override;
	optional<message_variant> peek() override;
	size_t availableAmount() const override;

	bool isOpen() const;
	bool isClosed() const;
	size_t maxMessageSize() const;

	string mid() const;
	Description::Direction direction() const;
	Description::Media description() const;
	void setDescription(Description::Media desc);

	shared_ptr<MediaHandler> getMediaHandler();
	void setMediaHandler(shared_ptr<MediaHandler> handler);

#if RTC_ENABLE_MEDIA
	void open(shared_ptr<DtlsSrtpTransport> transport);
#endif

	bool transportSend(message_ptr message);

private:
	const weak_ptr<PeerConnection> mPeerConnection;
#if RTC_ENABLE_MEDIA
	weak_ptr<DtlsSrtpTransport> mDtlsSrtpTransport;
#endif

	Description::Media mMediaDescription;
	shared_ptr<MediaHandler> mMediaHandler;

	mutable boost::shared_mutex mMutex;

	boost::atomic<bool> mIsClosed;

	Queue<message_ptr> mRecvQueue;
};

} // namespace impl
} // namespace rtc

#endif
