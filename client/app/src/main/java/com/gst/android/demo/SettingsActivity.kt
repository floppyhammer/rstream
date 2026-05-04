package com.gst.android.demo

import android.content.Context
import android.os.Bundle
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.runtime.*

class SettingsActivity : AppCompatActivity() {

    private val PREFS_NAME = "SettingsPrefs"
    private val VIDEO_QUALITY_KEY = "video_quality"
    private val FRAMERATE_KEY = "framerate"
    private val BITRATE_KEY = "bitrate"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            SettingsContent()
        }
    }

    @Composable
    fun SettingsContent() {
        val sharedPref = remember { getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE) }
        
        var videoQuality by remember { 
            mutableStateOf(sharedPref.getString(VIDEO_QUALITY_KEY, "1080p") ?: "1080p") 
        }
        var framerate by remember { 
            mutableStateOf(sharedPref.getString(FRAMERATE_KEY, "60") ?: "60") 
        }
        var bitrate by remember { 
            mutableStateOf(sharedPref.getString(BITRATE_KEY, "10") ?: "10") 
        }

        var showQualityDialog by remember { mutableStateOf(false) }
        var showFramerateDialog by remember { mutableStateOf(false) }
        var showBitrateDialog by remember { mutableStateOf(false) }

        SettingsScreen(
            videoQuality = videoQuality,
            framerate = framerate,
            bitrate = bitrate,
            onVideoQualityClick = { showQualityDialog = true },
            onFramerateClick = { showFramerateDialog = true },
            onBitrateClick = { showBitrateDialog = true },
            onBackClick = { finish() }
        )

        if (showQualityDialog) {
            SelectionDialog(
                title = "Select Video Quality",
                options = listOf("720p", "1080p", "1440p", "4k"),
                onSelect = { quality ->
                    videoQuality = quality
                    sharedPref.edit().putString(VIDEO_QUALITY_KEY, quality).apply()
                    showQualityDialog = false
                    Toast.makeText(this, "Selected quality: $quality", Toast.LENGTH_SHORT).show()
                },
                onDismiss = { showQualityDialog = false }
            )
        }

        if (showFramerateDialog) {
            SelectionDialog(
                title = "Select Framerate",
                options = listOf("15", "30", "60", "90"),
                onSelect = { fps ->
                    framerate = fps
                    sharedPref.edit().putString(FRAMERATE_KEY, fps).apply()
                    showFramerateDialog = false
                    Toast.makeText(this, "Selected framerate: $fps FPS", Toast.LENGTH_SHORT).show()
                },
                onDismiss = { showFramerateDialog = false }
            )
        }

        if (showBitrateDialog) {
            BitrateDialog(
                initialBitrate = bitrate.toIntOrNull() ?: 10,
                onConfirm = { newBitrate ->
                    bitrate = newBitrate.toString()
                    sharedPref.edit().putString(BITRATE_KEY, bitrate).apply()
                    showBitrateDialog = false
                    Toast.makeText(this, "Selected bitrate: $bitrate Mbps", Toast.LENGTH_SHORT).show()
                },
                onDismiss = { showBitrateDialog = false }
            )
        }
    }
}
