package com.example.mediaplayer;

import android.content.Context;
import android.os.PowerManager;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import static android.content.Context.POWER_SERVICE;
import static android.graphics.PixelFormat.RGBA_8888;

/**
 * Created by cloud on 2019/4/24.
 */

public class VideoView extends SurfaceView implements SurfaceHolder.Callback {

    private String TAG = "VideoView";
    private PowerManager.WakeLock mWakeLock;

    public VideoView(Context context) {
        super(context);
        init();
    }

    public VideoView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public VideoView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        SurfaceHolder holder = getHolder();
        holder.addCallback(this);
        holder.setFormat(RGBA_8888);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        Log.i(TAG, "surfaceCreated: ");
        PowerManager powerManager = (PowerManager)getContext().getSystemService(POWER_SERVICE);
        if (powerManager != null) {
            mWakeLock = powerManager.newWakeLock(PowerManager.FULL_WAKE_LOCK, "WakeLock");
            if (mWakeLock != null) {
                mWakeLock.acquire();
            }
        }

    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.i(TAG, "surfaceChanged: ");
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.i(TAG, "surfaceDestroyed: ");
        if (mWakeLock != null) {
            mWakeLock.release();
        }
    }
}
