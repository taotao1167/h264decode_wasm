#include <stdio.h>
#include <unistd.h>
#include "tt_web.h"

/* only for debug */
#include <zmq.h>

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

AVCodecContext *g_decode_ctx = NULL;
AVCodecContext *g_encode_ctx = NULL;
PACKETINFO *g_pkg_head = NULL;
PACKETINFO *g_pkg_tail = NULL;
PACKETINFO *g_pkg_cursor = NULL;

void *g_publisher = NULL;/* only for debug */

struct SWSINFO {
	int width;
	int height;
	enum AVPixelFormat infmt;
	enum AVPixelFormat outfmt;
	struct SwsContext *sws_ctx;
} g_swsinfo = {0};

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
	g_encode_ctx = codec_ctx;
	return 0;
}
/* only for debug begin */
static int publish_data(void *content, size_t size) {
	int ret = 0;
	zmq_msg_t msg;
	unsigned char *p_con = NULL;

	zmq_msg_init_size(&msg, size);
	p_con = (unsigned char *)zmq_msg_data(&msg);
	memcpy(p_con, content, size);
	ret = zmq_msg_send(&msg, g_publisher, 0);
	// printf("send %d bytes.\n", ret);
	zmq_msg_close(&msg);
	return ret;
}
/* only for debug end */
static int append_new_pkg(void *data, size_t size) {
	int ret = -1;
	PACKETINFO *p_new = NULL;
	p_new = (PACKETINFO *)MY_MALLOC(sizeof(PACKETINFO));
	if (p_new == NULL) {
		goto exit;
	}
	memset(p_new, 0x00, sizeof(PACKETINFO));
	p_new->data = MY_MALLOC(size);
	if (p_new->data == NULL) {
		goto exit;
	}
	memcpy(p_new->data, data, size);
	p_new->size = size;
	if (g_pkg_head == NULL) {
		g_pkg_head = g_pkg_tail = p_new;
	} else {
		g_pkg_tail->next = p_new;
		g_pkg_tail = p_new;
	}
	ret = 0;
exit:
	return ret;
}
static int encode_frame(AVFrame *frame) {
	int ret = 0;
	AVPacket *packet = NULL;

	if (!g_encode_ctx) {
		return -1;
	}
	if (frame) {
		frame->pict_type = AV_PICTURE_TYPE_NONE;
		frame->pts = 0;
	}
	ret = avcodec_send_frame(g_encode_ctx, frame);
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
		ret = avcodec_receive_packet(g_encode_ctx, packet);
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
		// printf("write pkt, pts: %ld, size: %d\n", packet->pts, packet->size);

		append_new_pkg(packet->data, packet->size);
		// notify_web("video", packet->data, packet->size, NULL, NULL);
		// publish_data(packet->data, packet->size);

		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	return 0;
}
static int decode_packet(void *_buf, size_t size) {
	int ret = 0;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;
	unsigned char *bufdup = NULL;
	unsigned char *buf = (unsigned char *)_buf;

	if (g_decode_ctx == NULL) {
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
	ret = avcodec_send_packet(g_decode_ctx, packet);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		return -1;
	}
	while (1) {
		ret = avcodec_receive_frame(g_decode_ctx, frame);
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
		encode_frame(frame);
		usleep(15000);
		// printf("\tkey_frame: %d\n", frame->key_frame);
		av_frame_unref(frame);
	}
	av_frame_free(&frame);
	av_packet_free(&packet);
	return 0;
}

int init_frames(const char *fname) {
	int ret = -1, i = 0;
	AVFormatContext *fmt_ctx = NULL;
	AVStream *stream = NULL;
	AVCodec *codec = NULL;
	AVCodecContext *codec_ctx = NULL;
	AVPacket packet;

/* only for debug begin */
	void *context = NULL;

	context = zmq_ctx_new();
	if (context == NULL) {
		printf("create context failed.\n");
		goto exit;
	}
	g_publisher = zmq_socket(context, ZMQ_PUB);
	if (g_publisher == NULL) {
		printf("create publisher failed.\n");
		goto exit;
	}
	ret = zmq_bind(g_publisher, "tcp://*:6666");
	if (ret != 0) {
		printf("bind failed.\n");
		goto exit;
	}
	sleep(1);
/* only for debug end */

	avformat_open_input(&fmt_ctx, fname, NULL, NULL);
	if (!fmt_ctx) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if (ret < 0) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}
	av_dump_format(fmt_ctx, 0, "localfile", 0);

	if (fmt_ctx->nb_streams != 1) {
		printf("%s %d: nb_streams: %d\n", __FILE__, __LINE__, fmt_ctx->nb_streams);
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
		g_decode_ctx = codec_ctx;
		break;
	}
	if (!g_decode_ctx) {
		printf("%s %d\n", __FILE__, __LINE__);
		goto exit;
	}

	init_encoder(codec_ctx->width, codec_ctx->height, 512*1024, codec_ctx->framerate.num, codec_ctx->framerate.den, codec_ctx->pix_fmt);
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
	g_pkg_tail->next = g_pkg_head;
exit:
	if (codec_ctx) {
		avcodec_free_context(&codec_ctx);
	}
	if (fmt_ctx) {
		avformat_close_input(&fmt_ctx);
	}
	return 0;
}
void *get_frames(void *arg) {
	void *data_dup = NULL;

	if (init_frames((const char *)arg) == 0) {
		if (g_pkg_head == NULL) {
			return NULL;
		}
		while (1) {
			if (g_pkg_cursor == NULL) {
				g_pkg_cursor = g_pkg_head;
			} else {
				g_pkg_cursor = g_pkg_cursor->next;
			}
			data_dup = MY_MALLOC(g_pkg_cursor->size);
			if (data_dup) {
				memcpy(data_dup, g_pkg_cursor->data, g_pkg_cursor->size);
				notify_web("video", data_dup, g_pkg_cursor->size, NULL, NULL);
			}
			publish_data(g_pkg_cursor->data, g_pkg_cursor->size);
			usleep(40000);
		}
	}
	return NULL;
}

#if 1
int http_msg_video(const char *name, void *buf, size_t len) {
	HTTP_FD *p_link = NULL;

	for (p_link = g_http_links; p_link != NULL; p_link = p_link->next) {
		if (p_link->state != STATE_WS_CONNECTED) {
			continue;
		}
		if (0 != strcmp(p_link->path, "/ws/video")) {
			continue;
		}
		ws_write(p_link, buf, len);
		ws_pack(p_link, WS_OPCODE_BINARY);
	}
	return 0;
}
int http_ws_video(HTTP_FD *p_link, E_WS_EVENT event) {
	return 0;
}
#endif
