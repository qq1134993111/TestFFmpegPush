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


//rtmp out_uri Ϊ rtmp://127.0.0.1:1935/live/camera ������  out_fmt_nameΪflv
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
	output_vec_.setTimeBase(av::Rational{1,fps_});
	output_vec_.raw()->framerate = av::Rational{fps_,1};
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


	output_ast_ = output_ctx_.addStream();
	output_ast_.setCodecParameters(input_ast_.codecParameters());
	output_ast_.setTimeBase(input_ast_.timeBase());

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

/*
���ȣ����Ǳ������ zerolatency ���������������ʲô��Ϊ��ʵ����͵��ӳ٣��������һϵ�� x264 ���������������ؼ��������Ķ��ǣ�

������B֡ (B-Frames)��B֡��˫��Ԥ��֡������Ҫ�ο�ǰ��ͺ����֡���ܽ��롣�����ζ�ű��������뻺��һЩ֡���ȴ�����֡�ĵ�������������ӳ١�zerolatency Ϊ�����������ӳ٣���ǿ������ bframes=0��

������֡������ (Frame Reordering)������Ϊû����B֡����Ƶ���е�֡�Ͳ�����Ҫ������֡��**����˳��DTS, Decoding Time Stamp������ʾ˳��PTS, Presentation Time Stamp��**�����ȫһ�¡�

���۾��ǣ��� zerolatency ģʽ�£�����ÿһ֡���ݣ����� DTS ������� PTS��
*/
//#define  SET_ZEROLATENCY

bool PushCameraRtmp::HandlePacket()
{
#ifdef SET_ZEROLATENCY
	int64_t next_pts = 0;  //������"tune"Ϊ "zerolatency"  �����ֶ�Ϊ���������������ÿһ֡ AVFrame ����һ���ϸ����ԡ�����������PTS��
	AVRational framerate = output_vec_.raw()->framerate;
	AVRational time_base = output_vec_.timeBase().getValue();
	// �������Ƿ���Ч
	if (framerate.num <= 0 || framerate.den <= 0)
	{
		// ����֡��δ���û���Ч���޷����� frame_duration
		return false;
	}
	//av_inv_q �������ڼ���һ�� AVRational �ṹ��ĵ���
	//framerate {30��1}��ʾÿ��30֡   av_inv_q(framerate) ��ʾÿ֡��Ҫ������ {1��30}
	//int64_t av_rescale_q(int64_t a, AVRational bq, AVRational cq) �������ڽ�һ����ֵ��ͨ����ʱ��������ʱ�䣩��һ��ʱ���׼����λ��ת������һ��ʱ���׼��
	//�൱��a*bq/cq
	int64_t frame_duration = av_rescale_q(1, av_inv_q(framerate), time_base);

#endif // SET_ZEROLATENCY
 

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

			//std::clog << "Read packet: pts=" << pkt.pts() << ", dts=" << pkt.dts() << " / " << pkt.pts().seconds() << " / " << pkt.timeBase() << " / st: " << pkt.streamIndex() << std::endl;

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

			//std::clog << "inpFrame: pts=" << inpFrame.pts() << " / " << inpFrame.pts().seconds() << " / " << inpFrame.timeBase() << ", " << inpFrame.width() << "x" << inpFrame.height() << ", size=" << inpFrame.size() << ", ref=" << inpFrame.isReferenced() << ":" << inpFrame.refCount() << " / type: " << inpFrame.pictureType() << std::endl;

			// Change timebase
			inpFrame.setTimeBase(output_vec_.timeBase());
			inpFrame.setStreamIndex(output_vst_.index());
			inpFrame.setPictureType();

			//std::clog << "inpFrame: pts=" << inpFrame.pts() << " / " << inpFrame.pts().seconds() << " / " << inpFrame.timeBase() << ", " << inpFrame.width() << "x" << inpFrame.height() << ", size=" << inpFrame.size() << ", ref=" << inpFrame.isReferenced() << ":" << inpFrame.refCount() << " / type: " << inpFrame.pictureType() << std::endl;

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

			//std::clog << "outFrame: pts=" << outFrame.pts() << " / " << outFrame.pts().seconds() << " / " << outFrame.timeBase() << ", " << outFrame.width() << "x" << outFrame.height() << ", size=" << outFrame.size() << ", ref=" << outFrame.isReferenced() << ":" << outFrame.refCount() << " / type: " << outFrame.pictureType() << std::endl;

#ifdef SET_ZEROLATENCY
			//outFrame.setTimeBase(output_vst_.timeBase());
			outFrame.setPictureType();
			outFrame.setStreamIndex(output_vst_.index());
			outFrame.raw()->format = AV_PIX_FMT_YUV420P;
			outFrame.raw()->pts = next_pts;
			outFrame.raw()->pkt_dts = next_pts;
#endif
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

			//std::clog << "Write packet: pts=" << opkt.pts() << ", dts=" << opkt.dts() << " / " << opkt.pts().seconds() << " / " << opkt.timeBase() << " / st: " << opkt.streamIndex() << std::endl;

			output_ctx_.writePacket(opkt, ec);
			if (ec)
			{
				std::cerr << "Error write packet: " << ec.value() << ", " << ec.message() << std::endl;
				return false;
			}
#ifdef SET_ZEROLATENCY
			next_pts += frame_duration;
#endif
		}
		else if (pkt.streamIndex() == input_audio_stream_index_)
		{
			//std::clog << "Read audio packet: pts=" << pkt.pts() << ", dts=" << pkt.dts() << " / " << pkt.pts().seconds() << " / " << pkt.timeBase() << " / st: " << pkt.streamIndex() << std::endl;

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
