package com.gst.android.demo

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.Button
import android.widget.EditText
import androidx.core.content.edit

class ConnectActivity : Activity() {
    private val PREFS_NAME = "MyPrefsFile"

    private lateinit var hostIpEditText: EditText
    private lateinit var connectButton: Button

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)

        hostIpEditText = findViewById(R.id.host_ip_edit_text)
        connectButton = findViewById(R.id.connect_button)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedText = sharedPref.getString("host_ip", "")
        hostIpEditText.setText(savedText)

        connectButton.setOnClickListener {
            val hostIp = hostIpEditText.text.toString()
            val sharedPref = getSharedPreferences("SettingsPrefs", Context.MODE_PRIVATE)
            val videoQuality = sharedPref.getString("video_quality", "1080p")
            val framerate = sharedPref.getString("framerate", "60")
            val bitrate = sharedPref.getString("bitrate", "10")

            val intent = Intent(this, StreamingActivity::class.java)
            intent.putExtra("host_ip", hostIp)
            intent.putExtra("video_quality", videoQuality)
            intent.putExtra("framerate", framerate)
            intent.putExtra("bitrate", bitrate)
            startActivity(intent)
        }
    }

    override fun onPause() {
        super.onPause()

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        sharedPref.edit {
            putString("host_ip", hostIpEditText.text.toString())
        }
    }
}
