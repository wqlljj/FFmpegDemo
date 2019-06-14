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
#include "queue.h"

extern "C" {
#include <libyuv.h>
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"
#include "libavutil/time.h"
#include <libswresample/swresample.h>
}

#define LOGI(FORMAT, ...) __android_log_print(ANDROID_LOG_INFO,"jason",FORMAT,##__VA_ARGS__);
#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"jason",FORMAT,##__VA_ARGS__);
#define MAX_STREAM 2
#define PACKET_QUEUE_SIZE 50
#define MAX_AUDIO_FRAME_SIZE 48000*4
typedef struct _DecoderData DecoderData;
#define MIN_SLEEP_TIME_US 1000ll
#define AUDIO_TIME_ADJUST_US -200000ll
struct Player{
    JavaVM *javaVM;
    AVFormatContext *input_format_ctx;
    int video_index;
    int audio_index;
    //流的总个数
    int streams_num ;
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

    pthread_t thread_read_from_stream;
    Queue *packets[MAX_STREAM];

    //互斥锁
    pthread_mutex_t mutex;
    //条件变量
    pthread_cond_t cond;

    int64_t start_time;
    int64_t audio_clock;
};

struct _DecoderData{
    Player *player;
    int stream_index;
};

void* player_read_from_stream(void* data);

void* player_fill_packet();

void init_input_format_ctx(Player *pPlayer, const char *cstr);

void init_codec_context(Player *pPlayer,int index);

void decode_video_pre(JNIEnv *pEnv, Player *pPlayer, jobject pJobject);
void *decode_data(void* arg);

void decode_video(Player *pPlayer, AVPacket *pPacket);

void decode_audio_pre(Player *pPlayer);

void jni_audiotrack_init(JNIEnv *pEnv,  Player *pPlayer);

void decode_audio(Player *pPlayer, AVPacket *pPacket);

/**
 * 初始化音频，视频AVPacket队列，长度50
 */
void player_alloc_queues(Player *player);

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
    player_alloc_queues(player);

    pthread_mutex_init(&player->mutex,NULL);
    pthread_cond_init(&player->cond,NULL);

    //生产者线程
    pthread_create(&(player->thread_read_from_stream),NULL,player_read_from_stream,(void*)player);
    sleep(1);

    player->start_time = 0;

    //消费者线程
    DecoderData data1 = {player,player->video_index}, *decoder_data1 = &data1;
    pthread_create(&(player->decode_threads[player->video_index]),NULL,decode_data,(void*)decoder_data1);

    DecoderData data2 = {player,player->audio_index}, *decoder_data2 = &data2;
    pthread_create(&(player->decode_threads[player->audio_index]),NULL,decode_data,(void*)decoder_data2);


    pthread_join(player->thread_read_from_stream,NULL);
    pthread_join(player->decode_threads[player->video_index],NULL);
    pthread_join(player->decode_threads[player->audio_index],NULL);

//    pthread_create(&(player->decode_threads[player->video_index]),NULL,decode_data,(void*)player);
//    pthread_create(&(player->decode_threads[player->audio_index]),NULL,decode_data,(void*)player);
}

/**
 * 生产者线程：负责不断的读取视频文件中AVPacket，分别放入两个队列中
 */
void* player_read_from_stream(void* data){
    Player* player = (Player *) data;
    int index = 0;
    int ret;
    //栈内存上保存一个AVPacket
    AVPacket packet, *pkt = &packet;
    for(;;){
        ret = av_read_frame(player->input_format_ctx,pkt);
        //到文件结尾了
        if(ret < 0){
            break;
        }
        //根据AVpacket->stream_index获取对应的队列
        Queue *queue = player->packets[pkt->stream_index];

        //示范队列内存释放
        //queue_free(queue,packet_free_func);
        pthread_mutex_lock(&player->mutex);
        //将AVPacket压入队列
        AVPacket *packet_data = (AVPacket *) queue_push(queue, &player->mutex, &player->cond);
        //拷贝（间接赋值，拷贝结构体数据）
        *packet_data = packet;
        pthread_mutex_unlock(&player->mutex);
        LOGI("queue:%#x, packet:%#x",queue,packet);
    }
}
/**
 * 初始化音频，视频AVPacket队列，长度50
 */
void player_alloc_queues(Player *player){
    int i;
    //这里，正常是初始化两个队列
    for (i = 0; i < player->streams_num; ++i) {
        Queue *queue = queue_init(PACKET_QUEUE_SIZE,(queue_fill_func)player_fill_packet);
        player->packets[i] = queue;
        //打印视频音频队列地址
        LOGI("stream index:%d,queue:%#x",i,queue);
    }
}
/**
 * 给AVPacket开辟空间，后面会将AVPacket栈内存数据拷贝至这里开辟的空间
 */
