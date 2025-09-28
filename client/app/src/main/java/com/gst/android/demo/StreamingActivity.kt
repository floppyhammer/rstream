// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

package com.gst.android.demo

import android.app.NativeActivity
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.PersistableBundle
import android.util.Log
import android.view.WindowManager

class StreamingActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?, persistentState: PersistableBundle?) {
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        super.onCreate(savedInstanceState, persistentState)
    }

    companion object {
        init {
            Log.i("GstAndroidDemo", "StreamingActivity: In StreamingActivity static init")

            System.loadLibrary("gst_android_demo")
            Log.i("GstAndroidDemo", "StreamingActivity: loaded gst_android_demo")
        }
    }
}
