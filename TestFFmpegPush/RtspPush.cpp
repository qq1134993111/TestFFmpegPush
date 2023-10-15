#include "RtspPush.h"


//����ffmpeg���ͷ�ļ������ӿ�
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
		//����ͷ ��˷�
		std::string videoDevice = R"##(video=XiaoMi USB 2.0 Webcam)##";
		std::string audioDevice = R"##(audio=Microphone (3- High Definition Audio Device))##";
		std::string deviceName = videoDevice + ":" + audioDevice;

		//����dshow ����ͷ��˷����沶����Ҫ����������Ƶ�ļ�����Ҫ
		const AVInputFormat* inputFormat = av_find_input_format("dshow");
		if (inputFormat == nullptr)
		{
			std::cout << "av_find_input_format can not find dshow \n";
			break;
		}

		//������ԼӲ����򿪣��������ָ���ɼ�֡��
		AVDictionary* options = nullptr;
		av_dict_set(&options, "rtbufsize", "30412800", 0);//Ĭ�ϴ�С3041280
		av_dict_set(&options, "framerate", "30", NULL);
		//av_dict_set(&options,"offset_x","20",0);
		//The distance from the top edge of the screen or desktop
		//av_dict_set(&options,"offset_y","40",0);
		//Video frame size. The default is to capture the full screen
		//av_dict_set(&options,"video_size","320x240",0);

		//�������ļ�����ȡ��װ��ʽ�����Ϣ
		ret = avformat_open_input(&ifmtCtx, deviceName.c_str(), inputFormat, &options);

		av_dict_free(&options);//ʹ�����ͷ�options

		if (ret < 0)
		{
			std::printf("avformat_open_input error:%d,%s", ret, AvErrorString(ret));
			break;
		}

		//����һ�����ݣ���ȡ�������Ϣ
		if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0)
		{
			printf("avformat_find_stream_info failed to retrieve input stream information.%d,%s\n", ret, AvErrorString(ret));
			break;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//��ȡ�����ļ��� �������� ������ ���������� ��Ϣ
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
				//��������������
				videoCodecContext = avcodec_alloc_context3(NULL);
				ret = avcodec_parameters_to_context(videoCodecContext, inCodecpar);
				if (ret < 0)
				{
					printf("avcodec_parameters_to_context error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				//����������
				videoCodec = avcodec_find_decoder(videoCodecContext->codec_id);
				if (!videoCodec)
				{
					printf("avcodec_find_decoder error:%d,%s", videoCodecContext->codec_id, avcodec_get_name(videoCodecContext->codec_id));
					break;
				}

				av_opt_set(videoCodecContext->priv_data, "tune", "zerolatency", 0);

				//�򿪽�����
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
				//��Ƶ
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

		//��ӡ������Ϣ������ ������ ����ʽ��
		av_dump_format(ifmtCtx, 0, deviceName.c_str(), 0);

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//�������
		//����rtmp����������  
		const char* outFilename = R"(rtsp://127.0.0.1:554/live/camera)";
		const char* ofmtName = "rtsp";//�����ʽ;

		const AVCodec* outVideoCodec = NULL;
		AVCodecContext* outVideoCodecCtx = NULL;
		//�����
		//�������ctx

		ret = avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
		if (ret < 0 || ofmtCtx == nullptr)
		{
			std::printf("avformat_alloc_output_context2 error:%d,%s", ret, AvErrorString(ret));
			break;
		}



		for (int i = 0; i < ifmtCtx->nb_streams; ++i)
		{
			//�������������������
			AVStream* inStream = ifmtCtx->streams[i];
			AVStream* outStream = avformat_new_stream(ofmtCtx, NULL);
			if (outStream == nullptr)
			{
				printf("avformat_new_stream failed to allocate output stream\n");
				break;
			}

			//����ǰ�������еĲ����������������
			ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
			if (ret < 0)
			{
				printf("avcodec_parameters_copy failed to copy codec parameters.%d,%s\n", ret, AvErrorString(ret));
				break;
			}


			if (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				// �����������һЩ����
				outStream->time_base = inStream->time_base;
				outStream->time_base.den = 30;


				outStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
				outStream->codecpar->codec_id = ofmtCtx->oformat->video_codec;
				outStream->codecpar->width = videoCodecContext->width;
				outStream->codecpar->height = videoCodecContext->height;
				outStream->codecpar->bit_rate = 110000;// videoCodecContext->bit_rate;

				outStream->codecpar->codec_tag = 0; //���ñ����ǩΪ0��������ܵ�������ļ����ɲ���



				//���ұ�����
				outVideoCodec = avcodec_find_encoder(outStream->codecpar->codec_id);
				if (outVideoCodec == NULL)
				{
					printf("avcodec_find_encoder Cannot find any encoder.%d,%s\n", outStream->codecpar->codec_id, avcodec_get_name(outStream->codecpar->codec_id));
					break;
				}

				//���ñ���������
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

				//�򿪱�����
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

		//��ӡ�����Ϣ������ ������ ����ʽ��
		av_dump_format(ofmtCtx, 0, outFilename, 1);

		if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE))
		{
			//��������ʼ��һ��AVIOContext, ���Է���URL��outFilename��ָ������Դ
			ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
			if (ret < 0)
			{
				printf("avio_open can't open output URL: %s.%d,%s\n", outFilename, ret, AvErrorString(ret));
				break;
			}
		}
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//���ݴ���
		//д����ļ�
		ret = avformat_write_header(ofmtCtx, NULL);
		if (ret < 0)
		{
			printf("avformat_write_header error ,accourred when opening output file.%d,%s\n", ret, AvErrorString(ret));
			break;
		}


		//��������ת������
		//��ԭʼ����תΪRGB��ʽ
		struct SwsContext* swsContext = sws_getContext(videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,//Դ��ʽ
			outVideoCodecCtx->width, outVideoCodecCtx->height, outVideoCodecCtx->pix_fmt,//Ŀ���ʽ
			SWS_BICUBIC, NULL, NULL, NULL);

		//����ռ� 
		//һ֡ͼ�����ݴ�С
		int numBytes = av_image_get_buffer_size(outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		unsigned char* outBuffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

		AVFrame* srcFrame = av_frame_alloc();
		AVFrame* dstFrame = av_frame_alloc();

		//�ὫdstFrame�����ݰ�ָ����ʽ�Զ�"����"��outBuffer  ��dstFrame�е����ݸı���out_buffer�е�����Ҳ����Ӧ�ĸı�
		av_image_fill_arrays(dstFrame->data, dstFrame->linesize, outBuffer, outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		dstFrame->format = outVideoCodecCtx->pix_fmt;
		dstFrame->width = outVideoCodecCtx->width;
		dstFrame->height = outVideoCodecCtx->height;


		AVPacket* inPkt = av_packet_alloc();
		av_new_packet(inPkt, videoCodecContext->width * videoCodecContext->height); //����packet������
		AVPacket* outPkt = av_packet_alloc();

		uint32_t frameIndex = 0;
		int64_t startTime = av_gettime();

		while (1)
		{
			AVStream* inStream = nullptr, * outStream = nullptr;

			//����������ȡһ��packet 
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

						//�����ǰ����֡����ʾʱ���Ϊ0����û�еȵȲ�������ֵ
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

		// 3.5 д����ļ�β
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
		//����

		std::string deviceName = "desktop";

		//����dshow ����ͷ��˷����沶����Ҫ����������Ƶ�ļ�����Ҫ
		const AVInputFormat* inputFormat = av_find_input_format("gdigrab");
		if (inputFormat == nullptr)
		{
			std::cout << "av_find_input_format can not find dshow \n";
			break;
		}

		//������ԼӲ����򿪣��������ָ���ɼ�֡��
		AVDictionary* options = nullptr;
		av_dict_set(&options, "rtbufsize", "30412800", 0);//Ĭ�ϴ�С3041280
		av_dict_set(&options, "framerate", "30", NULL);
		//av_dict_set(&options,"offset_x","20",0);
		//The distance from the top edge of the screen or desktop
		//av_dict_set(&options,"offset_y","40",0);
		//Video frame size. The default is to capture the full screen
		//av_dict_set(&options,"video_size","320x240",0);

		//�������ļ�����ȡ��װ��ʽ�����Ϣ
		ret = avformat_open_input(&ifmtCtx, deviceName.c_str(), inputFormat, &options);

		av_dict_free(&options);//ʹ�����ͷ�options

		if (ret < 0)
		{
			std::printf("avformat_open_input error:%d,%s", ret, AvErrorString(ret));
			break;
		}

		//����һ�����ݣ���ȡ�������Ϣ
		if ((ret = avformat_find_stream_info(ifmtCtx, 0)) < 0)
		{
			printf("avformat_find_stream_info failed to retrieve input stream information.%d,%s\n", ret, AvErrorString(ret));
			break;
		}

		//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//��ȡ�����ļ��� �������� ������ ���������� ��Ϣ
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
				//��������������
				videoCodecContext = avcodec_alloc_context3(NULL);
				ret = avcodec_parameters_to_context(videoCodecContext, inCodecpar);
				if (ret < 0)
				{
					printf("avcodec_parameters_to_context error:%d,%s", ret, AvErrorString(ret));
					break;
				}

				//����������
				videoCodec = avcodec_find_decoder(videoCodecContext->codec_id);
				if (!videoCodec)
				{
					printf("avcodec_find_decoder error:%d,%s", videoCodecContext->codec_id, avcodec_get_name(videoCodecContext->codec_id));
					break;
				}

				av_opt_set(videoCodecContext->priv_data, "tune", "zerolatency", 0);

				//�򿪽�����
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
				//��Ƶ
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

		//��ӡ������Ϣ������ ������ ����ʽ��
		av_dump_format(ifmtCtx, 0, deviceName.c_str(), 0);

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		//�������
		//����rtmp����������  
		const char* outFilename = R"(rtsp://127.0.0.1:554/live/desktop)";
		const char* ofmtName = "rtsp";//�����ʽ;

		const AVCodec* outVideoCodec = NULL;
		AVCodecContext* outVideoCodecCtx = NULL;
		//�����
		//�������ctx
		ret = avformat_alloc_output_context2(&ofmtCtx, NULL, ofmtName, outFilename);
		if (ret < 0 || ofmtCtx == nullptr)
		{
			std::printf("avformat_alloc_output_context2 error:%d,%s", ret, AvErrorString(ret));
			break;
		}



		for (int i = 0; i < ifmtCtx->nb_streams; ++i)
		{
			//�������������������
			AVStream* inStream = ifmtCtx->streams[i];
			AVStream* outStream = avformat_new_stream(ofmtCtx, NULL);
			if (outStream == nullptr)
			{
				printf("avformat_new_stream failed to allocate output stream\n");
				break;
			}

			//����ǰ�������еĲ����������������
			ret = avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
			if (ret < 0)
			{
				printf("avcodec_parameters_copy failed to copy codec parameters.%d,%s\n", ret, AvErrorString(ret));
				break;
			}


			if (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				// �����������һЩ����
				outStream->time_base = inStream->time_base;
				outStream->time_base.den = 30;

				outStream->codecpar->codec_tag = 0; //���ñ����ǩΪ0��������ܵ�������ļ����ɲ���


				outStream->codecpar->codec_id = ofmtCtx->oformat->video_codec;//AV_CODEC_ID_H265

				//���ұ�����
				outVideoCodec = avcodec_find_encoder(outStream->codecpar->codec_id);
				if (outVideoCodec == NULL)
				{
					printf("avcodec_find_encoder Cannot find any encoder.%d,%s\n", outStream->codecpar->codec_id, avcodec_get_name(outStream->codecpar->codec_id));
					break;
				}

				//���ñ���������
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


				//�򿪱�����
				AVDictionary* dict = NULL;
				av_dict_set(&dict, "rtsp_transport", "tcp", 0); //rtcp ����Э��
				//av_dict_set(&dict, "rtsp_transport", "udp", 0);   //rtp ����Э��

				//av_dict_set(&dict, "vcodec", "h264", 0);
				//av_dict_set(&dict, "f", "rtsp", 0);
				//�򿪱�����
				if ((ret = avcodec_open2(outVideoCodecCtx, outVideoCodec, &dict)) < 0)
				{
					printf("avcodec_open2 Open encoder failed.%d,%s\n", ret, AvErrorString(ret));
					break;
				}

			}

		}

		//��ӡ�����Ϣ������ ������ ����ʽ��
		av_dump_format(ofmtCtx, 0, outFilename, 1);

		if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE))
		{
			//��������ʼ��һ��AVIOContext, ���Է���URL��outFilename��ָ������Դ
			ret = avio_open(&ofmtCtx->pb, outFilename, AVIO_FLAG_WRITE);
			if (ret < 0)
			{
				printf("avio_open can't open output URL: %s.%d,%s\n", outFilename, ret, AvErrorString(ret));
				break;
			}
		}
		////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		//���ݴ���
		//д����ļ�
		ret = avformat_write_header(ofmtCtx, NULL);
		if (ret < 0)
		{
			printf("avformat_write_header error ,accourred when opening output file.%d,%s\n", ret, AvErrorString(ret));
			break;
		}


		//��������ת������
		//��ԭʼ����תΪĿ��ָ����ʽ
		struct SwsContext* swsContext = sws_getContext(videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,//Դ��ʽ
			outVideoCodecCtx->width, outVideoCodecCtx->height, outVideoCodecCtx->pix_fmt,//Ŀ���ʽ
			SWS_BICUBIC, NULL, NULL, NULL);

		//����ռ� 
		//һ֡ͼ�����ݴ�С
		int numBytes = av_image_get_buffer_size(outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		unsigned char* outBuffer = (unsigned char*)av_malloc(numBytes * sizeof(unsigned char));

		AVFrame* srcFrame = av_frame_alloc();
		AVFrame* dstFrame = av_frame_alloc();

		//�ὫdstFrame�����ݰ�ָ����ʽ�Զ�"����"��outBuffer  ��dstFrame�е����ݸı���out_buffer�е�����Ҳ����Ӧ�ĸı�
		av_image_fill_arrays(dstFrame->data, dstFrame->linesize, outBuffer, outVideoCodecCtx->pix_fmt, outVideoCodecCtx->width, outVideoCodecCtx->height, 1);
		dstFrame->format = outVideoCodecCtx->pix_fmt;
		dstFrame->width = outVideoCodecCtx->width;
		dstFrame->height = outVideoCodecCtx->height;

		AVPacket* inPkt = av_packet_alloc();
		av_new_packet(inPkt, videoCodecContext->width * videoCodecContext->height); //����packet������
		AVPacket* outPkt = av_packet_alloc();

		uint32_t frameIndex = 0;
		int64_t startTime = av_gettime();

		while (1)
		{
			//����������ȡһ��packet 
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
					// ����
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
						//	//��Ϊ��Ƶ����Ϣ���򽫵�ǰ��Ƶ��ʱ��now_time��pts_time��Ƚϣ���now_time>pts_time,��ֱ�Ӳ��ţ���֮���ӳٲ��ţ�
						//	AVRational time_base = ofmtCtx->streams[videoIndex]->time_base;
						//	AVRational time_base_q = { 1,AV_TIME_BASE };
						//	int64_t pts_time = av_rescale_q(outPkt->dts, time_base, time_base_q);
						//	int64_t now_time = av_gettime() - startTime;
						//	if (pts_time > now_time)
						//		av_usleep(pts_time - now_time);
						//}

						//�����ǰ����֡����ʾʱ���Ϊ0����û�еȵȲ�������ֵ
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

		// 3.5 д����ļ�β
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