void* player_fill_packet(){
    //请参照我在vs中写的代码
    AVPacket *packet = (AVPacket *) malloc(sizeof(AVPacket));
    return packet;
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

/**
 * 获取视频当前播放时间
 */
int64_t player_get_current_video_time(Player *player) {
    int64_t current_time = av_gettime();
    return current_time - player->start_time;
}
/**
 * 延迟
 */
void player_wait_for_frame(Player *player, int64_t stream_time,
                           int stream_no) {
    pthread_mutex_lock(&player->mutex);
    for(;;){
        int64_t current_video_time = player_get_current_video_time(player);
        int64_t sleep_time = stream_time - current_video_time;
        if (sleep_time < -300000ll) {
            // 300 ms late
            int64_t new_value = player->start_time - sleep_time;
            LOGI("player_wait_for_frame[%d] correcting %f to %f because late",
                 stream_no, (av_gettime() - player->start_time) / 1000000.0,
                 (av_gettime() - new_value) / 1000000.0);

            player->start_time = new_value;
            pthread_cond_broadcast(&player->cond);
        }

        if (sleep_time <= MIN_SLEEP_TIME_US) {
            // We do not need to wait if time is slower then minimal sleep time
            break;
        }

        if (sleep_time > 500000ll) {
            // if sleep time is bigger then 500ms just sleep this 500ms
            // and check everything again
            sleep_time = 500000ll;
        }
        //等待指定时长
        int timeout_ret = pthread_cond_timeout_np(&player->cond,
                                                  &player->mutex, sleep_time/1000ll);

        // just go further
        LOGI("player_wait_for_frame[%d] finish", stream_no);
    }
    pthread_mutex_unlock(&player->mutex);
}
/**
 * 解码子线程函数（消费）
 */
void* decode_data(void* arg){
    DecoderData *decoder_data = (DecoderData*)arg;
    Player *player = decoder_data->player;
    int stream_index = decoder_data->stream_index;
    //根据stream_index获取对应的AVPacket队列
    Queue *queue = player->packets[stream_index];

    AVFormatContext *format_ctx = player->input_format_ctx;
    //编码数据

    //6.一阵一阵读取压缩的视频数据AVPacket
    int video_frame_count = 0, audio_frame_count = 0;
    for(;;){
        //消费AVPacket
        pthread_mutex_lock(&player->mutex);
        AVPacket *packet = (AVPacket*)queue_pop(queue,&player->mutex,&player->cond);
        pthread_mutex_unlock(&player->mutex);
        if(stream_index == player->video_index){
            decode_video(player,packet);
            LOGI("video_frame_count:%d",video_frame_count++);
        }else if(stream_index == player->audio_index){
            decode_audio(player,packet);
            LOGI("audio_frame_count:%d",audio_frame_count++);
        }

    }
}
//void *decode_data(void* arg){
//    Player* player = (Player*)arg;
//    pthread_t tid = pthread_self();
//    int index= -1;
//    if(tid==player->decode_threads[player->video_index]){
//        index=player->video_index;
//    }else{
//        index = player->audio_index;
//    }
//    AVFormatContext* format_ctx = player->input_format_ctx;
//    AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
//    int video_frame_count = 0;
//    while( av_read_frame(format_ctx,packet)>=0){
//        if(packet->stream_index == index&&packet->stream_index == player->video_index){
//            decode_video(player,packet);
//            LOGI("video_frame_count:%d",video_frame_count++)
//        }else if(packet->stream_index == index&&packet->stream_index ==player->audio_index){
//            decode_audio(player,packet);
//            LOGI("audio_frame_count:%d",video_frame_count++)
//        }else{
//            LOGI("CANCEL %d",index)
//        }
//        av_free_packet(packet);
//    }
//}

void decode_audio(Player *player, AVPacket *packet) {
    AVFormatContext *input_format_ctx = player->input_format_ctx;
    AVStream *stream = input_format_ctx->streams[player->video_index];

    AVCodecContext* codecContext = player->input_codec_ctx[player->audio_index];
    AVFrame* frame = av_frame_alloc();
    int got_frame;
    avcodec_decode_audio4(codecContext,frame,&got_frame,packet);
    uint8_t *out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE);
    if(got_frame>0){
        swr_convert(player->swr_ctx, &out_buffer,MAX_AUDIO_FRAME_SIZE,
                    (const uint8_t **) frame->data, frame->nb_samples);
        int out_buffer_size = av_samples_get_buffer_size(NULL,player->out_channal_nb,frame->nb_samples,player->out_sample_fmt,1);

        int64_t pts = packet->pts;
        if (pts != AV_NOPTS_VALUE) {
            player->audio_clock = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
            //				av_q2d(stream->time_base) * pts;
            LOGI("player_write_audio - read from pts");
            player_wait_for_frame(player,
                                  player->audio_clock + AUDIO_TIME_ADJUST_US, player->audio_index);
        }

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
//        usleep(16000);
    }
    av_frame_free(&frame);
}

void decode_video(Player *player, AVPacket *packet) {
    AVFrame* yuv_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    AVFormatContext *input_format_ctx = player->input_format_ctx;
    AVStream *stream = input_format_ctx->streams[player->video_index];
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
        //计算延迟
        int64_t pts = av_frame_get_best_effort_timestamp(yuv_frame);
        //转换（不同时间基时间转换）
        int64_t time = av_rescale_q(pts,stream->time_base,AV_TIME_BASE_Q);

        player_wait_for_frame(player,time,player->video_index);

        ANativeWindow_unlockAndPost(player->nativeWindow);
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
    player->streams_num = format_ctx->nb_streams;
    LOGI("captrue_streams_no:%d",player->streams_num);
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
