#!/bin/bash
#
# Download, build and locally deploy external dependencies
#

set -e
EXTRA_DIR=$PWD/extra
export CFLAGS=-w
export PKG_CONFIG_PATH=$EXTRA_DIR/lib/pkgconfig

if [ -z "$MAKE" ]; then
	if [ $(uname -s) == "Darwin" ]; then
		CORES=$(sysctl -n hw.logicalcpu)
	else
		CORES=$(nproc)
	fi

	MAKE="make -j$CORES"
fi

if [ -z "$CPREFIX" ]; then
	case $OSTYPE in
	msys)
		CPREFIX=x86_64-w64-mingw32
		echo "MSYS detected ($OSTYPE): forcing use of prefix \"$CPREFIX\""
		;;
	*)
		CPREFIX=$(gcc -dumpmachine)
		echo "Platform $OSTYPE detected."
		;;
	esac
fi
HOST=$($CPREFIX-gcc -dumpmachine)
echo "Using compiler host ($HOST) with prefix ($CPREFIX)"

case $OSTYPE in
darwin*)
    if [ -z "$NASM" ]; then
        echo "You must set the NASM env variable."
    fi
    ;;
esac


#-------------------------------------------------------------------------------
echo zenbuild
#-------------------------------------------------------------------------------
if [ ! -f extra/src/zenbuild/zenbuild.sh ] ; then
	mkdir -p extra/src
	rm -rf extra/src/zenbuild
	git clone https://github.com/gpac/zenbuild extra/src/zenbuild
	pushd extra/src/zenbuild
	git checkout 75c2d6dbac6d
	#patch -p1 < ../../patches/gpac_01_revision.diff
	#patch -p1 < ../../patches/ffmpeg_01_version.diff
	popd
fi

if [ ! -f extra/src/zenbuild/zenbuild.built ] ; then
	## x264
	if [ ! -f extra/build/flags/$CPREFIX/x264.built ] ; then
		pushd extra/src/zenbuild
		./zenbuild.sh "$PWD/../../../extra/build" x264 $CPREFIX
		popd
	fi
	## vo-aacenc
	if [ ! -f extra/build/flags/$CPREFIX/voaac-enc.built ] ; then
		pushd extra/src/zenbuild
		./zenbuild.sh "$PWD/../../../extra/build" voaac-enc $CPREFIX
		popd
	fi
	## FFmpeg
	if [ ! -f extra/build/flags/$CPREFIX/ffmpeg.built ] ; then
		pushd extra/src/zenbuild
		./zenbuild.sh "$PWD/../../../extra/build" ffmpeg $CPREFIX
		popd
	fi
	## GPAC
	if [ ! -f extra/build/flags/$CPREFIX/gpac.built ] ; then
		pushd extra/src/zenbuild
		./zenbuild.sh "$PWD/../../../extra/build" gpac $CPREFIX
		popd
	fi
	## SDL2
	if [ ! -f extra/build/flags/$CPREFIX/libsdl2.built ] ; then
		pushd extra/src/zenbuild
		./zenbuild.sh "$PWD/../../../extra/build" libsdl2 $CPREFIX
		popd
	fi
	## move files
	rsync -ar extra/build/release/$HOST/* extra/
	touch extra/src/zenbuild/zenbuild.built
fi

#-------------------------------------------------------------------------------
echo ASIO
#-------------------------------------------------------------------------------
if [ ! -f extra/src/asio/asio/include/asio.hpp ] ; then
	mkdir -p extra/src
	rm -rf extra/src/asio
	git clone --depth 1000 https://github.com/chriskohlhoff/asio extra/src/asio
	pushd extra/src/asio
	git checkout f05ccf18df816f8999dcc6449f65e520787899c6
	popd
fi

if [ ! -f extra/include/asio/asio.hpp ] ; then
	mkdir -p extra/include/asio
	cp -r extra/src/asio/asio/include/* extra/include/asio/
fi

#-------------------------------------------------------------------------------
echo libjpeg-turbo
#-------------------------------------------------------------------------------
if [ ! -f extra/src/libjpeg_turbo_1.3.x/configure.ac ] ; then
	mkdir -p extra/src
	rm -rf extra/src/libjpeg_turbo_1.3.x
	pushd extra/src
	svn co svn://svn.code.sf.net/p/libjpeg-turbo/code/branches/1.3.x -r 1397 libjpeg_turbo_1.3.x
	pushd libjpeg_turbo_1.3.x
	autoreconf -fiv
	popd
	popd
fi

if [ ! -f extra/build/libjpeg_turbo_1.3.x/buildOk ] ; then
	mkdir -p extra/build/libjpeg_turbo_1.3.x

    case $OSTYPE in
	linux-gnu)
		pushd extra/build/libjpeg_turbo_1.3.x
		../../src/libjpeg_turbo_1.3.x/configure \
			--prefix=$EXTRA_DIR \
			--host=$HOST
		;;
	msys)
		pushd extra/src/libjpeg_turbo_1.3.x
		cmake -G "Unix Makefiles" -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_INSTALL_PREFIX:PATH=$EXTRA_DIR ../../src/libjpeg_turbo_1.3.x
		;;
	darwin*)
		pushd extra/build/libjpeg_turbo_1.3.x
		../../src/libjpeg_turbo_1.3.x/configure \
			--prefix=$EXTRA_DIR \
			--host x86_64-apple-darwin
		;;
	esac

	$MAKE
	$MAKE install
	popd
	touch extra/build/libjpeg_turbo_1.3.x/buildOk
fi

#-------------------------------------------------------------------------------
echo optionparser
#-------------------------------------------------------------------------------
if [ ! -f extra/src/optionparser-1.3/src/optionparser.h ] ; then
	mkdir -p extra/src
	rm -rf extra/src/optionparser-1.3
	wget http://sourceforge.net/projects/optionparser/files/optionparser-1.3.tar.gz/download -O optionparser-1.3.tar.gz
	tar xvf optionparser-1.3.tar.gz -C extra/src
	rm optionparser-1.3.tar.gz
fi

if [ ! -f extra/include/optionparser/optionparser.h ] ; then
	mkdir -p extra/include/optionparser
	cp extra/src/optionparser-1.3/src/optionparser.h extra/include/optionparser/
fi

echo "Done"
