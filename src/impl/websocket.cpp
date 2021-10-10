/**
 * Copyright (c) 2020-2021 Paul-Louis Ageneau
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if RTC_ENABLE_WEBSOCKET

#include "websocket.hpp"
#include "common.hpp"
#include "internals.hpp"
#include "threadpool.hpp"

#include "tcptransport.hpp"
#include "tlstransport.hpp"
#include "verifiedtlstransport.hpp"
#include "wstransport.hpp"

#include <regex>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace rtc{
namespace impl {

using namespace std::placeholders;

WebSocket::WebSocket(optional<Configuration> optConfig, certificate_ptr certificate)
    : config(optConfig ? std::move(*optConfig) : Configuration()),
      mCertificate(std::move(certificate)), mIsSecure(mCertificate != nullptr),
      mRecvQueue(RECV_QUEUE_LIMIT, message_size_func),
	state(State::Closed)
{
	PLOG_VERBOSE << "Creating WebSocket";
}

WebSocket::~WebSocket() {
	PLOG_VERBOSE << "Destroying WebSocket";
	remoteClose();
}

void WebSocket::open(const string &url) {
	PLOG_VERBOSE << "Opening WebSocket to URL: " << url;

	if (state != State::Closed)
		throw std::logic_error("WebSocket must be closed before opening");

	// Modified regex from RFC 3986, see https://tools.ietf.org/html/rfc3986#appendix-B
	static const char *rs =
	    R"(^(([^:.@/?#]+):)?(/{0,2}((([^:@]*)(:([^@]*))?)@)?(([^:/?#]*)(:([^/?#]*))?))?([^?#]*)(\?([^#]*))?(#(.*))?)";

	static const std::regex r(rs, std::regex::extended);

	std::smatch m;
	if (!std::regex_match(url, m, r) || m[10].length() == 0)
		throw std::invalid_argument("Invalid WebSocket URL: " + url);

	string scheme = m[2];
	if (scheme.empty())
		scheme = "ws";

	if (scheme != "ws" && scheme != "wss")
		throw std::invalid_argument("Invalid WebSocket scheme: " + scheme);

	mIsSecure = (scheme != "ws");

	string host;
	string hostname = m[10];
	string service = m[12];
	if (service.empty()) {
		service = mIsSecure ? "443" : "80";
		host = hostname;
	} else {
		host = hostname + ':' + service;
	}

	while (!hostname.empty() && hostname.front() == '[')
		hostname.erase(hostname.begin());
	while (!hostname.empty() && hostname.back() == ']')
		hostname.pop_back();

	string path = m[13];
	if (path.empty())
		path += '/';

	{
		string query = m[15];
		if (!query.empty())
			path += "?" + query;
	}

	mHostname = hostname; // for TLS SNI
	boost::atomic_store(&mWsHandshake, boost::make_shared<WsHandshake>(host, path, config.protocols));

	changeState(State::Connecting);
	setTcpTransport(boost::make_shared<TcpTransport>(hostname, service, nullptr));
}

void WebSocket::close() {
	auto s = state.load();
	if (s == State::Connecting || s == State::Open) {
		PLOG_VERBOSE << "Closing WebSocket";
		changeState(State::Closing);
		if (auto transport = boost::atomic_load(&mWsTransport))
			transport->close();
		else
			changeState(State::Closed);
	}
}

void WebSocket::remoteClose() {
	if (state.load() != State::Closed) {
		close();
		closeTransports();
	}
}

bool WebSocket::isOpen() const { return state == State::Open; }

bool WebSocket::isClosed() const { return state == State::Closed; }

size_t WebSocket::maxMessageSize() const { return DEFAULT_MAX_MESSAGE_SIZE; }

optional<message_variant> WebSocket::receive() {
	while (auto next = mRecvQueue.tryPop()) {
		message_ptr message = *next;
		if (message->type != Message::Control)
			return to_variant(std::move(*message));
	}
	return none;
}

optional<message_variant> WebSocket::peek() {
	while (auto next = mRecvQueue.peek()) {
		message_ptr message = *next;
		if (message->type != Message::Control)
			return to_variant(std::move(*message));

		mRecvQueue.tryPop();
	}
	return none;
}

size_t WebSocket::availableAmount() const { return mRecvQueue.amount(); }

bool WebSocket::changeState(State newState) { return state.exchange(newState) != newState; }

bool WebSocket::outgoing(message_ptr message) {
	if (state != State::Open || !mWsTransport)
		throw std::runtime_error("WebSocket is not open");

	if (message->size() > maxMessageSize())
		throw std::runtime_error("Message size exceeds limit");

	return mWsTransport->send(message);
}

void WebSocket::incoming(message_ptr message) {
	if (!message) {
		remoteClose();
		return;
	}

	if (message->type == Message::String || message->type == Message::Binary) {
		mRecvQueue.push(message);
		triggerAvailable(mRecvQueue.size());
	}
}

shared_ptr<TcpTransport> WebSocket::setTcpTransport(shared_ptr<TcpTransport> transport) {
	PLOG_VERBOSE << "Starting TCP transport";

	if (!transport)
		throw std::logic_error("TCP transport is null");

	using State = TcpTransport::State;
	try {
		if (boost::atomic_load(&mTcpTransport))
			throw std::logic_error("TCP transport is already set");

		transport->onStateChange([this, weak_this = weak_from_this()](State transportState) {
			auto shared_this = weak_this.lock();
			if (!shared_this)
				return;
			switch (transportState) {
			case State::Connected:
				if (mIsSecure)
					initTlsTransport();
				else
					initWsTransport();
				break;
			case State::Failed:
				triggerError("TCP connection failed");
				remoteClose();
				break;
			case State::Disconnected:
				remoteClose();
				break;
			default:
				// Ignore
				break;
			}
		});

		boost::atomic_store(&mTcpTransport, transport);
		if (state == WebSocket::State::Closed) {
			std::atomic_store(&mTcpTransport, decltype(mTcpTransport)(nullptr));
			throw std::runtime_error("Connection is closed");
		}
		transport->start();
		return transport;

	} catch (const std::exception &e) {
		PLOG_ERROR << e.what();
		remoteClose();
		throw std::runtime_error("TCP transport initialization failed");
	}
}

shared_ptr<TlsTransport> WebSocket::initTlsTransport() {
	PLOG_VERBOSE << "Starting TLS transport";
	using State = TlsTransport::State;
	try {
		if (auto transport = boost::atomic_load(&mTlsTransport))
			return transport;

		auto lower = boost::atomic_load(&mTcpTransport);
		if (!lower)
			throw std::logic_error("No underlying TCP transport for TLS transport");

		auto stateChangeCallback = [this, weak_this = weak_from_this()](State transportState) {
			auto shared_this = weak_this.lock();
			if (!shared_this)
				return;
			switch (transportState) {
			case State::Connected:
				initWsTransport();
				break;
			case State::Failed:
				triggerError("TCP connection failed");
				remoteClose();
				break;
			case State::Disconnected:
				remoteClose();
				break;
			default:
				// Ignore
				break;
			}
		};

		bool verify = mHostname.has_value() && !config.disableTlsVerification;

#ifdef _WIN32
		if (std::exchange(verify, false)) {
			PLOG_WARNING << "TLS certificate verification with root CA is not supported on Windows";
		}
#endif

		shared_ptr<TlsTransport> transport;
		if (verify)
			transport = boost::make_shared<VerifiedTlsTransport>(lower, mHostname.value(),
			                                                   mCertificate, stateChangeCallback);
		else
			transport =
			    boost::make_shared<TlsTransport>(lower, mHostname, mCertificate, stateChangeCallback);

		boost::atomic_store(&mTlsTransport, transport);
		if (state == WebSocket::State::Closed) {
			std::atomic_store(&mTlsTransport, decltype(mTlsTransport)(nullptr));
			throw std::runtime_error("Connection is closed");
		}
		transport->start();
		return transport;

	} catch (const std::exception &e) {
		PLOG_ERROR << e.what();
		remoteClose();
		throw std::runtime_error("TLS transport initialization failed");
	}
}

shared_ptr<WsTransport> WebSocket::initWsTransport() {
	PLOG_VERBOSE << "Starting WebSocket transport";
	using State = WsTransport::State;
	try {
		if (auto transport = boost::atomic_load(&mWsTransport))
			return transport;

		variant<shared_ptr<TcpTransport>, shared_ptr<TlsTransport>> lower;
		if (mIsSecure) {
			auto transport = boost::atomic_load(&mTlsTransport);
			if (!transport)
				throw std::logic_error("No underlying TLS transport for WebSocket transport");

			lower = transport;
		} else {
			auto transport = boost::atomic_load(&mTcpTransport);
			if (!transport)
				throw std::logic_error("No underlying TCP transport for WebSocket transport");

			lower = transport;
		}

		if (!atomic_load(&mWsHandshake))
			atomic_store(&mWsHandshake, boost::make_shared<WsHandshake>());

		auto stateChangeCallback = [this, weak_this = weak_from_this()](State transportState) {
			auto shared_this = weak_this.lock();
			if (!shared_this)
				return;
			switch (transportState) {
			case State::Connected:
				if (state == WebSocket::State::Connecting) {
					PLOG_DEBUG << "WebSocket open";
					changeState(WebSocket::State::Open);
					triggerOpen();
				}
				break;
			case State::Failed:
				triggerError("WebSocket connection failed");
				remoteClose();
				break;
			case State::Disconnected:
				remoteClose();
				break;
			default:
				// Ignore
				break;
			}
		};

		auto transport = boost::make_shared<WsTransport>(
		    lower, mWsHandshake, weak_bind(&WebSocket::incoming, this, _1), stateChangeCallback);

		boost::atomic_store(&mWsTransport, transport);
		if (state == WebSocket::State::Closed) {
			std::atomic_store(&mWsTransport, decltype(mWsTransport)(nullptr));
			throw std::runtime_error("Connection is closed");
		}
		transport->start();
		return transport;

	} catch (const std::exception &e) {
		PLOG_ERROR << e.what();
		remoteClose();
		throw std::runtime_error("WebSocket transport initialization failed");
	}
}

shared_ptr<TcpTransport> WebSocket::getTcpTransport() const {
	return boost::atomic_load(&mTcpTransport);
}

shared_ptr<TlsTransport> WebSocket::getTlsTransport() const {
	return boost::atomic_load(&mTlsTransport);
}

shared_ptr<WsTransport> WebSocket::getWsTransport() const {
	return boost::atomic_load(&mWsTransport);
}

shared_ptr<WsHandshake> WebSocket::getWsHandshake() const {
	return boost::atomic_load(&mWsHandshake);
}

void WebSocket::closeTransports() {
	PLOG_VERBOSE << "Closing transports";

	if (state.load() != State::Closed) {
		changeState(State::Closed);
		triggerClosed();
	}

	// Reset callbacks now that state is changed
	resetCallbacks();

	// Pass the pointers to a thread, allowing to terminate a transport from its own thread
	auto ws = boost::atomic_exchange(&mWsTransport, decltype(mWsTransport)(nullptr));
	auto tls = boost::atomic_exchange(&mTlsTransport, decltype(mTlsTransport)(nullptr));
	auto tcp = boost::atomic_exchange(&mTcpTransport, decltype(mTcpTransport)(nullptr));
	ThreadPool::Instance().enqueue([ws, tls, tcp]() mutable {
		if (ws)
			ws->stop();
		if (tls)
			tls->stop();
		if (tcp)
			tcp->stop();

		ws.reset();
		tls.reset();
		tcp.reset();
	});
}

} // namespace impl
} // namespace rtc

#endif
