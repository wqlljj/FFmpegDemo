package com.example.wqllj.ffmpegdemo;

import android.Manifest;
import android.content.Intent;
import android.os.Environment;
import android.support.annotation.NonNull;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
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
    private int chooseReqCode = 1000;
    private String TAG = "MainActivity";
    private VideoView videoView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Example of a call to a native method
        Button tv = (Button) findViewById(R.id.select);
        tv.setText(stringFromJNI());
        tv.setOnClickListener(this);
        videoView = (VideoView)findViewById(R.id.video_view);
    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native void play(String path, Surface surface);

    @Override
    public void onClick(View v) {
        new Thread(){
            @Override
            public void run() {
                super.run();
                Surface surface = videoView.getHolder().getSurface();
                play("/storage/emulated/0/kugou/mv/光年之外-art--G.E.M.邓紫棋--art-14a694dd09e9f655f0485b4d06a1ac4f.mp4",
                        surface);
            }
        }.start();

//        chooseFile();
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
    private void chooseFile(){
        String path = Environment.getExternalStorageDirectory().getAbsolutePath()
                + File.separator + "kugou"
                + File.separator + "mv";
        if(!new File(path).exists()){
            path = Environment.getExternalStorageDirectory().getAbsolutePath();
        }
        LFilePicker filePicker = new LFilePicker()
                .withActivity(this)
                .withRequestCode(chooseReqCode)
                .withStartPath(path)
                .withTitle("选择歌曲")
                .withFileFilter(new String[]{".mp4",".avi","."});
        filePicker.start();
    }
    List<String> listPath;
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == chooseReqCode){
            if (data==null || !data.hasExtra("paths")){
                return;
            }
            listPath = data.getStringArrayListExtra("paths");
            if (listPath.size()>0){
                String path = listPath.get(0);
                for (String s : listPath) {
                    Log.d(TAG, "onActivityResult: "+s);
                }
                Surface surface = videoView.getHolder().getSurface();
                play(path,surface);
                Log.d(TAG, "onActivityResult: path = "+path);
            }
        }
        super.onActivityResult(requestCode, resultCode, data);
    }
}
