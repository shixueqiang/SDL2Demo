#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <jni.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "SDL.h"
#include <android/log.h>
#define LOG_TAG "SDL"
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define FF_ALLOC_EVENT   (SDL_USEREVENT)
#define FF_REFRESH_EVENT (SDL_USEREVENT + 1)
#define FF_QUIT_EVENT (SDL_USEREVENT + 2)

#define VIDEO_PICTURE_QUEUE_SIZE 1
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

typedef struct Sprite {
	SDL_Texture* texture;
	Uint16 w;
	Uint16 h;
} Sprite;

typedef struct VideoDecoder {
	AVFormatContext *pFormatCtx;
	AVCodec *codec;
	AVCodecContext *codec_ctx;
	struct SwsContext *convert_ctx;
	AVFrame *pFrame;
	AVFrame *pFrameYUV;
	AVPacket *packet;
	SDL_Window *screen;
	SDL_Renderer *renderer;
	SDL_Texture *bmp;
	SDL_Rect rect;
	SDL_Event event;
} VideoDecoder;

typedef struct AudioDecoder {
	AVCodec *codec;
	AVCodecContext *codec_ctx;
	AVPacket *aPacket;
	AVFrame *aFrame;
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
	SDL_AudioSpec desired, obtained; //SDL_OpenAudio需要传递的参数，1期望的参数2实际音频设备的参数，一般传空就好
} AudioDecoder;
int quit = 0;
static void av_log_callback(void *ptr, int level, const char *fmt, __va_list vl) {
	static char line[1024] = { 0 };
	vsnprintf(line, sizeof(line), fmt, vl);
//	LOGE("av_log_callback : %s", line);
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
	av_register_all();
	av_log_set_callback(av_log_callback);

	return JNI_VERSION_1_4;
}
/* Adapted from SDL's testspriteminimal.c */
Sprite LoadSprite(const char* file, SDL_Renderer* renderer) {
	Sprite result;
	result.texture = NULL;
	result.w = 0;
	result.h = 0;

	SDL_Surface* temp;

	/* Load the sprite image */
	temp = SDL_LoadBMP(file);
	if (temp == NULL) {
		fprintf(stderr, "Couldn't load %s: %s\n", file, SDL_GetError());
		return result;
	}
	result.w = temp->w;
	result.h = temp->h;

	/* Create texture from the image */
	result.texture = SDL_CreateTextureFromSurface(renderer, temp);
	if (!result.texture) {
		fprintf(stderr, "Couldn't create texture: %s\n", SDL_GetError());
		SDL_FreeSurface(temp);
		return result;
	}
	SDL_FreeSurface(temp);

	return result;
}

void draw(SDL_Window* window, SDL_Renderer* renderer, const Sprite sprite) {
	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	SDL_Rect destRect = { w / 2 - sprite.w / 2, h / 2 - sprite.h / 2, sprite.w,
			sprite.h };
	/* Blit the sprite onto the screen */
	SDL_RenderCopy(renderer, sprite.texture, NULL, &destRect);
}

void decodeVideo() {

}

