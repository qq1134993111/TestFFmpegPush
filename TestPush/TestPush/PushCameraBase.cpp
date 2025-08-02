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

	//// ����ʵʱ��������С
	//in_format_options.set("rtbufsize", "304128000");
	in_format_options.set("rtbufsize", "256M");
	////���ô��豸ץȡ��֡��Ϊ 30 FPS
	//in_format_options.set("framerate", "30");
	// ���� analyzeduration Ϊ 10,000,000 ΢�� (10��)
	// FFmpeg��ѡ������������ܣ�ͨ��Ҳ֧�� "10M" ������д��
	in_format_options.set("analyzeduration", "10000000");
	// ���� probesize Ϊ 10,485,760 �ֽ� (10MB)
	// ͬ����"10M" ��д��Ҳ������֧��
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
		input_vdec_.raw()->thread_count = 0;//����Ϊ0����FFmpeg�Զ���������߳��� (�Ƽ�)
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

		input_adec_.raw()->thread_count = 0;//����Ϊ0����FFmpeg�Զ���������߳��� (�Ƽ�)
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
		printf("[%d] %s �� %s\n", i, device_list->devices[i]->device_name, device_list->devices[i]->device_description);

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
ʹ�� FFmpeg API (libavcodec) ���� H.264 ����ʱ���Ż���ҪΧ����ƽ�� �����ٶȡ�������� �� �ļ���С�����ʣ� ����������Ҫ�ء�
��Щ�Ż�ͨ��ͨ������ AVCodecContext �Ĳ����Լ� H.264 ����������Ҫ�� libx264����˽�в�����ʵ�֡�
�����ǿ��Խ����Ż��Ĺؼ���������Ϊ������Ҫ���
1. �����Ż���Ԥ������� (Preset & Tune)
��������Ҫ������Ӧ�ÿ��ǵ��Ż�ѡ������ǹٷ����ĵ�У��һϵ�в����ļ��ϣ��������ڲ��˽�ÿ����ϸ����������£����ٴﵽһ���ܺõ�ƽ��㡣
preset (Ԥ��)
����: ���Ʊ����ٶ���ѹ���ʵ�ƽ�⡣Խ���Ԥ�裬�����ٶ�Խ�죬��ѹ����Խ�ͣ�ͬ���������ļ����󣩡�
API ����: av_opt_set(codec_context->priv_data, "preset", "medium", 0);
��ѡֵ:
ultrafast
superfast
veryfast
faster
fast
medium (Ĭ��ֵ���ٶȺ�����������ƽ��)
slow
slower
veryslow
placebo (����������������������ʹ��)
�Ż�����:
ʵʱ��ý��/���ӳٳ���: ʹ�� veryfast, faster��
��ͨ��Ƶת��/Web��Ƶ: ʹ�� medium, fast��
׷�������ѹ��/��Ƶ�鵵: ʹ�� slow, slower��
tune (����)
����: ������Ƶ���ݵ��ص���㷨����΢�������Ż��ض����͵��Ӿ�Ч����
API ����: av_opt_set(codec_context->priv_data, "tune", "film", 0);
��ѡֵ:
film: ��Ӱ���������ݡ�
animation: ����Ƭ��
grain: ���ڱ�����Ƭ�����еĳ�����
stillimage: ��̬ͼ�����ݣ�����õ�Ƭ����
fastdecode: �Ż��Ա��ڸ���ؽ��롣
zerolatency: ���ӳ٣�����ʵʱͨ�ţ������һЩ��Ҫ��δ����֡�Ĺ��ܣ��� B ֡����
�Ż�����: ��ظ��������Ƶ����ѡ����ʵ� tune��������������кܴ�������zerolatency ��ʵʱ��Ҫ��ߵĳ���������Ҫ��
2. ���ʿ��� (Rate Control)
���ǿ�������ļ���С�������ĺ��ġ�
crf (Constant Rate Factor, �㶨��������)
����: �Ƽ�����������ģʽ����׷���Ӿ��ϵĺ㶨������CRF ֵԽ�ͣ�����Խ�ߣ��ļ�Խ��
API ����: av_opt_set(codec_context->priv_data, "crf", "23", 0);
ȡֵ��Χ: 0-51��0������23��Ĭ��ֵ��18����ͨ������Ϊ���Ӿ��Ͻӽ�����ģ�28���������������½���
�Ż�����: ����������ģʽ������Ҫ�ϸ�����ļ���Сʱ������ʹ�� CRF��ͨ������ CRF ֵ��Ѱ�������ʹ�С�����ƽ��㡣
b:v (Bitrate, ����)
����: ������Ҫ�ϸ�����ļ���Сʱ��������ý��������ƣ���ʹ�ô�ģʽ���������ᾡ����ƽ�����ʽӽ����趨��ֵ��
API ����: codec_context->bit_rate = 2000000; // 2 Mbps
�Ż�����: ͨ���� maxrate �� bufsize һ��ʹ�ã��Կ������ʲ������ⱻ��Ϊ ABR (Average Bitrate) ģʽ���� CRF ģʽ�´˲�����Ч��
maxrate �� bufsize
����: �� b:v ���ʹ�ã������������ʵ�˲ʱ��ֵ��bufsize �����˿ͻ��˵Ļ�������С��maxrate �����������������ʡ�
API ����:
av_opt_set(codec_context->priv_data, "maxrate", "4000k", 0);
av_opt_set(codec_context->priv_data, "bufsize", "8000k", 0);
�Ż�����: ����ý�峡���·ǳ���Ҫ�����Է�ֹ������˲ʱ���ߵ��µĿ��١�ͨ�� bufsize ����Ϊ maxrate ��1-2����
3. Profile & Level
��Ҫ����ȷ������˵ļ����ԡ�
profile
����: ���Ʊ���ʱ����ʹ�õĹ����Ӽ�����ͬ���豸�����Ͼ��ֻ������Ӻ��ӣ�֧�ֲ�ͬ�� Profile��
API ����: av_opt_set(codec_context->priv_data, "profile", "high", 0);
��ѡֵ: baseline, main, high, high10, high422, high444 �ȡ�
�Ż�����:
�߼����� (�Ͼ��豸): baseline��
��׼������/Web: main��
����/�ִ��豸: high (ͨ��ѡ��)��
level
����: �������ʡ��ֱ��ʡ�֡�ʵȲ��������ֵ����ƥ��������Ĵ���������
API ����: codec_context->level = 41; // Level 4.1
�Ż�����: ͨ�������ֶ����ã��������������ķֱ��ʡ�֡�ʵ��Զ�ѡ�������Ҫǿ�Ƽ����ض��豸�����Բ��ĸ��豸�Ĺ���ֲ������á����磬Level 4.1 ֧�� 1080p@30fps��
4. ͼ���� (GOP) ��ز���
g (GOP Size / keyint)
����: ���������ؼ�֡��I֡��֮������֡����
API ����: codec_context->gop_size = 250;
�Ż�����:
GOP Խ��ѹ����Խ�ߣ���ΪP/B֡���ࣩ��������϶���seek������Ӧ�ٶ�Խ����
��Ƶ�㲥��VOD��һ������Ϊ֡�ʵ�2-10������ 25fps -> GOP 250����
ֱ����ý��Ϊ�˼ӿ�����ٶȺ���Ƭ��ͨ������Ϊ��С��ֵ����֡�ʵ�1-2������ 25-50����
bf (B-Frames)
����: ��������B֡�����������B֡�����������ѹ���ʡ�
API ����: codec_context->max_b_frames = 3;
�Ż�����: preset ���Զ�����һ�����ʵ�ֵ��zerolatency ���ŻὫ����Ϊ0�������Ҫ�ֶ�������2 �� 3 �ǳ���ֵ������B֡�����ӱ����ӳ١�

