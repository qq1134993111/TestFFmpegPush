#include "CFFHandle.h"
#include <thread>
#include <chrono>

static const char* AvErrorString(int32_t av_error)
{
	thread_local static char sz_error[256] = { 0 };
	return av_make_error_string(sz_error, 256, av_error);
}

CFFInput::CFFInput()
{
	input_fmt_ctx_ = avformat_alloc_context();
}

CFFInput::~CFFInput()
{
	if (input_vdec_ != nullptr)
	{
		//avcodec_close(input_vdec_);
		avcodec_free_context(&input_vdec_);
	}

	if (input_adec_ != nullptr)
	{
		//avcodec_close(input_adec_);
		avcodec_free_context(&input_adec_);
	}

	if (input_fmt_ctx_ != nullptr)
	{
		avformat_close_input(&input_fmt_ctx_);
	}
}

bool CFFInput::InitInput(const std::string& in_uri, const std::string& in_format_name)
{

	const AVInputFormat* input_format = av_find_input_format(in_format_name.c_str());
	if (input_format == nullptr)
	{
		printf("av_find_input_format can not find %s \n", in_format_name.c_str());
		return false;
	}

	//������ԼӲ����򿪣��������ָ���ɼ�֡��
	AVDictionary* options = nullptr;
	av_dict_set(&options, "rtbufsize", "256M", 0);//Ĭ�ϴ�С3041280
	//av_dict_set(&options, "framerate", "30", 0);
	//av_dict_set(&options, "use_wallclock_as_timestamps", "1", 0);// ����ʹ��ϵͳʱ���

	//�������ļ�����ȡ��װ��ʽ�����Ϣ
	auto ret = avformat_open_input(&input_fmt_ctx_, in_uri.c_str(), input_format, &options);
	av_dict_free(&options);//ʹ�����ͷ�options

	if (ret < 0)
	{
		std::printf("avformat_open_input %s %s error:%d,%s\n", in_uri.c_str(), in_format_name.c_str(), ret, AvErrorString(ret));
		return false;
	}

	//����һ�����ݣ���ȡ�������Ϣ
	ret = avformat_find_stream_info(input_fmt_ctx_, 0);
	if (ret < 0)
	{
		printf("avformat_find_stream_info failed to retrieve input stream information.%d,%s\n", ret, AvErrorString(ret));
		return false;
	}



	for (size_t i = 0; i < input_fmt_ctx_->nb_streams; i++)
	{
		auto this_stream = input_fmt_ctx_->streams[i];

		AVCodecParameters* inCodecpar = this_stream->codecpar;

		if (inCodecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			input_video_stream_index_ = i;
			input_vst_ = this_stream;

		}
		else if (inCodecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			input_audio_stream_index_ = i;
			input_ast_ = this_stream;
		}
	}

	if (input_video_stream_index_ == -1 && input_audio_stream_index_ == -1)
	{
		// handle error
		std::cout << "can not find videoIndex and audioIndex\n";
		return false;
	}

	if (input_video_stream_index_ != -1)
	{
		//����������
		const AVCodec* video_decoder = avcodec_find_decoder(input_vst_->codecpar->codec_id);
		if (video_decoder == nullptr)
		{
			printf("avcodec_find_decoder error:%d,%s\n", input_vst_->codecpar->codec_id, avcodec_get_name(input_vst_->codecpar->codec_id));
			return false;
		}

		input_vdec_ = avcodec_alloc_context3(video_decoder);


		ret = avcodec_parameters_to_context(input_vdec_, input_vst_->codecpar);
		if (ret != 0)
		{
			printf("avcodec_parameters_to_context error %d,%s\n", ret, AvErrorString(ret));
			return false;
		}

		input_vdec_->time_base = input_vst_->time_base;

		input_vdec_->thread_count = 0;
		input_vdec_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
		ret = avcodec_open2(input_vdec_, nullptr, nullptr);
		if (ret < 0)
		{
			printf("avcodec_open2 error %d,%s\n", ret, AvErrorString(ret));
			return false;
		}
	}

	if (input_audio_stream_index_ != -1)
	{
		//����������
		const AVCodec* audio_decoder = avcodec_find_decoder(input_ast_->codecpar->codec_id);
		if (audio_decoder == nullptr)
		{
			printf("avcodec_find_decoder error:%d,%s\n", input_ast_->codecpar->codec_id, avcodec_get_name(input_ast_->codecpar->codec_id));
			return false;
		}

		input_adec_ = avcodec_alloc_context3(audio_decoder);


		ret = avcodec_parameters_to_context(input_adec_, input_ast_->codecpar);
		if (ret != 0)
		{
			printf("avcodec_parameters_to_context error %d,%s\n", ret, AvErrorString(ret));
			return false;
		}

		input_adec_->time_base = input_ast_->time_base;

		ret = avcodec_open2(input_adec_, nullptr, nullptr);
		if (ret < 0)
		{
			printf("avcodec_open2 error %d,%s\n", ret, AvErrorString(ret));
			return false;
		}
	}


	//��ӡ������Ϣ������ ������ ����ʽ��
	av_dump_format(input_fmt_ctx_, 0, in_uri.c_str(), 0);


	return  true;
}

