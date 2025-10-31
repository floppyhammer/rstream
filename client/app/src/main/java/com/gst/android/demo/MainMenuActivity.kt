package com.gst.android.demo

import android.content.Context
import android.content.Intent
import android.net.wifi.WifiManager
import android.os.Bundle
import android.util.Log
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity
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

    override fun onCreate(savedInstanceState: Bundle?) {
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

        super.onCreate(savedInstanceState)

        // Do this after super.onCreate
        // -----------------------------------------------
        editText = findViewById(R.id.editTextText)

        // 1. Get a reference to the SharedPreferences file
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        // 2. Retrieve the saved string
        // The second parameter ("") is the default value if the key is not found
        val savedText = sharedPref.getString(TEXT_KEY, "")

        // 3. Set the retrieved text back into the EditText
        editText.setText(savedText)
        // -----------------------------------------------

        // 1. Get WifiManager and Cast to WifiManager.
        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        // 2. Initialize the listener
        udpListener = UdpListener(UDP_PORT, wifiManager) { message, senderIp ->
            // This is the callback for received packets.
            // It runs on the Coroutine's IO dispatcher (background thread).

            Log.i("UDP_RECEIVE", "Packet from $senderIp: $message")

            // If you need to update the UI (e.g., a TextView), switch to the main thread:
            /*
            runOnUiThread {
                 // update your TextView here
                 myTextView.text = "Last received: $message from $senderIp"
            }
            */
        }
    }

    override fun onStart() {
        Log.i("RStreamClient", "MainMenuActivity: onStart")

        super.onStart()

        udpListener.startListening()
    }

    override fun onStop() {
        Log.i("RStreamClient", "MainMenuActivity: onStop")

        super.onStop()

        udpListener.stopListening()
    }

    override fun onPause() {
        Log.i("RStreamClient", "MainMenuActivity: onPause")

        super.onPause()

        // Get a reference to the SharedPreferences file
        val sharedPref = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

        sharedPref.edit {
            // Put the current text from the EditText into the editor
            putString(TEXT_KEY, editText.text.toString())
        }
    }

    override fun onDestroy() {
        Log.i("RStreamClient", "MainMenuActivity: onDestroy")

        super.onDestroy()
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        if (resultCode == RESULT_OK) {
            // 🔑 Process the data from the 'data' intent here
//            val receivedValue = data.getStringExtra("key")
            // Update UI based on receivedValue
        }
    }
}
