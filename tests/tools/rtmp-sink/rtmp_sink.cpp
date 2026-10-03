// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 RelayDock contributors
//
// rd-rtmp-sink: a small RTMP ingest server for RelayDock's integration tests.
//
// It accepts publishers the way a streaming platform does, counts what arrives, and writes a
// JSON report that the tests read. It never stores or forwards the media.
//
// The stream key selects how the sink treats a publisher, so one sink can play a healthy
// platform and a broken one at the same time:
//
//   ok-<anything>          Accept and count. Any key without a known prefix does this too.
//   reject-<anything>      Refuse the publish request, like a platform with a wrong key.
//   drop<N>-<anything>     Cut the connection N seconds after publishing starts, every time.
//   droponce<N>-<anything> Cut the connection once. The next connection with the key is accepted.
//   stall<N>-<anything>    Stop reading after N seconds, like a congested network.
//
// Usage:
//   rd-rtmp-sink --port 19350 --report sink-report.json [--bind 127.0.0.1] [--blackhole-port 19351]
//
// A blackhole port accepts TCP connections and then says nothing, like a server that hangs.
// It keeps a client in its "connecting" state until the client gives up.
//
// The sink runs until a file named <report>.stop appears, then writes a final report and exits.
//
// It listens on 127.0.0.1 unless told otherwise. Use test keys only. The report contains every
// key the sink saw.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

// ---- Shared state ---------------------------------------------------------------------------

struct SessionRecord {
	int64_t startMs = 0;
	int64_t endMs = 0;
	uint64_t bytes = 0;
	std::string ended = "open"; // open, client, dropped, rejected, stalled, error
};

struct StreamStats {
	std::string app;
	int sessions = 0;
	int rejected = 0;
	bool publishing = false;
	uint64_t videoBytes = 0;
	uint64_t audioBytes = 0;
	uint64_t videoMessages = 0;
	uint64_t audioMessages = 0;
	uint64_t keyframes = 0;
	std::string videoCodec;
	int64_t lastDataMs = 0;
	// The newest session only, for measuring the real frame rate.
	uint64_t sessionVideoMessages = 0;
	int64_t sessionFirstVideoMs = 0;
	int64_t sessionLastVideoMs = 0;
	std::map<std::string, double> metadataNumbers;
	std::map<std::string, std::string> metadataStrings;
	std::vector<SessionRecord> sessionRecords;
};

struct State {
	std::mutex mutex;
	std::map<std::string, StreamStats> streams;
	std::set<std::string> droppedOnce;
	int connectionsTotal = 0;
	int connectionsOpen = 0;
	int64_t startedMs = nowMs();
	std::atomic<bool> stopping{false};
};

State g_state;

// ---- Socket helpers -------------------------------------------------------------------------

bool recvAll(SOCKET socket, uint8_t *buffer, size_t size)
{
	size_t done = 0;
	while (done < size) {
		const int got = recv(socket, reinterpret_cast<char *>(buffer + done), static_cast<int>(size - done), 0);
		if (got <= 0)
			return false;
		done += static_cast<size_t>(got);
	}
	return true;
}

bool sendAll(SOCKET socket, const uint8_t *buffer, size_t size)
{
	size_t done = 0;
	while (done < size) {
		const int sent = send(socket, reinterpret_cast<const char *>(buffer + done), static_cast<int>(size - done), 0);
		if (sent <= 0)
			return false;
		done += static_cast<size_t>(sent);
	}
	return true;
}

void hardClose(SOCKET socket)
{
	// Linger 0 sends a reset, which is what a dropped network connection looks like.
	linger option{1, 0};
	setsockopt(socket, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char *>(&option), sizeof(option));
	closesocket(socket);
}

// ---- AMF0 -----------------------------------------------------------------------------------

struct AmfValue {
	enum class Type { Number, Boolean, String, Object, Null, Other } type = Type::Null;
	double number = 0.0;
	bool boolean = false;
	std::string string;
	std::map<std::string, AmfValue> object;
};

class AmfReader {
public:
	AmfReader(const uint8_t *data, size_t size) : data_(data), size_(size) {}

	bool atEnd() const { return pos_ >= size_; }

