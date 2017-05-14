// simple_player_sdl2.cpp : 定义控制台应用程序的入口点。
//
#include "stdafx.h"
#include <stdio.h>
#include <iostream>
#define __STDC_CONSTANT_MACROS

#ifdef _WIN32
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "SDL2/SDL.h"
}
#endif // _WIN32

//Output YUV420P data as a file 
#define OUTPUT_YUV420P 1

extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }

int main(int argc, char **argv)
{
	const char filePath[] = "bigbuckbunny_480x272.h265";
	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtx;
	AVCodec *pCodec;
	AVFrame *pFrame, *pFrameYUV;
	unsigned char *out_buffer;
	AVPacket *pPacket;

	av_register_all();
	pFormatCtx = avformat_alloc_context();

	if (avformat_open_input(&pFormatCtx, filePath, NULL, NULL) != 0) {
		std::cout << "could not open input file!" << std::endl;
		return -1;
	}

	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		std::cout << "can not find stream info" << std::endl;
		return -1;
	}

	int videoIndex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = i;
			break;
		}
	}
	std::cout << "videoIndex: " << videoIndex << std::endl;
	if (videoIndex == -1) {
		std::cout << "can not find video stream" << std::endl;
		return -1;
	}

	pCodecCtx = pFormatCtx->streams[videoIndex]->codec;
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);

	int frameRate = pFormatCtx->streams[videoIndex]->avg_frame_rate.num / pFormatCtx->streams[videoIndex]->avg_frame_rate.den;
	std::cout << "video frame rate: " << frameRate << std::endl;
	if (pCodec == NULL) {
		std::cout << "Can not find codec" << std::endl;
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		std::cout << "can not open codec" << std::endl;
		return -1;
	}
	pFrameYUV = av_frame_alloc();
	pFrame = av_frame_alloc();
	unsigned char* outBuffer = (unsigned char*)av_malloc(av_image_get_buffer_size(
		AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, outBuffer, AV_PIX_FMT_YUV420P, pCodecCtx->width,
		pCodecCtx->height, 1);
	pPacket = (AVPacket *)av_malloc(sizeof(AVPacket));
	//Output video info
	std::cout << "-----------File info---------------" << std::endl;
	av_dump_format(pFormatCtx, videoIndex, filePath, 0);

	std::cout << "-----------------------------------" << std::endl;
	SwsContext *imageConvertCtx = sws_getContext(pCodecCtx->width, pCodecCtx->height,
		pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	FILE *fpYUV;
#ifdef OUTPUT_YUV420P
	fpYUV = fopen("output.yuv", "wb+");
#endif

	//show the decoded frame
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		std::cout << "can not init SDL" << SDL_GetError() << std::endl;
		return -1;
	}
	int screenWidth = pCodecCtx->width;
	int screenHeight = pCodecCtx->height;
	//create a SDL window
	SDL_Window *screen = SDL_CreateWindow("palyer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screenWidth, screenHeight, SDL_WINDOW_OPENGL);
	if (screen == NULL) {
		std::cout << "can not create SDL window" << std::endl;
		return -1;
	}
	SDL_Renderer *sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	SDL_Texture *sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width,
		pCodecCtx->height);
	SDL_Rect sdlRect;
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screenWidth;
	sdlRect.h = screenHeight;
	//Show sdl window
	int ret = -1, ySize;
	int gotPicture;
	while (av_read_frame(pFormatCtx, pPacket) >= 0) {
		ret = avcodec_decode_video2(pCodecCtx, pFrame, &gotPicture, pPacket);
		if (ret < 0) {
			std::cout << "decode packet error" << std::endl;
			return -1;
		}
		if (gotPicture) {
			sws_scale(imageConvertCtx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
				pFrameYUV->data, pFrameYUV->linesize);
#ifdef OUTPUT_YUV420P
			ySize = pCodecCtx->width * pCodecCtx->height;
			fwrite(pFrameYUV->data[0], 1, ySize, fpYUV);
			fwrite(pFrameYUV->data[1], 1, ySize/4, fpYUV);
			fwrite(pFrameYUV->data[2], 1, ySize/4, fpYUV);
#endif
			SDL_UpdateYUVTexture(sdlTexture, &sdlRect,
				pFrameYUV->data[0], pFrameYUV->linesize[0],
				pFrameYUV->data[1], pFrameYUV->linesize[1],
				pFrameYUV->data[2], pFrameYUV->linesize[2]);
			SDL_RenderClear(sdlRenderer);
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
			SDL_RenderPresent(sdlRenderer);
			//SDL end
			SDL_Delay(1000/frameRate);
		}
		av_free_packet(pPacket);
	}
	//flush decoder
	//FIX: Flush Frames remained in Codec
	while (1) {
		ret = avcodec_decode_video2(pCodecCtx, pFrame, &gotPicture, pPacket);
		if (ret < 0)
			break;
		if (!gotPicture)
			break;
		sws_scale(imageConvertCtx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
			pFrameYUV->data, pFrameYUV->linesize);
#if OUTPUT_YUV420P
		int y_size = pCodecCtx->width*pCodecCtx->height;
		fwrite(pFrameYUV->data[0], 1, y_size, fpYUV);    //Y 
		fwrite(pFrameYUV->data[1], 1, y_size / 4, fpYUV);  //U
		fwrite(pFrameYUV->data[2], 1, y_size / 4, fpYUV);  //V
#endif
															//SDL---------------------------
		SDL_UpdateTexture(sdlTexture, &sdlRect, pFrameYUV->data[0], pFrameYUV->linesize[0]);
		SDL_RenderClear(sdlRenderer);
		SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
		SDL_RenderPresent(sdlRenderer);
		//SDL End-----------------------
		//Delay 40ms
		SDL_Delay(40);
	}

	sws_freeContext(imageConvertCtx);

#if OUTPUT_YUV420P 
	fclose(fpYUV);
#endif 

	SDL_Quit();





end:
	//close ffmpeg
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
	

	//getchar();
	return 0;
}
