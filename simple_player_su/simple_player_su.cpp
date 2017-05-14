// simple_player_su.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <stdio.h>
#include <iostream>

#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "SDL2/SDL.h"
}
#endif


extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }


int thread_exit = 0;
int thread_pause = 0;
int frameRate = 25;

#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)

int sfp_refresh_thread(void *opaque) {
	thread_exit = 0;
	thread_pause = 0;

	while (!thread_exit) {
		if (!thread_pause) {
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(1000 / frameRate);
	}
	thread_exit = 0;
	thread_pause = 0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);

	return 0;
}

int main(int argc, char* argv[])
{
	const char filePath[] = "bigbuckbunny_480x272.h265";

	//ffmpeg variables
	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtx;
	AVCodec *pCodec;
	AVFrame *pFrame, *pFrameYUV;
	AVPacket *pPacket;
	SwsContext *pSwsCtx;

	unsigned char *outBuffer;
	int videoIndex;
	int ret = 0, gotPicture;

	//SDL 
	int screenWidth, screenHeight;
	SDL_Window *pSdlWindow;
	SDL_Texture *pSdlTexture;
	SDL_Renderer *pSdlRenderer;
	SDL_Rect sdlRect;
	SDL_Thread *pSdlThread;
	SDL_Event sdlEvent;
	
	//ffmpeg init
	av_register_all();
	pFormatCtx = avformat_alloc_context();

	if ((ret = avformat_open_input(&pFormatCtx, filePath, NULL, NULL)) != 0) {
		std::cout << "can not open file" << std::endl;
		goto end;
	}
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		std::cout << "can not find stream info" << std::endl;
		ret = -1;
		goto end;
	}
	videoIndex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
			break;
		}
	}
	if (videoIndex == -1) {
		std::cout << "can not find video streams" << std::endl;
		ret = -1;
		goto end;
	}
	pCodecCtx = pFormatCtx->streams[videoIndex]->codec;
	frameRate = pFormatCtx->streams[videoIndex]->avg_frame_rate.num / pFormatCtx->streams[videoIndex]->avg_frame_rate.den;
	std::cout << "video frame rate: " << frameRate << std::endl;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		std::cout << "can not find decoder" << std::endl;
		ret = -1;
		goto end;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		std::cout << "can not open decoder" << std::endl;
		ret = -1;
		goto end;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();

	outBuffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width,
		pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, outBuffer, AV_PIX_FMT_YUV420P, pCodecCtx->width,
		pCodecCtx->height, 1);

	//output video info
	std::cout << "------------video info-----------------" << std::endl;
	av_dump_format(pFormatCtx, 0, filePath, 0);
	std::cout << "---------------------------------------" << std::endl;

	pSwsCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "Could not initialize SDL: " << SDL_GetError() << std::endl;
		ret = -1;
		goto end;
	}
	//SDL 2.0 Support for multiple windows
	screenWidth = pCodecCtx->width;
	screenHeight = pCodecCtx->height;
	pSdlWindow = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_OPENGL);
	if (!pSdlWindow) {
		std::cout << "SDL: could not create window - exiting: " << SDL_GetError() << std::endl;
		ret = -1;
		goto end;
	}
	pSdlRenderer = SDL_CreateRenderer(pSdlWindow, -1, 0);
	pSdlTexture = SDL_CreateTexture(pSdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screenWidth;
	sdlRect.h = screenHeight;


	pPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
	pSdlThread = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

	// SDL event loop
	for (; ;) {
		SDL_WaitEvent(&sdlEvent);
		if (sdlEvent.type == SFM_REFRESH_EVENT) {
			while (1) {
				if (av_read_frame(pFormatCtx, pPacket) < 0)
					thread_exit = 1;

				if (pPacket->stream_index == videoIndex)
					break;
			}
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &gotPicture, pPacket);
			if (ret < 0) {
				printf("Decode Error.\n");
				ret = -1;
				goto end;
			}
			if (gotPicture) {
				sws_scale(pSwsCtx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				//SDL---------------------------
				SDL_UpdateTexture(pSdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
				SDL_RenderClear(pSdlRenderer);
				//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
				SDL_RenderCopy(pSdlRenderer, pSdlTexture, NULL, NULL);
				SDL_RenderPresent(pSdlRenderer);
				//SDL End-----------------------
			}
			av_free_packet(pPacket);
		} else if (sdlEvent.type == SDL_KEYDOWN) {
			//Pause
			if (sdlEvent.key.keysym.sym == SDLK_SPACE)
				thread_pause = !thread_pause;
		} else if(sdlEvent.type == SDL_QUIT){
			thread_exit=1;
		} else if(sdlEvent.type == SFM_BREAK_EVENT){
			break;
		}
	}
	SDL_Quit();



		
end:
	//close ffmpeg
	if (pSwsCtx != NULL) {
		sws_freeContext(pSwsCtx);
	}
	if (pFrameYUV != NULL) {
		av_frame_free(&pFrameYUV);
	}
	if (pFrame != NULL) {
		av_frame_free(&pFrame);
	}
	if (pCodecCtx != NULL) {
		avcodec_close(pCodecCtx);
	}
	if (pFormatCtx != NULL) {
		avformat_close_input(&pFormatCtx);
	}


	return ret;
}

