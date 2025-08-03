#include "PushCameraRtsp.h"

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

#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavformat/avformat.h>


bool PushCameraRtsp::InitOutput(const std::string& out_uri, const std::string& out_format_name, av::Dictionary out_format_options /*= {}*/)
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
	//output_vec_.raw()->framerate = av::Rational{ fps_,1 };
	output_vec_.setBitRate(4000000);
	output_vec_.addFlags(output_ctx_.outputFormat().isFlags(AVFMT_GLOBALHEADER) ? AV_CODEC_FLAG_GLOBAL_HEADER : 0);

	av::Dictionary x264_opts;
	SetH264EncoderOption(x264_opts, output_vec_);
	//����rtsp����
	x264_opts.set("rtsp_transport", "tcp");  //ǿ��ʹ�� TCP ����Ĭ�� UDP
	//x264_opts.set("max_delay", "200000");   //�����������ӳ٣�200 ms��
	//x264_opts.set("fflags", "nobuffer");    //���û��壬�����ӳ�
	//x264_opts.set("timeout", "5000000");    //���������ȴ����ޣ�5 s��
	//x264_opts.set("stimeout", "3000000");   //������ʱ��3 s��
	//x264_opts.set("analyzeduration", "1000000"); //���������ʱ����1 s��
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




	av::Codec acodec = av::findEncodingCodec("aac"); // ��ȷָ��ʹ�� AAC ������
	if (acodec.isNull())
	{
		std::cerr << "Can't find aac encoder\n";
		return false;
	}

	auto sampleFormats = acodec.supportedSampleFormats(); // ������Ƶ
	auto sampleRates = acodec.supportedSamplerates();     // ������Ƶ
	auto channelLayouts = acodec.supportedChannelLayouts(); // ������Ƶ

	{
		av::AudioEncoderContext audio_encoder_context{ acodec };
		output_aec_ = std::move(audio_encoder_context);
	}

	output_aec_.setSampleRate(input_adec_.sampleRate());
	output_aec_.setSampleFormat(AV_SAMPLE_FMT_FLTP); // ע�⣺AAC���������ܲ�֧������PCM��ʽ��������Ҫ�ز���
	output_aec_.setChannelLayout(AV_CH_LAYOUT_STEREO);
	output_aec_.setBitRate(128000); // ����һ�����ʵı����ʣ��� 128kbps
	output_aec_.setTimeBase(av::Rational(1, input_adec_.sampleRate())); // ʹ�ò�������Ϊʱ���

	output_aec_.raw()->thread_count = 1;//����Ϊ0����FFmpeg�Զ���������߳��� (�Ƽ�)
	output_aec_.open(ec);
	if (ec)
	{
		std::cerr << "Can't open audio encoder: " << ec.value() << "," << ec.message() << "\n";
		return 1;
	}
	output_ast_ = output_ctx_.addStream(output_aec_);
	//output_ast_.setTimeBase(output_aec_.timeBase());


	output_ctx_.openOutput(out_uri, ec);
	if (ec)
	{
		std::cerr << "Can't open output:" << ec.value() << "," << ec.message() << "\n";
		return false;
	}
	output_ctx_.dump();


	{
		av::VideoRescaler video_rescaler(output_vec_.width(), output_vec_.height(), output_vec_.pixelFormat());
		video_rescaler_ = std::move(video_rescaler);
	}

	{

		if (input_adec_.channelLayout() == 0)
		{
			AVChannelLayout ch_layout = {  };
			av_channel_layout_default(&ch_layout, input_adec_.channels());
			input_adec_.setChannelLayout(ch_layout);
		}

		if (!audio_resampler_.init(output_aec_.channelLayout(), output_aec_.sampleRate(), output_aec_.sampleFormat(),
			input_adec_.channelLayout(), input_adec_.sampleRate(), input_adec_.sampleFormat(), ec))
		{
			std::cerr << "audio_resampler_ init failed:" << ec.value() << "," << ec.message() << "\n";
			return false;
		}
	}

	return true;

}