	bool read(AmfValue &out)
	{
		if (pos_ >= size_)
			return false;
		const uint8_t marker = data_[pos_++];
		switch (marker) {
		case 0x00: { // number
			if (pos_ + 8 > size_)
				return false;
			uint64_t bits = 0;
			for (int i = 0; i < 8; ++i)
				bits = (bits << 8) | data_[pos_ + i];
			pos_ += 8;
			out.type = AmfValue::Type::Number;
			std::memcpy(&out.number, &bits, sizeof(double));
			return true;
		}
		case 0x01: // boolean
			if (pos_ + 1 > size_)
				return false;
			out.type = AmfValue::Type::Boolean;
			out.boolean = data_[pos_++] != 0;
			return true;
		case 0x02: // string
			out.type = AmfValue::Type::String;
			return readShortString(out.string);
		case 0x03: // object
			out.type = AmfValue::Type::Object;
			return readProperties(out.object);
		case 0x05: // null
		case 0x06: // undefined
			out.type = AmfValue::Type::Null;
			return true;
		case 0x08: // ECMA array: a count, then properties like an object
			if (pos_ + 4 > size_)
				return false;
			pos_ += 4;
			out.type = AmfValue::Type::Object;
			return readProperties(out.object);
		case 0x0A: { // strict array
			if (pos_ + 4 > size_)
				return false;
			uint32_t count = 0;
			for (int i = 0; i < 4; ++i)
				count = (count << 8) | data_[pos_ + i];
			pos_ += 4;
			out.type = AmfValue::Type::Other;
			for (uint32_t i = 0; i < count; ++i) {
				AmfValue ignored;
				if (!read(ignored))
					return false;
			}
			return true;
		}
		case 0x0C: { // long string
			if (pos_ + 4 > size_)
				return false;
			uint32_t length = 0;
			for (int i = 0; i < 4; ++i)
				length = (length << 8) | data_[pos_ + i];
			pos_ += 4;
			if (pos_ + length > size_)
				return false;
			out.type = AmfValue::Type::String;
			out.string.assign(reinterpret_cast<const char *>(data_ + pos_), length);
			pos_ += length;
			return true;
		}
		default:
			return false;
		}
	}

private:
	bool readShortString(std::string &out)
	{
		if (pos_ + 2 > size_)
			return false;
		const size_t length = (static_cast<size_t>(data_[pos_]) << 8) | data_[pos_ + 1];
		pos_ += 2;
		if (pos_ + length > size_)
			return false;
		out.assign(reinterpret_cast<const char *>(data_ + pos_), length);
		pos_ += length;
		return true;
	}

	bool readProperties(std::map<std::string, AmfValue> &out)
	{
		for (;;) {
			std::string name;
			if (!readShortString(name))
				return false;
			if (name.empty()) {
				if (pos_ < size_ && data_[pos_] == 0x09) {
					++pos_;
					return true;
				}
				return false;
			}
			AmfValue value;
			if (!read(value))
				return false;
			out[name] = std::move(value);
		}
	}

	const uint8_t *data_;
	size_t size_;
	size_t pos_ = 0;
};

class AmfWriter {
public:
	void number(double value)
	{
		bytes_.push_back(0x00);
		uint64_t bits = 0;
		std::memcpy(&bits, &value, sizeof(double));
		for (int i = 7; i >= 0; --i)
			bytes_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
	}

	void string(const std::string &value)
	{
		bytes_.push_back(0x02);
		rawString(value);
	}

	void null() { bytes_.push_back(0x05); }
	void beginObject() { bytes_.push_back(0x03); }
	void endObject()
	{
		bytes_.push_back(0x00);
		bytes_.push_back(0x00);
		bytes_.push_back(0x09);
	}

	void propertyString(const std::string &name, const std::string &value)
	{
		rawString(name);
		string(value);
	}

	void propertyNumber(const std::string &name, double value)
	{
		rawString(name);
		number(value);
	}

	const std::vector<uint8_t> &bytes() const { return bytes_; }

private:
	void rawString(const std::string &value)
	{
		bytes_.push_back(static_cast<uint8_t>((value.size() >> 8) & 0xFF));
		bytes_.push_back(static_cast<uint8_t>(value.size() & 0xFF));
		bytes_.insert(bytes_.end(), value.begin(), value.end());
	}

	std::vector<uint8_t> bytes_;
};

// ---- One RTMP connection ----------------------------------------------------------------------

