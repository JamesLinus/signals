#include "lib_modules/modules.hpp"
#include "lib_media/media.hpp"
#include "pipeliner.hpp"
#include <sstream>

using namespace Modules;
using namespace Pipelines;

namespace {
Module *createSink(bool isHLS) {
	if (isHLS) {
		const bool isLive = false; //TODO
		const uint64_t segmentDuration = 10000; //TODO
		return new Stream::Apple_HLS(isLive ? Modules::Stream::Apple_HLS::Live : Modules::Stream::Apple_HLS::Static, segmentDuration);
	} else {
		return new Out::File("output.ts"); //FIXME: hardcoded
	}
}
}

void declarePipeline(Pipeline &pipeline, const mp42tsXOptions &opt) {
	auto connect = [&](auto* src, auto* dst) {
		pipeline.connect(src, 0, dst, 0);
	};

	const bool isHLS = false; //TODO

	auto demux = pipeline.addModule(new Demux::LibavDemux(opt.url));
	auto m2tsmux = pipeline.addModule(new Mux::GPACMuxMPEG2TS(false, 1000, 100, 0)); //FIXME: hardcoded
	auto sink = pipeline.addModule(createSink(isHLS));
	for (size_t i = 0; i < demux->getNumOutputs(); ++i) {
		//FIXME: only video is supported for now //pipeline.connect(demux, i, m2tsmux, i);
		if (getMetadataFromOutput<MetadataPktLibav>(demux->getOutput(i))->getStreamType() == VIDEO_PKT) {
			pipeline.connect(demux, i, m2tsmux, 0);
		} else {
			auto null = pipeline.addModule(new Out::Null());
			pipeline.connect(demux, i, null, 0);
		}
	}

	connect(m2tsmux, sink);
}