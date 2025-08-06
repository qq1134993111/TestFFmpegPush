#include "PushCameraRtmp.h"

#include <iostream>
#include <sstream>
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


//rtmp out_uri 为 rtmp://127.0.0.1:1935/live/camera 这样的  out_fmt_name为flv
bool PushCameraRtmp::InitOutput(const std::string& out_uri, const std::string& out_format_name, av::Dictionary out_format_options)
{

	av::OutputFormat  ofrmt;
	ofrmt.setFormat(out_format_name, out_uri);

	output_ctx_.setFormat(ofrmt);

	av::Codec               ocodec = av::findEncodingCodec(/*ofrmt*/AV_CODEC_ID_H264);
	if (ocodec.isNull())
	{
		std::cerr << "findEncodingCodec failed:" << ofrmt.name() << "," << ofrmt.longName() << "\n";
		return false;
	}

	std::error_code ec;

	{
		av::VideoEncoderContext video_encoder_context{ ocodec };
		output_vec_ = std::move(video_encoder_context);
	}

	AVPixelFormat used_pixel_format = AV_PIX_FMT_YUV420P;
	IsSupportedPixelFormat(ocodec, used_pixel_format);

	// Settings
	output_vec_.setWidth(input_vdec_.width());
	output_vec_.setHeight(input_vdec_.height());
	output_vec_.setPixelFormat(used_pixel_format);
	output_vec_.setTimeBase(av::Rational{ 1,fps_ });
	output_vec_.raw()->framerate = av::Rational{ fps_,1 };
	output_vec_.setBitRate(4000000);
	output_vec_.addFlags(output_ctx_.outputFormat().isFlags(AVFMT_GLOBALHEADER) ? AV_CODEC_FLAG_GLOBAL_HEADER : 0);

	av::Dictionary x264_opts;
	SetH264EncoderOption(x264_opts, output_vec_);
	output_vec_.open(x264_opts, ec);
	if (ec)
	{
		std::cerr << "Can't opent video encoder :" << ec.value() << "," << ec.message() << "\n";
		return false;
	}

	IsSupportedFramerate(ocodec, input_vst_.frameRate());

	output_vst_ = output_ctx_.addStream(output_vec_);
	output_vst_.setFrameRate(av::Rational{ fps_,1 });
	output_vst_.setAverageFrameRate(av::Rational{ fps_,1 }); // try to comment this out and look at the output of ffprobe or mpv
	// it'll show 1k fps regardless of the real fps;
	// see https://github.com/FFmpeg/FFmpeg/blob/7d4fe0c5cb9501efc4a434053cec85a70cae156e/libavformat/matroskaenc.c#L2659
	// also used in the CLI ffmpeg utility: https://github.com/FFmpeg/FFmpeg/blob/7d4fe0c5cb9501efc4a434053cec85a70cae156e/fftools/ffmpeg.c#L3058
	// and https://github.com/FFmpeg/FFmpeg/blob/7d4fe0c5cb9501efc4a434053cec85a70cae156e/fftools/ffmpeg.c#L3364
	output_vst_.setTimeBase(av::Rational{ 1,fps_ });
	output_vst_.raw()->codecpar->codec_tag = 0;


	output_ast_ = output_ctx_.addStream();
	output_ast_.setCodecParameters(input_ast_.codecParameters());
	output_ast_.setTimeBase(input_ast_.timeBase());
	output_ast_.raw()->codecpar->codec_tag = 0;

	output_ctx_.openOutput(out_uri, ec);
	if (ec)
	{
		std::cerr << "Can't open output:" << ec.value() << "," << ec.message() << "\n";
		return false;
	}
	output_ctx_.dump();


	av::VideoRescaler video_rescaler(output_vec_.width(), output_vec_.height(), output_vec_.pixelFormat());
	video_rescaler_ = std::move(video_rescaler);

	return true;
}



bool PushCameraRtmp::HandlePacket()
{
	is_running_.store(true, std::memory_order_relaxed);

	//
	// PROCESS
	//

	std::error_code ec;
	output_ctx_.writeHeader();
	output_ctx_.flush();

	while (is_running_.load(std::memory_order_relaxed))
	{
		// READING
		av::Packet pkt = input_ctx_.readPacket(ec);
		if (ec)
		{
			std::clog << "Packet reading error: " << ec << ", " << ec.message() << std::endl;
			break;
		}

		// EOF
		if (!pkt)
		{
			break;
		}

		if (pkt.streamIndex() == input_video_stream_index_)
		{

			PrintPacketInfo(pkt, "Read video packet");

			// DECODING
			auto inpFrame = input_vdec_.decode(pkt, ec);

			if (ec)
			{
				std::cerr << "Decoding error: " << ec.value() << "," << ec.message() << std::endl;
				return false;
			}
			else if (!inpFrame)
			{
				std::cerr << "Empty frame\n";
				continue;
			}

			PrintVideoFrameInfo(inpFrame,"inpFrame");

			// Change timebase
			inpFrame.setTimeBase(output_vec_.timeBase());
			inpFrame.setStreamIndex(output_vst_.index());
			inpFrame.setPictureType();

			PrintVideoFrameInfo(inpFrame, "inpFrame set timeBase");

			if (inpFrame.pixelFormat() == AV_PIX_FMT_YUVJ422P)
			{
				inpFrame.raw()->format = AV_PIX_FMT_YUV422P;
				inpFrame.raw()->color_range = AVCOL_RANGE_JPEG;
			}

			// SCALE
			auto outFrame = video_rescaler_.rescale(inpFrame, ec);
			if (ec)
			{
				std::cerr << "Can't rescale frame: " << ec.value() << ", " << ec.message() << std::endl;
				return false;
			}

			PrintVideoFrameInfo(inpFrame, "outFrame");

			// ENCODE
			av::Packet opkt = output_vec_.encode(outFrame, ec);
			if (ec)
			{
				std::cerr << "Encoding error: " << ec.value() << "," << ec.message() << std::endl;
				return false;
			}
			else if (!opkt)
			{
				std::cerr << "Empty packet\n";
				continue;
			}

			// output stream
			opkt.setStreamIndex(output_vst_.index());

			PrintPacketInfo(opkt, "Write video packet");

			output_ctx_.writePacket(opkt, ec);
			if (ec)
			{
				std::cerr << "Error write packet: " << ec.value() << ", " << ec.message() << std::endl;
				return false;
			}
			}
		else if (pkt.streamIndex() == input_audio_stream_index_)
		{
			PrintPacketInfo(pkt, "Read audio packet");

			pkt.setStreamIndex(output_ast_.index());
			output_ctx_.writePacket(pkt, ec);
			if (ec)
			{
				std::cerr << "Error write packet: " << ec.value() << ", " << ec.message() << std::endl;
				return false;
			}

		}
		}

	output_ctx_.writeTrailer();
	input_ctx_.close();

	is_running_.store(false, std::memory_order_relaxed);

	return true;
	}
