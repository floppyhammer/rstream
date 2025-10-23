package com.gst.android.demo

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import androidx.databinding.DataBindingUtil
import com.gst.android.demo.databinding.ActivityMainMenuBinding

class MainMenuActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainMenuBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        Log.i("RStreamClient", "MainMenuActivity: onCreate")

        binding = DataBindingUtil.setContentView(this, R.layout.activity_main_menu)

        // Set up the button click listener
        binding.connectButton.setOnClickListener {
            // Create an Intent to switch to the StreamingActivity
            val intent = Intent(this, StreamingActivity::class.java)

            // You can also pass data to the native activity if needed
            // intent.putExtra("key", "value")

            // Start the StreamingActivity
            startActivity(intent)
        }

        super.onCreate(savedInstanceState)
    }

    override fun onDestroy() {
        Log.i("RStreamClient", "MainMenuActivity: onDestroy")

        super.onDestroy()
    }
}
