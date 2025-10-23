package com.gst.android.demo

import android.app.NativeActivity
import android.content.pm.ActivityInfo
import android.media.AudioManager
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log
import android.view.View
import android.view.WindowManager

class StreamingActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {
        Log.i("RStreamClient", "StreamingActivity: onCreate")

        volumeControlStream = AudioManager.STREAM_MUSIC

        super.onCreate(savedInstanceState, persistentState)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        window.decorView.setSystemUiVisibility(
            (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        )

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        super.onWindowFocusChanged(hasFocus)
    }

    override fun onDestroy() {
        Log.i("RStreamClient", "StreamingActivity: onDestroy")

        super.onDestroy()
    }

    companion object {
        init {
            Log.i("RStreamClient", "StreamingActivity: static init")

            System.loadLibrary("rstream_client")
            Log.i("RStreamClient", "StreamingActivity: loaded rstream_client.so")
        }
    }
}
