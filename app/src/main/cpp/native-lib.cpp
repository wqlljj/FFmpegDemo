#include <jni.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <libyuv.h>

extern "C" {
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include <libswresample/swresample.h>
}

#define LOGI(FORMAT, ...) __android_log_print(ANDROID_LOG_INFO,"jason",FORMAT,##__VA_ARGS__);
#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"jason",FORMAT,##__VA_ARGS__);

bool check(JNIEnv *env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGI("JNI 异常")
        return true;
    }
    return false;
}

static const int kTileX = 8;
static const int kTileY = 8;

void scaleI420(jbyte *src_i420_data, jint width, jint height, jbyte *dst_i420_data, jint dst_width,
               jint dst_height, jint mode) {

    jint src_i420_y_size = width * height;
    jint src_i420_u_size = (width >> 1) * (height >> 1);
    jbyte *src_i420_y_data = src_i420_data;
    jbyte *src_i420_u_data = src_i420_data + src_i420_y_size;
    jbyte *src_i420_v_data = src_i420_data + src_i420_y_size + src_i420_u_size;

    jint dst_i420_y_size = dst_width * dst_height;
    jint dst_i420_u_size = (dst_width >> 1) * (dst_height >> 1);
    jbyte *dst_i420_y_data = dst_i420_data;
    jbyte *dst_i420_u_data = dst_i420_data + dst_i420_y_size;
    jbyte *dst_i420_v_data = dst_i420_data + dst_i420_y_size + dst_i420_u_size;

    libyuv::I420Scale((const uint8 *) src_i420_y_data, width,
                      (const uint8 *) src_i420_u_data, width >> 1,
                      (const uint8 *) src_i420_v_data, width >> 1,
                      width, height,
                      (uint8 *) dst_i420_y_data, dst_width,
                      (uint8 *) dst_i420_u_data, dst_width >> 1,
                      (uint8 *) dst_i420_v_data, dst_width >> 1,
                      dst_width, dst_height,
                      (libyuv::FilterMode) mode);
}

void rotateI420(jbyte *src_i420_data, jint width, jint height, jbyte *dst_i420_data, jint degree) {
    jint src_i420_y_size = width * height;
    jint src_i420_u_size = (width >> 1) * (height >> 1);

    jbyte *src_i420_y_data = src_i420_data;
    jbyte *src_i420_u_data = src_i420_data + src_i420_y_size;
    jbyte *src_i420_v_data = src_i420_data + src_i420_y_size + src_i420_u_size;

    jbyte *dst_i420_y_data = dst_i420_data;
    jbyte *dst_i420_u_data = dst_i420_data + src_i420_y_size;
    jbyte *dst_i420_v_data = dst_i420_data + src_i420_y_size + src_i420_u_size;

    if (degree == libyuv::kRotate90 || degree == libyuv::kRotate270) {
        libyuv::I420Rotate((const uint8 *) src_i420_y_data, width,
                           (const uint8 *) src_i420_u_data, width >> 1,
                           (const uint8 *) src_i420_v_data, width >> 1,
                           (uint8 *) dst_i420_y_data, height,
                           (uint8 *) dst_i420_u_data, height >> 1,
                           (uint8 *) dst_i420_v_data, height >> 1,
                           width, height,
                           (libyuv::RotationMode) degree);
    }
}