int decodeAudio(AudioDecoder *audioCtx, AVPacket *pkt) {
	LOGE("decodeAudio()=========start%d",audioCtx->aPacket->size);
	AVPacketList *pkt1;
	if (av_dup_packet(pkt) < 0) {
		return -1;
	}
	LOGE("decodeAudio()=========start1");
	pkt1 = (AVPacketList *) av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;
	SDL_LockMutex(audioCtx->mutex);
	LOGE("decodeAudio()=========start2");
	if (!audioCtx->last_pkt)
		audioCtx->first_pkt = pkt1;
	else
		audioCtx->last_pkt->next = pkt1;
	audioCtx->last_pkt = pkt1;
	audioCtx->nb_packets++;
	audioCtx->size += pkt1->pkt.size;
	SDL_CondSignal(audioCtx->cond);
	SDL_UnlockMutex(audioCtx->mutex);
	LOGE("decodeAudio()=========end");
	return 0;
}
int audio_decode_frame(AudioDecoder *audioCtx, uint8_t *audio_buf, int buf_size) {
	LOGE("start audio_decode_frame==========");
	AVPacket *pkt = audioCtx->aPacket;
	if(pkt == NULL)
		LOGE("start audio_decode_frame==========pkt == NULL");
	uint8_t *audio_pkt_data = NULL;
	int audio_pkt_size = 0;
	int len1, data_size;
	int framFinish = 0;
	LOGE("start audio_decode_frame==========1 packet size:%d",pkt->size);
	audioCtx->aFrame = av_frame_alloc();
	for (;;) {
		while (audio_pkt_size > 0) {
			data_size = buf_size;
			LOGE("start audio_decode_frame==========2");
			len1 = avcodec_decode_audio4(audioCtx->codec_ctx, audioCtx->aFrame,
					&framFinish, audioCtx->aPacket);
			LOGE("decode audio ...");
			if (len1 < 0) { /* if error, skip frame */
				audio_pkt_size = 0;
				break;
			}
			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			if (data_size <= 0) { /* No data yet, get more frames */
				continue;
			} /* We have data, return it and come back for more later */
			return data_size;
		}
		if (pkt->data)
			av_free_packet(pkt);
		if (quit) {
			return -1;
		}
		if (packet_queue_get(&audioCtx, 1) < 0) {
			return -1;
		}
		LOGE("audio_decode_frame ======aaaaaa");
		audio_pkt_data = pkt->data;
		audio_pkt_size = pkt->size;
	}
	return 0;
}

int packet_queue_get(AudioDecoder *q, int block) {
	AVPacket *pkt = q->aPacket;
	AVPacketList *pkt1;
	int ret;
	SDL_LockMutex(q->mutex);
	for (;;) {
		if (quit) {
			ret = -1;
			break;
		}
		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		} else if (!block) {
			ret = 0;
			break;
		} else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}
void audio_callback(void *userdata, Uint8 *stream, int len) {
	AudioDecoder *audioCtx = (AudioDecoder *) userdata;
	int len1, audio_size;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audio_buf_size = 0;
	unsigned int audio_buf_index = 0;
	while (len > 0) {
		if (audio_buf_index >= audio_buf_size) { /* We have already sent all our data; get more */
			LOGE("audio_decode_frame audioCtx->pkt:");
			audio_size = audio_decode_frame(audioCtx, audio_buf,
					sizeof(audio_buf));
			if (audio_size < 0) { /* If error, output silence */
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);
			} else {
				audio_buf_size = audio_size;
			}
			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;
		memcpy(stream, (uint8_t *) audio_buf + audio_buf_index, len1);
		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}
}

