#include "ffmpgeStramTest.h"

static const char* AvErrorString(int32_t av_error)
{
	thread_local static char sz_error[256] = { 0 };
	return av_make_error_string(sz_error, 256, av_error);
}

//#define RTSP_PUSH_STREAM
//#define RTMP_PUSH_STREAM

//rtsp 可行
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

	//初始化设备
	//av_register_all();
	avdevice_register_all();

	//Network
	avformat_network_init();
	//输入（Input）
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
	{
		printf("Could not open input file.");
		goto end;
	}
	//获取音视频流信息
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

	//打印关于输入或输出格式的详细信息，例如持续时间，比特率，流，容器，程序，元数据，边数据，编解码器和时基
	printf("************start to print in_filename infomation**************\n");
	av_dump_format(ifmt_ctx, 0, in_filename, 0);
	printf("************end to print in_filename infomation**************\n");

	//输出（Output）
	//初始化输出码流的AVFormatContext	
#ifdef RTMP_PUSH_STREAM
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); //RTMP封装器
	//avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);//UDP
#else RTSP_PUSH_STREAM
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "rtsp", out_filename);//RTSP封装器
#endif // RTMP_PUSH_STREAM

	if (!ofmt_ctx)
	{
		printf("Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}

	//从文件中提取流信息并添加到封装器中
	ofmt = ofmt_ctx->oformat;
	for (i = 0; i < ifmt_ctx->nb_streams; i++)
	{
		//根据输入流创建输出流（Create output AVStream according to input AVStream）
		AVStream* in_stream = ifmt_ctx->streams[i];
		//创建输出码流的AVStream
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
		//复制AVCodecContext的设置（Copy the settings of AVCodecContext）
		//没有这段推流不行
		ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
		if (ret < 0)
		{
			printf("Failed to copy context from input to output stream codec context\n");
			goto end;
		}
		printf(" copy context from input to output stream codec context succeed...\n");

		//没有这段推流也可以
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

			//打开编码器
			AVDictionary* dict = NULL;
			//av_dict_set(&dict, "rtsp_transport", "tcp", 0);
			//av_dict_set(&dict, "vcodec", "h264", 0);
			//av_dict_set(&dict, "f", "rtsp", 0);
			//打开编码器
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

	//打开输出URL（Open output URL）
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
	//写文件头（Write file header）AVStream.codecpar
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
		//获取一个AVPacket（Get an AVPacket）
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;
		//FIX：No PTS (Example: Raw H.264)
		//Simple Write PTS
		if (pkt.pts == AV_NOPTS_VALUE) 
		{
			//若pts无效时，重新写入PTS
			//Write PTS
			AVRational time_base1 = ifmt_ctx->streams[videoindex]->time_base;
			//Duration between 2 frames (us)
			int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
			//Parameters	//PTS表示这帧图像什么时候显示给用户
			pkt.pts = (double)(frame_index * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
			pkt.dts = pkt.pts;	//DTS表示压缩包什么时候被解码
			pkt.duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
		}

		//Important:Delay
		if (pkt.stream_index == videoindex)
		{
			//若为视频流信息，则将当前视频流时间now_time与pts_time相比较，若now_time>pts_time,则直接播放；反之，延迟播放；
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
		//转换PTS/DTS（Convert PTS/DTS）
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

		//开始推流
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

		if (ret < 0) 
		{
			printf("Error muxing packet\n");
			break;
		}

	}
	//写文件尾（Write file trailer）
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
