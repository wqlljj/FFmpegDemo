package com.example.wqllj.ffmpegdemo;

import android.Manifest;
import android.content.ComponentName;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.text.LoginFilter;
import android.util.Log;
import android.view.Surface;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

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

        // Example of a call to a native method
        findViewById(R.id.media_select).setOnClickListener(this);
        findViewById(R.id.audio_select).setOnClickListener(this);
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

    public native String stringFromJNI();
    public native void play(String path, Surface surface);

    @Override
    public void onClick(View v) {
        switch (v.getId()){
            case R.id.audio_select:
                chooseFile(AUDIOREQCODE);
                break;
            case R.id.media_select:
                chooseFile(MEIDAREQCODE);
                break;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        MainActivityPermissionsDispatcher.needWithPermissionCheck(this);
    }

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
    private void chooseFile(int chooseReqCode){
        String path ="" ;
        String[] filter = null;
        String title = "";
        if(chooseReqCode==MEIDAREQCODE) {
                path=Environment.getExternalStorageDirectory().getAbsolutePath()
                    + File.separator + "kugou"
                    + File.separator + "mv";
            filter = new String[]{".mp4",".avi","."};
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
                        final String path = listPath.get(0);
                        for (String s : listPath) {
                            Log.d(TAG, "onActivityResult: "+s);
                        }
                        if(requestCode == MEIDAREQCODE) {
                            Surface surface = videoView.getHolder().getSurface();
                            play(path, surface);
//                            MediaPlayAPI.play(path);
                        }else if(requestCode == AUDIOREQCODE){
                            MediaPlayAPI.convertAudio(path,1);
                        }
                        Log.d(TAG, "onActivityResult: path = "+path);
                    }
                },1000);
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
//        stopService(intent);
    }
}
