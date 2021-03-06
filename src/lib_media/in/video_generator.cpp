#include "lib_utils/tools.hpp"
#include "video_generator.hpp"
#include "../common/pcm.hpp"
#include <cmath>

auto const FRAMERATE = 25;

namespace Modules {
namespace In {

VideoGenerator::VideoGenerator() {
	output = addOutput(new OutputPicture);
}

void VideoGenerator::process(Data /*data*/) {
	auto const dim = VIDEO_RESOLUTION;
	auto pic = DataPicture::create(output, dim, YUV420P);

	// generate video
	auto const p = pic->data();
	auto const FLASH_PERIOD = FRAMERATE;
	auto const flash = (m_numFrames % FLASH_PERIOD) == 0;
	auto const val = flash ? 0xCC : 0x80;
	memset(p, val, pic->getSize());

	auto const framePeriodIn180k = IClock::Rate / FRAMERATE;
	assert(IClock::Rate % FRAMERATE == 0);
	pic->setTime(m_numFrames * framePeriodIn180k);

	if (m_numFrames % 25 < 2)
		output->emit(pic);

	++m_numFrames;
}

}
}
