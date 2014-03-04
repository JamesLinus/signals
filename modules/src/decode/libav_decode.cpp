#include "libav_decode.hpp"
#include "../utils/log.hpp"
#include "../utils/tools.hpp"
#include <cassert>
#include <string>

#include "ffpp.hpp"

namespace {
auto g_InitAv = runAtStartup(&av_register_all);
auto g_InitAvcodec = runAtStartup(&avcodec_register_all);
auto g_InitAvLog = runAtStartup(&av_log_set_callback, avLog);
}

// TODO move this to its own module, when we have media types
class AudioConverter {
public:

	AudioConverter(AVCodecContext const& src) {
		m_Swr.setInputSampleFmt(src.sample_fmt);
		m_Swr.setInputLayout(src.channel_layout);
		m_Swr.setInputSampleRate(src.sample_rate);

		m_Swr.setOutputSampleFmt(DST_FMT);
		m_Swr.setOutputLayout(DST_LAYOUT);
		m_Swr.setOutputSampleRate(DST_FREQ);

		m_Swr.init();
	}

	std::shared_ptr<Data> convert(AVCodecContext* codecCtx, AVFrame* avFrame, AudioConverter& converter) {
		const int bufferSize = av_samples_get_buffer_size(nullptr, codecCtx->channels, avFrame->nb_samples, codecCtx->sample_fmt, 0);

		auto const srcNumSamples = avFrame->nb_samples;
		auto const dstNumSamples = DivUp(srcNumSamples * DST_FREQ, codecCtx->sample_rate);

		std::shared_ptr<Data> out(new PcmData(bufferSize * 10)); // FIXME

		uint8_t* pDst = out->data();
		auto const numSamples = m_Swr.convert(&pDst, dstNumSamples, (const uint8_t**)avFrame->data, srcNumSamples);

		auto const dstChannels = av_get_channel_layout_nb_channels(DST_LAYOUT);
		auto const sampleSize = av_samples_get_buffer_size(nullptr, dstChannels, numSamples, DST_FMT, 1);

		out->resize(sampleSize);

		return out;
	}

private:

	static const auto DST_FREQ = 44100;
	static const uint64_t DST_LAYOUT = AV_CH_LAYOUT_STEREO;
	static const auto DST_FMT = AV_SAMPLE_FMT_S16;
	ffpp::SwResampler m_Swr;
};

namespace Decode {

LibavDecode* LibavDecode::create(const PropsDecoder &props) {
	auto codecCtx = props.getAVCodecContext();

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
	case AVMEDIA_TYPE_AUDIO:
		break;
	default:
		Log::msg(Log::Warning, "Module LibavDecode: codec_type not supported. Must be audio or video.");
		throw std::runtime_error("Unknown decoder type. Failed.");
	}

	//find an appropriate decoder
	auto codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec) {
		Log::msg(Log::Warning, "Module LibavDecode: Codec not found");
		throw std::runtime_error("Decoder not found.");
	}

	//TODO: test: force single threaded as h264 probing seems to miss SPS/PPS and seek fails silently
	ffpp::Dict dict;
	dict.set("threads", "1");

	//open the codec
	if (avcodec_open2(codecCtx, codec, &dict) < 0) {
		Log::msg(Log::Warning, "Module LibavDecode: Couldn't open stream");
		throw std::runtime_error("Couldn't open stream.");
	}

	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		//check colorspace
		if ((codecCtx->pix_fmt != PIX_FMT_YUV420P) && (codecCtx->pix_fmt != PIX_FMT_YUVJ420P)) {
			const char *codecName = codecCtx->codec_name ? codecCtx->codec_name : "[unknown]";
			Log::msg(Log::Warning, "Module LibavDecode: Unsupported colorspace for codec \"%s\". Only planar YUV 4:2:0 is supported.", codecName);
			throw std::runtime_error("unsupported colorspace.");
		}
		break;
	case AVMEDIA_TYPE_AUDIO:
		break;
	default:
		assert(0);
		throw std::runtime_error("Unknown decoder type. Failed.");
	}

	return new LibavDecode(codecCtx);
}

LibavDecode::LibavDecode(AVCodecContext *codecCtx2)
	: codecCtx(new AVCodecContext), avFrame(new ffpp::Frame) {
	*codecCtx = *codecCtx2;
	signals.push_back(uptr(pinFactory->createPin()));
}

LibavDecode::~LibavDecode() {
	avcodec_close(codecCtx.get());
}

bool LibavDecode::processAudio(DataAVPacket *decoderData) {
	AVPacket *pkt = decoderData->getPacket();
	int gotFrame;
	if (avcodec_decode_audio4(codecCtx.get(), avFrame->get(), &gotFrame, pkt) < 0) {
		Log::msg(Log::Warning, "[LibavDecode] Error encoutered while decoding audio.");
		return true;
	}
	if (gotFrame) {

		if(!m_pAudioConverter)
			m_pAudioConverter.reset(new AudioConverter(*codecCtx));

		auto out = m_pAudioConverter->convert(codecCtx.get(), avFrame->get(), *m_pAudioConverter);
		signals[0]->emit(out);
	}

	return true;
}

bool LibavDecode::processVideo(DataAVPacket *decoderData) {
	AVPacket *pkt = decoderData->getPacket();
	int gotPicture;
	if (avcodec_decode_video2(codecCtx.get(), avFrame->get(), &gotPicture, pkt) < 0) {
		Log::msg(Log::Warning, "[LibavDecode] Error encoutered while decoding video.");
		return true;
	}
	if (gotPicture) {
		const int frameSize = (codecCtx->width * codecCtx->height * 3) / 2;
		std::shared_ptr<Data> out(new Data(frameSize));
		//TODO: YUV specific + wrap the avFrame output size
		for (int h = 0; h < codecCtx->height; ++h) {
			memcpy(out->data() + h*codecCtx->width, avFrame->get()->data[0] + h*avFrame->get()->linesize[0], codecCtx->width);
		}
		uint8_t *UPlane = out->data() + codecCtx->width * codecCtx->height;
		for (int h = 0; h < codecCtx->height / 2; ++h) {
			memcpy((void*)(UPlane + h*codecCtx->width / 2), avFrame->get()->data[1] + h*avFrame->get()->linesize[1], codecCtx->width / 2);
		}
		uint8_t *VPlane = out->data() + (codecCtx->width * codecCtx->height * 5) / 4;
		for (int h = 0; h < codecCtx->height / 2; ++h) {
			memcpy((void*)(VPlane + h*codecCtx->width / 2), avFrame->get()->data[2] + h*avFrame->get()->linesize[2], codecCtx->width / 2);
		}
		signals[0]->emit(out);
	}
	return true;
}

bool LibavDecode::process(std::shared_ptr<Data> data) {
	auto decoderData = dynamic_cast<DataAVPacket*>(data.get());
	if (!decoderData) {
		Log::msg(Log::Warning, "[LibavDecode] Invalid packet type.");
		return false;
	}
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		return processVideo(decoderData);
		break;
	case AVMEDIA_TYPE_AUDIO:
		return processAudio(decoderData);
		break;
	default:
		assert(0);
		return false;
	}
}

bool LibavDecode::handles(const std::string &url) {
	return LibavDecode::canHandle(url);
}

bool LibavDecode::canHandle(const std::string &/*url*/) {
	return true; //TODO
}

}
