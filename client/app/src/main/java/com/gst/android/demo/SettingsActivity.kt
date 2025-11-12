package com.gst.android.demo

import android.content.Context
import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.widget.PopupMenu
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
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
            showBitrateSliderDialog(view)
        }
    }

    private fun showVideoQualityMenu(view: View) {
        val popup = PopupMenu(this, view, Gravity.END)
        popup.menuInflater.inflate(R.menu.video_quality_menu, popup.menu)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedQuality = sharedPref.getString(VIDEO_QUALITY_KEY, "1080p")
        val itemId = when (savedQuality) {
            "720p" -> R.id.quality_720p
            "1080p" -> R.id.quality_1080p
            "1440p" -> R.id.quality_1440p
            else -> R.id.quality_1080p
        }
        popup.menu.findItem(itemId).isChecked = true

        popup.setOnMenuItemClickListener { item ->
            item.isChecked = true
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
        val popup = PopupMenu(this, view, Gravity.END)
        popup.menuInflater.inflate(R.menu.framerate_menu, popup.menu)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedFramerate = sharedPref.getString(FRAMERATE_KEY, "60")
        val itemId = when (savedFramerate) {
            "30" -> R.id.framerate_30
            "60" -> R.id.framerate_60
            "90" -> R.id.framerate_90
            else -> R.id.framerate_60
        }
        popup.menu.findItem(itemId).isChecked = true

        popup.setOnMenuItemClickListener { item ->
            item.isChecked = true
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

    private fun showBitrateSliderDialog(view: View) {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_bitrate_slider, null)
        val bitrateValueText = dialogView.findViewById<TextView>(R.id.bitrate_value_text)
        val bitrateSeekbar = dialogView.findViewById<SeekBar>(R.id.bitrate_seekbar)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedBitrate = sharedPref.getString(BITRATE_KEY, "10")?.toInt() ?: 10

        bitrateSeekbar.progress = savedBitrate
        bitrateValueText.text = "$savedBitrate Mbps"

        bitrateSeekbar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                bitrateValueText.text = "$progress Mbps"
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        AlertDialog.Builder(this)
            .setTitle("Select Bitrate")
            .setView(dialogView)
            .setPositiveButton("OK") { _, _ ->
                val newBitrate = bitrateSeekbar.progress.toString()
                sharedPref.edit().putString(BITRATE_KEY, newBitrate).apply()
                updateBitrateLabel()
                Toast.makeText(this, "Selected bitrate: $newBitrate Mbps", Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton("Cancel", null)
            .show()
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