CFFOutput::CFFOutput()
{

}

CFFOutput::~CFFOutput()
{
	if (output_fmt_ctx_ != nullptr)
	{
		/* close output */
		if (output_fmt_ctx_ && !(output_fmt_ctx_->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&output_fmt_ctx_->pb);
		}
		avformat_free_context(output_fmt_ctx_);
	}
}

bool CFFOutput::InitOutput(const std::string& out_uri, const std::string& out_format_name)
{
	if (input_video_stream_index_ == -1 && input_audio_stream_index_ == -1)
		return false;

	const AVOutputFormat* out_fmt = av_guess_format(out_format_name.c_str(), out_uri.c_str(), nullptr);

	output_fmt_ctx_ = avformat_alloc_context();
	output_fmt_ctx_->oformat = out_fmt;
	output_fmt_ctx_->url = av_strdup(out_uri.data());
	output_fmt_ctx_->iformat = nullptr;

	int ret = 0;
	if (input_video_stream_index_ != -1)
	{
		const AVCodec* vedio_encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
		if (vedio_encoder == nullptr)
		{
			printf("avcodec_find_encoder AV_CODEC_ID_H264 failed\n");
			return false;
		}

		output_vec_ = avcodec_alloc_context3(vedio_encoder);
		if (output_vec_ == nullptr)
		{
			printf("avcodec_alloc_context3  failed\n");

			return false;
		}

		output_vec_->width = input_vdec_->width;
		output_vec_->height = input_vdec_->height;
		output_vec_->pix_fmt = AV_PIX_FMT_YUV420P;
		output_vec_->time_base = AVRational{ 1,fps_ };
		output_vec_->bit_rate = 4000000;
		output_vec_->flags |= (output_fmt_ctx_->flags & AVFMT_GLOBALHEADER) ? AV_CODEC_FLAG_GLOBAL_HEADER : 0;

		output_vec_->level = 51;
		output_vec_->gop_size = 30;
		AVDictionary* x264_options = nullptr;
		av_dict_set(&x264_options, "preset", "veryfast", 0);
		av_dict_set(&x264_options, "tune", "zerolatency", 0);
		av_dict_set(&x264_options, "maxrate", "6000k", 0);
		av_dict_set(&x264_options, "bufsize", "6000k", 0);
		av_dict_set(&x264_options, "profile", "main", 0);
		output_vec_->thread_count = 0;
		output_vec_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

		ret = avcodec_open2(output_vec_, nullptr, &x264_options);
		av_dict_free(&x264_options);//ʹ�����ͷ�options

		if (ret < 0)
		{
			printf("avcodec_open2 failed.%d,%s\n", ret, AvErrorString(ret));
			return false;
		}

		output_vst_ = avformat_new_stream(output_fmt_ctx_, nullptr);
		if (output_vst_ == nullptr)
		{
			printf("avformat_new_stream failed\n");
			return false;
		}

		ret = avcodec_parameters_from_context(output_vst_->codecpar, output_vec_);
		if (ret != 0)
		{
			printf("avcodec_parameters_from_context error,%d,%s\n", ret, AvErrorString(ret));
			return false;
		}

		output_vst_->r_frame_rate = AVRational{ fps_,1 };
		output_vst_->avg_frame_rate = AVRational{ fps_,1 };
		output_vst_->time_base = AVRational{ 1,fps_ };
		output_vst_->codecpar->codec_tag = 0;

	}

	if (input_audio_stream_index_ != -1)
	{
		const AVCodec* codec = avcodec_find_encoder_by_name("aac");
		if (!codec)
		{
			printf("Cannot find aac codec for audio.\n");
			return false;
		}
		output_aec_ = avcodec_alloc_context3(codec);

		output_aec_->sample_rate = 44100;
		output_aec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
		AVChannelLayout ch_layout = AV_CHANNEL_LAYOUT_STEREO;
		output_aec_->ch_layout = ch_layout;
		output_aec_->bit_rate = 128000;
		output_aec_->time_base = AVRational{ 1,44100 };


		ret = avcodec_open2(output_aec_, nullptr, nullptr);
		if (ret < 0)
		{
			printf("Cannot open audio codec.%dm%s\n", ret, AvErrorString(ret));
			return false;
		}

		output_ast_ = avformat_new_stream(output_fmt_ctx_, nullptr);
		if (output_ast_ == nullptr)
		{
			printf("avformat_new_stream failed\n");
			return false;
		}

		ret = avcodec_parameters_from_context(output_ast_->codecpar, output_aec_);
		if (ret != 0)
		{
			printf("avcodec_parameters_from_context error,%d,%s\n", ret, AvErrorString(ret));
			return false;
		}
		output_ast_->time_base = input_ast_->time_base;
		output_ast_->codecpar->codec_tag = 0;

		if (!(output_fmt_ctx_->oformat->flags & AVFMT_NOFILE))
		{
			//��������ʼ��һ��AVIOContext, ���Է���URL��outFilename��ָ������Դ
			ret = avio_open(&output_fmt_ctx_->pb, out_uri.c_str(), AVIO_FLAG_WRITE);
			if (ret < 0)
			{
				printf("avio_open can't open output URL: %s.%d,%s\n", out_uri.c_str(), ret, AvErrorString(ret));
				return false;
			}
		}

	}

	//��ӡ�����Ϣ������ ������ ����ʽ��
	av_dump_format(output_fmt_ctx_, 0, out_uri.c_str(), 1);


	return true;
}

