#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <set>
#include <map>
#include <memory>
#include <functional>

#include "avcpp/av.h"
#include "avcpp/ffmpeg.h"
#include "avcpp/codec.h"
#include "avcpp/packet.h"
#include "avcpp/videorescaler.h"
#include "avcpp/audioresampler.h"
#include "avcpp/avutils.h"
#include "avcpp/format.h"
#include "avcpp/formatcontext.h"
#include "avcpp/codeccontext.h"


class PushCameraBase
{
public:
	PushCameraBase();
	bool InitInput(const std::string& in_uri, const std::string& in_format_name, av::Dictionary in_format_options = {});
	virtual bool InitOutput(const std::string& out_uri,const std::string& out_format_name, av::Dictionary out_format_options = {});
	virtual bool HandlePacket()=0;
protected:
	//
	// INPUT
	//
	av::FormatContext input_ctx_;
	int64_t      input_video_stream_index_ = -1;
	av::VideoDecoderContext input_vdec_;
	av::Stream      input_vst_;

	int64_t      input_audio_stream_index_ = -1;
	av::AudioDecoderContext input_adec_;
	av::Stream      input_ast_;

	//
    // OUTPUT
    //
	av::FormatContext output_ctx_;
	av::VideoEncoderContext output_vec_;
	av::Stream output_vst_;
	av::AudioEncoderContext output_aec_;
	av::Stream output_ast_;

	//
    // RESCALER
    //
	av::VideoRescaler video_rescaler_;
	av::AudioResampler audio_resampler_;
public:
	static void Init(int log_leve = AV_LOG_DEBUG);
	static std::vector<std::string>  GetAllDeviceName();
	static std::string GetCameraAndmicrophoneInputString(const std::string& vedio_dev_name, const std::string& audio_dev_name);
	static bool IsSupportedPixelFormat(const av::Codec& ocodec, AVPixelFormat used_pixel_format);
	static bool IsSupportedFramerate(const av::Codec& ocodec, av::Rational framerate);
	static bool IsSupportedSampleFormat(const av::Codec& ocodec, av::SampleFormat sample_format);
	static bool IsSupportedSamplerate(const av::Codec& ocodec, int sample_rates);
	static void SetH264EncoderOption(av::Dictionary& x264_opts, av::VideoEncoderContext& vec);
};

