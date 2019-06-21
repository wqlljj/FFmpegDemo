package com.example.mediaplayer;

import android.media.AudioFormat;
import android.media.AudioTrack;
import android.os.Build;
import android.util.Log;

import java.io.File;

/**
 * Created by cloud on 2019/4/28.
 */

public class MediaPlayAPI {

    private static String TAG = "MediaPlayAPI";

    public static native void convertAudio(String path, int type);
    public static native void videoToAudio(String path,int type);
    public static native void play(String path);
    public static AudioTrack createAudioTrack(int sampleRate,int ch_nb){
        int audioFormat = AudioFormat.ENCODING_PCM_16BIT;
        Log.i(TAG, "createAudioTrack: "+ch_nb);
        int channelConfig;
        switch (ch_nb){
            case 1:
                channelConfig = AudioFormat.CHANNEL_OUT_MONO;
                break;
            case 2:
                channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
                break;
            default:
                channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
                break;
        }
        int bufferSize = AudioTrack.getMinBufferSize(sampleRate,channelConfig,audioFormat);
        AudioTrack audioTrack = new AudioTrack(AudioTrack.MODE_STATIC,
                sampleRate,channelConfig,audioFormat,
                bufferSize,AudioTrack.MODE_STREAM);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            audioTrack.setVolume(0.05f);
        }else{
            audioTrack.setStereoVolume(0.05f,0.05f);
        }
        if(!new File("/storage/emulated/0/kugou/mv/bmp/").exists()){
           mkdir("/storage/emulated/0/kugou/mv/bmp/");
        }
        return audioTrack;
    }

    public static void mkdir(String file){
        File f = new File(file);
        if(!f.exists()){
            f.mkdirs();
        }
    }
}
