/**
 * Copyright (c) 2019-2020 Paul-Louis Ageneau
 * Copyright (c) 2020 Staz Modrzynski
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

#ifndef RTC_DESCRIPTION_H
#define RTC_DESCRIPTION_H

#include "candidate.hpp"
#include "include.hpp"

#include <iostream>
#include <map>
#include <memory>
#include <boost/optional.hpp>
#include <boost/variant.hpp>
#include <vector>

namespace rtc {

class Description {
public:
	enum class Type { Unspec, Offer, Answer, Pranswer, Rollback };
	enum class Role { ActPass, Passive, Active };
	enum class Direction { SendOnly, RecvOnly, SendRecv, Inactive, Unknown };

	Description(const string &sdp, Type type = Type::Unspec, Role role = Role::ActPass);
	Description(const string &sdp, string typeString);

	Type type() const;
	string typeString() const;
	Role role() const;
	string bundleMid() const;
	boost::optional<string> iceUfrag() const;
	boost::optional<string> icePwd() const;
	boost::optional<string> fingerprint() const;
	bool ended() const;

	void hintType(Type type);
	void setFingerprint(string fingerprint);

	void addCandidate(Candidate candidate);
	void addCandidates(std::vector<Candidate> candidates);
	void endCandidates();
	std::vector<Candidate> extractCandidates();

	operator string() const;
	string generateSdp(boost::string_view eol) const;
	string generateApplicationSdp(boost::string_view eol) const;

	class Entry {
	public:
		virtual ~Entry() = default;

		virtual string type() const { return mType; }
		virtual string description() const { return mDescription; }
		virtual string mid() const { return mMid; }
		Direction direction() const { return mDirection; }
		void setDirection(Direction dir);

		operator string() const;
		string generateSdp(boost::string_view eol, boost::string_view addr,
		                   boost::string_view port) const;

		virtual void parseSdpLine(boost::string_view line);

		std::vector<string>::iterator beginAttributes();
		std::vector<string>::iterator endAttributes();
		std::vector<string>::iterator removeAttribute(std::vector<string>::iterator iterator);

	protected:
		Entry(const string &mline, string mid, Direction dir = Direction::Unknown);
		virtual string generateSdpLines(boost::string_view eol) const;

		std::vector<string> mAttributes;

	private:
		string mType;
		string mDescription;
		string mMid;
		Direction mDirection;
	};

	struct Application : public Entry {
	public:
		Application(string mid = "data");
		virtual ~Application() = default;

		string description() const override;
		Application reciprocate() const;

		void setSctpPort(uint16_t port) { mSctpPort = port; }
		void hintSctpPort(uint16_t port) { mSctpPort = mSctpPort.value_or(port); }
		void setMaxMessageSize(size_t size) { mMaxMessageSize = size; }

		boost::optional<uint16_t> sctpPort() const { return mSctpPort; }
		boost::optional<size_t> maxMessageSize() const { return mMaxMessageSize; }

		virtual void parseSdpLine(boost::string_view line) override;

	private:
		virtual string generateSdpLines(boost::string_view eol) const override;

		boost::optional<uint16_t> mSctpPort;
		boost::optional<size_t> mMaxMessageSize;
	};

	// Media (non-data)
	class Media : public Entry {
	public:
		Media(const string &sdp);
		Media(const string &mline, string mid, Direction dir = Direction::SendOnly);
		virtual ~Media() = default;

		string description() const override;
		Media reciprocate() const;

		void removeFormat(const string &fmt);

		void addSSRC(uint32_t ssrc, std::string name);
		void addSSRC(uint32_t ssrc);
		void replaceSSRC(uint32_t oldSSRC, uint32_t ssrc, string name);
		bool hasSSRC(uint32_t ssrc);
		std::vector<uint32_t> getSSRCs();

		void setBitrate(int bitrate);
		int getBitrate() const;

		bool hasPayloadType(int payloadType) const;

		virtual void parseSdpLine(boost::string_view line) override;

		struct RTPMap {
			RTPMap(boost::string_view mline);
			RTPMap() {}

			void removeFB(const string &string);
			void addFB(const string &string);
			void addAttribute(std::string attr) { fmtps.emplace_back(attr); }

			int pt;
			string format;
			int clockRate;
			string encParams;

			std::vector<string> rtcpFbs;
			std::vector<string> fmtps;

			static int parsePT(boost::string_view view);
			void setMLine(boost::string_view view);
		};

		std::map<int, RTPMap>::iterator beginMaps();
		std::map<int, RTPMap>::iterator endMaps();
		std::map<int, RTPMap>::iterator removeMap(std::map<int, RTPMap>::iterator iterator);

	private:
		virtual string generateSdpLines(boost::string_view eol) const override;

		int mBas = -1;

		Media::RTPMap &getFormat(int fmt);
		Media::RTPMap &getFormat(const string &fmt);

		std::map<int, RTPMap> mRtpMap;
		std::vector<uint32_t> mSsrcs;

	public:
		void addRTPMap(const RTPMap &map);
	};

	class Audio : public Media {
	public:
		Audio(string mid = "audio", Direction dir = Direction::SendOnly);

		void addAudioCodec(int payloadType, const string &codec);
		void addOpusCodec(int payloadType);
	};

	class Video : public Media {
	public:
		Video(string mid = "video", Direction dir = Direction::SendOnly);

		void addVideoCodec(int payloadType, const string &codec);
		void addH264Codec(int payloadType);
		void addVP8Codec(int payloadType);
		void addVP9Codec(int payloadType);
	};

	bool hasApplication() const;
	bool hasAudioOrVideo() const;
	bool hasMid(boost::string_view mid) const;

	int addMedia(Media media);
	int addMedia(Application application);
	int addApplication(string mid = "data");
	int addVideo(string mid = "video", Direction dir = Direction::SendOnly);
	int addAudio(string mid = "audio", Direction dir = Direction::SendOnly);

	boost::variant<Media *, Application *> media(unsigned int index);
	boost::variant<const Media *, const Application *> media(unsigned int index) const;
	unsigned int mediaCount() const;

	Application *application();

	static Type stringToType(const string &typeString);
	static string typeToString(Type type);

private:
	boost::optional<Candidate> defaultCandidate() const;
	std::shared_ptr<Entry> createEntry(string mline, string mid, Direction dir);
	void removeApplication();

	Type mType;

	// Session-level attributes
	Role mRole;
	string mUsername;
	string mSessionId;
	boost::optional<string> mIceUfrag, mIcePwd;
	boost::optional<string> mFingerprint;

	// Entries
	std::vector<std::shared_ptr<Entry>> mEntries;
	std::shared_ptr<Application> mApplication;

	// Candidates
	std::vector<Candidate> mCandidates;
	bool mEnded = false;
};

} // namespace rtc

std::ostream &operator<<(std::ostream &out, const rtc::Description &description);
std::ostream &operator<<(std::ostream &out, rtc::Description::Type type);
std::ostream &operator<<(std::ostream &out, rtc::Description::Role role);

#endif
