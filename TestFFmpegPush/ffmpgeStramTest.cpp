#include "ffmpgeStramTest.h"

static const char* AvErrorString(int32_t av_error)
{
	thread_local static char sz_error[256] = { 0 };
	return av_make_error_string(sz_error, 256, av_error);
}

//#define RTSP_PUSH_STREAM
//#define RTMP_PUSH_STREAM

//rtsp ����
int ffmpgeStramTest()
{
	const AVOutputFormat* ofmt = NULL;
	AVFormatContext* ifmt_ctx = NULL, * ofmt_ctx = NULL;

	AVPacket pkt;
	const char* in_filename, * out_filename;
	int ret, i;
	int videoindex = -1;
	int frame_index = 0;
	int64_t start_time = 0;

	in_filename = R"(D:\BaiduNetdiskDownload\Creation.of.the.Gods.I.Kingdom.of.Storms.2023.1080p.WEB-DL.H265.AAC-DreamHD.mp4)";
#ifdef RTMP_PUSH_STREAM
	out_filename = "rtmp://localhost/live/test_rtmp";
#else RTSP_PUSH_STREAM
	out_filename = "rtsp://localhost/live/test_rtsp";
#endif // RTMP_PUSH_STREAM

	//��ʼ���豸
	//av_register_all();
	avdevice_register_all();

	//Network
	avformat_network_init();
	//���루Input��
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
	{
		printf("Could not open input file.");
		goto end;
	}
	//��ȡ����Ƶ����Ϣ
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
	{
		printf("Failed to retrieve input stream information");
		goto end;
	}

	for (i = 0; i < ifmt_ctx->nb_streams; i++)
	{
		if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	}

	//��ӡ��������������ʽ����ϸ��Ϣ���������ʱ�䣬�����ʣ���������������Ԫ���ݣ������ݣ����������ʱ��
	printf("************start to print in_filename infomation**************\n");
	av_dump_format(ifmt_ctx, 0, in_filename, 0);
	printf("************end to print in_filename infomation**************\n");

	//�����Output��
	//��ʼ�����������AVFormatContext	
#ifdef RTMP_PUSH_STREAM
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); //RTMP��װ��
	//avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);//UDP
#else RTSP_PUSH_STREAM
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtsp", out_filename);//RTSP��װ��
#endif // RTMP_PUSH_STREAM

	if (!ofmt_ctx)
	{
		printf("Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}

	//���ļ�����ȡ����Ϣ����ӵ���װ����
	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < ifmt_ctx->nb_streams; i++)
	{
		//���������������������Create output AVStream according to input AVStream��
		AVStream* in_stream = ifmt_ctx->streams[i];
		//�������������AVStream
		auto in_codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
		assert(in_codec != nullptr);
		AVStream* out_stream = avformat_new_stream(ofmt_ctx, in_codec);
		if (!out_stream)
		{
			printf("Failed allocating output stream\n");
			ret = AVERROR_UNKNOWN;
			goto end;
		}
		printf(" allocating output stream succeed...\n");
		//����AVCodecContext�����ã�Copy the settings of AVCodecContext��
		//û�������������
		ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
		if (ret < 0)
		{
			printf("Failed to copy context from input to output stream codec context\n");
			goto end;
		}
		printf(" copy context from input to output stream codec context succeed...\n");

		//û���������Ҳ����
		//if (i == videoindex)
		{

			auto outVideoCodecCtx = avcodec_alloc_context3(in_codec);
			ret = avcodec_parameters_to_context(outVideoCodecCtx, out_stream->codecpar);
			if (ret < 0 || outVideoCodecCtx == NULL)
			{
				printf("avcodec_parameters_to_context  error\n");
				goto end;
			}

			//av_opt_set(outVideoCodecCtx->priv_data, "tune", "zerolatency", 0);

			//�򿪱�����
			AVDictionary* dict = NULL;
			//av_dict_set(&dict, "rtsp_transport", "tcp", 0);
			//av_dict_set(&dict, "vcodec", "h264", 0);
			//av_dict_set(&dict, "f", "rtsp", 0);
			//�򿪱�����
			if ((ret = avcodec_open2(outVideoCodecCtx, in_codec, &dict)) < 0)
			{
				printf("avcodec_open2  error\n");
				goto end;
			}

			out_stream->codecpar->codec_tag = 0;
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
				outVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}
	}

	//Dump Format------------------
	printf("************start to print out_filename infomation**************\n");
	av_dump_format(ofmt_ctx, 0, out_filename, 1);
	printf("************end to print out_filename infomation**************\n");

	//�����URL��Open output URL��
	if (!(ofmt->flags & AVFMT_NOFILE)) 
	{
		ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
		if (ret < 0) 
		{
			printf("Could not open output URL '%s'", out_filename);
			goto end;
		}
	}
	printf(" open output URL succeed...\n");
	//д�ļ�ͷ��Write file header��AVStream.codecpar
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) 
	{
		printf("Error occurred when opening output URL.%d,%s\n",ret, AvErrorString(ret));
		goto end;
	}

	start_time = av_gettime();
	while (1) 
	{
		AVStream* in_stream, * out_stream;
		//��ȡһ��AVPacket��Get an AVPacket��
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;
		//FIX��No PTS (Example: Raw H.264)
		//Simple Write PTS
		if (pkt.pts == AV_NOPTS_VALUE) 
		{
			//��pts��Чʱ������д��PTS
			//Write PTS
			AVRational time_base1 = ifmt_ctx->streams[videoindex]->time_base;
			//Duration between 2 frames (us)
			int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
			//Parameters	//PTS��ʾ��֡ͼ��ʲôʱ����ʾ���û�
			pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
			pkt.dts = pkt.pts;	//DTS��ʾѹ����ʲôʱ�򱻽���
			pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
		}

		//Important:Delay
		if (pkt.stream_index == videoindex)
		{
			//��Ϊ��Ƶ����Ϣ���򽫵�ǰ��Ƶ��ʱ��now_time��pts_time��Ƚϣ���now_time>pts_time,��ֱ�Ӳ��ţ���֮���ӳٲ��ţ�
			AVRational time_base = ifmt_ctx->streams[videoindex]->time_base;
			AVRational time_base_q = { 1,AV_TIME_BASE };
			int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
			int64_t now_time = av_gettime() - start_time;
			if (pts_time > now_time)
				av_usleep(pts_time - now_time);
		}

		in_stream = ifmt_ctx->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];
		/* copy packet */
		//ת��PTS/DTS��Convert PTS/DTS��
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		//Print to Screen
		if (pkt.stream_index == videoindex) 
		{
			printf("Send %8d video frames to output URL\n", frame_index);
			frame_index++;
		}

		//��ʼ����
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

		if (ret < 0) 
		{
			printf("Error muxing packet\n");
			break;
		}

	}
	//д�ļ�β��Write file trailer��
	av_write_trailer(ofmt_ctx);
end:
	avformat_close_input(&ifmt_ctx);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	if (ret < 0 && ret != AVERROR_EOF) 
	{
		printf("Error occurred.\n");
		return -1;
	}
	return 0;
}
