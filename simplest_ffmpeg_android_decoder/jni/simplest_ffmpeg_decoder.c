/**
 * ��򵥵Ļ���FFmpeg����Ƶ������-��׿
 * Simplest FFmpeg Android Decoder
 *
 * ������ Lei Xiaohua
 * leixiaohua1020@126.com
 * �й���ý��ѧ/���ֵ��Ӽ���
 * Communication University of China / Digital TV Technology
 * http://blog.csdn.net/leixiaohua1020
 *
 * �������ǰ�׿ƽ̨����򵥵Ļ���FFmpeg����Ƶ�������������Խ��������Ƶ���ݽ����YUV�������ݡ�
 *
 * This software is the simplest decoder based on FFmpeg in Android. It can decode video stream
 * to raw YUV data.
 *
 */


#include <stdio.h>
#include <time.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h"

#ifdef ANDROID
#include <jni.h>
#include <android/log.h>
#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, "(>_<)", format, ##__VA_ARGS__)
#define LOGI(format, ...)  __android_log_print(ANDROID_LOG_INFO,  "(^_^)", format, ##__VA_ARGS__)
#else
#define LOGE(format, ...)  printf("(>_<) " format "\n", ##__VA_ARGS__)
#define LOGI(format, ...)  printf("(^_^) " format "\n", ##__VA_ARGS__)
#endif

static sp<Surface> surface;
//Output FFmpeg's av_log()
void custom_log(void *ptr, int level, const char* fmt, va_list vl){
	FILE *fp=fopen("/storage/emulated/0/av_log.txt","a+");
	if(fp){
		vfprintf(fp,fmt,vl);
		fflush(fp);
		fclose(fp);
	}
}