bool CFFHandle::InitVedioPkg()
{
	if (input_video_stream_index_ == -1)
		return true;

	video_out_pkt_ = av_packet_alloc();
	if (video_out_pkt_ == nullptr)
	{
		printf("av_packet_alloc failed\n");
		return false;
	}

	//��������ת������
			//��ԭʼ����תΪRGB��ʽ
	video_sws_context_ = sws_getContext(
		input_vdec_->width, input_vdec_->height, input_vdec_->pix_fmt,//Դ��ʽ
		output_vec_->width, output_vec_->height, output_vec_->pix_fmt,//Ŀ���ʽ
		SWS_BICUBIC, NULL, NULL, NULL);
	if (video_sws_context_ == nullptr)
	{
		printf("sws_getContext failed\n");
		return false;
	}

	//����ռ� 
	//һ֡ͼ�����ݴ�С
	int numBytes = av_image_get_buffer_size(output_vec_->pix_fmt, output_vec_->width, output_vec_->height, 1);
	vedio_out_buffer_ = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));
	if (vedio_out_buffer_ == nullptr)
	{
		printf("av_malloc failed\n");
		return false;
	}

	vedio_in_frame_ = av_frame_alloc();
	video_out_frame_ = av_frame_alloc();
	if (vedio_in_frame_ == nullptr || video_out_frame_ == nullptr)
	{
		printf("av_frame_alloc failed\n");
		return false;
	}

	//�ὫdstFrame�����ݰ�ָ����ʽ�Զ�"����"��outBuffer  ��dstFrame�е����ݸı���out_buffer�е�����Ҳ����Ӧ�ĸı�
	int ret = av_image_fill_arrays(video_out_frame_->data, video_out_frame_->linesize, vedio_out_buffer_, output_vec_->pix_fmt, output_vec_->width, output_vec_->height, 1);
	if (ret < 0)
	{
		printf("av_image_fill_arrays failed %d,%s", ret, AvErrorString(ret));
		return false;
	}
	video_out_frame_->format = output_vec_->pix_fmt;
	video_out_frame_->width = output_vec_->width;
	video_out_frame_->height = output_vec_->height;

	return true;
}

