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
    private val PREFS_NAME = "SettingsPrefs"
    private val VIDEO_QUALITY_KEY = "video_quality"
    private val FRAMERATE_KEY = "framerate"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        videoQualityOption = findViewById(R.id.video_quality_option)
        framerateOption = findViewById(R.id.framerate_option)
        updateVideoQualityLabel()
        updateFramerateLabel()

        videoQualityOption.setOnClickListener { view ->
            showVideoQualityMenu(view)
        }

        framerateOption.setOnClickListener { view ->
            showFramerateMenu(view)
        }
    }

    private fun showVideoQualityMenu(view: View) {
        val popup = PopupMenu(this, view)
        popup.menuInflater.inflate(R.menu.video_quality_menu, popup.menu)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedQualityId =
            sharedPref.getInt(VIDEO_QUALITY_KEY, R.id.quality_1080p) // Default to 1080p
        popup.menu.findItem(savedQualityId)?.isChecked = true

        popup.setOnMenuItemClickListener { item ->
            item.isChecked = true
            val sharedPrefEditor = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            sharedPrefEditor.putInt(VIDEO_QUALITY_KEY, item.itemId)
            sharedPrefEditor.apply()

            updateVideoQualityLabel()
            Toast.makeText(this, "Selected quality: ${item.title}", Toast.LENGTH_SHORT).show()
            true
        }
        popup.show()
    }

    private fun showFramerateMenu(view: View) {
        val popup = PopupMenu(this, view)
        popup.menuInflater.inflate(R.menu.framerate_menu, popup.menu)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedFramerateId =
            sharedPref.getInt(FRAMERATE_KEY, R.id.framerate_60) // Default to 60 FPS
        popup.menu.findItem(savedFramerateId)?.isChecked = true

        popup.setOnMenuItemClickListener { item ->
            item.isChecked = true
            val sharedPrefEditor = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE).edit()
            sharedPrefEditor.putInt(FRAMERATE_KEY, item.itemId)
            sharedPrefEditor.apply()

            updateFramerateLabel()
            Toast.makeText(this, "Selected framerate: ${item.title}", Toast.LENGTH_SHORT).show()
            true
        }
        popup.show()
    }

    private fun updateVideoQualityLabel() {
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedQualityId =
            sharedPref.getInt(VIDEO_QUALITY_KEY, R.id.quality_1080p) // Default to 1080p
        val quality = when (savedQualityId) {
            R.id.quality_720p -> "720P"
            R.id.quality_1080p -> "1080P"
            R.id.quality_1440p -> "1440P"
            else -> "1080P"
        }
        videoQualityOption.text = "Video Quality: $quality"
    }

    private fun updateFramerateLabel() {
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedFramerateId =
            sharedPref.getInt(FRAMERATE_KEY, R.id.framerate_60) // Default to 60 FPS
        val framerate = when (savedFramerateId) {
            R.id.framerate_30 -> "30 FPS"
            R.id.framerate_60 -> "60 FPS"
            R.id.framerate_90 -> "90 FPS"
            else -> "60 FPS"
        }
        framerateOption.text = "Framerate: $framerate"
    }
}
