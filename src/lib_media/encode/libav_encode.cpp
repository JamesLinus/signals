#include "libav_encode.hpp"
#include "lib_utils/tools.hpp"
#include "../common/pcm.hpp"
#include <cassert>

#include "lib_ffpp/ffpp.hpp"


namespace Modules {

namespace {
void fps2NumDen(const double fps, int &num, int &den) {
	if (fabs(fps - (int)fps) < 0.001) {
		//infer integer frame rates
		num = (int)fps;
		den = 1;
	} else if (fabs((fps*1001.0) / 1000.0 - (int)(fps + 1)) < 0.001) {
		//infer ATSC frame rates
		num = (int)(fps + 1) * 1000;
		den = 1001;
	} else if (fabs(fps * 2 - (int)(fps * 2)) < 0.001) {
		//infer rational frame rates; den = 2
		num = (int)(fps * 2);
		den = 2;
	} else if (fabs(fps * 4 - (int)(fps * 4)) < 0.001) {
		//infer rational frame rates; den = 4
		num = (int)(fps * 4);
		den = 4;
	} else {
		num = (int)fps;
		den = 1;
		Log::msg(Warning, "Frame rate '%s' was not recognized. Truncating to '%s'.", fps, num);
	}
}

auto g_InitAv = runAtStartup(&av_register_all);
auto g_InitAvcodec = runAtStartup(&avcodec_register_all);
auto g_InitAvLog = runAtStartup(&av_log_set_callback, avLog);
}

namespace Encode {

LibavEncode::LibavEncode(Type type, const LibavEncodeParams &params)
	: pcmFormat(new PcmFormat()), avFrame(new ffpp::Frame), frameNum(-1) {
	std::string codecOptions, generalOptions, codecName;
	switch (type) {
	case Video:
		codecOptions = format("-b %s -g %s -keyint_min %s -bf 0", params.bitrate_v, params.GOPSize, params.GOPSize);
		generalOptions = format("-vcodec libx264 -r %s -pass 1", params.frameRate);
		if (params.isLowLatency)
			codecOptions += " -preset ultrafast -tune zerolatency";
		codecName = "vcodec";
		break;
	case Audio:
		codecOptions = format("-b %s", params.bitrate_a);
		generalOptions = "-acodec aac";
		codecName = "acodec";
		break;
	default:
		throw error("Unknown encoder type. Failed.");
	}

	/* parse the codec optionsDict */
	ffpp::Dict codecDict;
	buildAVDictionary(typeid(*this).name(), &codecDict, codecOptions.c_str(), "codec");
	codecDict.set("threads", "auto");

	/* parse other optionsDict*/
	ffpp::Dict generalDict;
	buildAVDictionary(typeid(*this).name(), &generalDict, generalOptions.c_str(), "other");

	/* find the encoder */
	auto entry = generalDict.get(codecName);
	if(!entry)
		throw error("Could not get codecName.");
	AVCodec *codec = avcodec_find_encoder_by_name(entry->value);
	if (!codec)
		throw error(format("codec '%s' not found, disable output.", entry->value));

	codecCtx = avcodec_alloc_context3(codec);
	if (!codecCtx)
		throw error("could not allocate the codec context.");

	/* parameters */
	switch (type) {
	case Video: {
		codecCtx->width = params.res.width;
		codecCtx->height = params.res.height;
		if (strcmp(generalDict.get("vcodec")->value, "mjpeg")) {
			codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
		} else {
			codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
		}

		double fr = atof(generalDict.get("r")->value);
		AVRational fps;
		fps2NumDen(fr, fps.den, fps.num); //for FPS, num and den are inverted
		codecCtx->time_base = fps;
	}
	break;
	case Audio:
		libavAudioCtxConvert(pcmFormat.get(), codecCtx);
		break;
	default:
		assert(0);
	}

#if 0 //TODO: how to pass extra data?
	/* user extra params */
	std::string extraParams;
	if (Parse::populateString("LibavOutputWriter", config, "extra_params", extraParams, false) == Parse::PopulateResult_Ok) {
		log(Debug, "extra_params : " << extraParams.c_str());
		std::vector<std::string> paramList;
		Util::split(extraParams.c_str(), ',', &paramList);
		auto param = paramList.begin();
		for (; param != paramList.end(); ++param) {
			std::vector<std::string> paramValue;
			Util::split(param->c_str(), '=', &paramValue);
			if (paramValue.size() != 2) {
				log(Warning, "extra_params :   wrong param (" << paramValue.size() << " value detected, 2 expected) in " << param->c_str());
			} else {
				log(Debug, "extra_params :   detected param " << paramValue[0].c_str() << " with value " << paramValue[1].c_str() << " [" << param->c_str() << "]");
				av_dict_set(&codecDict, paramValue[0].c_str(), paramValue[1].c_str(), 0);
			}
		}
	}
#endif

	/* open it */
	codecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER; //gives access to the extradata (e.g. H264 SPS/PPS, etc.)
	if (avcodec_open2(codecCtx, codec, &codecDict) < 0)
		throw error("could not open codec, disable output.");

	/* check all optionsDict have been consumed */
	auto opt = stringDup(codecOptions.c_str());
	char *tok = strtok(opt.data(), "- ");
	while (tok && strtok(nullptr, "- ")) {
		AVDictionaryEntry *avde = nullptr;
		avde = codecDict.get(tok, avde);
		if (avde) {
			log(Warning, "codec option \"%s\", value \"%s\" was ignored.", avde->key, avde->value);
		}
		tok = strtok(nullptr, "- ");
	}

	output = addOutput(new OutputDataDefault<DataAVPacket>);
	switch (type) {
	case Video: {
		auto input = addInput(new Input<DataPicture>(this));
		input->setMetadata(new MetadataRawVideo);
		output->setMetadata(new MetadataPktLibavVideo(codecCtx));
		break;
	}
	case Audio: {
		auto input = addInput(new Input<DataPcm>(this));
		input->setMetadata(new MetadataRawAudio);
		output->setMetadata(new MetadataPktLibavAudio(codecCtx));
		break;
	}
	default:
		assert(0);
	}
}

void LibavEncode::flush() {
	if (codecCtx && (codecCtx->codec->capabilities & CODEC_CAP_DELAY)) {
		switch (codecCtx->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			while (processVideo(nullptr)) {}
			break;
		case AVMEDIA_TYPE_AUDIO:
			while (processAudio(nullptr)) {}
			break;
		default:
			assert(0);
			break;
		}
	}
}

LibavEncode::~LibavEncode() {
	if (codecCtx) {
		avcodec_close(codecCtx);
	}
}

bool LibavEncode::processAudio(const DataPcm *data) {
	auto out = output->getBuffer(0);
	AVPacket *pkt = out->getPacket();
	AVFrame *f = nullptr;
	if (data) {
		f = avFrame->get();
		libavFrameDataConvert(data, f);
		avFrame->get()->pts = ++frameNum;
	}

	int gotPkt = 0;
	if (avcodec_encode_audio2(codecCtx, pkt, f, &gotPkt)) {
		log(Warning, "error encountered while encoding audio frame %s.", frameNum);
		return false;
	}
	if (gotPkt) {
		pkt->pts = pkt->dts = frameNum * pkt->duration;
		uint64_t time;
		if (times.tryPop(time)) {
			out->setTime(time);
		} else {
			log(Warning, "error encountered: more output packets than input. Discard", frameNum);
			return false;
		}
		assert(pkt->size);
		if (out) output->emit(out);
		return true;
	}

	return false;
}

bool LibavEncode::processVideo(const DataPicture *pic) {
	auto out = output->getBuffer(0);
	AVPacket *pkt = out->getPacket();

	std::shared_ptr<ffpp::Frame> f;
	if (pic) {
		f = std::make_shared<ffpp::Frame>();
		f->get()->pict_type = AV_PICTURE_TYPE_NONE;
		f->get()->pts = ++frameNum;
		pixelFormat2libavPixFmt(pic->getFormat().format, (AVPixelFormat&)f->get()->format);
		for (int i = 0; i < 3; ++i) {
			f->get()->width = pic->getFormat().res.width;
			f->get()->height = pic->getFormat().res.height;
			f->get()->data[i] = (uint8_t*)pic->getPlane(i);
			f->get()->linesize[i] = (int)pic->getPitch(i);
		}
	}

	int gotPkt = 0;
	if (avcodec_encode_video2(codecCtx, pkt, f ? f->get() : nullptr, &gotPkt)) {
		log(Warning, "error encountered while encoding video frame %s.", frameNum);
		return false;
	} else {
		if (gotPkt) {
			assert(pkt->size);
			if (pkt->duration <= 0) {
				/*store duration in 180k in case a forward module (e.g. muxer) would need it*/
				assert(codecCtx->time_base.num >= 0);
				pkt->duration = (int)timescaleToClock((uint64_t)codecCtx->time_base.num, codecCtx->time_base.den);
			}
			out->setTime(times.pop());
			output->emit(out);
			return true;
		}
	}

	return false;
}

void LibavEncode::process(Data data) {
	times.push(data->getTime());
	switch (codecCtx->codec_type) {
	case AVMEDIA_TYPE_VIDEO: {
		const auto encoderData = safe_cast<const DataPicture>(data);
		processVideo(encoderData.get());
		break;
	}
	case AVMEDIA_TYPE_AUDIO: {
		const auto pcmData = safe_cast<const DataPcm>(data);
		if (pcmData->getFormat() != *pcmFormat)
			throw error("Incompatible audio data");
		processAudio(pcmData.get());
		break;
	}
	default:
		assert(0);
		return;
	}
}

}
}
