rm -rf libffmpeg.wasm libffmpeg.js
export TOTAL_MEMORY=67108864
export EXPORTED_FUNCTIONS="[ \
		'_malloc', \
		'_free', \
		'_init_decoder', \
		'_decode_pkg', \
		'_close_decoder'\
]"

echo "Running Emscripten..."
emcc ffmpeg_decode.c ffmpeg_wasm/lib/libavcodec.a ffmpeg_wasm/lib/libavutil.a ffmpeg_wasm/lib/libswscale.a \
    -O3 \
    -I "ffmpeg_wasm/include" \
    -s WASM=1 \
    -s TOTAL_MEMORY=${TOTAL_MEMORY} \
   	-s EXPORTED_FUNCTIONS="${EXPORTED_FUNCTIONS}" \
   	-s EXTRA_EXPORTED_RUNTIME_METHODS="['addFunction', 'removeFunction']" \
		-s RESERVED_FUNCTION_POINTERS=14 \
    -o libffmpeg.js

echo "Finished Build"

