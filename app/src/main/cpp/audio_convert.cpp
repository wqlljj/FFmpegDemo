//
// Created by cloud on 2019/4/28.
//
#include <jni.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <libyuv.h>

#include "logutils.cpp"

extern "C"{
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

}

char const *audioTyps[3] = {"mp3","pcm","aac"};
//播放视频中的音频
extern "C"
JNIEXPORT void JNICALL
Java_com_example_wqllj_ffmpegdemo_MediaPlayAPI_play(JNIEnv *env, jclass cls, jstring path_) {
    const char *path = env->GetStringUTFChars(path_, 0);
    size_t MAX_AUDIO_FRAME_SIZE = 44100;
    av_register_all();
    AVFormatContext * avFormatContext = avformat_alloc_context();
    if(avformat_open_input(&avFormatContext,path,NULL,NULL)!=0){
        LOGE("文件打开失败")
        return;
    }
    if(avformat_find_stream_info(avFormatContext,NULL)<0){
        LOGE("获取文件信息失败")
        return;
    }
    int audio_index = -1;
    int i=0;
    for(;i<avFormatContext->nb_streams;i++){
        if(avFormatContext->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
            audio_index = i;
        }
    }
    AVCodecContext * audio_codec_context = avFormatContext->streams[audio_index]->codec;
    AVCodec *audio_codec = avcodec_find_decoder(audio_codec_context->codec_id);
    if(audio_codec==NULL){
        LOGE("获取解码器失败")
        return;
    }
    if(avcodec_open2(audio_codec_context,audio_codec,NULL)<0){
        LOGE("打开解码器失败")
        return;
    }
    AVPacket *packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    AVFrame *frame = av_frame_alloc();
    int in_sample_rate = audio_codec_context->sample_rate;
    int out_sample_rate = MAX_AUDIO_FRAME_SIZE;
    AVSampleFormat in_sample_fmt = audio_codec_context->sample_fmt;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    uint64_t in_ch_layout = audio_codec_context->channel_layout;
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    SwrContext *swr_ctx = swr_alloc();
    swr_alloc_set_opts(swr_ctx,out_ch_layout,out_sample_fmt,
    out_sample_rate,in_ch_layout,in_sample_fmt,in_sample_rate,1,NULL);
    swr_init(swr_ctx);
    int out_ch_nb_layout = av_get_channel_layout_nb_channels(out_ch_layout);
    jmethodID createAudioTrack = env->GetStaticMethodID(cls,"createAudioTrack","(II)Landroid/media/AudioTrack;");
    jobject audio_track = env->CallStaticObjectMethod(cls,createAudioTrack,out_sample_rate
    ,out_ch_nb_layout);

    jclass  audio_track_cls = env->GetObjectClass(audio_track);
    jmethodID play_mid = env->GetMethodID(audio_track_cls,"play","()V");
    env->CallVoidMethod(audio_track,play_mid);

    jmethodID write_mid = env->GetMethodID(audio_track_cls,"write","([BII)I");
    uint8_t *out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
    int got_frame = 0,index = 0,ret = 0;
    while (av_read_frame(avFormatContext,packet)>=0){
        if(packet->stream_index == audio_index) {
            ret = avcodec_decode_audio4(audio_codec_context, frame, &got_frame,
                                        packet);
            if (ret < 0) {
                LOGI("解码完成")
            }
            if (got_frame > 0) {
                LOGI("解码 ：%d", index++)
                swr_convert(swr_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE * 2,
                            (const uint8_t **) frame->data, frame->nb_samples);

                int out_buffer_size = av_samples_get_buffer_size(NULL, out_ch_nb_layout,
                                                                 frame->nb_samples, out_sample_fmt,
                                                                 1);
//        fwrite(out_buffer,1,out_buffer_size,file);
                jbyteArray audio_sample_array = env->NewByteArray(out_buffer_size);
                jbyte *sample_byte = env->GetByteArrayElements(audio_sample_array, NULL);
                memcpy(sample_byte, out_buffer, out_buffer_size);
                env->ReleaseByteArrayElements(audio_sample_array, sample_byte, 0);
                env->CallIntMethod(audio_track, write_mid,
                                   audio_sample_array, 0, out_buffer_size);
                env->DeleteLocalRef(audio_sample_array);
                usleep(1000 * 16);
            }
        }
        av_free_packet(packet);
    }
    av_frame_free(&frame);
    av_free(out_buffer);
    swr_free(&swr_ctx);
    avcodec_close(audio_codec_context);
    avformat_close_input(&avFormatContext);
    env->ReleaseStringUTFChars(path_, path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_wqllj_ffmpegdemo_MediaPlayAPI_videoToAudio(JNIEnv *env, jclass type_,
                                                            jstring path_, jint type) {
    const char *path = env->GetStringUTFChars(path_, 0);
    av_register_all();
    AVFormatContext *avFormatContext = avformat_alloc_context();
    if(avformat_open_input(&avFormatContext,path,NULL,NULL)!=0){
        LOGE("文件打开失败")
        return;
    }
    if(avformat_find_stream_info(avFormatContext,NULL)<0){
        LOGE("获取文件信息失败")
        return;
    }
    int audio_index = -1,i=0;
    for(;i<avFormatContext->nb_streams;i++){
        if(avFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
            audio_index = i;
        }
    }
    AVCodecContext * audio_codec_ctt = avFormatContext->streams[audio_index]->codec;
    AVCodec* audio_codec = avcodec_find_decoder(audio_codec_ctt->codec_id);
    if(audio_codec==NULL){
        LOGE("解码器打开失败")
        return;
    }else{
        LOGE("解码器名称  %s",audio_codec->name)
    }
    if(avcodec_open2(audio_codec_ctt,audio_codec,NULL)!=0){
        LOGE("解码器打开失败")
        return;
    }
    AVPacket *audioPacket = (AVPacket*)av_malloc(sizeof(AVPacket));
    AVFrame *frame = av_frame_alloc();
    int got_frame = 0,index = 0,ret = 0;
    char fpath[100];
    std::string inPath(path);
    sprintf(fpath, "%s%s%s", inPath.substr(0,inPath.find_last_of("/") + 1).c_str(),
            inPath.substr(inPath.find_last_of("/") + 1,inPath.find_last_of(".") -
                    inPath.find_last_of("/")).c_str(),
            audio_codec->name);
    LOGI("保存地址：%s" ,fpath)
    FILE *file = fopen(fpath,"wb");
    while(av_read_frame(avFormatContext,audioPacket)>=0){
        if(audioPacket->stream_index == audio_index){
            LOGI("获取  %d",index++)
            ret = avcodec_decode_audio4(audio_codec_ctt,frame,&got_frame,audioPacket);
            if(ret<0){
                LOGI("完成")
            }
//            if(got_frame>0) {
//                fwrite(audioPacket->data, 1, audioPacket->size, file);
//            }
        }
        av_free_packet(audioPacket);
    }
    LOGI("转码完成")
    fclose(file);
    if(audio_codec_ctt!=NULL){
        avcodec_close(audio_codec_ctt);
        audio_codec_ctt = NULL;
    }
    avformat_close_input(&avFormatContext);
    avformat_free_context(avFormatContext);
    env->ReleaseStringUTFChars(path_, path);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_wqllj_ffmpegdemo_MediaPlayAPI_convertAudio(JNIEnv *env, jclass type_,
        jstring path_, jint type) {
const char *path = env->GetStringUTFChars(path_, 0);
    size_t MAX_AUDIO_FRAME_SIZE = 44100;
    av_register_all();
    AVFormatContext *avFormatContext = avformat_alloc_context();
    if(avformat_open_input(&avFormatContext,path,NULL,NULL)!=0){
        LOGE("音频打开失败")
        return;
    }
    if(avformat_find_stream_info(avFormatContext,NULL)<0){
        LOGE("获取音频信息失败")
        return;
    }
    int audio_index = -1;
    int i=0;
    for(;i<avFormatContext->nb_streams;i++){
        if(avFormatContext->streams[i]->codec->codec_type==AVMEDIA_TYPE_AUDIO){
            audio_index = i;
        }
    }
    AVCodecContext *audioCodecContext = avFormatContext->streams[audio_index]->codec;
    AVCodec *audio_codec = avcodec_find_decoder(audioCodecContext->codec_id);
    if(audio_codec == NULL){
        LOGE("获取解码器失败")
        return;
    }
    LOGI("编码器名称: %s",audio_codec->name)
    if(avcodec_open2(audioCodecContext,audio_codec,NULL)!=0){
        LOGE("打开解码器失败")
        return;
    }
    AVPacket *packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    AVFrame * frame = av_frame_alloc();
    SwrContext * swrContext = swr_alloc();
    enum AVSampleFormat inSampeFmt = audioCodecContext->sample_fmt;
    enum AVSampleFormat outSampeFmt = AV_SAMPLE_FMT_S16;
    int inSampleRate = audioCodecContext->sample_rate;
    int outSampleRate = MAX_AUDIO_FRAME_SIZE;
    uint64_t in_ch_layout = audioCodecContext->channel_layout;
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    swr_alloc_set_opts(swrContext,out_ch_layout,
    outSampeFmt,outSampleRate,
    in_ch_layout,inSampeFmt,inSampleRate,0,NULL);
    swr_init(swrContext);
    int out_ch_nb = av_get_channel_layout_nb_channels(out_ch_layout);

    uint8_t *outBuffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE*2);
    char fpath[100];
    std::string inPath(path);
    sprintf(fpath, "%s%s%s", inPath.substr(0,inPath.find_last_of("/") + 1).c_str(),inPath.substr(inPath.find_last_of("/") + 1,
                                                                                                inPath.find_last_of(".") -
                                                                                                inPath.find_last_of(
                                                                                                        "/")).c_str(),
            audioTyps[type]);
    LOGI("保存地址：%s" ,fpath)
    FILE *file = fopen(fpath,"wb");
    int got_frame =0,index = 0,ret;
    while(av_read_frame(avFormatContext,packet)>=0){
        if(packet->stream_index == audio_index) {
            ret = avcodec_decode_audio4(audioCodecContext, frame, &got_frame, packet);
            if (ret < 0) {
                LOGI("解码完成");
            }
            if (got_frame > 0) {
                LOGI("解压 %d", index++);
                swr_convert(swrContext, &outBuffer, MAX_AUDIO_FRAME_SIZE,
                            (const uint8_t **) frame->data, frame->nb_samples);
                int out_buffer_size = av_samples_get_buffer_size(NULL, out_ch_nb, frame->nb_samples,
                                                                 outSampeFmt, 1);
                fwrite(outBuffer, 1, out_buffer_size, file);
            }
        }
        av_free_packet(packet);
    }
    LOGI("转码完成")
    fclose(file);
    av_frame_free(&frame);
    av_free(outBuffer);
    swr_free(&swrContext);

    if(audioCodecContext!=NULL){
        avcodec_close(audioCodecContext);
        audioCodecContext = NULL;
    }
    avformat_close_input(&avFormatContext);
    env->ReleaseStringUTFChars(path_, path);
}
