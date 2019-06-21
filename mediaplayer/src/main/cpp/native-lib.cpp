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
//    int frame_nb[MAX_STREAM];
//    int current_frame_nb[MAX_STREAM];
//    AVCodecContext *input_codec_ctx[MAX_STREAM];
//    pthread_t decode_threads[MAX_STREAM];
//    Queue *packets[MAX_STREAM];
    //每个流已读或总帧数
    int *frame_nb;
    //每个流已解码帧数
    int *current_frame_nb;
    AVCodecContext **input_codec_ctx;
    pthread_t *decode_threads;
    Queue **packets;

    bool isReadFinish = false;
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

void init_input_format_ctx(Player *player, const char *path);

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
    init_input_format_ctx(player, input_cstr);
    int frame_nb[player->streams_num];
    int current_frame_nb[player->streams_num];
    AVCodecContext *input_codec_ctx[player->streams_num];
    pthread_t decode_threads[player->streams_num];
    Queue *packets[player->streams_num];
    player->frame_nb = frame_nb;
    player->current_frame_nb = current_frame_nb;
    player->input_codec_ctx = input_codec_ctx;
    player->decode_threads = decode_threads;
    player->packets = packets;
    init_codec_context(player,player->video_index);
    init_codec_context(player,player->audio_index);

    decode_video_pre(env,player,surface);
    decode_audio_pre(player);

    jni_audiotrack_init(env,player);
    player_alloc_queues(player);

    pthread_mutex_init(&player->mutex,NULL);
    pthread_cond_init(&player->cond,NULL);
    player->isReadFinish = false;
    AVFormatContext *format_ctx = player->input_format_ctx;
    for(int i=0;i<player->streams_num;i++){
        player->frame_nb[i] = format_ctx->streams[i]->nb_frames;
        player->current_frame_nb[i] = 0;
    }
    //生产者线程
    pthread_create(&(player->thread_read_from_stream),NULL,player_read_from_stream,(void*)player);
    usleep(500000);

    player->start_time = 0;
    //todo 解码所有流
    //消费者线程
    DecoderData data1 = {player,player->video_index}, *decoder_data1 = &data1;
    LOGI("pthread_create decode_data %d  %#x",player->video_index,player->decode_threads[player->video_index])
    pthread_create(&(player->decode_threads[player->video_index]),NULL,decode_data,(void*)decoder_data1);

    DecoderData data2 = {player,player->audio_index}, *decoder_data2 = &data2;
    LOGI("pthread_create decode_data %d  %#x",player->audio_index,player->decode_threads[player->audio_index])
    pthread_create(&(player->decode_threads[player->audio_index]),NULL,decode_data,(void*)decoder_data2);


    pthread_join(player->thread_read_from_stream,NULL);
    pthread_join(player->decode_threads[player->video_index],NULL);
    pthread_join(player->decode_threads[player->audio_index],NULL);
}

/**
 * 生产者线程：负责不断的读取视频文件中AVPacket，分别放入两个队列中
 */
void* player_read_from_stream(void* data){
    Player* player = (Player *) data;
    int ret;
    //栈内存上保存一个AVPacket
    AVPacket packet, *pkt = &packet;
    for(;;){
        ret = av_read_frame(player->input_format_ctx,pkt);
        //到文件结尾了
        if(ret < 0){
            LOGI("read frame finish")
            player->isReadFinish = true;
            goto end;
        }
        //根据AVpacket->stream_index获取对应的队列
        Queue *queue = player->packets[pkt->stream_index];
        player->frame_nb[pkt->stream_index]+=1;
        if(pkt->stream_index == player->video_index){
            LOGI("read video_frame_count:%d", player->frame_nb[pkt->stream_index])
        }else{
            LOGI("read audio_frame_count:%d", player->frame_nb[pkt->stream_index])
        }
        //示范队列内存释放
//        queue_free(queue,packet_free_func);
        pthread_mutex_lock(&player->mutex);
        //将AVPacket压入队列
        AVPacket *packet_data = (AVPacket *) queue_push(queue, &player->mutex, &player->cond);
        //拷贝（间接赋值，拷贝结构体数据）

        *packet_data = packet;
        pthread_mutex_unlock(&player->mutex);
//        LOGI("queue:%#x, packet:%#x",queue,packet);
    }
    end:
    return 0;
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
//        LOGI("stream index:%d,queue:%#x",i,queue);
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
    LOGI("decode_audio_pre audio_index:%d  %d %s",player->audio_index,player->out_sample_rate,codecContext->codec->name)
    uint64_t in_ch_layout = codecContext->channel_layout;
    uint64_t  out_ch_layout = AV_CH_LAYOUT_STEREO;
    LOGI("swresample swr_alloc")
    player->swr_ctx = swr_alloc();
    LOGI("swresample swr_alloc_set_opts")
    swr_alloc_set_opts(player->swr_ctx,out_ch_layout,player->out_sample_fmt,player->out_sample_rate,
    in_ch_layout,player->in_sample_fmt,player->in_sample_rate,0,NULL);
    LOGI("swresample swr_init")
    swr_init(player->swr_ctx);
    player->out_channal_nb = av_get_channel_layout_nb_channels(out_ch_layout);


}

