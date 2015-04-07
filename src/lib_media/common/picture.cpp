#include "picture.hpp"

namespace Modules {
Picture* Picture::create(const Resolution &res, const PixelFormat &format) {
	switch (format) {
	case YUV420P: return new PictureYUV420P(res);
	case YUYV422: return new PictureYUYV422(res);
	case RGB24: return new PictureRGB24(res);
	default:
		throw std::runtime_error("Unknown pixel format for Picture. Please contact your vendor");
	}
}
}
