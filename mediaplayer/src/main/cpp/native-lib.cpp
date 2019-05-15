#include <jni.h>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <android/native_window.h>
#include <pthread.h>
#include <sys/syscall.h>

extern "C" {
#include <libyuv.h>
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include <libswresample/swresample.h>
}

#define LOGI(FORMAT, ...) __android_log_print(ANDROID_LOG_INFO,"jason",FORMAT,##__VA_ARGS__);
#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"jason",FORMAT,##__VA_ARGS__);
#define MAX_STREAM 2
#define MAX_AUDIO_FRAME_SIZE 48000*4
struct Player{
    JavaVM *javaVM;
    AVFormatContext *input_format_ctx;
    int video_index;
    int audio_index;
    AVCodecContext *input_codec_ctx[MAX_STREAM];

    pthread_t decode_threads[MAX_STREAM];
    ANativeWindow* nativeWindow;

    SwrContext* swr_ctx;
    AVSampleFormat in_sample_fmt;
    AVSampleFormat out_sample_fmt;
    int in_sample_rate;
    int out_sample_rate;
    int out_channal_nb;

    jobject audio_track;
    jmethodID audio_track_write_mid;
};

void init_input_format_ctx(Player *pPlayer, const char *cstr);

void init_codec_context(Player *pPlayer,int index);

void decode_video_pre(JNIEnv *pEnv, Player *pPlayer, jobject pJobject);
void *decode_data(void* arg);

void decode_video(Player *pPlayer, AVPacket *pPacket);

void decode_audio_pre(Player *pPlayer);

void jni_audiotrack_init(JNIEnv *pEnv,  Player *pPlayer);

void decode_audio(Player *pPlayer, AVPacket *pPacket);

bool check(JNIEnv *env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        LOGI("JNI 异常")
        return true;
    }
    return false;
}


extern "C"
JNIEXPORT void JNICALL Java_com_example_mediaplayer_MainActivity_play
        (JNIEnv *env, jobject jobj, jstring input_jstr, jobject surface) {
    const char *input_cstr = env->GetStringUTFChars(input_jstr, NULL);
    LOGI("PLAY %s", input_cstr);
    struct Player* player = (struct Player*)malloc(sizeof(struct Player));
    env->GetJavaVM(&(player->javaVM));
    init_input_format_ctx(player,input_cstr);
    init_codec_context(player,player->video_index);
    init_codec_context(player,player->audio_index);

    decode_video_pre(env,player,surface);
    decode_audio_pre(player);

    jni_audiotrack_init(env,player);

    pthread_create(&(player->decode_threads[player->video_index]),NULL,decode_data,(void*)player);
    pthread_create(&(player->decode_threads[player->audio_index]),NULL,decode_data,(void*)player);
}

void jni_audiotrack_init(JNIEnv *env, Player *player) {
    jclass media_player_api_class = env->FindClass("com/example/mediaplayer/MediaPlayAPI");
    jmethodID createAudioTrack = env->GetStaticMethodID(media_player_api_class,"createAudioTrack","(II)Landroid/media/AudioTrack;");
    jobject audio_track = env->CallStaticObjectMethod(media_player_api_class,createAudioTrack,player->out_sample_rate
            ,player->out_channal_nb);

    jclass  audio_track_cls = env->GetObjectClass(audio_track);
    jmethodID play_mid = env->GetMethodID(audio_track_cls,"play","()V");
    env->CallVoidMethod(audio_track,play_mid);

    jmethodID write_mid = env->GetMethodID(audio_track_cls,"write","([BII)I");

    player->audio_track = env->NewGlobalRef(audio_track);
    player->audio_track_write_mid = write_mid;
}

void decode_audio_pre(Player *player) {
    AVCodecContext* codecContext = player->input_codec_ctx[player->audio_index];
    player->out_sample_fmt = AV_SAMPLE_FMT_S16;
    player->in_sample_fmt = codecContext->sample_fmt;
    player->in_sample_rate = player->out_sample_rate = codecContext->sample_rate;
    uint64_t in_ch_layout = codecContext->channel_layout;
    uint64_t  out_ch_layout = AV_CH_LAYOUT_STEREO;
    player->swr_ctx = swr_alloc();
    swr_alloc_set_opts(player->swr_ctx,out_ch_layout,player->out_sample_fmt,player->out_sample_rate,
    in_ch_layout,player->in_sample_fmt,player->in_sample_rate,0,NULL);
    swr_init(player->swr_ctx);
    player->out_channal_nb = av_get_channel_layout_nb_channels(out_ch_layout);


}

