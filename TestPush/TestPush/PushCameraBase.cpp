#include "PushCameraBase.h"

PushCameraBase::PushCameraBase()
{
	static std::once_flag s_flag;
	std::call_once(s_flag, []
		{
			Init();
		});
}

bool PushCameraBase::InitInput(const std::string& in_uri, const std::string& in_format_name, av::Dictionary in_format_options)
{

	//// 设置实时缓冲区大小
	//in_format_options.set("rtbufsize", "304128000");
	in_format_options.set("rtbufsize", "256M");
	////设置从设备抓取的帧率为 30 FPS
	//in_format_options.set("framerate", "30");
	// 设置 analyzeduration 为 10,000,000 微秒 (10秒)
	// FFmpeg的选项解析器很智能，通常也支持 "10M" 这样的写法
	in_format_options.set("analyzeduration", "10000000");
	// 设置 probesize 为 10,485,760 字节 (10MB)
	// 同样，"10M" 的写法也经常被支持
	in_format_options.set("probesize", "10485760");
	in_format_options.set("video_size", "640x480");

	std::error_code ec;
	input_ctx_.openInput(in_uri, in_format_options, av::InputFormat(in_format_name), ec);
	if (ec)
	{
		std::cerr << " Can't open input . " << ec.value() << "," << ec.message() << "\n";
		return false;
	}

	input_ctx_.findStreamInfo(ec);
	if (ec)
	{
		std::cerr << "Can't find streams . " << ec.value() << ", " << ec.message() << std::endl;
		return false;
	}

	for (size_t i = 0; i < input_ctx_.streamsCount(); ++i)
	{
		auto st = input_ctx_.stream(i);
		if (st.mediaType() == AVMEDIA_TYPE_VIDEO)
		{
			input_video_stream_index_ = i;
			input_vst_ = st;
		}
		else if (st.mediaType() == AVMEDIA_TYPE_AUDIO)
		{
			input_audio_stream_index_ = i;
			input_ast_ = st;
		}
		if (input_video_stream_index_ != -1 && input_audio_stream_index_ != -1)
			break;
	}

	if (input_vst_.isNull())
	{
		std::cerr << "Video stream not found\n";
		//return false;
	}

	if (input_vst_.isValid())
	{
		input_vdec_ = av::VideoDecoderContext(input_vst_);
		input_vdec_.setRefCountedFrames(true);

		std::cerr << "PTR: " << (void*)input_vdec_.raw()->codec << std::endl;
		input_vdec_.raw()->thread_count = 0;//设置为0，让FFmpeg自动决定最佳线程数 (推荐)
		input_vdec_.open(av::Codec(), ec);
		if (ec)
		{
			std::cerr << "Can't open video decoder ." << ec.value() << ", " << ec.message() << std::endl;
			return false;
		}
	}

	if (input_ast_.isNull())
	{
		std::cerr << "audio stream not found\n";
		//return false;
	}

	if (input_ast_.isValid())
	{
		input_adec_ = av::AudioDecoderContext(input_ast_);
		input_adec_.setRefCountedFrames(true);

		std::cerr << "PTR: " << (void*)input_adec_.raw()->codec << std::endl;

		input_adec_.raw()->thread_count = 0;//设置为0，让FFmpeg自动决定最佳线程数 (推荐)
		input_adec_.open(av::Codec(), ec);
		if (ec)
		{
			std::cerr << "Can't open audio decoder . " << ec.value() << ", " << ec.message() << std::endl;
			return false;
		}
	}

	input_ctx_.dump();
	return true;
}

bool PushCameraBase::InitOutput(const std::string& out_uri, const std::string& out_format_name, av::Dictionary out_format_options /*= {}*/)
{
	return true;
}


void PushCameraBase::Init(int log_leve /*= AV_LOG_DEBUG*/)
{
	av::init();
	av::setFFmpegLoggingLevel(log_leve);
}