int main(int argc, char *argv[]) {
	VideoDecoder *videoCtx = (VideoDecoder *) av_mallocz(sizeof(VideoDecoder));
	AudioDecoder *audioCtx = (AudioDecoder *) av_mallocz(sizeof(AudioDecoder));
	audioCtx->mutex = SDL_CreateMutex();
	audioCtx->cond = SDL_CreateCond();
	LOGE("SDL init");
	//init sdl2.0
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		LOGE("Could not initialize SDL - %s\n", SDL_GetError());
		exit(-1);
	}
	LOGE("SDL init success");

	// Open video file
	if (avformat_open_input(&videoCtx->pFormatCtx, argv[1], NULL, NULL) != 0)
		return -1;
	LOGE("open video file success.");
	//获取视频流信息
	if (avformat_find_stream_info(videoCtx->pFormatCtx, NULL) < 0)
		return -1;
	LOGE("stream info success.");
	// Dump information about file onto standard error
	av_dump_format(videoCtx->pFormatCtx, 0, argv[1], 0);

	int videoIndex = -1, audioIndex = -1, i = 0;
	//获取视频数据的下标值
	for (i = 0; i < videoCtx->pFormatCtx->nb_streams; i++) {
		if (videoCtx->pFormatCtx->streams[i]->codec->codec_type
				== AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
		}
		if (videoCtx->pFormatCtx->streams[i]->codec->codec_type
				== AVMEDIA_TYPE_AUDIO) {
			audioIndex = i;
		}
	}
	if (videoIndex == -1 || audioIndex == -1)
		return -1;
	LOGE("拿到解码环境videoIndex:%d,audioIndex:%d",videoIndex,audioIndex);
	//拿到音频解码环境
	audioCtx->codec_ctx = videoCtx->pFormatCtx->streams[audioIndex]->codec;
	audioCtx->desired.freq = audioCtx->codec_ctx->sample_rate;
	audioCtx->desired.format = AUDIO_S16SYS;
	audioCtx->desired.channels = audioCtx->codec_ctx->channels;
	audioCtx->desired.silence = 0;
	audioCtx->desired.samples = SDL_AUDIO_BUFFER_SIZE;
	audioCtx->desired.callback = audio_callback;
	audioCtx->desired.userdata = audioCtx;

	if (SDL_OpenAudio(&audioCtx->desired, &audioCtx->obtained) < 0) {
		LOGE("SDL_OpenAudio: %s/n", SDL_GetError());
		return -1;
	}
	//寻找合适的音频解码器
	audioCtx->codec = avcodec_find_decoder(audioCtx->codec_ctx->codec_id);
	if (audioCtx->codec == NULL) {
		LOGE("Unsupported audio codec! \n");
		return -1;
	}
	//打开音频解码器
	if (avcodec_open2(audioCtx->codec_ctx, audioCtx->codec, NULL) < 0) {
		LOGE("Couldn't open audio codec.\n");
		return -1;
	}

	//解码音频先注掉，有问题待修复
