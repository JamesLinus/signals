#pragma once

#include "../../internal/data.hpp"
#include "../../internal/pin.hpp"
#include "../../internal/props.hpp"
#include <string>
#include <cstdarg>
#include <memory>

struct AVCodecContext;
struct AVFormatContext;
struct AVPacket;
struct AVDictionary;

namespace Modules {
//FIXME: move in libmm, or add a new namespace
class MODULES_EXPORT PropsDecoder : public IProps {
public:
	/**
	 * Doesn't take the ownership
	 */
	PropsDecoder(AVCodecContext *codecCtx)
		: codecCtx(codecCtx) {
	}

	AVCodecContext* getAVCodecContext() const {
		return codecCtx;
	}

private:
	AVCodecContext *codecCtx;
};

class MODULES_EXPORT DataAVPacket : public Data {
public:
	DataAVPacket(size_t size = 0);
	~DataAVPacket();
	uint8_t* data();
	uint64_t size() const;
	AVPacket* getPacket() const;
	void resize(size_t size);

private:
	std::unique_ptr<AVPacket> const pkt;
};

void buildAVDictionary(const std::string &moduleName, AVDictionary **dict, const char *options, const char *type);

void avLog(void* /*avcl*/, int level, const char *fmt, va_list vl);

typedef PinDataDefault<DataAVPacket> PinLibav;

class PinLibavFactory : public PinFactory {
public:
	PinLibavFactory();
	Pin* createPin(IProps *props = nullptr);
};

}
