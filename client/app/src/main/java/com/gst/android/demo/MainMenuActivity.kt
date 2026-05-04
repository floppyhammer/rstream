package com.gst.android.demo

import android.content.Context
import android.content.Intent
import android.content.pm.ActivityInfo
import android.net.wifi.WifiManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.edit
import androidx.activity.compose.setContent
import androidx.compose.runtime.*
import androidx.compose.runtime.mutableStateListOf

data class Host(val name: String, val ipAddress: String)

class MainMenuActivity : AppCompatActivity() {
    private val UDP_PORT = 55555 // The port to listen on
    private lateinit var udpListener: UdpListener

    private val discoveredHosts = mutableMapOf<String, Long>()
    private val hostList = mutableStateListOf<Host>()

    private val handler = Handler(Looper.getMainLooper())
    private val cleanupInterval = 2000L // 2 seconds
    private val hostTimeout = 5000L // 10 seconds

    private val PIN_PREFS_NAME = "PinPrefs"

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

        setContent {
            var showConnectDialog by remember { mutableStateOf(false) }
            var pinDialogHost by remember { mutableStateOf<Host?>(null) }
            var clearPinDialogHost by remember { mutableStateOf<Host?>(null) }

            val sharedPref = remember { getSharedPreferences("MyPrefsFile", Context.MODE_PRIVATE) }
            val initialIp = remember { sharedPref.getString("host_ip", "") ?: "" }

            MainMenuScreen(
                hosts = hostList,
                onManualConnectClick = {
                    showConnectDialog = true
                },
                onSettingsClick = {
                    val intent = Intent(this, SettingsActivity::class.java)
                    startActivity(intent)
                },
                onHostClick = { host ->
                    val pinPrefs = getSharedPreferences(PIN_PREFS_NAME, Context.MODE_PRIVATE)
                    val savedPin = pinPrefs.getString("pin_${host.ipAddress}", null)

                    if (savedPin != null) {
                        startStreaming(host, savedPin)
                    } else {
                        pinDialogHost = host
                    }
                },
                onHostLongClick = { host ->
                    clearPinDialogHost = host
                }
            )

            if (showConnectDialog) {
                ConnectDialog(
                    initialIp = initialIp,
                    onConnect = { hostIp ->
                        sharedPref.edit { putString("host_ip", hostIp) }
                        startManualStreaming(hostIp)
                        showConnectDialog = false
                    },
                    onDismiss = { showConnectDialog = false }
                )
            }

            pinDialogHost?.let { host ->
                PinDialog(
                    hostName = host.name,
                    onConfirm = { pin ->
                        val pinPrefs = getSharedPreferences(PIN_PREFS_NAME, Context.MODE_PRIVATE)
                        pinPrefs.edit {
                            putString("pin_${host.ipAddress}", pin)
                        }
                        startStreaming(host, pin)
                        pinDialogHost = null
                    },
                    onDismiss = { pinDialogHost = null }
                )
            }

            clearPinDialogHost?.let { host ->
                val pinPrefs = getSharedPreferences(PIN_PREFS_NAME, Context.MODE_PRIVATE)
                val savedPin = pinPrefs.getString("pin_${host.ipAddress}", null)
                ClearPinDialog(
                    hostName = host.name,
                    savedPin = savedPin,
                    onConfirm = {
                        pinPrefs.edit {
                            remove("pin_${host.ipAddress}")
                        }
                        Toast.makeText(this, "PIN cleared for ${host.name}", Toast.LENGTH_SHORT).show()
                        clearPinDialogHost = null
                    },
                    onDismiss = { clearPinDialogHost = null }
                )
            }
        }
        
        // ... rest of onCreate

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        udpListener = UdpListener(UDP_PORT, wifiManager) { message, senderIp ->
            val isNewHost = !discoveredHosts.containsKey(senderIp)
            discoveredHosts[senderIp] = System.currentTimeMillis()

            if (isNewHost) {
                runOnUiThread {
                    val host = Host(message, senderIp)
                    hostList.add(host)
                }
            }
        }

        requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    }

    private fun startManualStreaming(hostIp: String) {
        val sharedPref = getSharedPreferences("SettingsPrefs", Context.MODE_PRIVATE)
        val videoQuality = sharedPref.getString("video_quality", "1080p")
        val framerate = sharedPref.getString("framerate", "60")
        val bitrate = sharedPref.getString("bitrate", "10")

        val intent = Intent(this@MainMenuActivity, StreamingActivity::class.java)
        intent.putExtra("host_ip", hostIp)
        intent.putExtra("video_quality", videoQuality)
        intent.putExtra("framerate", framerate)
        intent.putExtra("bitrate", bitrate)

        Log.i(
            "RStreamClient",
            "Starting manual stream for $hostIp with quality: $videoQuality, framerate: $framerate, bitrate: $bitrate"
        )

        startActivity(intent)
    }

    private fun startStreaming(host: Host, pin: String) {
        val sharedPref = getSharedPreferences("SettingsPrefs", Context.MODE_PRIVATE)
        val videoQuality = sharedPref.getString("video_quality", "1080p")
        val framerate = sharedPref.getString("framerate", "60")
        val bitrate = sharedPref.getString("bitrate", "10")

        val intent = Intent(this@MainMenuActivity, StreamingActivity::class.java)
        intent.putExtra("host_ip", host.ipAddress)
        intent.putExtra("video_quality", videoQuality)
        intent.putExtra("framerate", framerate)
        intent.putExtra("bitrate", bitrate)
        intent.putExtra("pin", pin)

        Log.i(
            "RStreamClient",
            "Starting stream for ${host.ipAddress} with quality: $videoQuality, framerate: $framerate, bitrate: $bitrate, pin: $pin"
        )

        startActivity(intent)
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