/**
 * 获取视频当前播放时间
 */
int64_t player_get_current_video_time(Player *player) {
    int64_t current_time = av_gettime();
    if(player->start_time==0){
        player->start_time = current_time;
    }
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

        LOGI("stream_time:%lld current_video_time:%lld sleep_time:%lld stream_no:%d",stream_time,current_video_time,sleep_time,stream_no)

        if (sleep_time <= MIN_SLEEP_TIME_US) {
            LOGI("goto end %d",stream_no)
            goto end;
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
        LOGI("stream_time player_wait_for_frame[%d] finish sleep_time =  %lld  stream_no : %d", stream_no ,sleep_time,stream_no)
    }
    end:pthread_mutex_unlock(&player->mutex);
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
    LOGI("decode_data %d  %#x",stream_index,queue)
    for(;;){
        if(player->isReadFinish&&(player->current_frame_nb[stream_index]>=player->frame_nb[stream_index])){
            LOGI("decode finish stream_index:%d  %d  %d",stream_index,player->current_frame_nb[stream_index],player->frame_nb[stream_index])
            break;
        }
        //消费AVPacket
        pthread_mutex_lock(&player->mutex);
        LOGI("decode  A")
        AVPacket *packet = (AVPacket*)queue_pop(queue,&player->mutex,&player->cond);
        player->current_frame_nb[stream_index]+=1;
        LOGI("decode  B")
        pthread_mutex_unlock(&player->mutex);
        LOGI("decode %d  %d %d",stream_index,player->video_index,player->audio_index)
        if(stream_index == player->video_index){
            decode_video(player,packet);
            LOGI("decode video_frame_count:%d",player->current_frame_nb[stream_index]);
        }else if(stream_index == player->audio_index){
            decode_audio(player,packet);
            LOGI("decode audio_frame_count:%d ",player->current_frame_nb[stream_index]);
        }

    }
    LOGI("decode_data %d end",stream_index)
    return 0;
}

