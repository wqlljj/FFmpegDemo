package com.example.wqllj.ffmpegdemo;

/**
 * Created by cloud on 2019/4/28.
 */

public class MediaPlayAPI {
    public static native void convertAudio(String path,int type);
    public static native void videoToAudio(String path,int type);
    public static native void play(String path);
}
