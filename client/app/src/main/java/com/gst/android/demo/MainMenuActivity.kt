package com.gst.android.demo

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.edit
import androidx.databinding.DataBindingUtil
import androidx.recyclerview.widget.LinearLayoutManager
import com.gst.android.demo.databinding.ActivityMainMenuBinding

class MainMenuActivity : AppCompatActivity() {
    private val HOST_IP_KEY = "host_ip"
    private lateinit var binding: ActivityMainMenuBinding

    private val UDP_PORT = 55555 // The port to listen on
    private lateinit var udpListener: UdpListener

    private val discoveredHosts = mutableMapOf<String, Long>()
    private val hostList = mutableListOf<Host>()
    private lateinit var hostAdapter: HostAdapter

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
                runOnUiThread {
                    staleHosts.forEach { ip ->
                        discoveredHosts.remove(ip)
                        val index = hostList.indexOfFirst { it.ipAddress == ip }
                        if (index != -1) {
                            hostList.removeAt(index)
                            hostAdapter.notifyItemRemoved(index)
                        }
                    }
                }
            }
            handler.postDelayed(this, cleanupInterval)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.d("RStreamClient", "MainMenuActivity: onCreate")

        binding = DataBindingUtil.setContentView(this, R.layout.activity_main_menu)

        setupRecyclerView()

        binding.manualConnectButton.setOnClickListener {
            val intent = Intent(this, ConnectActivity::class.java)
            startActivity(intent)
        }

        binding.settingsButton.setOnClickListener {
            val intent = Intent(this, SettingsActivity::class.java)
            startActivity(intent)
        }

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        udpListener = UdpListener(UDP_PORT, wifiManager) { message, senderIp ->
            val isNewHost = !discoveredHosts.containsKey(senderIp)
            discoveredHosts[senderIp] = System.currentTimeMillis()

            if (isNewHost) {
                runOnUiThread {
                    val host = Host(message, senderIp)
                    hostList.add(host)
                    hostAdapter.notifyItemInserted(hostList.size - 1)
                }
            }
        }

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    }

    private fun setupRecyclerView() {
        hostAdapter = HostAdapter(hostList) { host ->
            val sharedPref = getSharedPreferences("SettingsPrefs", Context.MODE_PRIVATE)
            val videoQuality = sharedPref.getString("video_quality", "1080p")
            val framerate = sharedPref.getString("framerate", "60")
            val bitrate = sharedPref.getString("bitrate", "10")

            val intent = Intent(this@MainMenuActivity, StreamingActivity::class.java)
            intent.putExtra(HOST_IP_KEY, host.ipAddress)
            intent.putExtra("video_quality", videoQuality)
            intent.putExtra("framerate", framerate)
            intent.putExtra("bitrate", bitrate)

            Log.i(
                "RStreamClient",
                "Starting stream for ${host.ipAddress} with quality: $videoQuality, framerate: $framerate, bitrate: $bitrate"
            )

            startActivity(intent)
        }
        binding.hostsRecyclerView.adapter = hostAdapter
        binding.hostsRecyclerView.layoutManager = LinearLayoutManager(this)
    }

    override fun onResume() {
        super.onResume()
        Log.d("RStreamClient", "MainMenuActivity: onResume")
        udpListener.startListening()
        handler.post(cleanupRunnable) // Start cleanup task
    }

    override fun onPause() {
        super.onPause()
        Log.d("RStreamClient", "MainMenuActivity: onPause")
        udpListener.stopListening()
        handler.removeCallbacks(cleanupRunnable) // Stop cleanup task
    }

    override fun onStart() {
        super.onStart()
        Log.d("RStreamClient", "MainMenuActivity: onStart")
    }

    override fun onStop() {
        super.onStop()
        Log.d("RStreamClient", "MainMenuActivity: onStop")
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d("RStreamClient", "MainMenuActivity: onDestroy")
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        if (resultCode == RESULT_OK) {
            // Handle result if needed
        }
    }
}
