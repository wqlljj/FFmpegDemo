package com.example.mediaplayer;

import android.Manifest;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.View;
import android.widget.Toast;

import com.leon.lfilepickerlibrary.LFilePicker;

import java.io.File;
import java.util.List;

import permissions.dispatcher.NeedsPermission;
import permissions.dispatcher.OnShowRationale;
import permissions.dispatcher.PermissionRequest;
import permissions.dispatcher.RuntimePermissions;

@RuntimePermissions
public class MainActivity extends AppCompatActivity implements View.OnClickListener {

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("avutil-54");
        System.loadLibrary("swresample-1");
        System.loadLibrary("avcodec-56");
        System.loadLibrary("avformat-56");
        System.loadLibrary("swscale-3");
        System.loadLibrary("postproc-53");
        System.loadLibrary("avfilter-5");
        System.loadLibrary("avdevice-56");
        System.loadLibrary("yuv");
        System.loadLibrary("native-lib");
    }
    private int MEIDAREQCODE = 1000;
    private int AUDIOREQCODE = 1001;

    private static final int ACTION_PLAY_VIDEO=100;
    private static final int ACTION_PLAY_AUDIO=101;
    private static final int ACTION_CONVERT_AUDIO_PCM=102;
    private int action;
    private String TAG = "MainActivity";
    private VideoView videoView;
    private HandlerThread handlerThread;
    private Handler playHandler;
    private Intent intent;
    private ServiceConnection conn;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        findViewById(R.id.play_video).setOnClickListener(this);
        findViewById(R.id.play_audio).setOnClickListener(this);
        findViewById(R.id.convert_audio_pcm).setOnClickListener(this);
        videoView = (VideoView)findViewById(R.id.video_view);
        handlerThread = new HandlerThread("player");
        handlerThread.start();
        playHandler = new Handler(handlerThread.getLooper());
//        startPlayService();
    }
    private void startPlayService() {
        intent = new Intent(this,PlayService.class);
        startService(intent);
    }
    @Override
    public void onClick(View v) {
        switch (v.getId()){
            case R.id.convert_audio_pcm:
                action = ACTION_CONVERT_AUDIO_PCM;
                chooseFile(AUDIOREQCODE);
                break;
            case R.id.play_audio:
                action = ACTION_PLAY_AUDIO;
                chooseFile(MEIDAREQCODE);
                break;
            case R.id.play_video:
                action = ACTION_PLAY_VIDEO;
                chooseFile(MEIDAREQCODE);
                break;
        }
    }
    private void handleAction(int action){
        String path = listPath.get(0);
        Log.i(TAG, "handleAction: "+path);
        toast(path);
        switch (action) {
            case ACTION_PLAY_AUDIO:
                Log.e(TAG, "handleAction: "+test() );
                MediaPlayAPI.play(path);
                break;
            case ACTION_PLAY_VIDEO:
                Surface surface = videoView.getHolder().getSurface();
                play(path, surface);
                break;
            case ACTION_CONVERT_AUDIO_PCM:
                MediaPlayAPI.convertAudio(path,1);
                break;
        }
    }
    @Override
    protected void onResume() {
        super.onResume();
        MainActivityPermissionsDispatcher.needWithPermissionCheck(this);
    }
    private void chooseFile(int chooseReqCode){
        String path ="" ;
        String[] filter = null;
        String title = "";
        if(chooseReqCode==MEIDAREQCODE) {
            path= Environment.getExternalStorageDirectory().getAbsolutePath()
                    + File.separator + "kugou"
                    + File.separator + "mv";
            filter = new String[]{".mp4",".avi",".flv",".wmv",".rmvb",".mov",".3gp",".swf"};
            title = "选择视频";
        }else if(chooseReqCode == AUDIOREQCODE){
            path = Environment.getExternalStorageDirectory().getAbsolutePath()
                    + File.separator + "kgmusic"
                    + File.separator + "download";
            filter = new String[]{".mp3",".aac","."};
            title = "选择音频";
        }
        if(!new File(path).exists()){
            path = Environment.getExternalStorageDirectory().getAbsolutePath();
        }
        LFilePicker filePicker = new LFilePicker()
                .withActivity(this)
                .withRequestCode(chooseReqCode)
                .withStartPath(path)
                .withTitle(title)
                .withFileFilter(filter);
        filePicker.start();
    }
    List<String> listPath;
    protected void onActivityResult(final int requestCode, int resultCode, Intent data) {
        if (requestCode == MEIDAREQCODE||requestCode == AUDIOREQCODE){
            if (data==null || !data.hasExtra("paths")){
                return;
            }
            listPath = data.getStringArrayListExtra("paths");
            if (listPath.size()>0){
                playHandler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        handleAction(action);
                    }
                },1000);
            }else{
                toast("未选择文件");
                Log.i(TAG, "onActivityResult: listPath.size()==0");
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    private void toast(final String msg) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                Toast.makeText(MainActivity.this,msg, Toast.LENGTH_SHORT).show();
            }
        });

    }

    public native void play(String path, Surface surface);
    public static native String test();
    @NeedsPermission({Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.WAKE_LOCK})
    void need() {
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        MainActivityPermissionsDispatcher.onRequestPermissionsResult(this, requestCode, grantResults);
    }

    @OnShowRationale({Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.WAKE_LOCK})
    void onShowRational(final PermissionRequest request) {
        request.proceed();
    }
    @Override
    protected void onDestroy() {
        super.onDestroy();
//        stopService(intent);
    }
}

