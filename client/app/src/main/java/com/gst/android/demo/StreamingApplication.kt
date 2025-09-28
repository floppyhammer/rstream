// Copyright 2023, Pluto VR, Inc.
//
// SPDX-License-Identifier: BSL-1.0

package com.gst.android.demo

import android.app.Application;
import android.util.Log
import org.freedesktop.gstreamer.GStreamer

class StreamingApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Log.i("GstAndroidDemo", "StreamingApplication: In onCreate")

        System.loadLibrary("gstreamer_android")
        Log.i("GstAndroidDemo", "StreamingApplication: loaded gstreamer_android")

        Log.i("GstAndroidDemo", "StreamingApplication: Calling GStreamer.init")
        GStreamer.init(this)

        Log.i("GstAndroidDemo", "StreamingApplication: Done with GStreamer.init")
    }
}
