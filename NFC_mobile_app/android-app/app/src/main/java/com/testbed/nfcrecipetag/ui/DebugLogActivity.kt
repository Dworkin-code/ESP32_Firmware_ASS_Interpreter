package com.testbed.nfcrecipetag.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Bundle
import android.widget.Button
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.debug.DebugLog

class DebugLogActivity : AppCompatActivity() {

    private lateinit var logText: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_debug_log)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        logText = findViewById(R.id.debug_log_text)
        val btnCopy = findViewById<Button>(R.id.btn_copy_log)
        val btnClear = findViewById<Button>(R.id.btn_clear_log)

        refreshLog()

        btnCopy.setOnClickListener {
            val text = DebugLog.getAll(this)
            if (text.isEmpty()) {
                Toast.makeText(this, "Log is empty", Toast.LENGTH_SHORT).show()
            } else {
                val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                clipboard.setPrimaryClip(ClipData.newPlainText("Debug log", text))
                Toast.makeText(this, "Log copied to clipboard", Toast.LENGTH_SHORT).show()
            }
        }
        btnClear.setOnClickListener {
            DebugLog.clear(this)
            refreshLog()
            Toast.makeText(this, "Log cleared", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onResume() {
        super.onResume()
        refreshLog()
    }

    private fun refreshLog() {
        logText.text = DebugLog.getAll(this).ifEmpty { "(empty)" }
        logText.post {
            findViewById<ScrollView>(R.id.debug_log_scroll)?.fullScroll(android.view.View.FOCUS_DOWN)
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }
}