enum class Behaviour { Ok, Reject, Drop, DropOnce, Stall };

struct KeyPlan {
	Behaviour behaviour = Behaviour::Ok;
	int seconds = 0;
};

KeyPlan planForKey(const std::string &key)
{
	auto numberAfter = [&](size_t prefixLength, int &seconds) {
		size_t i = prefixLength;
		int value = 0;
		bool any = false;
		while (i < key.size() && key[i] >= '0' && key[i] <= '9') {
			value = value * 10 + (key[i] - '0');
			++i;
			any = true;
		}
		seconds = value;
		return any && i < key.size() && key[i] == '-';
	};

	KeyPlan plan;
	if (key.rfind("reject-", 0) == 0) {
		plan.behaviour = Behaviour::Reject;
	} else if (key.rfind("droponce", 0) == 0 && numberAfter(8, plan.seconds)) {
		plan.behaviour = Behaviour::DropOnce;
	} else if (key.rfind("drop", 0) == 0 && numberAfter(4, plan.seconds)) {
		plan.behaviour = Behaviour::Drop;
	} else if (key.rfind("stall", 0) == 0 && numberAfter(5, plan.seconds)) {
		plan.behaviour = Behaviour::Stall;
	}
	return plan;
}

struct ChunkStream {
	uint32_t timestamp = 0;
	uint32_t length = 0;
	uint8_t typeId = 0;
	uint32_t streamId = 0;
	bool extendedTimestamp = false;
	std::vector<uint8_t> payload;
};

class Connection {
public:
	explicit Connection(SOCKET socket) : socket_(socket) {}

	void run()
	{
		const std::string ended = serve();
		finish(ended);
	}

private:
	std::string serve()
	{
		if (!handshake())
			return "error";

		for (;;) {
			if (g_state.stopping)
				return "client";

			if (publishing_ && plan_.behaviour != Behaviour::Ok && plan_.behaviour != Behaviour::Reject) {
				const int64_t elapsed = nowMs() - publishStartMs_;
				if (elapsed >= static_cast<int64_t>(plan_.seconds) * 1000) {
					if (plan_.behaviour == Behaviour::Stall) {
						// Keep the socket open and stop reading until the sink exits.
						while (!g_state.stopping)
							std::this_thread::sleep_for(std::chrono::milliseconds(100));
						return "stalled";
					}
					if (plan_.behaviour == Behaviour::DropOnce) {
						std::lock_guard<std::mutex> lock(g_state.mutex);
						g_state.droppedOnce.insert(key_);
					}
					hardClose(socket_);
					socket_ = INVALID_SOCKET;
					return "dropped";
				}
			}

			// Wake up regularly so a drop or stall happens on time even with no data.
			fd_set readable;
			FD_ZERO(&readable);
			FD_SET(socket_, &readable);
			timeval timeout{0, 100 * 1000};
			const int ready = select(0, &readable, nullptr, nullptr, &timeout);
			if (ready < 0)
				return "error";
			if (ready == 0)
				continue;

			const int outcome = readChunk();
			if (outcome < 0)
				return "client";
			if (outcome > 0)
				return rejected_ ? "rejected" : "client";
		}
	}

	bool handshake()
	{
		uint8_t c0c1[1 + 1536];
		if (!recvAll(socket_, c0c1, sizeof(c0c1)) || c0c1[0] != 0x03)
			return false;

		std::vector<uint8_t> reply(1 + 1536 + 1536, 0);
		reply[0] = 0x03;
		std::mt19937 random{std::random_device{}()};
		for (size_t i = 9; i < 1 + 1536; ++i)
			reply[i] = static_cast<uint8_t>(random() & 0xFF);
		std::memcpy(reply.data() + 1 + 1536, c0c1 + 1, 1536); // S2 echoes C1
		if (!sendAll(socket_, reply.data(), reply.size()))
			return false;

		uint8_t c2[1536];
		return recvAll(socket_, c2, sizeof(c2));
	}

