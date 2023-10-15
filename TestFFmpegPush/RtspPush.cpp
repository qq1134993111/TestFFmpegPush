#include "RtspPush.h"


//引入ffmpeg库的头文件和链接库
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>

#pragma comment(lib, "avformat.lib")
#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "swscale.lib")
#pragma comment(lib, "avutil.lib")

}

#include <iostream>

static const char* AvErrorString(int32_t av_error)
{
	thread_local static char sz_error[256] = { 0 };
	return av_make_error_string(sz_error, 256, av_error);
}

int32_t RtspPushCamera()
{
	avdevice_register_all();
	avformat_network_init();

	AVFormatContext* ifmtCtx = NULL;
	AVFormatContext* ofmtCtx = NULL;

	int32_t ret = 0;

	do
	{
		//摄像头 麦克风
		std::string videoDevice = R"##(video=XiaoMi USB 2.0 Webcam)##";
		std::string audioDevice = R"##(audio=Microphone (3- High Definition Audio Device))##";
		std::string deviceName = videoDevice + ":" + audioDevice;

		//查找dshow 摄像头马克风桌面捕获需要，其他音视频文件不需要
		const AVInputFormat* inputFormat = av_find_input_format("dshow");
		if (inputFormat == nullptr)
		{
			std::cout << "av_find_input_format can not find dshow \n";
			break;
		}

		//这里可以加参数打开，例如可以指定采集帧率
		AVDictionary* options = nullptr;
		av_dict_set(&options, "rtbufsize", "30412800", 0);//默认大小3041280
		av_dict_set(&options, "framerate", "30", NULL);
		//av_dict_set(&options,"offset_x","20",0);
		//The distance from the top edge of the screen or desktop
		//av_dict_set(&options,"offset_y","40",0);
		//Video frame size. The default is to capture the full screen
		//av_dict_set(&options,"video_size","320x240",0);

		//打开输入文件，获取封装格式相关信息
		ret = avformat_open_input(&ifmtCtx, deviceName.c_str(), inputFormat, &options);

		av_dict_free(&options);//使用完释放options

		if (ret < 0)
		{
			std::printf("avformat_open_input error:%d,%s", ret, AvErrorString(ret));
			break;
		}

		//解码一段数据，获取流相关信息
		if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0)
		{
			printf("avformat_find_stream_info failed to retrieve input stream information.%d,%s\n", ret, AvErrorString(ret));
			break;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//获取输入文件的 流索引号 解码器 解码器内容 信息
		int videoIndex = -1;
		int audioIndex = -1;
		AVCodecContext* videoCodecContext = NULL;
		AVCodecContext* audioCodecContext = NULL;
		const AVCodec* videoCodec = NULL;
		const AVCodec* audioCodec = NULL;

		for (int i = 0; i < ifmtCtx->nb_streams; i++)
		{
			AVCodecParameters* inCodecpar = ifmtCtx->streams[i]->codecpar;
			if (inCodecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				videoIndex = i;
				//创建解码器内容
				videoCodecContext = avcodec_alloc_context3(NULL);
				ret = avcodec_parameters_to_context(videoCodecContext, inCodecpar);
				if (ret < 0)
				{
					printf("avcodec_parameters_to_context error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				//创建解码器
				videoCodec = avcodec_find_decoder(videoCodecContext->codec_id);
				if (!videoCodec)
				{
					printf("avcodec_find_decoder error:%d,%s", videoCodecContext->codec_id, avcodec_get_name(videoCodecContext->codec_id));
					break;
				}

				av_opt_set(videoCodecContext->priv_data, "tune", "zerolatency", 0);

				//打开解码器
				ret = avcodec_open2(videoCodecContext, videoCodec, NULL);
				if (ret < 0)
				{
					printf("avcodec_open2 error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				printf("<Open> pxlFmt=%d, frameSize=%d*%d\n",
					(int)ifmtCtx->streams[i]->codecpar->format,
					ifmtCtx->streams[i]->codecpar->width,
					ifmtCtx->streams[i]->codecpar->height);

			}
			else if (inCodecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				//音频
				audioIndex = i;
				audioCodecContext = avcodec_alloc_context3(NULL);
				ret = avcodec_parameters_to_context(audioCodecContext, inCodecpar);
				if (ret < 0)
				{
					printf("avcodec_parameters_to_context error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				audioCodec = avcodec_find_decoder(audioCodecContext->codec_id);
				if (!audioCodec)
				{
					printf("avcodec_find_decoder error:%d,%s", audioCodecContext->codec_id, avcodec_get_name(audioCodecContext->codec_id));
					break;
				}

				ret = avcodec_open2(audioCodecContext, audioCodec, NULL);
				if (ret < 0)
				{
					printf("avcodec_open2 error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				printf("<Open> sample_fmt=%d, sampleRate=%d, channels=%d, chnl_layout=%d\n",
					(int)inCodecpar->format,
					inCodecpar->sample_rate,
					inCodecpar->ch_layout.nb_channels,
					inCodecpar->ch_layout.nb_channels);
			}
		}

		if (videoIndex == -1 || audioIndex == -1)
		{
			// handle error
			std::cout << "can not find videoIndex or audioIndex\n";
			break;
		}

		//打印输入信息：长度 比特率 流格式等
		av_dump_format(ifmtCtx, 0, deviceName.c_str(), 0);

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//输出编码
		//推送rtmp流到服务器  
		const char* outFilename = R"(rtsp://127.0.0.1:554/live/camera)";
		const char* ofmtName = "rtsp";//输出格式;

		const AVCodec* outVideoCodec = NULL;
		AVCodecContext* outVideoCodecCtx = NULL;
		//打开输出
		//分配输出ctx

		ret = avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
		if (ret < 0 || ofmtCtx == nullptr)
		{
			std::printf("avformat_alloc_output_context2 error:%d,%s", ret, AvErrorString(ret));
			break;
		}



		for (int i = 0; i < ifmtCtx->nb_streams; ++i)
		{
			//基于输入流创建输出流
			AVStream* inStream = ifmtCtx->streams[i];
			AVStream* outStream = avformat_new_stream(ofmtCtx, NULL);
			if (outStream == nullptr)
			{
				printf("avformat_new_stream failed to allocate output stream\n");
				break;
			}

			//将当前输入流中的参数拷贝到输出流中
			ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
			if (ret < 0)
			{
				printf("avcodec_parameters_copy failed to copy codec parameters.%d,%s\n", ret, AvErrorString(ret));
				break;
			}


			if (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				// 调整输出流的一些属性
				outStream->time_base = inStream->time_base;
				outStream->time_base.den = 30;


				outStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
				outStream->codecpar->codec_id = ofmtCtx->oformat->video_codec;
				outStream->codecpar->width = videoCodecContext->width;
				outStream->codecpar->height = videoCodecContext->height;
				outStream->codecpar->bit_rate = 110000;// videoCodecContext->bit_rate;

				outStream->codecpar->codec_tag = 0; //设置编码标签为0，否则可能导致输出文件不可播放



				//查找编码器
				outVideoCodec = avcodec_find_encoder(outStream->codecpar->codec_id);
				if (outVideoCodec == NULL)
				{
					printf("avcodec_find_encoder Cannot find any encoder.%d,%s\n", outStream->codecpar->codec_id, avcodec_get_name(outStream->codecpar->codec_id));
					break;
				}

				//设置编码器内容
				outVideoCodecCtx = avcodec_alloc_context3(outVideoCodec);
				ret = avcodec_parameters_to_context(outVideoCodecCtx, outStream->codecpar);
				if (ret < 0 || outVideoCodecCtx == NULL)
				{
					printf("avcodec_parameters_to_context error.%d,%s\n", ret, AvErrorString(ret));
					break;
				}


				outVideoCodecCtx->codec_id = outStream->codecpar->codec_id;
				outVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
				outVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
				outVideoCodecCtx->width = videoCodecContext->width;
				outVideoCodecCtx->height = videoCodecContext->height;
				outVideoCodecCtx->time_base = inStream->time_base;
				outVideoCodecCtx->time_base.num = 1;
				outVideoCodecCtx->time_base.den = 30;
				outVideoCodecCtx->bit_rate = 110000;//videoCodecContext->bit_rate;
				outVideoCodecCtx->gop_size = 10;


				if (outVideoCodecCtx->codec_id == AV_CODEC_ID_H264)
				{
					outVideoCodecCtx->qmin = 10;
					outVideoCodecCtx->qmax = 51;
					outVideoCodecCtx->qcompress = (float)0.6;
				}
				else if (outVideoCodecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO)
				{
					outVideoCodecCtx->max_b_frames = 2;
				}
				else if (outVideoCodecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO)
				{
					outVideoCodecCtx->mb_decision = 2;
				}

				if (ofmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
				{
					outVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
				}

				//av_opt_set(outVideoCodecCtx->priv_data, "tune", "zerolatency", 0);

				//打开编码器
				AVDictionary* dict = NULL;
				av_dict_set(&dict, "rtsp_transport", "tcp", 0);
				av_dict_set(&dict, "vcodec", "h264", 0);
				//av_dict_set(&dict, "f", "rtsp", 0);
				if ((ret = avcodec_open2(outVideoCodecCtx, outVideoCodec, &dict)) < 0)
				{
					printf("avcodec_open2 Open encoder failed.%d,%s\n", ret, AvErrorString(ret));
					break;
				}

				av_dict_free(&dict);

			}

		}

		//打印输出信息：长度 比特率 流格式等
		av_dump_format(ofmtCtx, 0, outFilename, 1);

		if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE))
		{
			//创建并初始化一个AVIOContext, 用以访问URL（outFilename）指定的资源
			ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
			if (ret < 0)
			{
				printf("avio_open can't open output URL: %s.%d,%s\n", outFilename, ret, AvErrorString(ret));
				break;
			}
		}
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//数据处理
		//写输出文件
		ret = avformat_write_header(ofmtCtx, NULL);
		if (ret < 0)
		{
			printf("avformat_write_header error ,accourred when opening output file.%d,%s\n", ret, AvErrorString(ret));
			break;
		}


		//设置数据转换参数
		//将原始数据转为RGB格式
		struct SwsContext* swsContext = sws_getContext(videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,//源格式
			outVideoCodecCtx->width, outVideoCodecCtx->height, outVideoCodecCtx->pix_fmt,//目标格式
			SWS_BICUBIC, NULL, NULL, NULL);

		//分配空间 
		//一帧图像数据大小
		int numBytes = av_image_get_buffer_size(outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		unsigned char* outBuffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

		AVFrame* srcFrame = av_frame_alloc();
		AVFrame* dstFrame = av_frame_alloc();

		//会将dstFrame的数据按指定格式自动"关联"到outBuffer  即dstFrame中的数据改变了out_buffer中的数据也会相应的改变
		av_image_fill_arrays(dstFrame->data, dstFrame->linesize, outBuffer, outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		dstFrame->format = outVideoCodecCtx->pix_fmt;
		dstFrame->width = outVideoCodecCtx->width;
		dstFrame->height = outVideoCodecCtx->height;


		AVPacket* inPkt = av_packet_alloc();
		av_new_packet(inPkt, videoCodecContext->width * videoCodecContext->height); //调整packet的数据
		AVPacket* outPkt = av_packet_alloc();

		uint32_t frameIndex = 0;
		int64_t startTime = av_gettime();

		while (1)
		{
			AVStream* inStream = nullptr, * outStream = nullptr;

			//从输入流读取一个packet 
			ret = av_read_frame(ifmtCtx, inPkt);
			if (ret < 0)
			{
				printf("av_read_frame error:%d,%s\n", ret, AvErrorString(ret));
				break;
			}

			inStream = ifmtCtx->streams[inPkt->stream_index];
			outStream = ofmtCtx->streams[inPkt->stream_index];

			if (inPkt->stream_index == videoIndex)
			{

				ret = avcodec_send_packet(videoCodecContext, inPkt);
				if (ret < 0)
				{
					printf("avcodec_send_packet error:%d,%s\n", ret, AvErrorString(ret));
					break;
				}

				while (ret >= 0)
				{
					ret = avcodec_receive_frame(videoCodecContext, srcFrame);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					{
						//printf("avcodec_receive_frame.[%d,%d]  %d,%s\n", AVERROR(EAGAIN), AVERROR_EOF, ret, AvErrorString(ret));
						break;
					}
					else if (ret < 0)
					{
						printf("avcodec_receive_frame error:%d,%s\n", ret, AvErrorString(ret));
						break;
					}

					sws_scale(swsContext, srcFrame->data, srcFrame->linesize, 0, videoCodecContext->height, dstFrame->data, dstFrame->linesize);
					//dstFrame->pts = srcFrame->pts;

					ret = avcodec_send_frame(outVideoCodecCtx, dstFrame);
					if (ret < 0)
					{
						printf("avcodec_send_frame error:%d,%s\n", ret, AvErrorString(ret));
						break;
					}

					while (ret >= 0)
					{
						ret = avcodec_receive_packet(outVideoCodecCtx, outPkt);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
						{
							break;
						}
						else if (ret < 0)
						{
							printf("avcodec_receive_packet error:%d,%s\n", ret, AvErrorString(ret));

						}

						//如果当前处理帧的显示时间戳为0或者没有等等不是正常值
						if (outPkt->pts == AV_NOPTS_VALUE)
						{
							printf("frame_index:%d\n", frameIndex);
							//Write PTS
							AVRational time_base1 = inStream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(inStream->r_frame_rate);
							//Parameters
							outPkt->pts = (double)(frameIndex * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							outPkt->dts = outPkt->pts;
							outPkt->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							frameIndex++;
						}

						//av_packet_rescale_ts(outPkt, inStream->time_base, outStream->time_base);

						outPkt->stream_index = outStream->index;
						outPkt->pos = -1;

						ret = av_interleaved_write_frame(ofmtCtx, outPkt);
						if (ret < 0)
						{
							printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
						}

						av_packet_unref(outPkt);
					}

				}


			}
			else if (inPkt->stream_index == audioIndex)
			{
				av_packet_rescale_ts(inPkt, inStream->time_base, outStream->time_base);
				inPkt->stream_index = outStream->index;
				ret = av_interleaved_write_frame(ofmtCtx, inPkt);
				if (ret < 0)
				{
					printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
				}
			}

			av_packet_unref(inPkt);
		}

		// 3.5 写输出文件尾
		ret = av_write_trailer(ofmtCtx);

		av_packet_free(&inPkt);
		av_packet_free(&outPkt);
		av_frame_free(&srcFrame);
		av_frame_free(&dstFrame);
		av_free(outBuffer);


	} while (0);


	if (ifmtCtx != nullptr)
		avformat_close_input(&ifmtCtx);

	if (ofmtCtx != nullptr)
	{
		/* close output */
		if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&ofmtCtx->pb);
		}
		avformat_free_context(ofmtCtx);
	}

	if (ret < 0 && ret != AVERROR_EOF)
	{
		printf("Error occurred\n");
		return -1;
	}


	return 0;
}

int32_t RtspPushDesktop()
{
	avdevice_register_all();
	avformat_network_init();

	AVFormatContext* ifmtCtx = NULL;
	AVFormatContext* ofmtCtx = NULL;

	int32_t ret = 0;

	do
	{
		//桌面

		std::string deviceName = "desktop";

		//查找dshow 摄像头马克风桌面捕获需要，其他音视频文件不需要
		const AVInputFormat* inputFormat = av_find_input_format("gdigrab");
		if (inputFormat == nullptr)
		{
			std::cout << "av_find_input_format can not find dshow \n";
			break;
		}

		//这里可以加参数打开，例如可以指定采集帧率
		AVDictionary* options = nullptr;
		av_dict_set(&options, "rtbufsize", "30412800", 0);//默认大小3041280
		av_dict_set(&options, "framerate", "30", NULL);
		//av_dict_set(&options,"offset_x","20",0);
		//The distance from the top edge of the screen or desktop
		//av_dict_set(&options,"offset_y","40",0);
		//Video frame size. The default is to capture the full screen
		//av_dict_set(&options,"video_size","320x240",0);

		//打开输入文件，获取封装格式相关信息
		ret = avformat_open_input(&ifmtCtx, deviceName.c_str(), inputFormat, &options);

		av_dict_free(&options);//使用完释放options

		if (ret < 0)
		{
			std::printf("avformat_open_input error:%d,%s", ret, AvErrorString(ret));
			break;
		}

		//解码一段数据，获取流相关信息
		if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0)
		{
			printf("avformat_find_stream_info failed to retrieve input stream information.%d,%s\n", ret, AvErrorString(ret));
			break;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//获取输入文件的 流索引号 解码器 解码器内容 信息
		int videoIndex = -1;
		int audioIndex = -1;
		AVCodecContext* videoCodecContext = NULL;
		AVCodecContext* audioCodecContext = NULL;
		const AVCodec* videoCodec = NULL;
		const AVCodec* audioCodec = NULL;

		for (int i = 0; i < ifmtCtx->nb_streams; i++)
		{
			AVCodecParameters* inCodecpar = ifmtCtx->streams[i]->codecpar;
			if (inCodecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				videoIndex = i;
				//创建解码器内容
				videoCodecContext = avcodec_alloc_context3(NULL);
				ret = avcodec_parameters_to_context(videoCodecContext, inCodecpar);
				if (ret < 0)
				{
					printf("avcodec_parameters_to_context error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				//创建解码器
				videoCodec = avcodec_find_decoder(videoCodecContext->codec_id);
				if (!videoCodec)
				{
					printf("avcodec_find_decoder error:%d,%s", videoCodecContext->codec_id, avcodec_get_name(videoCodecContext->codec_id));
					break;
				}

				av_opt_set(videoCodecContext->priv_data, "tune", "zerolatency", 0);

				//打开解码器
				ret = avcodec_open2(videoCodecContext, videoCodec, NULL);
				if (ret < 0)
				{
					printf("avcodec_open2 error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				printf("<Open> pxlFmt=%d, frameSize=%d*%d\n",
					(int)ifmtCtx->streams[i]->codecpar->format,
					ifmtCtx->streams[i]->codecpar->width,
					ifmtCtx->streams[i]->codecpar->height);

			}
			else if (inCodecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				//音频
				audioIndex = i;
				audioCodecContext = avcodec_alloc_context3(NULL);
				ret = avcodec_parameters_to_context(audioCodecContext, inCodecpar);
				if (ret < 0)
				{
					printf("avcodec_parameters_to_context error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				audioCodec = avcodec_find_decoder(audioCodecContext->codec_id);
				if (!audioCodec)
				{
					printf("avcodec_find_decoder error:%d,%s", audioCodecContext->codec_id, avcodec_get_name(audioCodecContext->codec_id));
					break;
				}

				ret = avcodec_open2(audioCodecContext, audioCodec, NULL);
				if (ret < 0)
				{
					printf("avcodec_open2 error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				printf("<Open> sample_fmt=%d, sampleRate=%d, channels=%d, chnl_layout=%d\n",
					(int)inCodecpar->format,
					inCodecpar->sample_rate,
					inCodecpar->ch_layout.nb_channels,
					inCodecpar->ch_layout.nb_channels);
			}
		}

		if (videoIndex == -1 /* || audioIndex == -1*/)
		{
			// handle error
			std::cout << "can not find videoIndex or audioIndex\n";
			break;
		}

		//打印输入信息：长度 比特率 流格式等
		av_dump_format(ifmtCtx, 0, deviceName.c_str(), 0);

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//输出编码
		//推送rtmp流到服务器  
		const char* outFilename = R"(rtsp://127.0.0.1:554/live/desktop)";
		const char* ofmtName = "rtsp";//输出格式;

		const AVCodec* outVideoCodec = NULL;
		AVCodecContext* outVideoCodecCtx = NULL;
		//打开输出
		//分配输出ctx
		ret = avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
		if (ret < 0 || ofmtCtx == nullptr)
		{
			std::printf("avformat_alloc_output_context2 error:%d,%s", ret, AvErrorString(ret));
			break;
		}



		for (int i = 0; i < ifmtCtx->nb_streams; ++i)
		{
			//基于输入流创建输出流
			AVStream* inStream = ifmtCtx->streams[i];
			AVStream* outStream = avformat_new_stream(ofmtCtx, NULL);
			if (outStream == nullptr)
			{
				printf("avformat_new_stream failed to allocate output stream\n");
				break;
			}

			//将当前输入流中的参数拷贝到输出流中
			ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
			if (ret < 0)
			{
				printf("avcodec_parameters_copy failed to copy codec parameters.%d,%s\n", ret, AvErrorString(ret));
				break;
			}


			if (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				// 调整输出流的一些属性
				outStream->time_base = inStream->time_base;
				outStream->time_base.den = 30;

				outStream->codecpar->codec_tag = 0; //设置编码标签为0，否则可能导致输出文件不可播放


				outStream->codecpar->codec_id = ofmtCtx->oformat->video_codec;//AV_CODEC_ID_H265

				//查找编码器
				outVideoCodec = avcodec_find_encoder(outStream->codecpar->codec_id);
				if (outVideoCodec == NULL)
				{
					printf("avcodec_find_encoder Cannot find any encoder.%d,%s\n", outStream->codecpar->codec_id, avcodec_get_name(outStream->codecpar->codec_id));
					break;
				}

				//设置编码器内容
				outVideoCodecCtx = avcodec_alloc_context3(outVideoCodec);
				ret = avcodec_parameters_to_context(outVideoCodecCtx, outStream->codecpar);
				if (ret < 0 || outVideoCodecCtx == NULL)
				{
					printf("avcodec_parameters_to_context error.%d,%s\n", ret, AvErrorString(ret));
					break;
				}


				outVideoCodecCtx->codec_id = outStream->codecpar->codec_id;
				outVideoCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
				outVideoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
				outVideoCodecCtx->width = videoCodecContext->width;
				outVideoCodecCtx->height = videoCodecContext->height;
				outVideoCodecCtx->time_base.num = 1;
				outVideoCodecCtx->time_base.den = 30;
				outVideoCodecCtx->bit_rate = 110000;
				outVideoCodecCtx->gop_size = 10;



				if (outVideoCodecCtx->codec_id == AV_CODEC_ID_H264)
				{
					outVideoCodecCtx->qmin = 10;
					outVideoCodecCtx->qmax = 51;
					outVideoCodecCtx->qcompress = (float)0.6;
				}
				else if (outVideoCodecCtx->codec_id == AV_CODEC_ID_MPEG2VIDEO)
				{
					outVideoCodecCtx->max_b_frames = 2;
				}
				else if (outVideoCodecCtx->codec_id == AV_CODEC_ID_MPEG1VIDEO)
				{
					outVideoCodecCtx->mb_decision = 2;
				}

				if (ofmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
				{
					outVideoCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
				}

				av_opt_set(outVideoCodecCtx->priv_data, "tune", "zerolatency", 0);
				av_opt_set(outVideoCodecCtx->priv_data, "preset", "ultrafast", 0);


				//打开编码器
				AVDictionary* dict = NULL;
				av_dict_set(&dict, "rtsp_transport", "tcp", 0); //rtcp 传输协议
				//av_dict_set(&dict, "rtsp_transport", "udp", 0);   //rtp 传输协议

				//av_dict_set(&dict, "vcodec", "h264", 0);
				//av_dict_set(&dict, "f", "rtsp", 0);
				//打开编码器
				if ((ret = avcodec_open2(outVideoCodecCtx, outVideoCodec, &dict)) < 0)
				{
					printf("avcodec_open2 Open encoder failed.%d,%s\n", ret, AvErrorString(ret));
					break;
				}

			}

		}

		//打印输出信息：长度 比特率 流格式等
		av_dump_format(ofmtCtx, 0, outFilename, 1);

		if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE))
		{
			//创建并初始化一个AVIOContext, 用以访问URL（outFilename）指定的资源
			ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
			if (ret < 0)
			{
				printf("avio_open can't open output URL: %s.%d,%s\n", outFilename, ret, AvErrorString(ret));
				break;
			}
		}
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//数据处理
		//写输出文件
		ret = avformat_write_header(ofmtCtx, NULL);
		if (ret < 0)
		{
			printf("avformat_write_header error ,accourred when opening output file.%d,%s\n", ret, AvErrorString(ret));
			break;
		}


		//设置数据转换参数
		//将原始数据转为目标指定格式
		struct SwsContext* swsContext = sws_getContext(videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,//源格式
			outVideoCodecCtx->width, outVideoCodecCtx->height, outVideoCodecCtx->pix_fmt,//目标格式
			SWS_BICUBIC, NULL, NULL, NULL);

		//分配空间 
		//一帧图像数据大小
		int numBytes = av_image_get_buffer_size(outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		unsigned char* outBuffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

		AVFrame* srcFrame = av_frame_alloc();
		AVFrame* dstFrame = av_frame_alloc();

		//会将dstFrame的数据按指定格式自动"关联"到outBuffer  即dstFrame中的数据改变了out_buffer中的数据也会相应的改变
		av_image_fill_arrays(dstFrame->data, dstFrame->linesize, outBuffer, outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		dstFrame->format = outVideoCodecCtx->pix_fmt;
		dstFrame->width = outVideoCodecCtx->width;
		dstFrame->height = outVideoCodecCtx->height;

		AVPacket* inPkt = av_packet_alloc();
		av_new_packet(inPkt, videoCodecContext->width * videoCodecContext->height); //调整packet的数据
		AVPacket* outPkt = av_packet_alloc();

		uint32_t frameIndex = 0;
		int64_t startTime = av_gettime();

		while (1)
		{
			//从输入流读取一个packet 
			ret = av_read_frame(ifmtCtx, inPkt);
			if (ret < 0)
			{
				printf("av_read_frame error:%d,%s\n", ret, AvErrorString(ret));
				break;
			}

			AVStream* inStream = ifmtCtx->streams[inPkt->stream_index];
			AVStream* outStream = ofmtCtx->streams[inPkt->stream_index];


			if (inPkt->stream_index == videoIndex)
			{

				ret = avcodec_send_packet(videoCodecContext, inPkt);
				if (ret < 0)
				{
					printf("avcodec_send_packet error:%d,%s\n", ret, AvErrorString(ret));
					break;
				}

				while (ret >= 0)
				{
					ret = avcodec_receive_frame(videoCodecContext, srcFrame);
					if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					{
						printf("avcodec_receive_frame.[%d,%d]  %d,%s\n", AVERROR(EAGAIN), AVERROR_EOF, ret, AvErrorString(ret));
						break;
					}
					else if (ret < 0)
					{
						printf("avcodec_receive_frame error:%d,%s\n", ret, AvErrorString(ret));
						break;
					}
					//srcFrame->pts = av_rescale_q(inPkt->pts, inStream->time_base, videoCodecContext->time_base);
					// 或者
					//srcFrame->best_effort_timestamp=inPkt->pts;

					sws_scale(swsContext, srcFrame->data, srcFrame->linesize, 0, videoCodecContext->height, dstFrame->data, dstFrame->linesize);
					//dstFrame->pts = srcFrame->pts;
					//dstFrame->best_effort_timestamp = av_rescale_q(srcFrame->pts, inStream->time_base, outStream->time_base);


					ret = avcodec_send_frame(outVideoCodecCtx, dstFrame);
					if (ret < 0)
					{
						printf("avcodec_send_frame error:%d,%s\n", ret, AvErrorString(ret));
						break;
					}

					while (ret >= 0)
					{
						ret = avcodec_receive_packet(outVideoCodecCtx, outPkt);
						if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
						{
							break;
						}
						else if (ret < 0)
						{
							printf("avcodec_receive_packet error:%d,%s\n", ret, AvErrorString(ret));

						}

						//Important:Delay
						//if (outPkt->stream_index == videoIndex)
						//{
						//	//若为视频流信息，则将当前视频流时间now_time与pts_time相比较，若now_time>pts_time,则直接播放；反之，延迟播放；
						//	AVRational time_base = ofmtCtx->streams[videoIndex]->time_base;
						//	AVRational time_base_q = { 1,AV_TIME_BASE };
						//	int64_t pts_time = av_rescale_q(outPkt->dts, time_base, time_base_q);
						//	int64_t now_time = av_gettime() - startTime;
						//	if (pts_time > now_time)
						//		av_usleep(pts_time - now_time);
						//}

						//如果当前处理帧的显示时间戳为0或者没有等等不是正常值
						if (outPkt->pts == AV_NOPTS_VALUE)
						{
							//printf("frame_index:%d\n", frameIndex);
							//Write PTS
							AVRational time_base1 = inStream->time_base;
							//Duration between 2 frames (us)
							int64_t calc_duration = (double)AV_TIME_BASE / av_q2d(inStream->r_frame_rate);
							//Parameters
							outPkt->pts = (double)(frameIndex * calc_duration) / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							outPkt->dts = outPkt->pts;
							outPkt->duration = (double)calc_duration / (double)(av_q2d(time_base1) * AV_TIME_BASE);
							frameIndex++;
						}

						//av_packet_rescale_ts(outPkt, inStream->time_base, outStream->time_base);
						outPkt->stream_index = inStream->index;
						outPkt->pos = -1;


						ret = av_interleaved_write_frame(ofmtCtx, outPkt);
						if (ret < 0)
						{
							printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
						}

						av_packet_unref(outPkt);

					}

				}


			}
			else if (inPkt->stream_index == audioIndex)
			{
				av_packet_rescale_ts(inPkt, inStream->time_base, outStream->time_base);
				inPkt->stream_index = outStream->index;
				ret = av_interleaved_write_frame(ofmtCtx, inPkt);
				if (ret < 0)
				{
					printf("av_interleaved_write_frame error:%d,%s\n", ret, AvErrorString(ret));
				}
			}

			av_packet_unref(inPkt);
		}

		// 3.5 写输出文件尾
		ret = av_write_trailer(ofmtCtx);

		av_packet_free(&inPkt);
		av_packet_free(&outPkt);
		av_frame_free(&srcFrame);
		av_frame_free(&dstFrame);
		av_free(outBuffer);


	} while (0);


	if (ifmtCtx != nullptr)
		avformat_close_input(&ifmtCtx);

	if (ofmtCtx != nullptr)
	{
		/* close output */
		if (ofmtCtx && !(ofmtCtx->oformat->flags & AVFMT_NOFILE))
		{
			avio_closep(&ofmtCtx->pb);
		}
		avformat_free_context(ofmtCtx);
	}

	if (ret < 0 && ret != AVERROR_EOF)
	{
		printf("Error occurred\n");
		return -1;
	}


	return 0;
}
