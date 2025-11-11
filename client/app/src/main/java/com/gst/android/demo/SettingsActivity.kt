package com.gst.android.demo

import android.content.Context
import android.os.Bundle
import android.view.View
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class SettingsActivity : AppCompatActivity() {

    private lateinit var videoQualityOption: TextView
    private lateinit var framerateOption: TextView
    private lateinit var bitrateOption: TextView
    private val PREFS_NAME = "SettingsPrefs"
    private val VIDEO_QUALITY_KEY = "video_quality"
    private val FRAMERATE_KEY = "framerate"
    private val BITRATE_KEY = "bitrate"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        videoQualityOption = findViewById(R.id.video_quality_option)
        framerateOption = findViewById(R.id.framerate_option)
        bitrateOption = findViewById(R.id.bitrate_option)
        updateVideoQualityLabel()
        updateFramerateLabel()
        updateBitrateLabel()

        videoQualityOption.setOnClickListener { view ->
            showVideoQualityMenu(view)
        }

        framerateOption.setOnClickListener { view ->
            showFramerateMenu(view)
        }

        bitrateOption.setOnClickListener { view ->
            showBitrateMenu(view)
        }
    }

    private fun showVideoQualityMenu(view: View) {
        val popup = PopupMenu(this, view)
        popup.menuInflater.inflate(R.menu.video_quality_menu, popup.menu)

        popup.setOnMenuItemClickListener { item ->
            val quality = when (item.itemId) {
                R.id.quality_720p -> "720p"
                R.id.quality_1080p -> "1080p"
                R.id.quality_1440p -> "1440p"
                else -> "1080p"
            }

            val sharedPrefEditor = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            sharedPrefEditor.putString(VIDEO_QUALITY_KEY, quality)
            sharedPrefEditor.apply()

            updateVideoQualityLabel()
            Toast.makeText(this, "Selected quality: $quality", Toast.LENGTH_SHORT).show()
            true
        }
        popup.show()
    }

    private fun showFramerateMenu(view: View) {
        val popup = PopupMenu(this, view)
        popup.menuInflater.inflate(R.menu.framerate_menu, popup.menu)

        popup.setOnMenuItemClickListener { item ->
            val framerate = when (item.itemId) {
                R.id.framerate_30 -> "30"
                R.id.framerate_60 -> "60"
                R.id.framerate_90 -> "90"
                else -> "60"
            }
            val sharedPrefEditor = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            sharedPrefEditor.putString(FRAMERATE_KEY, framerate)
            sharedPrefEditor.apply()

            updateFramerateLabel()
            Toast.makeText(this, "Selected framerate: $framerate FPS", Toast.LENGTH_SHORT).show()
            true
        }
        popup.show()
    }

    private fun showBitrateMenu(view: View) {
        val popup = PopupMenu(this, view)
        popup.menuInflater.inflate(R.menu.bitrate_menu, popup.menu)

        popup.setOnMenuItemClickListener { item ->
            val bitrate = when (item.itemId) {
                R.id.bitrate_5 -> "5"
                R.id.bitrate_10 -> "10"
                R.id.bitrate_15 -> "15"
                else -> "10"
            }
            val sharedPrefEditor = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            sharedPrefEditor.putString(BITRATE_KEY, bitrate)
            sharedPrefEditor.apply()

            updateBitrateLabel()
            Toast.makeText(this, "Selected bitrate: $bitrate Mbps", Toast.LENGTH_SHORT).show()
            true
        }
        popup.show()
    }

    private fun updateVideoQualityLabel() {
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val quality = sharedPref.getString(VIDEO_QUALITY_KEY, "1080p")
        videoQualityOption.text = "Video Quality: ${quality?.uppercase()}"
    }

    private fun updateFramerateLabel() {
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val framerate = sharedPref.getString(FRAMERATE_KEY, "60")
        framerateOption.text = "Framerate: $framerate FPS"
    }

    private fun updateBitrateLabel() {
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val bitrate = sharedPref.getString(BITRATE_KEY, "10")
        bitrateOption.text = "Bitrate: $bitrate Mbps"
    }
}