extern "C"
JNIEXPORT void JNICALL Java_com_example_wqllj_ffmpegdemo_MainActivity_play
        (JNIEnv *env, jobject jobj, jstring input_jstr, jobject surface) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, NULL);
    LOGI("PLAY %s", input_cstr);
    //1.注册组件
    av_register_all();

    //封装格式上下文
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    //2.打开输入视频文件
    if (avformat_open_input(&pFormatCtx, input_cstr, NULL, NULL) != 0) {
        LOGE("%s", "打开输入视频文件失败");
        return;
    }
    //3.获取视频信息
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        LOGE("%s", "获取视频信息失败");
        return;
    }
    LOGI("视频格式 %s", pFormatCtx->iformat->name)
    LOGI("视频时长 %d", (int) (pFormatCtx->duration / AV_TIME_BASE))

    //视频解码，需要找到视频对应的AVStream所在pFormatCtx->streams的索引位置
    int video_stream_idx = -1;
    int i = 0;
    for (; i < pFormatCtx->nb_streams; i++) {
        //根据类型判断，是否是视频流
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
        }
    }
    LOGI("video_stream_idx =  %d ", video_stream_idx)
    AVCodecContext *pCodeCtx;
    AVCodec *pCodec;
    if(video_stream_idx != -1) {
        //4.获取视频解码器
         pCodeCtx = pFormatCtx->streams[video_stream_idx]->codec;
         pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
        if (pCodec == NULL) {
            LOGE("%s", "视频无法解码");
        return;
        } else {
            LOGI("视频宽高  %d  %d", pCodeCtx->width, pCodeCtx->height)
            LOGI("视频帧  %d", pCodeCtx->bit_rate)
            LOGI("解码器名称 %s", pCodec->name)
        }
    }else{
        LOGE("%s", "无视频流");
        return;
    }
    //5.打开解码器
    if (pCodec==NULL||avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE("%s", "视频解码器无法打开");
//        return;
    }

    //编码数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    //像素数据（解码数据）
    AVFrame *yuv_frame = av_frame_alloc();
    AVFrame *rgb_frame = av_frame_alloc();
    //native绘制
    //窗体
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    check(env);
    //绘制时的缓冲区
    ANativeWindow_Buffer outBuffer;
    int width = 400;
    int height =0;
    struct SwsContext *sws_ctx;
    if(pCodeCtx !=NULL) {
        height = (int) 400 * (1.0 * pCodeCtx->height / pCodeCtx->width);
        //用于像素格式转换或者缩放
        sws_ctx = sws_getContext(
                pCodeCtx->width, pCodeCtx->height, pCodeCtx->pix_fmt,
                width, height, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, NULL, NULL, NULL);
    }
    int ret, got_frame, framecount = 0;
    //6.一阵一阵读取压缩的视频数据AVPacket
    if (nativeWindow != NULL) {
        while (av_read_frame(pFormatCtx, packet) >= 0) {
            if (packet->stream_index == video_stream_idx) {
                //解码AVPacket->AVFrame
                ret = avcodec_decode_video2(pCodeCtx, yuv_frame, &got_frame, packet);
                //Zero if no frame could be decompressed
                //非零，正在解码
                if (got_frame) {
                    LOGI("解码%d帧", framecount++);
//                LOGI("宽 %d 高 %d", nativeWindow, +pCodeCtx->height);
                    //lock
                    //设置缓冲区的属性（宽、高、像素格式）
                    ANativeWindow_setBuffersGeometry(nativeWindow, width, height,
                                                     WINDOW_FORMAT_RGBA_8888);
                    ANativeWindow_lock(nativeWindow, &outBuffer, NULL);
                    //设置rgb_frame的属性（像素格式、宽高）和缓冲区
                    //rgb_frame缓冲区与outBuffer.bits是同一块内存
                    avpicture_fill((AVPicture *) rgb_frame, (const uint8_t *) outBuffer.bits,
                                   PIX_FMT_RGBA,
                                   width, height);
                    LOGI("outBuffer.stride = %d  %d   %d", outBuffer.stride, outBuffer.width,
                         outBuffer.height);
//                if (!yuv_scale_frame->linesize[0]) {
//                    //只有指定了AVFrame的像素格式、画面大小才能真正分配内存
//                    //缓冲区分配内存
//
//                    uint8_t *out_buffer = (uint8_t *) av_malloc(
//                            avpicture_get_size(AV_PIX_FMT_RGBA, width, height));
//                    //初始化缓冲区
//                    avpicture_fill((AVPicture *) yuv_scale_frame, out_buffer, AV_PIX_FMT_RGBA,
//                                   width, height);
//
//                }
                    rgb_frame->linesize[0] = outBuffer.stride * 2;
                    LOGI("rgb_frame宽高1 %d %d %d  %d", yuv_frame->height, rgb_frame->width,
                         rgb_frame->height, rgb_frame->linesize[0])
                    int h = sws_scale(sws_ctx,
                                      (const uint8_t *const *) yuv_frame->data, yuv_frame->linesize,
                                      0, yuv_frame->height,
                                      rgb_frame->data, rgb_frame->linesize);

                    LOGI("rgb_frame宽高2 %d   %d  %d %d  %d", yuv_frame->height, h, rgb_frame->width,
                         rgb_frame->height, rgb_frame->linesize[0])


//                int h=libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
//                                   yuv_frame->data[2], yuv_frame->linesize[2],
//                                   yuv_frame->data[1], yuv_frame->linesize[1],
//                                   rgb_frame->data[0], rgb_frame->linesize[0],
//                                   pCodeCtx->width, pCodeCtx->height);
//                                LOGI("rgb_frame宽高2 %d   %d  %d %d ", yuv_frame->height, h,
//                                     rgb_frame->width, rgb_frame->height)

                    if (check(env)) {
                        break;
                    }

                    //unlock
                    ANativeWindow_unlockAndPost(nativeWindow);
                    if (check(env)) {
                        break;
                    }
                    usleep(1000 * 16);

                }
            }

            av_free_packet(packet);
        }
    } else {
        LOGI("nativeWindow == NULL");
    };
    if (nativeWindow) {
        ANativeWindow_release(nativeWindow);
    }
    sws_freeContext(sws_ctx);
    check(env);
    av_frame_free(&yuv_frame);
    check(env);
    avcodec_close(pCodeCtx);
    check(env);
    avformat_free_context(pFormatCtx);
    check(env);
    env->ReleaseStringUTFChars(input_jstr, input_cstr);
}
extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_wqllj_ffmpegdemo_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}