void CFFHandle::FreeVedioPkg()
{
	if (video_sws_context_ != nullptr)
	{
		sws_freeContext(video_sws_context_);
		video_sws_context_ = nullptr;
	}

	if (vedio_in_frame_ != nullptr)
	{
		av_frame_free(&vedio_in_frame_);
		vedio_in_frame_ = nullptr;
	}

	if (video_out_frame_ != nullptr)
	{
		av_frame_free(&video_out_frame_);
		video_out_frame_ = nullptr;
	}

	if (video_out_pkt_ != nullptr)
	{
		av_packet_free(&video_out_pkt_);
		video_out_pkt_ = nullptr;
	}

	if (vedio_out_buffer_ != nullptr)
	{
		av_free(vedio_out_buffer_);
		vedio_out_buffer_ = nullptr;
	}
}

bool CFFHandle::InitAudioPkg()
{
	if (input_audio_stream_index_ == -1)
		return true;

	audio_out_pkt_ = av_packet_alloc();
	if (audio_out_pkt_ == nullptr)
	{
		printf("av_packet_alloc failed\n");
		return false;
	}

	if (input_adec_->ch_layout.u.mask == 0)
	{
		AVChannelLayout ch_layout = {  };
		av_channel_layout_default(&ch_layout, input_adec_->ch_layout.nb_channels);
		input_adec_->ch_layout = ch_layout;
	}

	int ret = swr_alloc_set_opts2(&audio_swr_context_,
		&output_aec_->ch_layout,
		output_aec_->sample_fmt,
		output_aec_->sample_rate,
		&input_adec_->ch_layout,
		input_adec_->sample_fmt,// PCMԴ�ļ��Ĳ�����ʽ
		input_adec_->sample_rate, 0, nullptr);

	if (ret < 0)
	{
		printf("swr_alloc_set_opts2 error:%d,%s\n", ret, AvErrorString(ret));
		return false;
	}
	ret = swr_init(audio_swr_context_);
	if (ret < 0)
	{
		printf("swr_init error:%d,%s\n", ret, AvErrorString(ret));
		return false;
	}

	audio_in_frame_ = av_frame_alloc();
	audio_out_frame_ = av_frame_alloc();
	audio_resampled_frame_ = av_frame_alloc();
	if (audio_in_frame_ == nullptr || audio_resampled_frame_ == nullptr || audio_out_frame_ == nullptr)
	{
		printf("src_audio_frame failed\n");
		return false;
	}

	audio_out_frame_->ch_layout = output_aec_->ch_layout;
	audio_out_frame_->sample_rate = output_aec_->sample_rate;
	audio_out_frame_->format = output_aec_->sample_fmt;
	audio_out_frame_->nb_samples = output_aec_->frame_size;//AAC��ͨ��ÿ��֡�� 1024 �� 2048 �������㣨�ӱ��������ö�������
	ret = av_frame_get_buffer(audio_out_frame_, 0);
	if (ret < 0)
	{
		printf("av_frame_get_buffer error %d,%s", ret, AvErrorString(ret));
		return false;
	}

	audio_resampled_frame_->ch_layout = output_aec_->ch_layout;
	audio_resampled_frame_->sample_rate = output_aec_->sample_rate;
	audio_resampled_frame_->format = output_aec_->sample_fmt;
	audio_resampled_frame_->nb_samples = output_aec_->frame_size;//AAC��ͨ��ÿ��֡�� 1024 �� 2048 �������㣨�ӱ��������ö�������

	//��ʼ����Ƶ FIFO �����
	audio_fifo_ = av_audio_fifo_alloc(output_aec_->sample_fmt, output_aec_->ch_layout.nb_channels, 1);
	if (audio_fifo_ == nullptr)
	{
		std::cerr << "Could not allocate FIFO" << std::endl;
		return false;
	}

	return true;
}