/**
  AVFormatContext：			统领全局的基本结构体。主要用于处理封装格式（FLV/MKV/RMVB等）。
	AVIOContext：				输入输出对应的结构体，用于输入输出（读写文件，RTMP协议等）。
	AVStream，AVCodecContext：	视音频流对应的结构体，用于视音频编解码。
	AVFrame:					存储非压缩的数据（视频对应RGB/YUV像素数据，音频对应PCM采样数据）
	AVPacket:					存储压缩数据（视频对应H.264等码流数据，音频对应AAC/MP3等码流数据）
* 解码,并且显示到Surface上
*/
JNIEXPORT jint JNICALL Java_com_leixiaohua1020_sffmpegandroiddecoder_MainActivity_decodeAndPlay
  (JNIEnv *env, jobject obj, jstring input_jstr, jobject surface)
{
	AVFormatContext *pFormatCtx;
	AVCodecContext 	*pCodecCtx;
	AVCodec 				*pCodec;
	int 						i,videoindex,ret,
									frame_cnt,got_picture;
	struct 					SwsContext *img_convert_ctx;
	uint8_t 				*out_buffer;

	char info[1000]={0};
	//取出输入地址
	char input_str[500]={0};
	sprintf(input_str,"%s",(*env)->GetStringUTFChars(env,input_jstr, NULL));

  jclass clazz = env->FindClass("android/view/Surface");
	jfield field_surface = env->GetFieldID(clazz, ANDROID_VIEW_SURFACE_JNI_ID, "I");

	//FFmpeg av_log() callback 设置日志回调函数
	av_log_set_callback(custom_log);
	//注册--复用器/解复用器/Protocol(协议 file/udp/rtmp ...... )
	av_register_all();
	//网络初始化?
	avformat_network_init();

	//取出 封装函数
	pFormatCtx = avformat_alloc_context();
	//打开多媒体数据并且获得一些相关的信息 ,打开成功会返回大于等于0的值
	if(avformat_open_input(&pFormatCtx,input_str,NULL,NULL)<0){
		LOGE("Couldn't open input stream.\n");
		return -1;
	}
	//读取一部分视音频数据并且获得一些相关的信息
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		LOGE("Couldn't find stream information.\n");
		return -1;
	}

	videoindex=-1; //初始化视频数据下标
	//获取视频数据的下标值
	for(i=0; i<pFormatCtx->nb_streams; i++)
	  //AVFormatContext ->AVStream -> AVCodecContext -> AVMediaType
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}

	//如果视频下标为 -1 说明该文件没有视频流数据
	if(videoindex==-1){
		LOGE("Couldn't find a video stream.\n");
		return -1;
	}

	//拿到 AVCodecContext  解码环境
  pCodecCtx=pFormatCtx->streams[videoindex]->codec;

	//寻找合适的解码器
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);

	if(pCodec==NULL){
		LOGE("Couldn't find Codec.\n");
		return -1;
	}

	//打开解码器
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
		LOGE("Couldn't open codec.\n");
		return -1;
	}

	//AVFrame 获取帧指针
	pFrame=av_frame_alloc();
	pFrameYUV=av_frame_alloc();
	//计算YUV420P所需的内存大小,并分配内存
	out_buffer=(uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));

	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	//分配内存给 AVPacket
	packet=(AVPacket *)av_malloc(sizeof(AVPacket));
	//SwsContext   | libswscale 是一个主要用于处理图片像素数据的类库。可以完成图片像素格式的转换，图片的拉伸等工作的类库
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
	pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


	//提取视频信息
	sprintf(info,   "[Input     ]%s\n", input_str);
  // sprintf(info, "%s[Output    ]%s\n",info,output_str);
  sprintf(info, "%s[Format    ]%s\n",info, pFormatCtx->iformat->name);
  sprintf(info, "%s[Codec     ]%s\n",info, pCodecCtx->codec->name);
  sprintf(info, "%s[Resolution]%dx%d\n",info, pCodecCtx->width,pCodecCtx->height);


	frame_cnt=0;
	time_start = clock();

	while(av_read_frame(pFormatCtx, packet)>=0){

		if(packet->stream_index==videoindex){
			//作用是解码一帧视频数据。输入一个压缩编码的结构体AVPacket，输出一个解码后的结构体AVFrame
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
			if(ret < 0){
				LOGE("Decode Error.\n");
				return -1;
			}
			if(got_picture){
				//将得到的这一帧 转换为YUV格式
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
					pFrameYUV->data, pFrameYUV->linesize);

				//获取数据长度 宽x高x3/2
				y_size=pCodecCtx->width*pCodecCtx->height;
				//写入文件
				fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y
				fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
				fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
				//Output info
				char pictype_str[10]={0};
				switch(pFrame->pict_type){
					case AV_PICTURE_TYPE_I:sprintf(pictype_str,"I");break;
					case AV_PICTURE_TYPE_P:sprintf(pictype_str,"P");break;
					case AV_PICTURE_TYPE_B:sprintf(pictype_str,"B");break;
					default:sprintf(pictype_str,"Other");break;
				}
				LOGI("Frame Index: %5d. Type:%s",frame_cnt,pictype_str);
				frame_cnt++;
			}
		}
		//释放数据包
		av_free_packet(packet);
	}

	LOGI("-----------------------我是分割线----------------------------------------");
	//flush decoder
	//FIX: Flush Frames remained in Codec
	while (1) {
		ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
		if (ret < 0)
			break;
		if (!got_picture)
			break;
		sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
			pFrameYUV->data, pFrameYUV->linesize);
		int y_size=pCodecCtx->width*pCodecCtx->height;
		fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y
		fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
		fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
		//Output info
		char pictype_str[10]={0};
		switch(pFrame->pict_type){
			case AV_PICTURE_TYPE_I:sprintf(pictype_str,"I");break;
			case AV_PICTURE_TYPE_P:sprintf(pictype_str,"P");break;
			case AV_PICTURE_TYPE_B:sprintf(pictype_str,"B");break;
			default:sprintf(pictype_str,"Other");break;
		}
		LOGI("Frame Index: %5d. Type:%s",frame_cnt,pictype_str);
		frame_cnt++;
	}


	time_finish = clock();
	time_duration=(double)(time_finish - time_start);

	sprintf(info, "%s[Time      ]%fms\n",info,time_duration);
	sprintf(info, "%s[Count     ]%d\n",info,frame_cnt);

	LOGI(" %s \n start free memory ...",info);

	sws_freeContext(img_convert_ctx);
  // fclose(fp_yuv);
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);
	(*env)->ReleaseStringUTFChars(env,input_jstr,input_str);
	(*env)->ReleaseStringUTFChars(env,output_jstr,output_str);

}

