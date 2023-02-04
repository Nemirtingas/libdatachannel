/**
 * Copyright (c) 2019-2021 Paul-Louis Ageneau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef RTC_IMPL_SCTP_TRANSPORT_H
#define RTC_IMPL_SCTP_TRANSPORT_H

#include "common.hpp"
#include "configuration.hpp"
#include "global.hpp"
#include "processor.hpp"
#include "queue.hpp"
#include "transport.hpp"

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>

#include "usrsctp.h"

namespace rtc {
namespace impl {

class SctpTransport final : public Transport, public std::enable_shared_from_this<SctpTransport> {
public:
	static void Init();
	static void SetSettings(const SctpSettings &s);
	static void Cleanup();

	using amount_callback = std::function<void(uint16_t streamId, size_t amount)>;

	struct Ports {
		uint16_t local = DEFAULT_SCTP_PORT;
		uint16_t remote = DEFAULT_SCTP_PORT;
	};

	SctpTransport(shared_ptr<Transport> lower, const Configuration &config, Ports ports,
	              message_callback recvCallback, amount_callback bufferedAmountCallback,
	              state_callback stateChangeCallback);
	~SctpTransport();

	void onBufferedAmount(amount_callback callback);

	void start() override;
	void stop() override;
	bool send(message_ptr message) override; // false if buffered
	bool flush();
	void closeStream(unsigned int stream);
	void close();

	unsigned int maxStream() const;

	// Stats
	void clearStats();
	size_t bytesSent();
	size_t bytesReceived();
	optional<std::chrono::milliseconds> rtt();

private:
	// Order seems wrong but these are the actual values
	// See https://datatracker.ietf.org/doc/html/draft-ietf-rtcweb-data-channel-13#section-8
	enum PayloadId : uint32_t {
		PPID_CONTROL = 50,
		PPID_STRING = 51,
		PPID_BINARY_PARTIAL = 52,
		PPID_BINARY = 53,
		PPID_STRING_PARTIAL = 54,
		PPID_STRING_EMPTY = 56,
		PPID_BINARY_EMPTY = 57
	};

	struct sockaddr_conn getSockAddrConn(uint16_t port);

	void connect();
	void shutdown();
	void incoming(message_ptr message) override;
	bool outgoing(message_ptr message) override;

	void doRecv();
	void doFlush();
	void enqueueRecv();
	void enqueueFlush();
	bool trySendQueue();
	bool trySendMessage(message_ptr message);
	void updateBufferedAmount(uint16_t streamId, ptrdiff_t delta);
	void triggerBufferedAmount(uint16_t streamId, size_t amount);
	void sendReset(uint16_t streamId);

	void handleUpcall() noexcept;
	int handleWrite(byte *data, size_t len, uint8_t tos, uint8_t set_df) noexcept;

	void processData(binary &&data, uint16_t streamId, PayloadId ppid);
	void processNotification(const union sctp_notification *notify, size_t len);

	const Ports mPorts;
	struct socket *mSock;
	boost::optional<uint16_t> mNegotiatedStreamsCount;

	Processor mProcessor;
	boost::atomic<int> mPendingRecvCount;
	boost::atomic<int> mPendingFlushCount;
	std::mutex mRecvMutex;
	std::recursive_mutex mSendMutex; // buffered amount callback is synchronous
	Queue<message_ptr> mSendQueue;
	bool mSendShutdown;
	std::map<uint16_t, size_t> mBufferedAmount;
	amount_callback mBufferedAmountCallback;

	std::mutex mWriteMutex;
	std::condition_variable mWrittenCondition;
	boost::atomic<bool> mWritten;     // written outside lock
	boost::atomic<bool> mWrittenOnce; // same

	binary mPartialMessage, mPartialNotification;
	binary mPartialStringData, mPartialBinaryData;

	// Stats
	boost::atomic<size_t> mBytesSent, mBytesReceived;

	static void UpcallCallback(struct socket *sock, void *arg, int flags);
	static int WriteCallback(void *sctp_ptr, void *data, size_t len, uint8_t tos, uint8_t set_df);
	static void DebugCallback(const char *format, ...);

	class InstancesSet;
	static InstancesSet *Instances;
};

} // namespace impl
} // namespace rtc

#endif
