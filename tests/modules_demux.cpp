#include "tests.hpp"
#include "modules.hpp"
#include <memory>


namespace Tests {
	namespace Modules {
		namespace Demux {
			int main(int argc, char **argv) {
				//Romain: move to a 'simple' file test
				Test("empty param test");
				{
					Param paramFile;
					std::unique_ptr<File> f(File::create(paramFile));
					ASSERT(f == nullptr);
				}
				{
					Param paramMP4Demux;
					std::unique_ptr<GPAC_MP4_Simple> mp4Demux(GPAC_MP4_Simple::create(paramMP4Demux));
					ASSERT(mp4Demux == nullptr);
				}
				{
					Param paramPrint;
					std::unique_ptr<Print> p(Print::create(paramPrint));
					ASSERT(p != nullptr);
				}

				Test("simple param test");
				{
					Param paramFile;
					paramFile["filename"] = "data/BatmanHD_1000kbit_mpeg.mp4";
					std::unique_ptr<File> f(File::create(paramFile));
					ASSERT(f != nullptr);
				}

				Test("demux one track");
				{
					Param paramMP4Demux;
					paramMP4Demux["filename"] = "data/BatmanHD_1000kbit_mpeg.mp4";
					std::unique_ptr<GPAC_MP4_Simple> mp4Demux(GPAC_MP4_Simple::create(paramMP4Demux));
					ASSERT(mp4Demux != nullptr);

					Param paramPrint;
					std::unique_ptr<Print> p(Print::create(paramPrint));
					ASSERT(p != nullptr);
				}

#if 0
				//TODO
				Test("demux two tracks");

				//TODO
				Test("demux a dynamic number of tracks");
#endif

				return 0;
			}
		}
	}
}
