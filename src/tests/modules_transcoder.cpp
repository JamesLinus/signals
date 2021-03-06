#include "tests.hpp"
#include "lib_modules/modules.hpp"
#include "lib_media/decode/jpegturbo_decode.hpp"
#include "lib_media/decode/libav_decode.hpp"
#include "lib_media/demux/libav_demux.hpp"
#include "lib_media/encode/jpegturbo_encode.hpp"
#include "lib_media/encode/libav_encode.hpp"
#include "lib_media/in/file.hpp"
#include "lib_media/mux/libav_mux.hpp"
#include "lib_media/mux/gpac_mux_mp4.hpp"
#include "lib_media/out/file.hpp"
#include "lib_media/out/null.hpp"
#include "lib_media/transform/video_convert.hpp"
#include "lib_utils/tools.hpp"


using namespace Tests;
using namespace Modules;

namespace {

unittest("transcoder: video simple (libav mux)") {
	auto demux = uptr(new Demux::LibavDemux("data/beepbop.mp4"));
	auto null = uptr(new Out::Null);

	//find video signal from demux
	size_t videoIndex = std::numeric_limits<size_t>::max();
	for (size_t i = 0; i < demux->getNumOutputs(); ++i) {
		auto metadata = getMetadataFromOutput(demux->getOutput(i));
		if (metadata->getStreamType() == VIDEO_PKT) {
			videoIndex = i;
		} else {
			ConnectOutputToInput(demux->getOutput(i), null);
		}
	}
	ASSERT(videoIndex != std::numeric_limits<size_t>::max());

	//create the video decode
	auto metadata = getMetadataFromOutput<MetadataPktLibav>(demux->getOutput(videoIndex));
	auto decode = uptr(new Decode::LibavDecode(*metadata));
	auto encode = uptr(new Encode::LibavEncode(Encode::LibavEncode::Video));
	auto mux = uptr(new Mux::LibavMux("output_video_libav"));

	ConnectOutputToInput(demux->getOutput(videoIndex), decode);
	ConnectOutputToInput(decode->getOutput(0), encode);
	ConnectOutputToInput(encode->getOutput(0), mux);

	demux->process(nullptr);
}

unittest("transcoder: video simple (gpac mux)") {
	auto demux = uptr(new Demux::LibavDemux("data/beepbop.mp4"));

	//create stub output (for unused demuxer's outputs)
	auto null = uptr(new Out::Null);

	//find video signal from demux
	size_t videoIndex = std::numeric_limits<size_t>::max();
	for (size_t i = 0; i < demux->getNumOutputs(); ++i) {
		auto metadata = getMetadataFromOutput(demux->getOutput(i));
		if (metadata->getStreamType() == VIDEO_PKT) {
			videoIndex = i;
		} else {
			ConnectOutputToInput(demux->getOutput(i), null);
		}
	}
	ASSERT(videoIndex != std::numeric_limits<size_t>::max());

	//create the video decode
	auto metadata = getMetadataFromOutput<MetadataPktLibav>(demux->getOutput(videoIndex));
	auto decode = uptr(new Decode::LibavDecode(*metadata));
	auto encode = uptr(new Encode::LibavEncode(Encode::LibavEncode::Video));
	auto mux = uptr(new Mux::GPACMuxMP4("output_video_gpac"));

	ConnectOutputToInput(demux->getOutput(videoIndex), decode);
	ConnectOutputToInput(decode->getOutput(0), encode);
	ConnectOutputToInput(encode->getOutput(0), mux);

	demux->process(nullptr);
}

unittest("transcoder: jpg to jpg") {
	const std::string filename("data/sample.jpg");
	auto decode = uptr(new Decode::JPEGTurboDecode());
	{
		auto preReader = uptr(new In::File(filename));
		ConnectOutputToInput(preReader->getOutput(0), decode);
		preReader->process(nullptr);
	}

	auto reader = uptr(new In::File(filename));
	auto encoder = uptr(new Encode::JPEGTurboEncode());
	auto writer = uptr(new Out::File("data/test.jpg"));

	ConnectOutputToInput(reader->getOutput(0), decode);
	ConnectOutputToInput(decode->getOutput(0), encoder);
	ConnectOutputToInput(encoder->getOutput(0), writer);

	reader->process(nullptr);
}

unittest("transcoder: jpg to resized jpg") {
	const std::string filename("data/sample.jpg");
	auto decode = uptr(new Decode::JPEGTurboDecode());
	{
		auto preReader = uptr(new In::File(filename));
		ConnectOutputToInput(preReader->getOutput(0), decode);
		preReader->process(nullptr);
	}
	auto reader = uptr(new In::File(filename));

	auto dstFormat = PictureFormat(VIDEO_RESOLUTION / 2, RGB24);
	auto converter = uptr(new Transform::VideoConvert(dstFormat));
	auto encoder = uptr(new Encode::JPEGTurboEncode());
	auto writer = uptr(new Out::File("data/test.jpg"));

	ConnectOutputToInput(reader->getOutput(0), decode);
	ConnectOutputToInput(decode->getOutput(0), converter);
	ConnectOutputToInput(converter->getOutput(0), encoder);
	ConnectOutputToInput(encoder->getOutput(0), writer);

	reader->process(nullptr);
}

unittest("transcoder: h264/mp4 to jpg") {
	auto demux = uptr(new Demux::LibavDemux("data/beepbop.mp4"));

	auto metadata = getMetadataFromOutput<MetadataPktLibavVideo>(demux->getOutput(1));
	auto decode = uptr(new Decode::LibavDecode(*metadata));

	auto encoder = uptr(new Encode::JPEGTurboEncode());
	auto writer = uptr(new Out::File("data/test.jpg"));

	auto dstRes = metadata->getResolution();
	ASSERT(metadata->getPixelFormat() == YUV420P);
	auto dstFormat = PictureFormat(dstRes, RGB24);
	auto converter = uptr(new Transform::VideoConvert(dstFormat));

	ConnectOutputToInput(demux->getOutput(1), decode);
	ConnectOutputToInput(decode->getOutput(0), converter);
	ConnectOutputToInput(converter->getOutput(0), encoder);
	ConnectOutputToInput(encoder->getOutput(0), writer);

	demux->process(nullptr);
}

unittest("transcoder: jpg to h264/mp4 (gpac)") {
	const std::string filename("data/sample.jpg");
	auto decode = uptr(new Decode::JPEGTurboDecode());
	{
		auto preReader = uptr(new In::File(filename));
		ConnectOutputToInput(preReader->getOutput(0), decode);
		preReader->process(nullptr);
	}
	auto reader = uptr(new In::File(filename));

	auto dstFormat = PictureFormat(VIDEO_RESOLUTION, YUV420P);
	auto converter = uptr(new Transform::VideoConvert(dstFormat));

	auto encoder = uptr(new Encode::LibavEncode(Encode::LibavEncode::Video));
	auto mux = uptr(new Mux::GPACMuxMP4("data/test"));

	ConnectOutputToInput(reader->getOutput(0), decode);
	ConnectOutputToInput(decode->getOutput(0), converter);
	ConnectOutputToInput(converter->getOutput(0), encoder);
	ConnectOutputToInput(encoder->getOutput(0), mux);

	reader->process(nullptr);
	converter->flush();
	encoder->flush();
	mux->flush();
}

}