std::vector<std::string> PushCameraBase::GetAllDeviceName()
{
	avdevice_register_all();

	AVDeviceInfoList* device_list = nullptr;
	avdevice_list_input_sources(av_find_input_format("dshow"), nullptr, nullptr, &device_list);

	std::vector<std::string> v_name;
	for (int i = 0; i < device_list->nb_devices; i++)
	{
		printf("[%d] %s — %s\n", i, device_list->devices[i]->device_name, device_list->devices[i]->device_description);

		v_name.push_back(device_list->devices[i]->device_description);

	}
	avdevice_free_list_devices(&device_list);

	return v_name;
}

std::string PushCameraBase::GetCameraAndmicrophoneInputString(const std::string& vedio_dev_name, const std::string& audio_dev_name)
{
	std::string uri;
	if (!vedio_dev_name.empty() && !audio_dev_name.empty())
	{
		uri = "video=" + vedio_dev_name + ":" + "audio=" + audio_dev_name;
	}
	else if (!vedio_dev_name.empty())
	{

		uri = "video=" + vedio_dev_name;
	}
	else if (!audio_dev_name.empty())
	{
		uri = "audio=" + audio_dev_name;
	}

	return uri;
}

bool PushCameraBase::IsSupportedPixelFormat(const av::Codec& codec, AVPixelFormat used_pixel_format)
{
	bool support = false;
	auto v_value = codec.supportedPixelFormats();
	std::stringstream ss;
	ss << "supportedPixelFormats:";
	for (auto value : v_value)
	{
		ss << value << " , ";
		if (value == used_pixel_format)
		{
			support = true;
		}
	}
	std::cout << ss.str() << (support ? " support " : " not support ") << used_pixel_format << "\n";

	return support;
}

bool PushCameraBase::IsSupportedFramerate(const av::Codec& ocodec, av::Rational framerate)
{
	bool support = false;
	auto v_value = ocodec.supportedFramerates();
	std::stringstream ss;
	ss << "supportedFramerates:";
	for (auto value : v_value)
	{
		ss << value << " , ";
		if (value == framerate)
		{
			support = true;
		}
	}
	std::cout << ss.str() << (support ? " support " : " not support ") << framerate << "\n";

	return support;
}

bool PushCameraBase::IsSupportedSampleFormat(const av::Codec& ocodec, av::SampleFormat sample_format)
{
	bool support = false;
	auto v_value = ocodec.supportedSampleFormats();
	std::stringstream ss;
	ss << "supportedSampleFormats:";
	for (auto value : v_value)
	{
		ss << value << " , ";
		if (value == sample_format)
		{
			support = true;
		}
	}
	std::cout << ss.str() << (support ? " support " : " not support ") << sample_format << "\n";

	return support;
}

bool PushCameraBase::IsSupportedSamplerate(const av::Codec& ocodec, int sample_rates)
{
	bool support = false;
	auto v_value = ocodec.supportedSamplerates();
	std::stringstream ss;
	ss << "supportedSamplerates:";
	for (auto value : v_value)
	{
		ss << value << " , ";
		if (value == sample_rates)
		{
			support = true;
		}
	}
	std::cout << ss.str() << (support ? " support " : " not support ") << sample_rates << "\n";

	return support;
}


