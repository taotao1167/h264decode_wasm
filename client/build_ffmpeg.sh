ARGS=(
	--cc="emcc"
	--cxx="em++"
	--ar="emar"
	--ranlib="emranlib"
	--disable-stripping
	--prefix=$(pwd)/ffmpeg_wasm
	--enable-cross-compile
	--target-os=none
	--cpu=generic
	--arch=x86_32
	--disable-postproc
	--disable-programs
	--disable-logging
	--disable-asm
	--disable-doc
	--disable-debug
	--disable-ffplay
	--disable-ffmpeg
	--disable-ffprobe
	--disable-symver
	--disable-everything
	--disable-pthreads
	--disable-network
	--disable-hwaccels
	--disable-parsers
	--disable-bsfs
	--disable-protocols
	--disable-indevs
	--disable-outdevs
	--disable-avdevice
	--disable-avformat
	--disable-swresample
	--disable-avfilter
	--enable-decoder=hevc
	--enable-decoder=h264
)

# 	--enable-protocol=file
# 	--enable-parser=hevc
# 	--enable-parser=h264
# 	--enable-demuxer=h264

cd ffmpeg-4.3.1/
# emmake make clean

emconfigure ./configure "${ARGS[@]}"
emmake make
emmake make install