void CFFHandle::FreeAudioPkg()
{
	if (audio_swr_context_ == nullptr)//��Ƶ�ز���
	{
		swr_free(&audio_swr_context_);
		audio_swr_context_ = nullptr;
	}

	if (audio_out_pkt_ == nullptr)
	{
		av_packet_free(&audio_out_pkt_);
		audio_out_pkt_ = nullptr;
	}

	if (audio_in_frame_ != nullptr)
	{
		av_frame_free(&audio_in_frame_);
		audio_in_frame_ = nullptr;
	}
	if (audio_out_frame_ != nullptr)
	{
		av_frame_free(&audio_out_frame_);
		audio_out_frame_ = nullptr;
	}
	if (audio_resampled_frame_ != nullptr)
	{
		av_frame_free(&audio_resampled_frame_);
		audio_resampled_frame_ = nullptr;
	}

	if (audio_fifo_ != nullptr)
	{
		av_audio_fifo_free(audio_fifo_);
		audio_fifo_ = nullptr;
	}

}

int CFFHandle::DecodePacket(AVCodecContext* dec, AVPacket* pkt, AVFrame* frame, DecodePacketHandler handler)
{
	int ret = 0;

	// submit the packet to the decoder
	// �����ݰ����͵�������
	ret = avcodec_send_packet(dec, pkt);
	if (ret < 0)
	{
		fprintf(stderr, "Error submitting a packet for decoding %d(%s)\n", ret, AvErrorString(ret));
		return ret;
	}

	// get all the available frames from the decoder
	// �ӽ�������ȡ�ѽ����֡
	while (ret >= 0)
	{
		ret = avcodec_receive_frame(dec, frame);
		if (ret < 0)
		{
			// those two return values are special and mean there is no output
			// frame available, but there were no errors during decoding
			if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
				return 0;

			fprintf(stderr, "Error during decoding %d(%s)\n", ret, AvErrorString(ret));
			return ret;
		}

		ret = handler(frame);

		av_frame_unref(frame);//avcodec_receive_frame ��ȡ��֡��Ҫav_frame_unref �ͷ�
	}

	return ret;
}

