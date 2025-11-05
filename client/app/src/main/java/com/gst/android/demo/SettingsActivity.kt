package com.gst.android.demo

import android.os.Bundle
import android.view.View
import android.widget.PopupMenu
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class SettingsActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        val videoQualityOption = findViewById<TextView>(R.id.video_quality_option)
        videoQualityOption.setOnClickListener { view ->
            showVideoQualityMenu(view)
        }
    }

    private fun showVideoQualityMenu(view: View) {
        val popup = PopupMenu(this, view)
        popup.menuInflater.inflate(R.menu.video_quality_menu, popup.menu)
        popup.setOnMenuItemClickListener { item ->
            val quality = when (item.itemId) {
                R.id.quality_high -> "High"
                R.id.quality_medium -> "Medium"
                R.id.quality_low -> "Low"
                else -> ""
            }
            if (quality.isNotEmpty()) {
                Toast.makeText(this, "Selected quality: $quality", Toast.LENGTH_SHORT).show()
                // Here you would save the setting
            }
            true
        }
        popup.show()
    }
}