void decode_audio(Player *player, AVPacket *packet) {
    AVFormatContext *input_format_ctx = player->input_format_ctx;
    AVStream *stream = input_format_ctx->streams[player->audio_index];
    LOGI("audio time_base.den : %d %lld",stream->time_base.den,stream->nb_frames)

    AVCodecContext* codecContext = player->input_codec_ctx[player->audio_index];
    AVFrame* frame = av_frame_alloc();
    int got_frame;
    avcodec_decode_audio4(codecContext,frame,&got_frame,packet);
    uint8_t *out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE);
    if(got_frame>0){
        swr_convert(player->swr_ctx, &out_buffer,MAX_AUDIO_FRAME_SIZE,
                    (const uint8_t **) frame->data, frame->nb_samples);
//        LOGI("swresample swr_convert 2")
        int out_buffer_size = av_samples_get_buffer_size(NULL,player->out_channal_nb,frame->nb_samples,player->out_sample_fmt,1);

        int64_t pts = packet->pts;
//        LOGI("audio_frame %lld  %lld",pts,AV_NOPTS_VALUE)
        if (pts != AV_NOPTS_VALUE) {
            player->audio_clock = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
            double time = 				av_q2d(stream->time_base) * pts;//av_q2d计算一帧时间
            LOGI("audio_frame wait  %lld %f  %d", player->audio_clock,time,player->audio_index);
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
        av_free(out_buffer);
    }
    av_frame_free(&frame);
}
void packet_free_func(void* packet){
    AVPacket *packet1 = (AVPacket *)packet;
    av_free_packet(packet1);
};
typedef struct
{
    unsigned int   bfSize;
    unsigned short bfReserved1;
    unsigned short bfReserved2;
    unsigned int   bfOffBits;
} MyBITMAPFILEHEADER;
typedef struct
{
    unsigned int   biSize;
    int            biWidth;
    int            biHeight;
    unsigned short biPlanes;
    unsigned short biBitCount;
    unsigned int   biCompression;
    unsigned int   biSizeImage;
    int            biXPelsPerMeter;
    int            biYPelsPerMeter;
    unsigned int   biClrUsed;
    unsigned int   biClrImportant;
} MyBITMAPINFOHEADER;
void savebmp(Player *player,unsigned char *rgbbuf,const char *filename,int width,int height)
{
    int bpp = 32;
    MyBITMAPFILEHEADER bfh;
    MyBITMAPINFOHEADER bih;
    unsigned short bfType=0x4d42;
    bfh.bfReserved1=0;
    bfh.bfReserved2=0;
    bfh.bfSize=2+sizeof(MyBITMAPFILEHEADER)+sizeof(MyBITMAPINFOHEADER)+width*height*bpp/8;
    bfh.bfOffBits=0x36;
    bih.biSize=sizeof(MyBITMAPINFOHEADER);
    bih.biWidth=width;
    bih.biHeight=height;
    bih.biPlanes=1;
    bih.biBitCount=bpp;
    bih.biCompression=0;
    bih.biSizeImage=0;
    bih.biXPelsPerMeter=5000;
    bih.biYPelsPerMeter=5000;
    bih.biClrUsed=0;
    bih.biClrImportant=0;
    FILE *file=fopen(filename,"wb");
    if(!file){
        LOGE("Could not write file")
        return;
    }
    fwrite(&bfType,sizeof(bfType),1,file);
    fwrite(&bfh,sizeof(bfh),1,file);
    fwrite(&bih,sizeof(bih),1,file);
    fwrite(rgbbuf,width*height*bpp/8,1,file);
    fclose(file);
}
void decode_video(Player *player, AVPacket *packet) {
    AVFrame* yuv_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    AVFormatContext *input_format_ctx = player->input_format_ctx;
    AVStream *stream = input_format_ctx->streams[player->video_index];
    LOGI("video time_base.den : %d %lld",stream->time_base.den,stream->nb_frames)
    ANativeWindow_Buffer out_buffer;
    AVCodecContext *codec_ctx = player->input_codec_ctx[player->video_index];
    int gotFrame;
    avcodec_decode_video2(codec_ctx,yuv_frame,&gotFrame,packet);
    SwsContext * swsContext = sws_getContext(codec_ctx->width,codec_ctx->height,codec_ctx->pix_fmt,
                   codec_ctx->width,codec_ctx->height,AV_PIX_FMT_RGBA,//PIX_FMT_BGR24  AV_PIX_FMT_RGBA
                   SWS_BICUBIC,NULL,NULL,NULL);
    if(gotFrame){
        ANativeWindow_setBuffersGeometry(player->nativeWindow,
        codec_ctx->width,codec_ctx->height,WINDOW_FORMAT_RGBA_8888);
        ANativeWindow_lock(player->nativeWindow,&out_buffer,NULL);
        avpicture_fill((AVPicture*)rgb_frame,
                       (const uint8_t *) out_buffer.bits, AV_PIX_FMT_RGBA, codec_ctx->width, codec_ctx->height);
        //转换为rgb格式
        sws_scale(swsContext,(const uint8_t *const *)yuv_frame->data,yuv_frame->linesize,0,
                  yuv_frame->height,rgb_frame->data,
                  rgb_frame->linesize);

        LOGE("frame 宽%d,高%d",yuv_frame->width,yuv_frame->height);
        LOGE("rgb格式 宽%d,高%d",rgb_frame->width,rgb_frame->height);
        if(yuv_frame->key_frame) {
            char file[50];
            sprintf(file, "/storage/emulated/0/kugou/mv/bmp/%d.bmp",
                    player->current_frame_nb[player->video_index]);
            savebmp(player, rgb_frame->data[0], file, yuv_frame->width, yuv_frame->height);
        }
//        libyuv::I420ToARGB(yuv_frame->data[0], yuv_frame->linesize[0],
//                                   yuv_frame->data[2], yuv_frame->linesize[2],
//                                   yuv_frame->data[1], yuv_frame->linesize[1],
//                                   rgb_frame->data[0], rgb_frame->linesize[0],
//                                   codec_ctx->width, codec_ctx->height);
        LOGI("video width:%d height:%d linesize:%d",codec_ctx->width, codec_ctx->height,rgb_frame->linesize[0])
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
    LOGI("解码器名称：%s  %d",codec->name,index)
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
    player->streams_num = 2;
    LOGI("captrue_streams_no:%d",player->streams_num);
    int i;
    for(i=0;i<player->streams_num;i++){
        if(format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO){
            LOGI("video_index = %d",i)
            player->video_index = i;
        }
        if(format_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO){
            LOGI("audio_index = %d",i)
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
