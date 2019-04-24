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

static int TileARGBScale(const uint8 *src_argb, int src_stride_argb,
                         int src_width, int src_height,
                         uint8 *dst_argb, int dst_stride_argb,
                         int dst_width, int dst_height,
                         libyuv::FilterMode filtering) {
    for (int y = 0; y < dst_height; y += kTileY) {
        for (int x = 0; x < dst_width; x += kTileX) {
            int clip_width = kTileX;
            if (x + clip_width > dst_width) {
                clip_width = dst_width - x;
            }
            int clip_height = kTileY;
            if (y + clip_height > dst_height) {
                clip_height = dst_height - y;
            }
            int r = ARGBScaleClip(src_argb, src_stride_argb,
                                  src_width, src_height,
                                  dst_argb, dst_stride_argb,
                                  dst_width, dst_height,
                                  x, y, clip_width, clip_height, filtering);
            if (r) {
                return r;
            }
        }
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL Java_com_example_wqllj_ffmpegdemo_MainActivity_play
        (JNIEnv *env, jobject jobj, jstring input_jstr, jobject surface) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, NULL);
    LOGI("PLAY %s", input_cstr);
    //1.注册组件
    av_register_all();
    check(env);

    //封装格式上下文
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    check(env);
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
    LOGI("视频时长 %d", pFormatCtx->duration / 1000000)

    //视频解码，需要找到视频对应的AVStream所在pFormatCtx->streams的索引位置
    int video_stream_idx = -1;
    int i = 0;
    for (; i < pFormatCtx->nb_streams; i++) {
        //根据类型判断，是否是视频流
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    LOGI("video_stream_idx =  %d", video_stream_idx)
    check(env);

    //4.获取视频解码器
    AVCodecContext *pCodeCtx = pFormatCtx->streams[video_stream_idx]->codec;
    check(env);
    AVCodec *pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
    check(env);
    if (pCodec == NULL) {
        LOGE("%s", "无法解码");
        return;
    }
    LOGI("视频宽高  %d  %d", pCodeCtx->width, pCodeCtx->height)
    LOGI("视频帧率  %d", pCodeCtx->bit_rate)
    LOGI("解码器名称 %s", pCodec->name)
    //5.打开解码器
    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        LOGE("%s", "解码器无法打开");
        return;
    }

    //编码数据
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    check(env);
    //像素数据（解码数据）
    AVFrame *yuv_frame = av_frame_alloc();

    AVFrame *yuv_scale_frame = av_frame_alloc();
    check(env);
    AVFrame *rgb_frame = av_frame_alloc();
    check(env);
    //native绘制
    //窗体
    ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
    check(env);
    //绘制时的缓冲区
    ANativeWindow_Buffer outBuffer;
    int width = 400;
    int height = (int) 400 * (1.0 * pCodeCtx->height / pCodeCtx->width);
    //用于像素格式转换或者缩放
    struct SwsContext *sws_ctx = sws_getContext(
                    pCodeCtx->width, pCodeCtx->height, pCodeCtx->pix_fmt,
                    width, height, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, NULL, NULL, NULL);
    int len, got_frame, framecount = 0;
    //6.一阵一阵读取压缩的视频数据AVPacket
    if (nativeWindow != NULL) {
        while (av_read_frame(pFormatCtx, packet) >= 0) {
            if (check(env)) {
                break;
            }
            //解码AVPacket->AVFrame
            len = avcodec_decode_video2(pCodeCtx, yuv_frame, &got_frame, packet);
            if (check(env)) {
                break;
            }
            //Zero if no frame could be decompressed
            //非零，正在解码
            if (got_frame) {
                LOGI("解码%d帧", framecount++);
//                LOGI("宽 %d 高 %d", nativeWindow, +pCodeCtx->height);
                //lock
                //设置缓冲区的属性（宽、高、像素格式）
                ANativeWindow_setBuffersGeometry(nativeWindow, pCodeCtx->width, pCodeCtx->height,
                                                 WINDOW_FORMAT_RGBA_8888);
                if (check(env)) {
                    break;
                }
                ANativeWindow_lock(nativeWindow, &outBuffer, NULL);
                if (check(env)) {
                    break;
                }
                //设置rgb_frame的属性（像素格式、宽高）和缓冲区
                //rgb_frame缓冲区与outBuffer.bits是同一块内存
                avpicture_fill((AVPicture *) rgb_frame, (const uint8_t *) outBuffer.bits,
                               PIX_FMT_RGBA,
                               pCodeCtx->width, pCodeCtx->height);
                if (check(env)) {
                    break;
                }
//                sws_getContext();
//                sws_scale();

//                if (!yuv_scale_frame->linesize[0]) {
//                    //只有指定了AVFrame的像素格式、画面大小才能真正分配内存
//                    //缓冲区分配内存
//
//                    uint8_t *out_buffer = (uint8_t *) av_malloc(
//                            avpicture_get_size(AV_PIX_FMT_RGBA, pCodeCtx->width, pCodeCtx->height));
//                    //初始化缓冲区
//                    avpicture_fill((AVPicture *) yuv_scale_frame, out_buffer, AV_PIX_FMT_RGBA,
//                                   width, height);
//
//                }
                sws_scale(sws_ctx,
                          (const uint8_t *const *) yuv_frame->data, yuv_frame->linesize, 0, yuv_frame->height,
                          rgb_frame->data, rgb_frame->linesize);
//                libyuv::I420Scale(yuv_frame->data[0],pCodeCtx->width,yuv_frame->data[1],pCodeCtx->width >> 1,
//                                  yuv_frame->data[2],pCodeCtx->width >> 1,pCodeCtx->width,pCodeCtx->height,
//                                  yuv_scale_frame->data[0],width,yuv_scale_frame->data[1],width >> 1,
//                                  yuv_scale_frame->data[2],width >> 1,width,height,libyuv::kFilterNone);
                //YUV->RGBA_8888
//                libyuv::I420ToARGB(yuv_scale_frame->data[0], yuv_scale_frame->linesize[0],
//                                   yuv_scale_frame->data[2], yuv_scale_frame->linesize[2],
//                                   yuv_scale_frame->data[1], yuv_scale_frame->linesize[1],
//                                   rgb_frame->data[0], rgb_frame->linesize[0],
//                                   width, height);
//                if(!yuv_scale_frame->linesize[0]){
//                    yuv_scale_frame->data[0] = (uint8_t *) calloc(pCodeCtx->width*pCodeCtx->height, sizeof(char));
//                    yuv_scale_frame->linesize[0] = sizeof(pCodeCtx->width*pCodeCtx->height);
//                    LOGI("calloc %d",pCodeCtx->width*pCodeCtx->height);
//                }
//                libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
//                                   yuv_frame->data[2], yuv_frame->linesize[2],
//                                   yuv_frame->data[1], yuv_frame->linesize[1],
//                                   yuv_scale_frame->data[0], yuv_scale_frame->linesize[0],
//                                   pCodeCtx->width, pCodeCtx->height);
//                TileARGBScale(yuv_scale_frame->data[0],yuv_scale_frame->linesize[0],
//                              pCodeCtx->width, pCodeCtx->height,
//                              rgb_frame->data[0], rgb_frame->linesize[0],
//                width,height,libyuv::kFilterNone);
//                LOGI("pCodeCtx %d   %d rgb_frame %d   %d  %d   %d",pCodeCtx->width, pCodeCtx->height,width,height,yuv_scale_frame->linesize[0],rgb_frame->linesize[0])


//                libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
//                                   yuv_frame->data[2], yuv_frame->linesize[2],
//                                   yuv_frame->data[1], yuv_frame->linesize[1],
//                                   rgb_frame->data[0], rgb_frame->linesize[0],
//                                   pCodeCtx->width, pCodeCtx->height);
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

            av_free_packet(packet);
            if (check(env)) {
                break;
            }
        }
    } else {
        LOGI("nativeWindow == NULL");
    };
    if (nativeWindow) {
        ANativeWindow_release(nativeWindow);
    }
    check(env);
    av_frame_free(&yuv_frame);
    check(env);
//    delete(yuv_scale_frame->data);
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
