#include <stdio.h>
#include <unistd.h>
#include "tt_web.h"

#ifdef WATCH_RAM
#include "tt_malloc_debug.h"
#define MY_MALLOC(x) my_malloc((x), __func__, __LINE__)
#define MY_FREE(x) my_free((x), __func__, __LINE__)
#define MY_REALLOC(x, y) my_realloc((x), (y), __func__, __LINE__)
#else
#define MY_MALLOC(x) malloc((x))
#define MY_FREE(x) free((x))
#define MY_REALLOC(x, y) realloc((x), (y))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#ifdef __cplusplus
}
#endif

typedef struct PACKETINFO {
	void *data;
	size_t size;
	struct PACKETINFO *next;
}PACKETINFO;

AVCodecContext *g_usbdecode_ctx = NULL;
AVCodecContext *g_usbencode_ctx = NULL;

struct SWSINFO {
	int width;
	int height;
	enum AVPixelFormat infmt;
	enum AVPixelFormat outfmt;
	struct SwsContext *sws_ctx;
} g_usbswsinfo = {0};

int init_sws(int width, int height, int infmt, int outfmt) {
	if (g_usbswsinfo.sws_ctx == NULL || g_usbswsinfo.width != width || g_usbswsinfo.height != height
			|| g_usbswsinfo.infmt != infmt || g_usbswsinfo.outfmt != outfmt) {
		g_usbswsinfo.sws_ctx = sws_getContext(\
			width, height, (enum AVPixelFormat)infmt,
			width, height, (enum AVPixelFormat)outfmt,
			/*SWS_BICUBIC*/SWS_BILINEAR, NULL, NULL, NULL);
		g_usbswsinfo.width = width;
		g_usbswsinfo.height = height;
		g_usbswsinfo.infmt = infmt;
		g_usbswsinfo.outfmt = outfmt;
	}
	if (!g_usbswsinfo.sws_ctx) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	return 0;
}

static int convert_frame(AVFrame *frame, AVFrame **_dst_frame) {
	int ret = -1;
	AVFrame *dst_frame = NULL;

	if (0 != init_sws(frame->width, frame->height, frame->format, AV_PIX_FMT_YUV420P)) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	dst_frame = av_frame_alloc();
	if (!dst_frame) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	dst_frame->width = frame->width;
	dst_frame->height = frame->height;
	dst_frame->format = AV_PIX_FMT_YUV420P;
	av_frame_get_buffer(dst_frame, 0);
	ret = sws_scale(g_usbswsinfo.sws_ctx, (const uint8_t * const*)frame->data, frame->linesize, 0, frame->height, dst_frame->data, dst_frame->linesize);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	ret = 0;
exit:
	if (ret != 0) {
		if (dst_frame != NULL) {
			av_frame_free(&dst_frame);
		}
	}
	*_dst_frame = dst_frame;
	return 0;
}
static int init_encoder(int width, int height, int bit_rate, int fps_num, int fps_den, enum AVPixelFormat infmt) {
	int ret = 0;
	AVCodec *codec = NULL;
	AVCodecContext *codec_ctx = NULL;

	codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	codec_ctx->bit_rate = bit_rate;
	codec_ctx->width = width;
	codec_ctx->height = height;
	codec_ctx->time_base = (AVRational){fps_den, fps_num};
	codec_ctx->framerate = (AVRational){fps_num, fps_den};
	codec_ctx->gop_size = 30;
	codec_ctx->max_b_frames = 0;
	codec_ctx->pix_fmt = infmt;
	av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0); 

	ret = avcodec_open2(codec_ctx, codec, NULL);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	g_usbencode_ctx = codec_ctx;
	return 0;
}
static int encode_frame(AVFrame *frame) {
	int ret = 0;
	AVPacket *packet = NULL;
	void *data_dup = NULL;

	if (!g_usbencode_ctx) {
		return -1;
	}
	if (frame) {
		frame->pict_type = AV_PICTURE_TYPE_NONE;
		frame->pts = 0;
	}
	ret = avcodec_send_frame(g_usbencode_ctx, frame);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	if (!packet) {
		packet = av_packet_alloc();
	}
	if (!packet) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	while (1) {
		ret = avcodec_receive_packet(g_usbencode_ctx, packet);
		if (ret == AVERROR(EAGAIN)) {
			// printf("EAGAIN\n");
			break;
		}
		if (ret == AVERROR_EOF) {
			// printf("EOF\n");
			break;
		}
		if (ret < 0) {
			printf("%s %d\n", __FILE__, __LINE__);
			return -1;
		}
		data_dup = MY_MALLOC(packet->size);
		if (data_dup) {
			memcpy(data_dup, packet->data, packet->size);
			notify_web("usbimgs", data_dup, packet->size, NULL, NULL);
		}

		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	return 0;
}
static int decode_packet(void *_buf, size_t size) {
	int ret = 0;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL, *yuv_frame = NULL;
	unsigned char *bufdup = NULL;
	unsigned char *buf = (unsigned char *)_buf;

	if (g_usbdecode_ctx == NULL) {
		return -1;
	}
	frame = av_frame_alloc();
	if (frame == NULL) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}

	packet = av_packet_alloc();
	if (packet == NULL) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	bufdup = (unsigned char *)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE); /* free when call av_packet_free */
	if (bufdup == NULL) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	memset(bufdup, 0x00, size + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(bufdup, buf, size);
	ret = av_packet_from_data(packet, (uint8_t *)bufdup, size);
	if (ret != 0) {
		return -1;
	}
	ret = avcodec_send_packet(g_usbdecode_ctx, packet);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	while (1) {
		ret = avcodec_receive_frame(g_usbdecode_ctx, frame);
		if (ret == AVERROR(EAGAIN)) {
			// printf("EAGAIN\n");
			break;
		}
		if (ret == AVERROR_EOF) {
			// printf("EOF\n");
			break;
		}
		if (ret < 0) {
			printf("%s %d\n", __FILE__, __LINE__);
			return -1;
		}
#if 0
		printf("frame:\n");
		printf("\t%d x %d, ", frame->width, frame->height);
		if (frame->format == AV_PIX_FMT_YUV420P) {
			printf("YUV420P\n");
		} else if (frame->format == AV_PIX_FMT_YUVJ420P) {
			printf("YUVJ420P\n");
		} else {
			printf("unknown pixfmt %d\n", frame->format);
		}
		printf("\tframe num: %d\n", frame->coded_picture_number);
#endif
		convert_frame(frame, &yuv_frame);
		encode_frame(yuv_frame);
		av_frame_free(&yuv_frame);
		// printf("\tkey_frame: %d\n", frame->key_frame);
		av_frame_unref(frame);
	}
	av_frame_free(&frame);
	av_packet_free(&packet);
	return 0;
}

