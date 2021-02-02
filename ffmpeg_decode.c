#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>

#ifdef __cplusplus
}
#endif

AVCodecContext *g_decode_ctx = NULL;
AVFrame *g_frame = NULL;
unsigned char *g_data = NULL;
void (*g_notify)(long int data, size_t size, int width, int height) = NULL;

void hexdump(void *_buf, size_t size) {
	unsigned char *buf = (unsigned char *)_buf;
	size_t i = 0, offset = 0;
	char tmp = '\0';
	for (offset = 0; offset < size; offset += 16) {
		printf("%08x: ", (unsigned int)offset);
		for (i = 0; i < 16; i++) {
			if (offset + i < size) {
				printf("%02x ", *(buf + offset + i));
			} else {
				printf("   ");
			}
		}
		printf("  ");
		for (i = 0; i < 16; i++) {
			if (offset + i < size) {
				tmp = *(buf + offset + i);
				if (tmp < ' ' || tmp > '~') {
					printf(".");
				} else {
					printf("%c", tmp);
				}
			}
		}
		printf("\n");
	}
}

int init_decoder(int code, int pix_fmt, void (*notify)(long int data, size_t size, int width, int height)) {
	int ret = -1;
	AVCodec *codec = NULL;
	AVCodecContext *codec_ctx = NULL;
	AVFrame *frame = NULL;

	av_log_set_level(AV_LOG_PANIC);
	// if (code == AV_CODEC_ID_H264) {
	// 	printf("code = H264\n");
	// }
	// if (pix_fmt == AV_PIX_FMT_YUV420P) {
	// 	printf("pix_fmt = AV_PIX_FMT_YUV420P\n");
	// }
	// codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	codec = avcodec_find_decoder(code);
	if (!codec) {
		ret = __LINE__;
		goto exit;
	}
	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		ret = __LINE__;
		goto exit;
	}
	// codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	codec_ctx->pix_fmt = pix_fmt;

	ret = avcodec_open2(codec_ctx, codec, NULL);
	if (ret < 0) {
		ret = __LINE__;
		goto exit;
	}

	frame = av_frame_alloc();
	if (frame == NULL) {
		ret = __LINE__;
		goto exit;
	}
	ret = 0;
exit:
	if (ret != 0) {
		if (frame) {
			av_frame_free(&frame);
		}
		if (codec_ctx) {
			avcodec_free_context(&codec_ctx);
		}
	}
	g_decode_ctx = codec_ctx;
	g_frame = frame;
	g_notify = notify;
	return ret;
}

int decode_pkg(void *_buf, size_t size) {
	int i = 0, ret = -1;
	AVPacket *packet = NULL;
	unsigned char *bufdup = NULL;
	unsigned char *buf = (unsigned char *)_buf;
	AVFrame *frame = g_frame;
	unsigned char *data_head = NULL, *s_data = NULL, *d_data = NULL;
	size_t data_size = 0;

	// hexdump(_buf, size);
	if (g_decode_ctx == NULL) {
		ret = __LINE__;
		goto exit;
	}

	packet = av_packet_alloc();
	if (packet == NULL) {
		ret = __LINE__;
		goto exit;
	}
	bufdup = (unsigned char *)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE); /* freed by call av_packet_free */
	if (bufdup == NULL) {
		ret = __LINE__;
		goto exit;
	}
	memset(bufdup, 0x00, size + AV_INPUT_BUFFER_PADDING_SIZE);
	memcpy(bufdup, buf, size);
	ret = av_packet_from_data(packet, (uint8_t *)bufdup, size);
	if (ret != 0) {
		ret = __LINE__;
		goto exit;
	}
	bufdup = NULL;
	ret = avcodec_send_packet(g_decode_ctx, packet);
	if (ret < 0) {
		ret = __LINE__;
		goto exit;
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
			ret = __LINE__;
			goto exit;
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
#endif
		data_size = frame->width * frame->height + ((frame->width * frame->height) >> 1);
		data_head= (unsigned char *)malloc(data_size);
		if (data_head) {
			s_data = frame->data[0];
			d_data = data_head;
			for (i = 0; i < frame->height; i++) {
				memcpy(d_data, s_data, frame->width);
				s_data += frame->linesize[0];
				d_data += frame->width;
			}

			s_data = frame->data[1];
			for (i = 0; i < frame->height >> 1; i++) {
				memcpy(d_data, s_data, frame->width >> 1);
				s_data += frame->linesize[1];
				d_data += (frame->width >> 1);
			}

			s_data = frame->data[2];
			for (i = 0; i < frame->height >> 1; i++) {
				memcpy(d_data, s_data, frame->width >> 1);
				s_data += frame->linesize[2];
				d_data += (frame->width >> 1);
			}

			if (d_data - data_head != data_size) {
				printf("d_data - data_head (%ld) != data_size (%ld)\n", d_data - data_head, data_size);
			}
			if (g_notify != NULL) {
				g_notify((long int)data_head, data_size, frame->width, frame->height);
			}
			free(data_head);
		} else {
		}
		av_frame_unref(frame);
	}
	ret = 0;
exit:
	if (bufdup != NULL) {
		av_free(bufdup);
	}
	if (packet != NULL) {
		av_packet_free(&packet);
	}
	return ret;
}

int close_decoder() {
	if (g_decode_ctx != NULL) {
		avcodec_free_context(&g_decode_ctx);
	}
	if (g_frame != NULL) {
		av_frame_free(&g_frame);
	}
	return 0;
}
