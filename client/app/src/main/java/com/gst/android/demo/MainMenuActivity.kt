package com.gst.android.demo

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.wifi.WifiManager
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.widget.EditText
import android.widget.TableRow
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.databinding.DataBindingUtil
import com.gst.android.demo.databinding.ActivityMainMenuBinding


class MainMenuActivity : AppCompatActivity() {
    private lateinit var editText: EditText
    private val PREFS_NAME = "MyPrefsFile"
    private val TEXT_KEY = "host_ip"
    private lateinit var binding: ActivityMainMenuBinding

    private val UDP_PORT = 55555 // The port to listen on
    private lateinit var udpListener: UdpListener
    private val discoveredHosts = mutableSetOf<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i("RStreamClient", "MainMenuActivity: onCreate")

        binding = DataBindingUtil.setContentView(this, R.layout.activity_main_menu)

        // Set up the button click listener
        binding.connectButton.setOnClickListener {
            // Create an Intent to switch to the StreamingActivity
            val intent = Intent(this, StreamingActivity::class.java)

            // You can also pass data to the native activity if needed
            intent.putExtra(TEXT_KEY, editText.text.toString())

            // Start the StreamingActivity
            startActivity(intent)
        }

        binding.settingsButton.setOnClickListener {
            // Create an Intent to switch to the SettingsActivity
            val intent = Intent(this, SettingsActivity::class.java)
            startActivity(intent)
        }

        editText = findViewById(R.id.editTextText)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedText = sharedPref.getString(TEXT_KEY, "")
        editText.setText(savedText)

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        udpListener = UdpListener(UDP_PORT, wifiManager) { message, senderIp ->
            // This runs on a background thread
            Log.i("UDP_RECEIVE", "Packet from $senderIp: $message")

            if (discoveredHosts.add(senderIp)) {
                runOnUiThread {
                    addHostEntry(message, senderIp)
                }
            }
        }

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    }

    private fun addHostEntry(hostName: String, ipAddress: String) {
        val tableRow = TableRow(this).apply {
            layoutParams = TableRow.LayoutParams(
                TableRow.LayoutParams.MATCH_PARENT,
                TableRow.LayoutParams.WRAP_CONTENT
            )
            isClickable = true
            setBackgroundColor(ContextCompat.getColor(context, android.R.color.darker_gray))
            setPadding(16, 16, 16, 16)
            setOnClickListener {
                val intent = Intent(this@MainMenuActivity, StreamingActivity::class.java)
                intent.putExtra(TEXT_KEY, ipAddress)
                startActivity(intent)
            }
        }

        val textView = TextView(this).apply {
            text = "$hostName ($ipAddress)"
            setTextColor(ContextCompat.getColor(context, android.R.color.white))
            textSize = 18f
        }

        tableRow.addView(textView)
        binding.hostsTable.addView(tableRow)
    }

    override fun onStart() {
        Log.i("RStreamClient", "MainMenuActivity: onStart")
        super.onStart()
    }

    override fun onResume() {
        super.onResume()
        Log.i("RStreamClient", "MainMenuActivity: onResume")
        udpListener.startListening()
    }

    override fun onPause() {
        Log.i("RStreamClient", "MainMenuActivity: onPause")
        super.onPause()
        udpListener.stopListening()

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        sharedPref.edit {
            putString(TEXT_KEY, editText.text.toString())
        }
    }

    override fun onStop() {
        Log.i("RStreamClient", "MainMenuActivity: onStop")
        super.onStop()
    }

    override fun onDestroy() {
        Log.i("RStreamClient", "MainMenuActivity: onDestroy")
        super.onDestroy()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        if (resultCode == RESULT_OK) {
            // Handle result if needed
        }
    }
}
