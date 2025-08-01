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

// API2
#include "avcpp/format.h"
#include "avcpp/formatcontext.h"
#include "avcpp/codec.h"
#include "avcpp/codeccontext.h"

#include <SDL3/SDL.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h> 

#include "SDLPlayAudio.h"

class AudioInputInfo
{
public:
	AudioInputInfo() = default;
	AudioInputInfo(const AudioInputInfo&) = delete;
	AudioInputInfo& operator=(const AudioInputInfo&) = delete;
	std::error_code Open(const std::string& uri, const std::string& format = "")
	{
		std::error_code   ec;
		av::InputFormat ifmt;
		if (!format.empty())
		{
			ifmt.setFormat(format);
		}



		ictx_.openInput(uri, ifmt, ec);
		if (ec)
		{
			std::cerr << "Can't open input\n";
			return ec;
		}

		ictx_.findStreamInfo();

		for (size_t i = 0; i < ictx_.streamsCount(); ++i)
		{
			auto st = ictx_.stream(i);
			if (st.isAudio())
			{
				audioStream_ = i;
				ast_ = st;
				break;
			}
		}

		std::cerr << audioStream_ << std::endl;

		if (ast_.isNull())
		{
			std::cerr << "Audio stream not found\n";
			return ec;
		}

		if (ast_.isValid())
		{
			adec_ = av::AudioDecoderContext(ast_);

			av::Codec codec = av::findDecodingCodec(adec_.raw()->codec_id);

			adec_.setCodec(codec);
			adec_.setRefCountedFrames(true);

			adec_.open(av::Codec(), ec);
			if (ec)
			{
				std::cerr << "Can't open codec\n";
				return ec;
			}
		}

		std::clog << "NO_PTS: " << av::NoPts << std::endl;
		std::clog << "Duration: " << ictx_.duration() << " / " << ictx_.duration().seconds()
			<< ", Start Time: " << ictx_.startTime() << " / " << ictx_.startTime().seconds()
			<< std::endl;

		// Substract start time from the Packet PTS and DTS values: PTS starts from the zero
		ictx_.substractStartTime(true);

		return ec;
	}

	av::FormatContext& GetInputFormatContext() { return ictx_; }
	av::AudioDecoderContext& GetAudioDecoderContext() { return adec_; }
	av::Stream& GetAudioStream() { return ast_; }
	int64_t GetAudioStreamIndex() { return audioStream_; }
private:
	av::AudioDecoderContext adec_;
	av::FormatContext ictx_;
	av::Stream      ast_;
	int64_t      audioStream_ = -1;
};

int main(int argc, char** argv)
{
	if (argc < 2)
		return 1;

	av::init();
	av::setFFmpegLoggingLevel(AV_LOG_DEBUG);

	std::string uri{ argv[1] };

	std::string format;

	if (argc > 2)
	{
		format = argv[2];
	}

	std::error_code   ec;
	AudioInputInfo input_info;
	ec = input_info.Open(uri, format);
	if (ec)
	{
		std::cout << ec.value() << "," << ec.message() << "\n";
		return -1;
	}

	av::FormatContext& ictx = input_info.GetInputFormatContext();
	av::AudioDecoderContext& adec = input_info.GetAudioDecoderContext();
	int64_t      audioStream = input_info.GetAudioStreamIndex();
	av::Stream& ast = input_info.GetAudioStream();


	int count = 0;

	// 初始化 SDL
	SDLPlayAudio sdl_audio;
	SDLPlayAudio::Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO);
	sdl_audio.OpenDeviceStream(adec.channels(), adec.sampleRate());

	while (true)
	{
		av::Packet pkt = ictx.readPacket(ec);
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

		if (pkt.streamIndex() != audioStream)
		{
			continue;
		}

		std::clog << "Read packet: " << pkt.pts() << " / " << pkt.pts().seconds() << " / " << pkt.timeBase() << " / st: " << pkt.streamIndex() << std::endl;

		av::AudioSamples samples = adec.decode(pkt, ec);

		if (ec)
		{
			std::cerr << "Error: " << ec << std::endl;
			return 1;
		}
		else if (!samples)
		{
			//cerr << "Empty frame\n";
			//continue;
		}

		std::clog << "  Samples: " << samples.samplesCount() << ", ch: " << samples.channelsCount() << ", freq: " << samples.sampleRate() << ", name: " << samples.channelsLayoutString() << ", ref=" << samples.isReferenced() << ":" << samples.refCount() << ", ts: " << samples.pts() << ", tm: " << samples.pts().seconds() << std::endl;

		uint8_t* data = samples.data();
		int size = samples.size();

		sdl_audio.PutStreamData((char*)data, size, 2 * size);

		//return 0;
		count++;
		//if (count > 100)
		//	break;
	}

	// NOTE: stream decodec must be closed/destroyed before
	//ictx.close();
	//vdec.close();

	while (sdl_audio.GetStreamQueued() != 0)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	sdl_audio.ClearStream();
	SDLPlayAudio::Quit();

}