void *get_usbimgs(void *arg) {
	int ret = -1, i = 0;
	const char *fname = (const char *)arg;
	AVInputFormat *in_fmt = NULL;
	AVDictionary *dict = NULL;
	AVFormatContext *fmt_ctx = NULL;
	AVStream *stream = NULL;
	AVCodec *codec = NULL;
	AVCodecContext *codec_ctx = NULL;
	AVPacket packet;

	avdevice_register_all();
	in_fmt = av_find_input_format("video4linux2");
	if (!in_fmt) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	av_dict_set(&dict, "video_size", "640x480", 0);
	av_dict_set(&dict, "input_format", "mjpeg", 0);
	av_dict_set(&dict, "framerate", "30", 0);
	avformat_open_input(&fmt_ctx, fname, in_fmt, &dict);
	av_dict_free(&dict);
	if (!fmt_ctx) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	av_dump_format(fmt_ctx, 0, "camera", 0);

	if (fmt_ctx->nb_streams != 1) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	for (i = 0; i < (int)fmt_ctx->nb_streams; i++) {
		stream = fmt_ctx->streams[i];
		codec = avcodec_find_decoder(stream->codecpar->codec_id);
		if (!codec) {
			printf("%s %d\n", __FILE__, __LINE__);
			continue;
		}
		codec_ctx = avcodec_alloc_context3(codec);
		if (!codec_ctx) {
			printf("%s %d\n", __FILE__, __LINE__);
			continue;
		}
		ret = avcodec_parameters_to_context(codec_ctx, stream->codecpar);
		if (ret < 0) {
			printf("%s %d\n", __FILE__, __LINE__);
			avcodec_free_context(&codec_ctx);
			codec_ctx = NULL;
			continue;
		}
		if (codec_ctx->codec_type != AVMEDIA_TYPE_VIDEO) {
			printf("%s %d\n", __FILE__, __LINE__);
			avcodec_free_context(&codec_ctx);
			codec_ctx = NULL;
			continue;
		}
		codec_ctx->framerate = av_guess_frame_rate(fmt_ctx, stream, NULL);
		if (ret < 0) {
			printf("%s %d\n", __FILE__, __LINE__);
			goto exit;
		}
		printf("%d x %d, fps: %.2f (%d / %d)\n", \
				codec_ctx->width, codec_ctx->height, \
				(float)codec_ctx->framerate.num / codec_ctx->framerate.den, \
				codec_ctx->framerate.num, codec_ctx->framerate.den);
		ret = avcodec_open2(codec_ctx, fmt_ctx->video_codec, NULL);
		if (ret < 0) {
			printf("%s %d\n", __FILE__, __LINE__);
			goto exit;
		}
		g_usbdecode_ctx = codec_ctx;
		break;
	}
	if (!g_usbdecode_ctx) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}

	init_encoder(codec_ctx->width, codec_ctx->height, 512*1024, codec_ctx->framerate.num, codec_ctx->framerate.den, AV_PIX_FMT_YUV420P);
	while (1) {
		ret = av_read_frame(fmt_ctx, &packet);
		if (ret < 0) {
			break;
		}
		// printf("read: %d\n", packet.size);
		decode_packet(packet.data, packet.size);
		av_packet_unref(&packet);
	}
	encode_frame(NULL);
	ret = 0;
exit:
	if (codec_ctx) {
		avcodec_free_context(&codec_ctx);
	}
	if (fmt_ctx) {
		avformat_close_input(&fmt_ctx);
	}
	return NULL;
}

#if 1
int http_msg_usbimgs(const char *name, void *buf, size_t len) {
	HTTP_FD *p_link = NULL;

	for (p_link = g_http_links; p_link != NULL; p_link = p_link->next) {
		if (p_link->state != STATE_WS_CONNECTED) {
			continue;
		}
		if (0 != strcmp(p_link->path, "/ws/usbimgs")) {
			continue;
		}
		ws_write(p_link, buf, len);
		ws_pack(p_link, WS_OPCODE_BINARY);
	}
	return 0;
}
int http_ws_usbimgs(HTTP_FD *p_link, E_WS_EVENT event) {
	return 0;
}
#endif
