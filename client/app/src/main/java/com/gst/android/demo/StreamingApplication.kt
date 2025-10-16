package com.gst.android.demo

import android.app.Application;
import android.util.Log
import org.freedesktop.gstreamer.GStreamer

const val TAG = "rstream client"

class StreamingApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        Log.i(TAG, "StreamingApplication: In onCreate")

        System.loadLibrary("gstreamer_android")
        Log.i(TAG, "StreamingApplication: loaded gstreamer_android")

        GStreamer.init(this)
        Log.i(TAG, "StreamingApplication: Calling GStreamer.init")

        Log.i(TAG, "StreamingApplication: Done with GStreamer.init")
    }
}