//	SDL_PauseAudio(0);

	//拿到视频解码环境
	videoCtx->codec_ctx = videoCtx->pFormatCtx->streams[videoIndex]->codec;
	//寻找合适的视频解码器
	videoCtx->codec = avcodec_find_decoder(videoCtx->codec_ctx->codec_id);
	if (videoCtx->codec == NULL) {
		LOGE("Unsupported video codec! \n");
		return -1;
	}
	//打开视频解码器
	if (avcodec_open2(videoCtx->codec_ctx, videoCtx->codec, NULL) < 0) {
		LOGE("Couldn't open video codec.\n");
		return -1;
	}
	//AVFrame 获取帧指针
	videoCtx->pFrame = av_frame_alloc();
	videoCtx->pFrameYUV = av_frame_alloc();

	if (videoCtx->pFrameYUV == NULL)
		return -1;

	LOGE("create screen");
	videoCtx->screen = SDL_CreateWindow("my window",
	SDL_WINDOWPOS_UNDEFINED,
	SDL_WINDOWPOS_UNDEFINED, videoCtx->codec_ctx->width,
			videoCtx->codec_ctx->height,
			SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
	videoCtx->renderer = SDL_CreateRenderer(videoCtx->screen, -1, 0);
	LOGE("create screen success");
	if (!videoCtx->screen) {
		LOGE("SDL: could not set video mode - exiting\n");
		exit(1);
	}
	videoCtx->bmp = SDL_CreateTexture(videoCtx->renderer, SDL_PIXELFORMAT_YV12,
			SDL_TEXTUREACCESS_STREAMING, videoCtx->codec_ctx->width,
			videoCtx->codec_ctx->height);
	videoCtx->convert_ctx = sws_getContext(videoCtx->codec_ctx->width,
			videoCtx->codec_ctx->height, videoCtx->codec_ctx->pix_fmt,
			videoCtx->codec_ctx->width, videoCtx->codec_ctx->height,
			AV_PIX_FMT_YUV420P,
			SWS_BILINEAR,
			NULL,
			NULL,
			NULL);
	LOGE("计算YUV420P所需的内存大小");
	//计算YUV420P所需的内存大小,并分配内存
	int numBytes = avpicture_get_size(AV_PIX_FMT_YUV420P,
			videoCtx->codec_ctx->width, videoCtx->codec_ctx->height);
	uint8_t* buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

	avpicture_fill((AVPicture *) videoCtx->pFrameYUV, buffer,
			AV_PIX_FMT_YUV420P, videoCtx->codec_ctx->width,
			videoCtx->codec_ctx->height);
	LOGE("Read frames and save first five frames to disk");
	// Read frames and save first five frames to disk
	i = 0;
	videoCtx->rect.x = 0;
	videoCtx->rect.y = 0;
	videoCtx->rect.w = videoCtx->codec_ctx->width;
	videoCtx->rect.h = videoCtx->codec_ctx->height;
	int frameFinished = 0;
	LOGE("开始解码");
	int y_size = videoCtx->codec_ctx->width * videoCtx->codec_ctx->height;
	videoCtx->packet = (AVPacket *) malloc(sizeof(AVPacket));
	av_new_packet(videoCtx->packet, y_size);
	while (av_read_frame(videoCtx->pFormatCtx, videoCtx->packet) >= 0) {
		// Is this a packet from the video stream?
//		LOGE("Is this a packet from the video stream :%d",videoCtx->packet->stream_index);
		if (videoCtx->packet->stream_index == videoIndex) {
			// Decode video frame
			avcodec_decode_video2(videoCtx->codec_ctx, videoCtx->pFrame,
					&frameFinished, videoCtx->packet);
//			LOGE("Did we get a video frame");
			// Did we get a video frame?
			if (frameFinished) {
				LOGE("解码一帧成功");
				sws_scale(videoCtx->convert_ctx,
						(uint8_t const * const *) videoCtx->pFrame->data,
						videoCtx->pFrame->linesize, 0,
						videoCtx->codec_ctx->height, videoCtx->pFrameYUV->data,
						videoCtx->pFrameYUV->linesize);
				//计算yuv一行数据占的字节数
				SDL_UpdateTexture(videoCtx->bmp, &videoCtx->rect,
						videoCtx->pFrameYUV->data[0],
						videoCtx->pFrameYUV->linesize[0]);
				SDL_RenderClear(videoCtx->renderer);
				SDL_RenderCopy(videoCtx->renderer, videoCtx->bmp,
						&videoCtx->rect, &videoCtx->rect);
				SDL_RenderPresent(videoCtx->renderer);
			}
			SDL_Delay(50);
			// Free the packet that was allocated by av_read_frame
			av_free_packet(videoCtx->packet);
		} else if (videoCtx->packet->stream_index == audioIndex) {
//			LOGE("Did we get a audio frame");
			decodeAudio(audioCtx, videoCtx->packet);
		} else {
			av_free_packet(videoCtx->packet);
		}

		SDL_PollEvent(&videoCtx->event);
		switch (videoCtx->event.type) {
		case SDL_QUIT:
			quit = 1;
			SDL_Quit();
			exit(0);
			break;
		default:
			break;
		}
	}

	SDL_DestroyTexture(videoCtx->bmp);
	// Free the YUV frame
	av_free(videoCtx->pFrame);
	av_free(videoCtx->pFrameYUV);
	// Close the codec
	avcodec_close(videoCtx->codec_ctx);
	// Close the video file
	avformat_close_input(&videoCtx->pFormatCtx);
	free(videoCtx);

	//		SDL_Window *window;
	//		SDL_Renderer *renderer;
	//
	//		if (SDL_CreateWindowAndRenderer(0, 0, 0, &window, &renderer) < 0)
	//			exit(2);
	//
	//		Sprite sprite = LoadSprite("image.bmp", renderer);
	//		if (sprite.texture == NULL)
	//			exit(2);

	/* Main render loop */
	//	Uint8 done = 0;
	//	SDL_Event event;
	//	while (!done) {
	//		/* Check for events */
	//		while (SDL_PollEvent(&event)) {
	//			if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN
	//					|| event.type == SDL_FINGERDOWN) {
	//				done = 1;
	//			}
	//		}
	//			/* Draw a gray background */
	//			SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
	//			SDL_RenderClear(renderer);
	//
	//			draw(window, renderer, sprite);
	//
	//			/* Update the screen! */
	//			SDL_RenderPresent(renderer);
	//
	//			SDL_Delay(10);
	//	}
	return 0;
}