*/


void PushCameraBase::SetH264EncoderOption(av::Dictionary& x264_opts, av::VideoEncoderContext& vec)
{
	std::error_code ec;

	// ����A: ʹ�� CRF (�Ƽ���������������)
	//x264_opts.set("preset", "slow", ec, 0); //Ԥ�� ���Ʊ����ٶ���ѹ���ʵ�ƽ��
	//x264_opts.set("tune", "film", ec, 0);   //���� ������Ƶ���ݵ��ص���㷨����΢�������Ż��ض����͵��Ӿ�Ч��
	//x264_opts.set("crf", "22", ec, 0);      //�㶨��������  �Ƽ�����������ģʽ�� 0-51��0������23��Ĭ��ֵ��18����ͨ������Ϊ���Ӿ��Ͻӽ�����ģ�28���������������½�
	//x264_opts.set("profile", "high", ec, 0); //���Ʊ���ʱ����ʹ�õĹ����Ӽ�����ͬ���豸�����Ͼ��ֻ������Ӻ��ӣ�֧�ֲ�ͬ�� Profile
	////vec.raw()->level = 41; // Level 4.1 for 1080p
	vec.raw()->level = 51; //3840x2160 @ 30 fps	 ��׼֡�ʵ�4K��Ӱ�����Ӿ硢UGC��Ƶ��������á���������õ�4K Level��
	////vec.raw()->level = 52; //3840x2160 @ 60 fps	 ��֡�ʵ�4K��Ƶ�����������¡���Ϸ¼��������������ʾ��Ƶ��
	////vec.raw()->level = 60; //3840x2160 @ 120 fps (�����)  ����֡�ʵ�4K��Ƶ����߷ֱ��ʣ���8K������Ƶ���豸�����Իή�͡�


	// ����B: ʹ�� ABR (�������ʿ��ƣ�����ֱ��)
	vec.setBitRate(4000000); // 4 Mbps ������Ҫ�ϸ�����ļ���Сʱ��������ý��������ƣ���ʹ�ô�ģʽ���������ᾡ����ƽ�����ʽӽ����趨��ֵ ͨ���� maxrate �� bufsize һ��ʹ�ã��Կ������ʲ������ⱻ��Ϊ ABR (Average Bitrate) ģʽ���� CRF ģʽ�´˲�����Ч
	x264_opts.set("preset", "veryfast", ec, 0);
	x264_opts.set("tune", "zerolatency", ec, 0);
	//x264_opts.set("tune", "film", ec, 0);
	x264_opts.set("maxrate", "6000k", ec, 0); //�� b:v ���ʹ�ã������������ʵ�˲ʱ��ֵ��bufsize �����˿ͻ��˵Ļ�������С��maxrate ������������������
	x264_opts.set("bufsize", "6000k", ec, 0);
	x264_opts.set("profile", "main", ec, 0);

	//ͼ���� (GOP) ��ز��� 
	//���������ؼ�֡��I֡��֮������֡��.GOP Խ��ѹ����Խ�ߣ���ΪP/B֡���ࣩ��������϶���seek������Ӧ�ٶ�Խ��
	//��Ƶ�㲥��VOD��һ������Ϊ֡�ʵ�2-10������ 25fps -> GOP 250��
	//ֱ����ý��Ϊ�˼ӿ�����ٶȺ���Ƭ��ͨ������Ϊ��С��ֵ����֡�ʵ�1-2����25fps֡�� �� 25-50����
	vec.setGopSize(30);

	//bf (B-Frames) ��� B ֡��
	//��������B֡�����������B֡�����������ѹ����
	//preset ���Զ�����һ�����ʵ�ֵ��zerolatency ���ŻὫ����Ϊ0�������Ҫ�ֶ�������2 �� 3 �ǳ���ֵ������B֡�����ӱ����ӳ�
	//vec.setMaxBFrames(2);

	//��С I ֡���
	//vec.raw()->keyint_min = 25;

	//refs���ο�֡����Խ��ѹ��Ч��Խ�ߵ��ڴ� / ���㿪������
	//vec.raw()->refs = 4;

	//b_pyramid��b_adapt��weightb��weightp��B ֡��˫��Ԥ�����
	//x264_opts.set("b_pyramid", "strict",ec, 0);



	//���̣߳�Threading��
	//thread_count���߳�����0 ��ʾ�Զ�
	//thread_type��FRAME/SLICE ���߳�����
	vec.raw()->thread_count = 0;
	vec.raw()->thread_type = FF_THREAD_FRAME;
}