/*
使用 FFmpeg API (libavcodec) 进行 H.264 编码时，优化主要围绕着平衡 编码速度、输出质量 和 文件大小（码率） 这三个核心要素。
这些优化通常通过设置 AVCodecContext 的参数以及 H.264 编码器（主要是 libx264）的私有参数来实现。
以下是可以进行优化的关键参数，分为几个主要类别：
1. 核心优化：预设与调优 (Preset & Tune)
这是最重要、最先应该考虑的优化选项。它们是官方精心调校的一系列参数的集合，能让你在不了解每个精细参数的情况下，快速达到一个很好的平衡点。
preset (预设)
作用: 控制编码速度与压缩率的平衡。越快的预设，编码速度越快，但压缩率越低（同等质量下文件更大）。
API 设置: av_opt_set(codec_context->priv_data, "preset", "medium", 0);
可选值:
ultrafast
superfast
veryfast
faster
fast
medium (默认值，速度和质量的良好平衡)
slow
slower
veryslow
placebo (极慢，不建议在生产环境使用)
优化建议:
实时流媒体/低延迟场景: 使用 veryfast, faster。
普通视频转码/Web视频: 使用 medium, fast。
追求高质量压缩/视频归档: 使用 slow, slower。
tune (调优)
作用: 根据视频内容的特点对算法进行微调，以优化特定类型的视觉效果。
API 设置: av_opt_set(codec_context->priv_data, "tune", "film", 0);
可选值:
film: 电影、真人内容。
animation: 动画片。
grain: 用于保留胶片颗粒感的场景。
stillimage: 静态图像内容（例如幻灯片）。
fastdecode: 优化以便于更快地解码。
zerolatency: 零延迟，用于实时通信，会禁用一些需要“未来”帧的功能（如 B 帧）。
优化建议: 务必根据你的视频内容选择合适的 tune，这对主观质量有很大提升。zerolatency 对实时性要求高的场景至关重要。
2. 码率控制 (Rate Control)
这是控制输出文件大小和质量的核心。
crf (Constant Rate Factor, 恒定速率因子)
作用: 推荐的质量控制模式。它追求视觉上的恒定质量。CRF 值越低，质量越高，文件越大。
API 设置: av_opt_set(codec_context->priv_data, "crf", "23", 0);
取值范围: 0-51。0是无损，23是默认值，18左右通常被认为是视觉上接近无损的，28以上质量会明显下降。
优化建议: 这是最灵活的模式。不需要严格控制文件大小时，优先使用 CRF。通过调整 CRF 值来寻找质量和大小的最佳平衡点。
b:v (Bitrate, 码率)
作用: 当你需要严格控制文件大小时（例如流媒体带宽限制），使用此模式。编码器会尽量让平均码率接近你设定的值。
API 设置: codec_context->bit_rate = 2000000; // 2 Mbps
优化建议: 通常与 maxrate 和 bufsize 一起使用，以控制码率波动。这被称为 ABR (Average Bitrate) 模式。在 CRF 模式下此参数无效。
maxrate 和 bufsize
作用: 与 b:v 配合使用，用于限制码率的瞬时峰值。bufsize 定义了客户端的缓冲区大小，maxrate 定义了允许的最大码率。
API 设置:
av_opt_set(codec_context->priv_data, "maxrate", "4000k", 0);
av_opt_set(codec_context->priv_data, "bufsize", "8000k", 0);
优化建议: 在流媒体场景下非常重要，可以防止因码率瞬时过高导致的卡顿。通常 bufsize 设置为 maxrate 的1-2倍。
3. Profile & Level
主要用于确保解码端的兼容性。
profile
作用: 限制编码时可以使用的功能子集。不同的设备（如老旧手机、电视盒子）支持不同的 Profile。
API 设置: av_opt_set(codec_context->priv_data, "profile", "high", 0);
可选值: baseline, main, high, high10, high422, high444 等。
优化建议:
高兼容性 (老旧设备): baseline。
标准清晰度/Web: main。
高清/现代设备: high (通用选择)。
level
作用: 限制码率、分辨率、帧率等参数的最大值，以匹配解码器的处理能力。
API 设置: codec_context->level = 41; // Level 4.1
优化建议: 通常无需手动设置，编码器会根据你的分辨率、帧率等自动选择。如果需要强制兼容特定设备，可以查阅该设备的规格手册来设置。例如，Level 4.1 支持 1080p@30fps。
4. 图像组 (GOP) 相关参数
g (GOP Size / keyint)
作用: 设置两个关键帧（I帧）之间的最大帧数。
API 设置: codec_context->gop_size = 250;
优化建议:
GOP 越大，压缩率越高（因为P/B帧更多），但随机拖动（seek）的响应速度越慢。
视频点播（VOD）一般设置为帧率的2-10倍（如 25fps -> GOP 250）。
直播流媒体为了加快进入速度和切片，通常设置为较小的值（如帧率的1-2倍，即 25-50）。
bf (B-Frames)
作用: 设置连续B帧的最大数量。B帧可以显著提高压缩率。
API 设置: codec_context->max_b_frames = 3;
优化建议: preset 会自动设置一个合适的值。zerolatency 调优会将其设为0。如果需要手动调整，2 或 3 是常见值。增加B帧会增加编码延迟。

*/