void *decode_data(void* arg){
    Player* player = (Player*)arg;
    pthread_t tid = pthread_self();
    int index= -1;
    if(tid==player->decode_threads[player->video_index]){
        index=player->video_index;
    }else{
        index = player->audio_index;
    }
    AVFormatContext* format_ctx = player->input_format_ctx;
    AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    int video_frame_count = 0;
    while( av_read_frame(format_ctx,packet)>=0){
        if(packet->stream_index == index&&packet->stream_index == player->video_index){
            decode_video(player,packet);
            LOGI("video_frame_count:%d",video_frame_count++)
        }else if(packet->stream_index == index&&packet->stream_index ==player->audio_index){
            decode_audio(player,packet);
            LOGI("audio_frame_count:%d",video_frame_count++)
        }else{
            LOGI("CANCEL %d",index)
        }
        av_free_packet(packet);
    }
}

void decode_audio(Player *player, AVPacket *packet) {
    AVCodecContext* codecContext = player->input_codec_ctx[player->audio_index];
    AVFrame* frame = av_frame_alloc();
    int got_frame;
    avcodec_decode_audio4(codecContext,frame,&got_frame,packet);
    uint8_t *out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE);
    if(got_frame>0){
        swr_convert(player->swr_ctx, &out_buffer,MAX_AUDIO_FRAME_SIZE,
                    (const uint8_t **) frame->data, frame->nb_samples);
        int out_buffer_size = av_samples_get_buffer_size(NULL,player->out_channal_nb,frame->nb_samples,player->out_sample_fmt,1);
        JavaVM* javaVM = player->javaVM;
        JNIEnv* env;
        javaVM->AttachCurrentThread(&env,NULL);
        jbyteArray audio_sample_array = env->NewByteArray(out_buffer_size);
        jbyte *sample_bytes = env->GetByteArrayElements(audio_sample_array,NULL);
        memcpy(sample_bytes,out_buffer,out_buffer_size);
        env->ReleaseByteArrayElements(audio_sample_array,sample_bytes,0);
        env->CallIntMethod(player->audio_track, player->audio_track_write_mid,
                           audio_sample_array, 0, out_buffer_size);
        env->DeleteLocalRef(audio_sample_array);

        javaVM->DetachCurrentThread();
        usleep(16000);
    }
    av_frame_free(&frame);
}

void decode_video(Player *player, AVPacket *packet) {
    AVFrame* yuv_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    ANativeWindow_Buffer out_buffer;
    AVCodecContext *codec_ctx = player->input_codec_ctx[player->video_index];
    int gotFrame;
    avcodec_decode_video2(codec_ctx,yuv_frame,&gotFrame,packet);
    if(gotFrame){
        ANativeWindow_setBuffersGeometry(player->nativeWindow,
        codec_ctx->width,codec_ctx->height,WINDOW_FORMAT_RGBA_8888);
        ANativeWindow_lock(player->nativeWindow,&out_buffer,NULL);
        avpicture_fill((AVPicture*)rgb_frame,
                       (const uint8_t *) out_buffer.bits, AV_PIX_FMT_RGBA, codec_ctx->width, codec_ctx->height);
        libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
                                   yuv_frame->data[2], yuv_frame->linesize[2],
                                   yuv_frame->data[1], yuv_frame->linesize[1],
                                   rgb_frame->data[0], rgb_frame->linesize[0],
                                   codec_ctx->width, codec_ctx->height);
        LOGI("%d   %d %d",codec_ctx->width, codec_ctx->height,rgb_frame->linesize[0])
        ANativeWindow_unlockAndPost(player->nativeWindow);
        usleep(16000);
    }
    av_frame_free(&yuv_frame);
    av_frame_free(&rgb_frame);
}

void decode_video_pre(JNIEnv *pEnv, Player *player, jobject pJobject) {
    player->nativeWindow = ANativeWindow_fromSurface(pEnv,pJobject);
}

void init_codec_context(Player *player,int index) {
    AVFormatContext* formatContext = player->input_format_ctx;
    AVCodecContext* codecContext = formatContext->streams[index]->codec;
    if(index==player->video_index) {
        LOGE("视频宽高 %d  %d", codecContext->width, codecContext->height)
    }
    AVCodec* codec = avcodec_find_decoder(codecContext->codec_id);

    if(codec == NULL||avcodec_open2(codecContext, codec, NULL) < 0){
        LOGE("解码器打开失败")
        return;
    }
    player->input_codec_ctx[index] = codecContext;
}

void init_input_format_ctx(Player *player, const char *path) {
    av_register_all();
    AVFormatContext* format_ctx = avformat_alloc_context();
    if(avformat_open_input(&format_ctx,path,NULL,NULL)!=0){
        LOGE("打开视频文件失败")
        return;
    }
    if(avformat_find_stream_info(format_ctx,NULL)<0){
        LOGE("获取视频信息失败")
        return;
    }
    int i;
    for(i=0;i<format_ctx->nb_streams;i++){
        if(format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            player->video_index = i;
        }
        if(format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
            player->audio_index = i;
        }
    }
    player->input_format_ctx = format_ctx;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_wqllj_ffmpegdemo_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}
