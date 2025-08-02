#include "CFFHandle.h"

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
	//av_dict_set(&options, "rtbufsize", "30412800", 0);//Ĭ�ϴ�С3041280
	//av_dict_set(&options, "framerate", "30", 0);

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



	for (int i = 0; i < input_fmt_ctx_->nb_streams; i++)
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
	const AVOutputFormat* out_fmt = av_guess_format(out_format_name.c_str(), out_uri.c_str(), nullptr);

	output_fmt_ctx_ = avformat_alloc_context();

	output_fmt_ctx_->oformat = out_fmt;
	output_fmt_ctx_->url = av_strdup(out_uri.data());
	output_fmt_ctx_->iformat = nullptr;

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
	output_vec_->gop_size = 60;
	AVDictionary* x264_options = nullptr;
	av_dict_set(&x264_options, "preset", "veryfast", 0);
	av_dict_set(&x264_options, "tune", "zerolatency", 0);
	av_dict_set(&x264_options, "maxrate", "6000k", 0);
	av_dict_set(&x264_options, "bufsize", "6000k", 0);
	av_dict_set(&x264_options, "profile", "main", 0);

	int ret = avcodec_open2(output_vec_, nullptr, &x264_options);
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


	output_ast_ = avformat_new_stream(output_fmt_ctx_, nullptr);
	if (output_ast_ == nullptr)
	{
		printf("avformat_new_stream failed\n");
		return false;
	}

	ret = avcodec_parameters_copy(output_ast_->codecpar, input_ast_->codecpar);
	if (ret != 0)
	{
		printf("avcodec_parameters_copy error,%d,%s\n", ret, AvErrorString(ret));
		return false;
	}
	output_ast_->time_base = input_ast_->time_base;

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

	//��ӡ�����Ϣ������ ������ ����ʽ��
	av_dump_format(output_fmt_ctx_, 0, out_uri.c_str(), 1);


	return true;
}

int CFFHandle::DecodePacket(AVCodecContext* dec, const AVPacket* pkt, AVFrame* frame, DecodePacketHandler handler)
{
	int ret = 0;

	// submit the packet to the decoder
	// �����ݰ����͵�������
	ret = avcodec_send_packet(dec, pkt);
	if (ret < 0)
	{
		fprintf(stderr, "Error submitting a packet for decoding (%s)\n", AvErrorString(ret));
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

			fprintf(stderr, "Error during decoding (%s)\n", AvErrorString(ret));
			return ret;
		}

		ret = handler(frame);

		av_frame_unref(frame);
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
	if (video_sws_context_ != nullptr)
	{
		sws_freeContext(video_sws_context_);
	}
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

	//��������ת������
	//��ԭʼ����תΪRGB��ʽ
	video_sws_context_ = sws_getContext(
		input_vdec_->width, input_vdec_->height, input_vdec_->pix_fmt,//Դ��ʽ
		output_vec_->width, output_vec_->height, output_vec_->pix_fmt,//Ŀ���ʽ
		SWS_BICUBIC, NULL, NULL, NULL);

	//����ռ� 
	//һ֡ͼ�����ݴ�С
	int numBytes = av_image_get_buffer_size(output_vec_->pix_fmt, output_vec_->width, output_vec_->height, 1);
	unsigned char* outBuffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

	AVFrame* srcFrame = av_frame_alloc();
	AVFrame* dstFrame = av_frame_alloc();

	//�ὫdstFrame�����ݰ�ָ����ʽ�Զ�"����"��outBuffer  ��dstFrame�е����ݸı���out_buffer�е�����Ҳ����Ӧ�ĸı�
	av_image_fill_arrays(dstFrame->data, dstFrame->linesize, outBuffer, output_vec_->pix_fmt, output_vec_->width, output_vec_->height, 1);
	dstFrame->format = output_vec_->pix_fmt;
	dstFrame->width = output_vec_->width;
	dstFrame->height = output_vec_->height;

	AVPacket* pkt = av_packet_alloc();
	av_new_packet(pkt, input_vdec_->width * input_vdec_->height); //����packet������

	int frameIndex = 0;

	auto handle_frame = [&](AVFrame* frame) ->int
		{
			//ת��
			sws_scale(video_sws_context_, frame->data, frame->linesize, 0, input_vdec_->height, dstFrame->data, dstFrame->linesize);

			//ת�� PTS
			dstFrame->pts = av_rescale_q(srcFrame->pts,
				input_vst_->time_base, // ԭʼʱ���׼
				output_vec_->time_base);  // Ŀ��ʱ���׼

			EncodeFrame(output_vec_, dstFrame, pkt, [&](AVPacket* enc_pkt)->int
				{

					av_packet_rescale_ts(enc_pkt, output_vec_->time_base, output_vst_->time_base);
					enc_pkt->stream_index = output_vst_->index;
					enc_pkt->pos = -1;

					// д�����ݰ����ļ�
					enc_pkt->stream_index = output_vst_->index;
					ret = av_interleaved_write_frame(output_fmt_ctx_, enc_pkt);
					if (ret < 0)
					{
						printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
					}

					return 0;

				});


			return 0;
		};



	while (is_running_)
	{
		ret = av_read_frame(input_fmt_ctx_, pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF)
			{
				//
				printf("av_read_frame eof:%d,%s\n", ret, AvErrorString(ret));
			}
			else
			{
				printf("av_read_frame error:%d,%s\n", ret, AvErrorString(ret));
			}
			break;
		}


		if (pkt->stream_index == input_video_stream_index_)
		{

			DecodePacket(input_vdec_, pkt, srcFrame, handle_frame);

		}
		else if (pkt->stream_index == input_audio_stream_index_)
		{
			//pkt->time_base = input_ast_->time_base;
			av_packet_rescale_ts(pkt, input_ast_->time_base, output_ast_->time_base);
			pkt->stream_index = output_ast_->index;
			ret = av_interleaved_write_frame(output_fmt_ctx_, pkt);
			if (ret < 0)
			{
				printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
			}
		}

		av_packet_unref(pkt);
	}

	/* flush the decoders */
	if (output_vec_ != nullptr)
		DecodePacket(output_vec_, NULL, srcFrame, handle_frame);

	//д����ļ�β
	ret = av_write_trailer(output_fmt_ctx_);

	av_frame_free(&srcFrame);
	av_frame_free(&dstFrame);
	av_packet_free(&pkt);
	av_free(outBuffer);

	return true;
}