void PushCameraBase::SetH264EncoderOption(av::Dictionary& x264_opts, av::VideoEncoderContext& vec)
{
	std::error_code ec;

	// 方法A: 使用 CRF (推荐，用于质量控制)
	//x264_opts.set("preset", "slow", ec, 0); //预设 控制编码速度与压缩率的平衡
	//x264_opts.set("tune", "film", ec, 0);   //调优 根据视频内容的特点对算法进行微调，以优化特定类型的视觉效果
	//x264_opts.set("crf", "22", ec, 0);      //恒定速率因子  推荐的质量控制模式。 0-51。0是无损，23是默认值，18左右通常被认为是视觉上接近无损的，28以上质量会明显下降
	//x264_opts.set("profile", "high", ec, 0); //限制编码时可以使用的功能子集。不同的设备（如老旧手机、电视盒子）支持不同的 Profile
	////vec.raw()->level = 41; // Level 4.1 for 1080p
	vec.raw()->level = 51; //3840x2160 @ 30 fps	 标准帧率的4K电影、电视剧、UGC视频。这是最常用、兼容性最好的4K Level。
	////vec.raw()->level = 52; //3840x2160 @ 60 fps	 高帧率的4K视频，如体育赛事、游戏录屏、高流畅度演示视频。
	////vec.raw()->level = 60; //3840x2160 @ 120 fps (或更高)  极高帧率的4K视频或更高分辨率（如8K）的视频。设备兼容性会降低。


	// 方法B: 使用 ABR (用于码率控制，例如直播)
	vec.setBitRate(4000000); // 4 Mbps 当你需要严格控制文件大小时（例如流媒体带宽限制），使用此模式。编码器会尽量让平均码率接近你设定的值 通常与 maxrate 和 bufsize 一起使用，以控制码率波动。这被称为 ABR (Average Bitrate) 模式。在 CRF 模式下此参数无效
	x264_opts.set("preset", "veryfast", ec, 0);
	x264_opts.set("tune", "zerolatency", ec, 0);
	//x264_opts.set("tune", "film", ec, 0);
	x264_opts.set("maxrate", "6000k", ec, 0); //与 b:v 配合使用，用于限制码率的瞬时峰值。bufsize 定义了客户端的缓冲区大小，maxrate 定义了允许的最大码率
	x264_opts.set("bufsize", "6000k", ec, 0);
	x264_opts.set("profile", "main", ec, 0);

	//图像组 (GOP) 相关参数 
	//设置两个关键帧（I帧）之间的最大帧数.GOP 越大，压缩率越高（因为P/B帧更多），但随机拖动（seek）的响应速度越慢
	//视频点播（VOD）一般设置为帧率的2-10倍（如 25fps -> GOP 250）
	//直播流媒体为了加快进入速度和切片，通常设置为较小的值（如帧率的1-2倍，25fps帧率 即 25-50）。
	vec.setGopSize(30);

	//bf (B-Frames) 最大 B 帧数
	//设置连续B帧的最大数量。B帧可以显著提高压缩率
	//preset 会自动设置一个合适的值。zerolatency 调优会将其设为0。如果需要手动调整，2 或 3 是常见值。增加B帧会增加编码延迟
	//vec.setMaxBFrames(2);

	//最小 I 帧间隔
	//vec.raw()->keyint_min = 25;

	//refs：参考帧数，越多压缩效率越高但内存 / 计算开销增大
	//vec.raw()->refs = 4;

	//b_pyramid、b_adapt、weightb、weightp：B 帧和双向预测策略
	//x264_opts.set("b_pyramid", "strict",ec, 0);



	//多线程（Threading）
	//thread_count：线程数，0 表示自动
	//thread_type：FRAME/SLICE 多线程类型
	vec.raw()->thread_count = 0;
	vec.raw()->thread_type = FF_THREAD_FRAME;
}