	// Returns 0 to continue, -1 when the peer closed, 1 when this side decided to close.
	int readChunk()
	{
		uint8_t first = 0;
		if (!recvAll(socket_, &first, 1))
			return -1;

		const uint8_t format = first >> 6;
		uint32_t chunkStreamId = first & 0x3F;
		if (chunkStreamId == 0) {
			uint8_t extra = 0;
			if (!recvAll(socket_, &extra, 1))
				return -1;
			chunkStreamId = 64 + extra;
		} else if (chunkStreamId == 1) {
			uint8_t extra[2];
			if (!recvAll(socket_, extra, 2))
				return -1;
			chunkStreamId = 64 + extra[0] + (static_cast<uint32_t>(extra[1]) << 8);
		}

		ChunkStream &stream = chunkStreams_[chunkStreamId];

		if (format <= 2) {
			uint8_t header[11];
			const size_t headerSize = format == 0 ? 11 : format == 1 ? 7 : 3;
			if (!recvAll(socket_, header, headerSize))
				return -1;
			const uint32_t timestamp = (static_cast<uint32_t>(header[0]) << 16) |
						   (static_cast<uint32_t>(header[1]) << 8) | header[2];
			stream.extendedTimestamp = timestamp == 0xFFFFFF;
			if (format <= 1) {
				stream.length = (static_cast<uint32_t>(header[3]) << 16) |
						(static_cast<uint32_t>(header[4]) << 8) | header[5];
				stream.typeId = header[6];
			}
			if (format == 0) {
				stream.streamId = header[7] | (static_cast<uint32_t>(header[8]) << 8) |
						  (static_cast<uint32_t>(header[9]) << 16) |
						  (static_cast<uint32_t>(header[10]) << 24);
			}
			stream.timestamp = timestamp;
		}
		if (stream.extendedTimestamp) {
			uint8_t extended[4];
			if (!recvAll(socket_, extended, 4))
				return -1;
		}

		if (stream.length > 16 * 1024 * 1024)
			return 1; // Not a sane RTMP message.

		const size_t remaining = stream.length - stream.payload.size();
		const size_t toRead = std::min<size_t>(remaining, inChunkSize_);
		const size_t offset = stream.payload.size();
		stream.payload.resize(offset + toRead);
		if (toRead > 0 && !recvAll(socket_, stream.payload.data() + offset, toRead))
			return -1;

		bytesSinceAck_ += toRead;
		sessionBytes_ += toRead;
		if (bytesSinceAck_ >= 1250000) {
			totalAcked_ += static_cast<uint32_t>(bytesSinceAck_);
			bytesSinceAck_ = 0;
			uint8_t ack[4] = {static_cast<uint8_t>(totalAcked_ >> 24), static_cast<uint8_t>(totalAcked_ >> 16),
					  static_cast<uint8_t>(totalAcked_ >> 8), static_cast<uint8_t>(totalAcked_)};
			if (!sendMessage(2, 3, 0, ack, sizeof(ack)))
				return -1;
		}

		if (stream.payload.size() < stream.length)
			return 0;

		std::vector<uint8_t> message;
		message.swap(stream.payload);
		return handleMessage(stream.typeId, message);
	}

	int handleMessage(uint8_t typeId, const std::vector<uint8_t> &message)
	{
		switch (typeId) {
		case 1: // Set Chunk Size
			if (message.size() >= 4) {
				const uint32_t size = ((static_cast<uint32_t>(message[0]) & 0x7F) << 24) |
						      (static_cast<uint32_t>(message[1]) << 16) |
						      (static_cast<uint32_t>(message[2]) << 8) | message[3];
				if (size >= 1)
					inChunkSize_ = size;
			}
			return 0;
		case 8: // audio
			if (publishing_) {
				std::lock_guard<std::mutex> lock(g_state.mutex);
				StreamStats &stats = g_state.streams[key_];
				stats.audioBytes += message.size();
				stats.audioMessages += 1;
				stats.lastDataMs = nowMs();
			}
			return 0;
		case 9: // video
			if (publishing_ && !message.empty())
				recordVideo(message);
			return 0;
		case 18: // AMF0 data, carries onMetaData
			recordMetadata(message);
			return 0;
		case 20: // AMF0 command
			return handleCommand(message);
		default:
			return 0;
		}
	}