bool PushCameraRtsp::HandlePacket()
{
	is_running_ = true;
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

			std::clog << "Read packet: pts=" << pkt.pts() << ", dts=" << pkt.dts() << " / " << pkt.pts().seconds() << " / " << pkt.timeBase() << " / st: " << pkt.streamIndex() << std::endl;

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

			std::clog << "inpFrame: pts=" << inpFrame.pts()
				<< " seconds: " << inpFrame.pts().seconds() << " timeBase: " << inpFrame.timeBase() <<
				", " << inpFrame.width() << "x" << inpFrame.height() << ", size=" << inpFrame.size()
				<< ", ref=" << inpFrame.isReferenced() << ":" << inpFrame.refCount() << "  type: " << inpFrame.pictureType() << std::endl;

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

			std::clog << "outFrame: pts=" << outFrame.pts()
				<< " seconds: " << outFrame.pts().seconds() << " timeBase: " << outFrame.timeBase()
				<< ", " << outFrame.width() << "x" << outFrame.height() << ", size=" << outFrame.size()
				<< ", ref=" << outFrame.isReferenced() << ":" << outFrame.refCount() << " type: " << outFrame.pictureType() << std::endl;

			//outFrame.raw()->pkt_dts = outFrame.raw()->pts;

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
		}
		else if (pkt.streamIndex() == input_audio_stream_index_)
		{
			//std::clog << "Read audio packet: pts=" << pkt.pts() << ", dts=" << pkt.dts() << " / " << pkt.pts().seconds() << " / " << pkt.timeBase() << " / st: " << pkt.streamIndex() << std::endl;


			// DECODING
			av::AudioSamples in_sample = input_adec_.decode(pkt, ec);

			if (ec)
			{
				std::cerr << "Decoding error: " << ec.value() << "," << ec.message() << std::endl;
				return false;
			}
			else if (!in_sample)
			{
				std::cerr << "Empty frame\n";
				//continue;
			}

			std::clog << "  Samples [in]: " << in_sample.samplesCount()
				<< ", ch: " << in_sample.channelsCount()
				<< ", freq: " << in_sample.sampleRate()
				<< ", name: " << in_sample.channelsLayoutString()
				<< ", pts: " << in_sample.pts().seconds()
				<< ", ref=" << in_sample.isReferenced() << ":" << in_sample.refCount()
				<< std::endl;


			// Empty samples set should not be pushed to the resampler, but it is valid case for the
			// end of reading: during samples empty, some cached data can be stored at the resampler
			// internal buffer, so we should consume it.
			if (in_sample)
			{
				audio_resampler_.push(in_sample, ec);
				if (ec)
				{
					std::clog << "Resampler push error: " << ec << ", text: " << ec.message() << std::endl;
					continue;
				}
			}

			// Pop resampler data
			bool getAll = !in_sample;
			while (true)
			{
				av::AudioSamples ouSamples(output_aec_.sampleFormat(), output_aec_.frameSize(), output_aec_.channelLayout(), output_aec_.sampleRate());

				// Resample:
				bool hasFrame = audio_resampler_.pop(ouSamples, getAll, ec);
				if (ec)
				{
					std::clog << "Resampling status: " << ec << ", text: " << ec.message() << std::endl;
					break;
				}
				else if (!hasFrame)
				{
					break;
				}
				else
				{
					std::clog << "  Samples [ou]: " << ouSamples.samplesCount()
						<< ", ch: " << ouSamples.channelsCount()
						<< ", freq: " << ouSamples.sampleRate()
						<< ", name: " << ouSamples.channelsLayoutString()
						<< ", pts: " << ouSamples.pts().seconds()
						<< ", ref=" << ouSamples.isReferenced() << ":" << ouSamples.refCount()
						<< std::endl;
				}

				// ENCODE
				ouSamples.setStreamIndex(output_ast_.index());
				ouSamples.setTimeBase(output_aec_.timeBase());

				av::Packet opkt = output_aec_.encode(ouSamples, ec);
				if (ec)
				{
					std::cerr << "Encoding error: " << ec << ", " << ec.message() << std::endl;
					return false;
				}
				else if (!opkt)
				{
					//cerr << "Empty packet\n";
					continue;
				}

				opkt.setStreamIndex(output_ast_.index());

				std::clog << "Write packet: pts=" << opkt.pts() << ", dts=" << opkt.dts() << " / " << opkt.pts().seconds() << " / " << opkt.timeBase() << " / st: " << opkt.streamIndex() << std::endl;

				output_ctx_.writePacket(opkt, ec);
				if (ec)
				{
					std::cerr << "Error write packet: " << ec << ", " << ec.message() << std::endl;
					return false;
				}
			}

			// For the first packets samples can be empty: decoder caching
			//if (!pkt && !in_sample)
			//	break;

		}
	}

	output_ctx_.writeTrailer();
	input_ctx_.close();

	return true;
}