/**
解码视频 提取视频数据至YUV文件
*/
JNIEXPORT jint JNICALL Java_com_leixiaohua1020_sffmpegandroiddecoder_MainActivity_decode
  (JNIEnv *env, jobject obj, jstring input_jstr, jstring output_jstr)
{
	AVFormatContext	*pFormatCtx;
	int				i, videoindex;
	AVCodecContext	*pCodecCtx;
	AVCodec			*pCodec;
	AVFrame	*pFrame,*pFrameYUV;
	uint8_t *out_buffer;
	AVPacket *packet;
	int y_size;
	int ret, got_picture;
	struct SwsContext *img_convert_ctx;
	FILE *fp_yuv;
	int frame_cnt;
	clock_t time_start, time_finish;
	double  time_duration = 0.0;

	char input_str[500]={0};
	char output_str[500]={0};
	char info[1000]={0};
	sprintf(input_str,"%s",(*env)->GetStringUTFChars(env,input_jstr, NULL));
	sprintf(output_str,"%s",(*env)->GetStringUTFChars(env,output_jstr, NULL));

	//FFmpeg av_log() callback
  av_log_set_callback(custom_log);
	//注册--复用器/解复用器/Protocol(协议 file/udp/rtmp ...... )
	av_register_all();
	//
	avformat_network_init();
	/** AVFormatContext：			统领全局的基本结构体。主要用于处理封装格式（FLV/MKV/RMVB等）。
		AVIOContext：				输入输出对应的结构体，用于输入输出（读写文件，RTMP协议等）。
		AVStream，AVCodecContext：	视音频流对应的结构体，用于视音频编解码。
		AVFrame:					存储非压缩的数据（视频对应RGB/YUV像素数据，音频对应PCM采样数据）
		AVPacket:					存储压缩数据（视频对应H.264等码流数据，音频对应AAC/MP3等码流数据）    */
	pFormatCtx = avformat_alloc_context();

	//打开输入的流
	if(avformat_open_input(&pFormatCtx,input_str,NULL,NULL)!=0){
		LOGE("Couldn't open input stream.\n");
		return -1;
	}
	//寻找流信息
	if(avformat_find_stream_info(pFormatCtx,NULL)<0){
		LOGE("Couldn't find stream information.\n");
		return -1;
	}
	videoindex=-1;
	for(i=0; i<pFormatCtx->nb_streams; i++)
		if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}
	if(videoindex==-1){
		LOGE("Couldn't find a video stream.\n");
		return -1;
	}
	pCodecCtx=pFormatCtx->streams[videoindex]->codec;
	pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
	if(pCodec==NULL){
		LOGE("Couldn't find Codec.\n");
		return -1;
	}
	if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
		LOGE("Couldn't open codec.\n");
		return -1;
	}

	pFrame=av_frame_alloc();
	pFrameYUV=av_frame_alloc();
	out_buffer=(uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	//分配内存
	packet=(AVPacket *)av_malloc(sizeof(AVPacket));

	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
	pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);


  sprintf(info,   "[Input     ]%s\n", input_str);
  sprintf(info, "%s[Output    ]%s\n",info,output_str);
  sprintf(info, "%s[Format    ]%s\n",info, pFormatCtx->iformat->name);
  sprintf(info, "%s[Codec     ]%s\n",info, pCodecCtx->codec->name);
  sprintf(info, "%s[Resolution]%dx%d\n",info, pCodecCtx->width,pCodecCtx->height);

	LOGI("\ninfo:\n%s",info);

	//打开文件 ,赋予写入权限
  fp_yuv=fopen(output_str,"wb+");
  if(fp_yuv==NULL){
		printf("Cannot open output file.\n");
		return -1;
	}

	frame_cnt=0;
	time_start = clock();

	while(av_read_frame(pFormatCtx, packet)>=0){

		if(packet->stream_index==videoindex){
			ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
			if(ret < 0){
				LOGE("Decode Error.\n");
				return -1;
			}
			if(got_picture){
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
					pFrameYUV->data, pFrameYUV->linesize);

				y_size=pCodecCtx->width*pCodecCtx->height;
				fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y
				fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
				fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
				//Output info
				char pictype_str[10]={0};
				switch(pFrame->pict_type){
					case AV_PICTURE_TYPE_I:sprintf(pictype_str,"I");break;
				  case AV_PICTURE_TYPE_P:sprintf(pictype_str,"P");break;
					case AV_PICTURE_TYPE_B:sprintf(pictype_str,"B");break;
					default:sprintf(pictype_str,"Other");break;
				}
				LOGI("Frame Index: %5d. Type:%s",frame_cnt,pictype_str);
				frame_cnt++;
			}
		}
		av_free_packet(packet);
	}

	LOGI("-----------------------我是分割线----------------------------------------");
	//flush decoder
	//FIX: Flush Frames remained in Codec
	while (1) {
		ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
		if (ret < 0)
			break;
		if (!got_picture)
			break;
		sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
			pFrameYUV->data, pFrameYUV->linesize);
		int y_size=pCodecCtx->width*pCodecCtx->height;
		fwrite(pFrameYUV->data[0],1,y_size,fp_yuv);    //Y
		fwrite(pFrameYUV->data[1],1,y_size/4,fp_yuv);  //U
		fwrite(pFrameYUV->data[2],1,y_size/4,fp_yuv);  //V
		//Output info
		char pictype_str[10]={0};
		switch(pFrame->pict_type){
			case AV_PICTURE_TYPE_I:sprintf(pictype_str,"I");break;
		  case AV_PICTURE_TYPE_P:sprintf(pictype_str,"P");break;
			case AV_PICTURE_TYPE_B:sprintf(pictype_str,"B");break;
			default:sprintf(pictype_str,"Other");break;
		}
		LOGI("Frame Index: %5d. Type:%s",frame_cnt,pictype_str);
		frame_cnt++;
	}
	time_finish = clock();
	time_duration=(double)(time_finish - time_start);

	sprintf(info, "%s[Time      ]%fms\n",info,time_duration);
	sprintf(info, "%s[Count     ]%d\n",info,frame_cnt);

	LOGI(" %s \n start free memory ...",info);
	sws_freeContext(img_convert_ctx);

  fclose(fp_yuv);

	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}