	void recordVideo(const std::vector<uint8_t> &message)
	{
		const uint8_t first = message[0];
		bool keyframe = false;
		std::string codec;
		if (first & 0x80) {
			// Enhanced RTMP: frame type in bits 4 to 6, then a FourCC.
			keyframe = ((first >> 4) & 0x07) == 1;
			if (message.size() >= 5)
				codec.assign(reinterpret_cast<const char *>(message.data() + 1), 4);
		} else {
			keyframe = (first >> 4) == 1;
			const int codecId = first & 0x0F;
			codec = codecId == 7 ? "avc1" : "codec-" + std::to_string(codecId);
		}

		std::lock_guard<std::mutex> lock(g_state.mutex);
		StreamStats &stats = g_state.streams[key_];
		stats.videoBytes += message.size();
		stats.videoMessages += 1;
		if (keyframe)
			stats.keyframes += 1;
		if (stats.videoCodec.empty())
			stats.videoCodec = codec;
		stats.lastDataMs = nowMs();
		if (stats.sessionVideoMessages == 0)
			stats.sessionFirstVideoMs = stats.lastDataMs;
		stats.sessionLastVideoMs = stats.lastDataMs;
		stats.sessionVideoMessages += 1;
	}

	void recordMetadata(const std::vector<uint8_t> &message)
	{
		if (!publishing_)
			return;
		AmfReader reader(message.data(), message.size());
		AmfValue value;
		while (reader.read(value)) {
			if (value.type != AmfValue::Type::Object)
				continue;
			std::lock_guard<std::mutex> lock(g_state.mutex);
			StreamStats &stats = g_state.streams[key_];
			for (const auto &entry : value.object) {
				if (entry.second.type == AmfValue::Type::Number)
					stats.metadataNumbers[entry.first] = entry.second.number;
				else if (entry.second.type == AmfValue::Type::String)
					stats.metadataStrings[entry.first] = entry.second.string;
				else if (entry.second.type == AmfValue::Type::Boolean)
					stats.metadataNumbers[entry.first] = entry.second.boolean ? 1.0 : 0.0;
			}
		}
	}

	int handleCommand(const std::vector<uint8_t> &message)
	{
		AmfReader reader(message.data(), message.size());
		AmfValue name;
		AmfValue transaction;
		if (!reader.read(name) || name.type != AmfValue::Type::String || !reader.read(transaction))
			return 0;
		const double txn = transaction.number;

		if (name.string == "connect") {
			AmfValue command;
			if (reader.read(command) && command.type == AmfValue::Type::Object) {
				const auto app = command.object.find("app");
				if (app != command.object.end())
					app_ = app->second.string;
			}

			const uint8_t windowAck[4] = {0x00, 0x26, 0x25, 0xA0}; // 2500000
			const uint8_t peerBandwidth[5] = {0x00, 0x26, 0x25, 0xA0, 0x02};
			const uint8_t chunkSize[4] = {0x00, 0x00, 0x10, 0x00}; // 4096
			if (!sendMessage(2, 5, 0, windowAck, sizeof(windowAck)) ||
			    !sendMessage(2, 6, 0, peerBandwidth, sizeof(peerBandwidth)) ||
			    !sendMessage(2, 1, 0, chunkSize, sizeof(chunkSize)))
				return -1;
			outChunkSize_ = 4096;

			AmfWriter reply;
			reply.string("_result");
			reply.number(txn);
			reply.beginObject();
			reply.propertyString("fmsVer", "FMS/3,0,1,123");
			reply.propertyNumber("capabilities", 31);
			reply.endObject();
			reply.beginObject();
			reply.propertyString("level", "status");
			reply.propertyString("code", "NetConnection.Connect.Success");
			reply.propertyString("description", "Connection succeeded.");
			reply.propertyNumber("objectEncoding", 0);
			reply.endObject();
			return sendCommand(0, reply) ? 0 : -1;
		}

		if (name.string == "releaseStream" || name.string == "FCPublish") {
			AmfWriter reply;
			reply.string("_result");
			reply.number(txn);
			reply.null();
			return sendCommand(0, reply) ? 0 : -1;
		}

		if (name.string == "createStream") {
			AmfWriter reply;
			reply.string("_result");
			reply.number(txn);
			reply.null();
			reply.number(1);
			return sendCommand(0, reply) ? 0 : -1;
		}

		if (name.string == "publish") {
			AmfValue ignored;
			AmfValue key;
			reader.read(ignored); // command object, null
			if (reader.read(key) && key.type == AmfValue::Type::String)
				key_ = key.string;
			return startPublish();
		}

		if (name.string == "FCUnpublish" || name.string == "deleteStream")
			return 0;

		return 0;
	}