int CFFHandle::EncodeFrame(AVCodecContext* enc, const AVFrame* frame, AVPacket* pkt, EncodeFrameHandler handler)
{
	int ret = 0;

	// send the frame to the encoder
	// ������֡���͵�������
	ret = avcodec_send_frame(enc, frame);
	if (ret < 0)
	{
		fprintf(stderr, "Error sending a frame to the encoder: %s\n", AvErrorString(ret));
		//exit(1);
		return ret;
	}

	//�ӱ�������ȡ�ѱ�������ݰ�
	while (ret >= 0)
	{
		ret = avcodec_receive_packet(enc, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		else if (ret < 0)
		{
			fprintf(stderr, "Error encoding a frame: %s\n", AvErrorString(ret));
			//exit(1);
			return ret;
		}

		/* rescale output packet timestamp values from codec to stream timebase */
		//av_packet_rescale_ts(pkt, c->time_base, st->time_base);
		//pkt->stream_index = st->index;

		ret = handler(pkt);
		av_packet_unref(pkt);
		if (ret < 0)
		{
			fprintf(stderr, "Error while writing output packet: %s\n", AvErrorString(ret));
			//exit(1);
			return ret;
		}
	}

	return ret;
}

CFFHandle::~CFFHandle()
{

	FreeVedioPkg();
	FreeAudioPkg();
}

bool CFFHandle::HandlePakege()
{
	is_running_ = true;
	int ret = avformat_write_header(output_fmt_ctx_, nullptr);
	if (ret < 0)
	{
		printf("avformat_write_header error ,accourred when opening output file.%d,%s\n", ret, AvErrorString(ret));
		return false;
	}

	if (output_fmt_ctx_->pb != nullptr)
		avio_flush(output_fmt_ctx_->pb);

	if (!InitVedioPkg())
		return false;

	if (!InitAudioPkg())
		return false;

	// ��ȫ�ֻ����Ա������һ������������ʼ��Ϊ����ֵ
	int64_t first_packet_pts = AV_NOPTS_VALUE;
	// ����ѭ����ʼǰ����¼������������ʼʱ�䣨ʹ�õ���ʱ�ӣ�
	int64_t stream_start_time = 0;

	auto handle_vedio_frame = [&, this](AVFrame* src_frame) ->int {

		printf("in_frame: pts=%lld , seconds=%.4f , timeBase=%d/%d , %dx%d ,pictureType=%d\n",
			src_frame->pts, src_frame->pts * av_q2d(input_vdec_->time_base), input_vdec_->time_base.num, input_vdec_->time_base.den,
			src_frame->width, src_frame->height, src_frame->pict_type);

		//ת��
		ret = sws_scale(video_sws_context_, src_frame->data, src_frame->linesize, 0, input_vdec_->height, video_out_frame_->data, video_out_frame_->linesize);
		if (ret < 0)
		{
			printf("sws_scale failed %d,%s\n", ret, AvErrorString(ret));
			return 0;
		}
		//ת�� PTS
		video_out_frame_->pts = av_rescale_q(src_frame->pts,
			input_vdec_->time_base, // ԭʼʱ���׼
			output_vec_->time_base);  // Ŀ��ʱ���׼

		printf("out_frame: pts=%lld , seconds=%.4f , timeBase=%d/%d , %dx%d ,pictureType=%d\n",
			video_out_frame_->pts, video_out_frame_->pts * av_q2d(output_vec_->time_base), output_vec_->time_base.num, output_vec_->time_base.den,
			video_out_frame_->width, video_out_frame_->height, video_out_frame_->pict_type);

		EncodeFrame(output_vec_, video_out_frame_, video_out_pkt_, [&](AVPacket* out_pkt)->int {

			av_packet_rescale_ts(out_pkt, output_vec_->time_base, output_vst_->time_base);
			out_pkt->stream_index = output_vst_->index;
			out_pkt->pos = -1;

			printf("Write video packet: pts=%lld , dts=%lld , seconds=%.4f , timeBase=%d/%d ,st=%d \n",
				out_pkt->pts, out_pkt->dts, out_pkt->pts * av_q2d(output_vst_->time_base),
				output_vst_->time_base.num, output_vst_->time_base.den, out_pkt->stream_index);

			// д�����ݰ����ļ�
			ret = av_interleaved_write_frame(output_fmt_ctx_, out_pkt);
			if (ret < 0)
			{
				printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
			}

			return 0;

			});


		return 0;
		};

	int64_t next_audio_pts = 0;
	auto handle_audio_frame = [&, this](AVFrame* src_frame)->int {

		auto func_get_layout_name = [](const AVFrame* frame)->char*
			{
				static char buf[128] = { 0 };
				av_channel_layout_describe(&frame->ch_layout, buf, sizeof(buf));
				return buf;
			};

		printf("Samples [in]: nb_samples:%d , ch:%d , freq:%d , name:%s , pts seconds:%.4f  \n",
			src_frame->nb_samples, src_frame->ch_layout.nb_channels,
			src_frame->sample_rate, func_get_layout_name(src_frame),
			src_frame->pts * av_q2d(input_ast_->time_base));

		// ����ת��
		int ret = swr_convert_frame(audio_swr_context_, audio_resampled_frame_, src_frame);
		if (ret < 0)
		{
			// ������
			printf("swr_convert_frame error %d %s\n", ret, AvErrorString(ret));
			return 0;
		}

		//���ز�����Ŀɱ䳤������д�� FIFO
		if (av_audio_fifo_write(audio_fifo_, (void**)audio_resampled_frame_->data, audio_resampled_frame_->nb_samples) < audio_resampled_frame_->nb_samples)
		{
			std::cerr << "Could not write data to FIFO" << std::endl;
			return 0;
		}

		//�� FIFO ��ѭ����ȡ�̶�����(1024) �����ݿ鲢����
		while (av_audio_fifo_size(audio_fifo_) >= output_aec_->frame_size)
		{
			// �� FIFO ��ȡ 1024 ������
			if (av_audio_fifo_read(audio_fifo_, (void**)audio_out_frame_->data, output_aec_->frame_size) < output_aec_->frame_size)
			{
				std::cerr << "Could not read data from FIFO" << std::endl;
				continue;
			}

			//audio_out_frame_->pts = av_rescale_q(src_frame->pts, input_ast_->time_base, output_aec_->time_base);
			audio_out_frame_->pts = next_audio_pts;
		

			// Ϊ��һ֡����PTS
			next_audio_pts += audio_out_frame_->nb_samples; // 1024

			printf("Samples [out]: nb_samples:%d , ch:%d , freq:%d , name:%s , pts seconds:%.4f  \n",
				audio_out_frame_->nb_samples, audio_out_frame_->ch_layout.nb_channels,
				audio_out_frame_->sample_rate, func_get_layout_name(audio_out_frame_),
				audio_out_frame_->pts * av_q2d(output_aec_->time_base));

			EncodeFrame(output_aec_, audio_out_frame_, audio_out_pkt_, [&](AVPacket* out_pkt)->int {

				av_packet_rescale_ts(out_pkt, output_aec_->time_base, output_ast_->time_base);
				out_pkt->stream_index = output_ast_->index;
				out_pkt->pos = -1;

				printf("Write audio packet: pts=%lld , dts=%lld , seconds=%.4f , timeBase=%d/%d ,st=%d \n",
					out_pkt->pts, out_pkt->dts, out_pkt->pts * av_q2d(output_ast_->time_base),
					output_ast_->time_base.num, output_ast_->time_base.den, out_pkt->stream_index);

				// д�����ݰ����ļ�
				ret = av_interleaved_write_frame(output_fmt_ctx_, out_pkt);
				if (ret < 0)
				{
					printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
				}

				return 0;

				});

		}
		return 0;
		};

	AVPacket* in_pkt = av_packet_alloc();
	while (is_running_)
	{
		ret = av_read_frame(input_fmt_ctx_, in_pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				printf("av_read_frame eof:%d,%s\n", ret, AvErrorString(ret));
			}
			else
			{
				printf("av_read_frame error:%d,%s\n", ret, AvErrorString(ret));
			}
			break;
		}


		if (in_pkt->stream_index == input_video_stream_index_)
		{
			if (in_pkt->pts != AV_NOPTS_VALUE)
			{
				printf("Read vedio packet: pts=%lld , dts=%lld , seconds=%.4f , timeBase=%d/%d ,st=%d \n",
					in_pkt->pts, in_pkt->dts, in_pkt->pts * av_q2d(input_vst_->time_base),
					input_vst_->time_base.num, input_vst_->time_base.den, in_pkt->stream_index);
			}
			else
			{
				printf("Read vedio packet: N/A (No PTS)\n");
			}

			// 1. ����ǵ�һ֡����������PTS��Ϊ���ǵġ���㡱
			if (first_packet_pts == AV_NOPTS_VALUE)
			{
				first_packet_pts = in_pkt->pts;
				// ͬʱ����¼�µ�ǰ��ʵʱ����Ϊ���Ƿ���ʱ�ӵġ���㡱
				stream_start_time = av_gettime();
			}
			// 2. ���㵱ǰ������ڵ�һ����PTSƫ����
			int64_t pts_offset = in_pkt->pts - first_packet_pts;

			// 3. ������ɾ���ƫ����������������ʱ�����ת��Ϊ΢��
			//    �Ա��������ǵķ���ʱ���߼�
			int64_t pts_offset_us = av_rescale_q(pts_offset, input_vst_->time_base, { 1, 1000000 });

			// 4. ����Ŀ�귢��ʱ�� 
			int64_t target_send_time_us = stream_start_time + pts_offset_us;
			int64_t now_us = av_gettime();

			if (target_send_time_us > now_us)
			{
				auto delay_us = target_send_time_us - now_us;
				av_usleep((unsigned int)delay_us);
			}
			//����pts��ʹ���Լ������ʱ��
			in_pkt->pts = av_rescale_q(pts_offset_us, { 1, 1000000 }, input_vst_->time_base);
			in_pkt->dts = in_pkt->pts;

			printf("set new pts vedio packet: pts=%lld , dts=%lld , seconds=%.4f , timeBase=%d/%d ,st=%d \n",
				in_pkt->pts, in_pkt->dts, in_pkt->pts * av_q2d(input_vst_->time_base),
				input_vst_->time_base.num, input_vst_->time_base.den, in_pkt->stream_index);

			DecodePacket(input_vdec_, in_pkt, vedio_in_frame_, handle_vedio_frame);
		}
		else if (in_pkt->stream_index == input_audio_stream_index_)
		{
			if (in_pkt->pts != AV_NOPTS_VALUE)
			{
				printf("Read audio packet: pts=%lld , dts=%lld , seconds=%.4f , timeBase=%d/%d ,st=%d \n",
					in_pkt->pts, in_pkt->dts, in_pkt->pts * av_q2d(input_ast_->time_base),
					input_ast_->time_base.num, input_ast_->time_base.den, in_pkt->stream_index);
			}
			else
			{
				printf("Read audio packet: nullptr or  N/A (No PTS)\n");
			}
			////pkt->time_base = input_ast_->time_base;
			//av_packet_rescale_ts(in_pkt, input_ast_->time_base, output_ast_->time_base);
			//in_pkt->stream_index = output_ast_->index;
			//ret = av_interleaved_write_frame(output_fmt_ctx_, in_pkt);
			//if (ret < 0)
			//{
			//	printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
			//}

			DecodePacket(input_adec_, in_pkt, audio_in_frame_, handle_audio_frame);
		}

		av_packet_unref(in_pkt);//av_read_frame ��ȡ�� ��Ҫav_packet_unref

	}

	/* flush the decoders */
	if (output_vec_ != nullptr)
		DecodePacket(output_vec_, nullptr, vedio_in_frame_, handle_vedio_frame);

	if (output_aec_ != nullptr)
		DecodePacket(input_adec_, nullptr, audio_in_frame_, handle_audio_frame);

	//д����ļ�β
	ret = av_write_trailer(output_fmt_ctx_);


	av_packet_free(&in_pkt);


	return true;
}
