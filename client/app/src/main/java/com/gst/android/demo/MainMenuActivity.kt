package com.gst.android.demo

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.util.Log
import android.widget.EditText
import androidx.appcompat.app.AppCompatActivity
import androidx.databinding.DataBindingUtil
import com.gst.android.demo.databinding.ActivityMainMenuBinding
import androidx.core.content.edit

class MainMenuActivity : AppCompatActivity() {

    private lateinit var editText: EditText
    private val PREFS_NAME = "MyPrefsFile"
    private val TEXT_KEY = "host_ip"
    private lateinit var binding: ActivityMainMenuBinding

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
    }

    override fun onPause() {
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
}