	int startPublish()
	{
		plan_ = planForKey(key_);

		bool accept = plan_.behaviour != Behaviour::Reject;
		{
			std::lock_guard<std::mutex> lock(g_state.mutex);
			if (plan_.behaviour == Behaviour::DropOnce && g_state.droppedOnce.count(key_) > 0)
				plan_.behaviour = Behaviour::Ok;

			StreamStats &stats = g_state.streams[key_];
			stats.app = app_;
			if (accept) {
				stats.sessions += 1;
				stats.publishing = true;
				stats.sessionVideoMessages = 0;
				stats.sessionFirstVideoMs = 0;
				stats.sessionLastVideoMs = 0;
			} else {
				stats.rejected += 1;
			}
		}

		AmfWriter status;
		status.string("onStatus");
		status.number(0);
		status.null();
		status.beginObject();
		if (accept) {
			status.propertyString("level", "status");
			status.propertyString("code", "NetStream.Publish.Start");
			status.propertyString("description", "Publishing.");
		} else {
			status.propertyString("level", "error");
			status.propertyString("code", "NetStream.Publish.BadName");
			status.propertyString("description", "rd-rtmp-sink refused this stream key.");
		}
		status.endObject();
		if (!sendCommand(1, status))
			return -1;

		if (!accept) {
			rejected_ = true;
			// Give the client a moment to read the status before the socket closes.
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			return 1;
		}

		publishing_ = true;
		publishStartMs_ = nowMs();
		return 0;
	}

	bool sendCommand(uint32_t streamId, const AmfWriter &writer)
	{
		return sendMessage(3, 20, streamId, writer.bytes().data(), writer.bytes().size());
	}

	bool sendMessage(uint8_t chunkStreamId, uint8_t typeId, uint32_t streamId, const uint8_t *payload, size_t size)
	{
		std::vector<uint8_t> out;
		out.reserve(size + 32);
		out.push_back(chunkStreamId & 0x3F); // format 0
		out.push_back(0);
		out.push_back(0);
		out.push_back(0); // timestamp
		out.push_back(static_cast<uint8_t>((size >> 16) & 0xFF));
		out.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>(size & 0xFF));
		out.push_back(typeId);
		out.push_back(static_cast<uint8_t>(streamId & 0xFF));
		out.push_back(static_cast<uint8_t>((streamId >> 8) & 0xFF));
		out.push_back(static_cast<uint8_t>((streamId >> 16) & 0xFF));
		out.push_back(static_cast<uint8_t>((streamId >> 24) & 0xFF));

		size_t offset = 0;
		while (offset < size) {
			const size_t part = std::min<size_t>(outChunkSize_, size - offset);
			if (offset > 0)
				out.push_back(static_cast<uint8_t>(0xC0 | (chunkStreamId & 0x3F))); // format 3
			out.insert(out.end(), payload + offset, payload + offset + part);
			offset += part;
		}
		return sendAll(socket_, out.data(), out.size());
	}

	void finish(const std::string &ended)
	{
		if (socket_ != INVALID_SOCKET) {
			closesocket(socket_);
			socket_ = INVALID_SOCKET;
		}

		std::lock_guard<std::mutex> lock(g_state.mutex);
		g_state.connectionsOpen -= 1;
		if (key_.empty())
			return;
		StreamStats &stats = g_state.streams[key_];
		if (publishing_) {
			stats.publishing = false;
			SessionRecord record;
			record.startMs = publishStartMs_ - g_state.startedMs;
			record.endMs = nowMs() - g_state.startedMs;
			record.bytes = sessionBytes_;
			record.ended = ended;
			stats.sessionRecords.push_back(record);
		}
	}

	SOCKET socket_;
	std::map<uint32_t, ChunkStream> chunkStreams_;
	size_t inChunkSize_ = 128;
	size_t outChunkSize_ = 128;
	size_t bytesSinceAck_ = 0;
	uint32_t totalAcked_ = 0;
	uint64_t sessionBytes_ = 0;

	std::string app_;
	std::string key_;
	KeyPlan plan_;
	bool publishing_ = false;
	bool rejected_ = false;
	int64_t publishStartMs_ = 0;
};

