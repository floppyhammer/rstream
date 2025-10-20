// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

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
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        window.decorView.setSystemUiVisibility(
            (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_IMMERSIVE)
        )

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE

        volumeControlStream = AudioManager.STREAM_MUSIC

        super.onCreate(savedInstanceState, persistentState)
    }

    companion object {
        init {
            Log.i("GstAndroidDemo", "StreamingActivity: In StreamingActivity static init")

            System.loadLibrary("rstream_client")
            Log.i("GstAndroidDemo", "StreamingActivity: loaded rstream_client")
        }
    }
}
