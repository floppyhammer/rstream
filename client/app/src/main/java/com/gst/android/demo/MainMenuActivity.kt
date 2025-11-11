package com.gst.android.demo

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
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
    private val HOST_IP_KEY = "host_ip"
    private lateinit var binding: ActivityMainMenuBinding

    private val UDP_PORT = 55555 // The port to listen on
    private lateinit var udpListener: UdpListener

    // Store IP address and last seen timestamp
    private val discoveredHosts = mutableMapOf<String, Long>()

    // Map IP address to its TableRow view
    private val hostViewMap = mutableMapOf<String, TableRow>()

    private val handler = Handler(Looper.getMainLooper())
    private val cleanupInterval = 2000L // 2 seconds
    private val hostTimeout = 5000L // 10 seconds

    private val cleanupRunnable = object : Runnable {
        override fun run() {
            val now = System.currentTimeMillis()
            val staleHosts =
                discoveredHosts.filterValues { lastSeen -> now - lastSeen > hostTimeout }.keys

            if (staleHosts.isNotEmpty()) {
                Log.i("RStreamClient", "Removing stale hosts: $staleHosts")
                staleHosts.forEach { ip ->
                    discoveredHosts.remove(ip)
                    val viewToRemove = hostViewMap.remove(ip)
                    binding.hostsTable.removeView(viewToRemove)
                }
            }
            handler.postDelayed(this, cleanupInterval)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i("RStreamClient", "MainMenuActivity: onCreate")

        binding = DataBindingUtil.setContentView(this, R.layout.activity_main_menu)

        binding.connectButton.setOnClickListener {
            val intent = Intent(this, StreamingActivity::class.java)
            intent.putExtra(HOST_IP_KEY, editText.text.toString())
            startActivity(intent)
        }

        binding.settingsButton.setOnClickListener {
            val intent = Intent(this, SettingsActivity::class.java)
            startActivity(intent)
        }

        editText = findViewById(R.id.editTextText)

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val savedText = sharedPref.getString(HOST_IP_KEY, "")
        editText.setText(savedText)

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        udpListener = UdpListener(UDP_PORT, wifiManager) { message, senderIp ->
            // This runs on a background thread
            val isNewHost = !discoveredHosts.containsKey(senderIp)
            discoveredHosts[senderIp] = System.currentTimeMillis()

            if (isNewHost) {
                runOnUiThread {
                    addHostEntry(message, senderIp)
                }
            }
        }

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    }

    private fun addHostEntry(hostName: String, ipAddress: String) {
        Log.i("RStreamClient", "Adding new host: $hostName ($ipAddress)")
        val tableRow = TableRow(this).apply {
            layoutParams = TableRow.LayoutParams(
                TableRow.LayoutParams.MATCH_PARENT,
                TableRow.LayoutParams.WRAP_CONTENT
            )
            isClickable = true
            setBackgroundColor(ContextCompat.getColor(context, android.R.color.darker_gray))
            setPadding(16, 16, 16, 16)
            setOnClickListener {
                val sharedPref = getSharedPreferences("SettingsPrefs", Context.MODE_PRIVATE)
                val videoQuality = sharedPref.getString("video_quality", "1080p")
                val framerate = sharedPref.getString("framerate", "60")
                val bitrate = sharedPref.getString("bitrate", "10")

                val intent = Intent(this@MainMenuActivity, StreamingActivity::class.java)
                intent.putExtra(HOST_IP_KEY, ipAddress)
                intent.putExtra("video_quality", videoQuality)
                intent.putExtra("framerate", framerate)
                intent.putExtra("bitrate", bitrate)

                Log.i(
                    "RStreamClient",
                    "Starting stream for $ipAddress with quality: $videoQuality, framerate: $framerate, bitrate: $bitrate"
                )

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
        hostViewMap[ipAddress] = tableRow
    }

    override fun onResume() {
        super.onResume()
        Log.i("RStreamClient", "MainMenuActivity: onResume")
        udpListener.startListening()
        handler.post(cleanupRunnable) // Start cleanup task
    }

    override fun onPause() {
        super.onPause()
        Log.i("RStreamClient", "MainMenuActivity: onPause")
        udpListener.stopListening()
        handler.removeCallbacks(cleanupRunnable) // Stop cleanup task

        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        sharedPref.edit {
            putString(HOST_IP_KEY, editText.text.toString())
        }
    }

    override fun onStart() {
        super.onStart()
        Log.i("RStreamClient", "MainMenuActivity: onStart")
    }

    override fun onStop() {
        super.onStop()
        Log.i("RStreamClient", "MainMenuActivity: onStop")
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.i("RStreamClient", "MainMenuActivity: onDestroy")
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        if (resultCode == RESULT_OK) {
            // Handle result if needed
        }
    }
}