// ---- Report ---------------------------------------------------------------------------------

std::string jsonEscape(const std::string &text)
{
	std::string out;
	for (unsigned char c : text) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				char buffer[8];
				std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
				out += buffer;
			} else {
				out.push_back(static_cast<char>(c));
			}
		}
	}
	return out;
}

std::string buildReport(int port)
{
	std::lock_guard<std::mutex> lock(g_state.mutex);
	const int64_t now = nowMs();

	std::string out = "{\n";
	out += "  \"port\": " + std::to_string(port) + ",\n";
	out += "  \"uptime_ms\": " + std::to_string(now - g_state.startedMs) + ",\n";
	out += "  \"connections_total\": " + std::to_string(g_state.connectionsTotal) + ",\n";
	out += "  \"connections_open\": " + std::to_string(g_state.connectionsOpen) + ",\n";
	out += "  \"streams\": {";

	bool firstStream = true;
	for (const auto &entry : g_state.streams) {
		const StreamStats &s = entry.second;
		out += firstStream ? "\n" : ",\n";
		firstStream = false;
		out += "    \"" + jsonEscape(entry.first) + "\": {\n";
		out += "      \"app\": \"" + jsonEscape(s.app) + "\",\n";
		out += "      \"sessions\": " + std::to_string(s.sessions) + ",\n";
		out += "      \"rejected\": " + std::to_string(s.rejected) + ",\n";
		out += std::string("      \"publishing\": ") + (s.publishing ? "true" : "false") + ",\n";
		out += "      \"video_bytes\": " + std::to_string(s.videoBytes) + ",\n";
		out += "      \"audio_bytes\": " + std::to_string(s.audioBytes) + ",\n";
		out += "      \"video_messages\": " + std::to_string(s.videoMessages) + ",\n";
		out += "      \"audio_messages\": " + std::to_string(s.audioMessages) + ",\n";
		out += "      \"keyframes\": " + std::to_string(s.keyframes) + ",\n";
		out += "      \"video_codec\": \"" + jsonEscape(s.videoCodec) + "\",\n";
		{
			// Frames per second as they arrived in the newest session. This is the real
			// frame rate. The "framerate" metadata field is whatever the sender claims.
			const int64_t span = s.sessionLastVideoMs - s.sessionFirstVideoMs;
			char fps[32];
			std::snprintf(fps, sizeof(fps), "%.2f",
				      span > 0 && s.sessionVideoMessages > 1
					      ? (s.sessionVideoMessages - 1) * 1000.0 / static_cast<double>(span)
					      : 0.0);
			out += std::string("      \"measured_fps\": ") + fps + ",\n";
		}
		out += "      \"ms_since_last_data\": " + std::to_string(s.lastDataMs > 0 ? now - s.lastDataMs : -1) + ",\n";

		out += "      \"metadata\": {";
		bool firstMeta = true;
		for (const auto &meta : s.metadataNumbers) {
			out += firstMeta ? "" : ", ";
			firstMeta = false;
			char number[64];
			std::snprintf(number, sizeof(number), "%.3f", meta.second);
			out += "\"" + jsonEscape(meta.first) + "\": " + number;
		}
		for (const auto &meta : s.metadataStrings) {
			out += firstMeta ? "" : ", ";
			firstMeta = false;
			out += "\"" + jsonEscape(meta.first) + "\": \"" + jsonEscape(meta.second) + "\"";
		}
		out += "},\n";

		out += "      \"session_records\": [";
		for (size_t i = 0; i < s.sessionRecords.size(); ++i) {
			const SessionRecord &r = s.sessionRecords[i];
			out += i == 0 ? "" : ", ";
			out += "{\"start_ms\": " + std::to_string(r.startMs) + ", \"end_ms\": " + std::to_string(r.endMs) +
			       ", \"bytes\": " + std::to_string(r.bytes) + ", \"ended\": \"" + r.ended + "\"}";
		}
		out += "]\n    }";
	}
	out += firstStream ? "}\n" : "\n  }\n";
	out += "}\n";
	return out;
}

void writeReport(const std::filesystem::path &path, int port)
{
	const std::string report = buildReport(port);
	std::filesystem::path temporary = path;
	temporary += ".tmp";
	if (FILE *file = _wfopen(temporary.c_str(), L"wb")) {
		std::fwrite(report.data(), 1, report.size(), file);
		std::fclose(file);
		std::error_code ec;
		std::filesystem::rename(temporary, path, ec);
	}
}

} // namespace

