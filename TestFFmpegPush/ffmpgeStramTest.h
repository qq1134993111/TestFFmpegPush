#pragma once


#ifndef SIMPLEST_FFMPEG_STREAM_H_
#define SIMPLEST_FFMPEG_STREAM_H_


//引入ffmpeg库的头文件和链接库
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "libavutil/error.h"
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")

}
#include <assert.h>

//#define RTSP_PUSH_STREAM
//#define RTMP_PUSH_STREAM

int ffmpgeStramTest();

#endif // !SIMPLEST_FFMPEG_STREAM_H_