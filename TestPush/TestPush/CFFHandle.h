#pragma once

#include <iostream>
#include <string>
#include <functional>

//引入ffmpeg库的头文件和链接库
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")

}

class CFFInput
{
public:
	CFFInput();
	~CFFInput();
	bool InitInput(const std::string& in_uri, const std::string& in_format_name);
protected:
	AVFormatContext* input_fmt_ctx_ = nullptr;

	int64_t      input_video_stream_index_ = -1;
	AVCodecContext* input_vdec_ = nullptr;
	AVStream* input_vst_ = nullptr;

	int64_t      input_audio_stream_index_ = -1;
	AVCodecContext* input_adec_;
	AVStream* input_ast_ = nullptr;
};

class CFFOutput :public CFFInput
{
public:
	CFFOutput();
	~CFFOutput();
	bool InitOutput(const std::string& out_uri, const std::string& out_format_name);

protected:
	AVFormatContext* output_fmt_ctx_ = nullptr;
	AVCodecContext* output_vec_ = nullptr;
	AVStream* output_vst_ = nullptr;
	AVCodecContext* output_aec_ = nullptr;
	AVStream* output_ast_ = nullptr;

	int fps_ = 30;

protected:

};

class CFFHandle :public CFFOutput
{
public:
	CFFHandle() = default;
	~CFFHandle();
	bool HandlePakege();
	bool is_running_ = false;
	SwsContext* video_sws_context_ = nullptr;
private:
	using DecodePacketHandler = std::function<int(AVFrame* frame)>;
	int DecodePacket(AVCodecContext* dec, const AVPacket* pkt, AVFrame* frame, DecodePacketHandler handler);

	using EncodeFrameHandler = std::function<int(AVPacket* pkt)>;
	int EncodeFrame(AVCodecContext* enc, const AVFrame* frame, AVPacket* pkt, EncodeFrameHandler handler);
};