int main(int argc, char **argv)
{
	int port = 19350;
	int blackholePort = 0;
	std::string bindAddress = "127.0.0.1";
	std::filesystem::path reportPath = "rtmp-sink-report.json";

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--port" && i + 1 < argc) {
			port = std::atoi(argv[++i]);
		} else if (arg == "--blackhole-port" && i + 1 < argc) {
			blackholePort = std::atoi(argv[++i]);
		} else if (arg == "--bind" && i + 1 < argc) {
			bindAddress = argv[++i];
		} else if (arg == "--report" && i + 1 < argc) {
			reportPath = argv[++i];
		} else {
			std::fprintf(stderr, "Usage: rd-rtmp-sink --port <port> --report <file> [--bind <address>] "
					     "[--blackhole-port <port>]\n");
			return 2;
		}
	}

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::fprintf(stderr, "WSAStartup failed.\n");
		return 1;
	}

	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET) {
		std::fprintf(stderr, "Could not create a socket.\n");
		return 1;
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<u_short>(port));
	if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
		std::fprintf(stderr, "'%s' is not an IPv4 address.\n", bindAddress.c_str());
		return 2;
	}
	if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener, 16) != 0) {
		std::fprintf(stderr, "Could not listen on %s:%d. Is another program using the port?\n",
			     bindAddress.c_str(), port);
		return 1;
	}

	SOCKET blackhole = INVALID_SOCKET;
	if (blackholePort > 0) {
		blackhole = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		sockaddr_in blackholeAddress = address;
		blackholeAddress.sin_port = htons(static_cast<u_short>(blackholePort));
		if (blackhole == INVALID_SOCKET ||
		    bind(blackhole, reinterpret_cast<sockaddr *>(&blackholeAddress), sizeof(blackholeAddress)) != 0 ||
		    listen(blackhole, 16) != 0) {
			std::fprintf(stderr, "Could not listen on blackhole port %d.\n", blackholePort);
			return 1;
		}
	}
	std::vector<SOCKET> heldConnections;

	std::filesystem::path stopPath = reportPath;
	stopPath += ".stop";
	std::error_code ec;
	std::filesystem::remove(stopPath, ec);

	std::printf("rd-rtmp-sink listening on rtmp://%s:%d\n", bindAddress.c_str(), port);
	std::fflush(stdout);
	writeReport(reportPath, port);

	std::thread reporter([&] {
		while (!g_state.stopping) {
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			writeReport(reportPath, port);
			std::error_code check;
			if (std::filesystem::exists(stopPath, check))
				g_state.stopping = true;
		}
	});

	std::vector<std::thread> workers;
	while (!g_state.stopping) {
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(listener, &readable);
		if (blackhole != INVALID_SOCKET)
			FD_SET(blackhole, &readable);
		timeval timeout{0, 200 * 1000};
		if (select(0, &readable, nullptr, nullptr, &timeout) <= 0)
			continue;

		if (blackhole != INVALID_SOCKET && FD_ISSET(blackhole, &readable)) {
			// Accept and say nothing. The client waits for a handshake that never comes.
			SOCKET held = accept(blackhole, nullptr, nullptr);
			if (held != INVALID_SOCKET)
				heldConnections.push_back(held);
		}
		if (!FD_ISSET(listener, &readable))
			continue;

		SOCKET client = accept(listener, nullptr, nullptr);
		if (client == INVALID_SOCKET)
			continue;

		{
			std::lock_guard<std::mutex> lock(g_state.mutex);
			g_state.connectionsTotal += 1;
			g_state.connectionsOpen += 1;
		}
		workers.emplace_back([client] {
			Connection connection(client);
			connection.run();
		});
	}

	closesocket(listener);
	if (blackhole != INVALID_SOCKET)
		closesocket(blackhole);
	for (SOCKET held : heldConnections)
		closesocket(held);
	for (std::thread &worker : workers) {
		if (worker.joinable())
			worker.join();
	}
	reporter.join();
	writeReport(reportPath, port);
	std::filesystem::remove(stopPath, ec);
	WSACleanup();
	return 0;
}
