#include "libav.hpp"
#include "../utils/log.hpp"
#include "../utils/tools.hpp"
#include <cassert>
#include <cstdio>
#include <string.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {
Log::Level avLogLevel(int level) {
	switch (level) {
	case AV_LOG_QUIET:
		return Log::Quiet;
	case AV_LOG_PANIC:
	case AV_LOG_FATAL:
	case AV_LOG_ERROR:
		return Log::Warning;
	case AV_LOG_WARNING:
		return Log::Info;
	case AV_LOG_INFO:
	case AV_LOG_VERBOSE:
	case AV_LOG_DEBUG:
		return Log::Debug;
	default:
		assert(0);
		return Log::Debug;
	}
}

const char* avlogLevelName(int level) {
	switch (level) {
	case AV_LOG_QUIET:
		return "quiet";
	case AV_LOG_PANIC:
		return "panic";
	case AV_LOG_FATAL:
		return "fatal";
	case AV_LOG_ERROR:
		return "error";
	case AV_LOG_WARNING:
		return "warning";
	case AV_LOG_INFO:
		return "info";
	case AV_LOG_VERBOSE:
		return "verbose";
	case AV_LOG_DEBUG:
		return "debug";
	default:
		assert(0);
		return "unknown";
	}
}
}

namespace Modules {

DataAVPacket::DataAVPacket(size_t size)
	: Data(size), pkt(new AVPacket) {
	av_init_packet(pkt.get());
	av_free_packet(pkt.get());
}

DataAVPacket::~DataAVPacket() {
	Log::msg(Log::Debug, "Freeing %s, pts=%s", this, pkt->pts);
	av_free_packet(pkt.get());
}

uint8_t* DataAVPacket::data() {
	return pkt->data;
}

uint64_t DataAVPacket::size() const {
	return pkt->size;
}

AVPacket* DataAVPacket::getPacket() const {
	return pkt.get();
}

void DataAVPacket::resize(size_t /*size*/) {
	assert(0);
}

DataAVFrame::DataAVFrame(size_t size)
	: Data(size), frame(av_frame_alloc()) {
}

DataAVFrame::~DataAVFrame() {
	av_frame_free(&frame);
}

uint8_t* DataAVFrame::data() {
	return frame->data[0];
}

uint64_t DataAVFrame::size() const {
	return 0;
}

AVFrame* DataAVFrame::getFrame() const {
	return frame;
}

void DataAVFrame::resize(size_t /*size*/) {
	assert(0);
}

void buildAVDictionary(const std::string &moduleName, AVDictionary **dict, const char *options, const char *type) {
	auto opt = stringDup(options);
	char *tok = strtok(opt.data(), "- ");
	char *tokval = NULL;
	while (tok && (tokval = strtok(NULL, "- "))) {
		if (av_dict_set(dict, tok, tokval, 0) < 0) {
			Log::msg(Log::Warning, "[%s] unknown %s option \"%s\" with value \"%s\"", moduleName.c_str(), type, tok, tokval);
		}
		tok = strtok(NULL, "- ");
	}
}

void avLog(void* /*avcl*/, int level, const char *fmt, va_list vl) {
#if defined(__CYGWIN__) // cygwin does not have vsnprintf in std=c++11 mode. To be removed when cygwin is fixed
	Log::msg(avLogLevel(level), "[libav-log::%s] %s", avlogLevelName(level), fmt);
#else
	char buffer[1024];
	std::vsnprintf(buffer, sizeof(buffer)-1, fmt, vl);

	// remove trailing end of line
	{
		auto const N = strlen(buffer);
		if(N > 0 && buffer[N-1] == '\n')
			buffer[N-1] = 0;
	}
	Log::msg(avLogLevel(level), "[libav-log::%s] %s", avlogLevelName(level), buffer);
#endif
}

PinLibavPacketFactory::PinLibavPacketFactory() {
}

Pin* PinLibavPacketFactory::createPin(IProps *props) {
	return new PinLibavPacket(props);
}

PinLibavFrameFactory::PinLibavFrameFactory() {
}

Pin* PinLibavFrameFactory::createPin(IProps *props) {
	return new PinLibavFrame(props);
}

}
