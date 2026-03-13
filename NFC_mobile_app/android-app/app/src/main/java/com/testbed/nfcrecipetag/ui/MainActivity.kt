package com.testbed.nfcrecipetag.ui

import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private var nfcAdapter: NfcAdapter? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        nfcAdapter = try { NfcAdapter.getDefaultAdapter(this) } catch (_: Exception) { null }
        updateNfcStatus()
        binding.btnScan.setOnClickListener { startActivity(Intent(this, ScanActivity::class.java)) }
        binding.btnBackups.setOnClickListener { startActivity(Intent(this, BackupsActivity::class.java)) }
        binding.btnSettings.setOnClickListener { startActivity(Intent(this, SettingsActivity::class.java)) }
        handleIntent(intent)
    }

    override fun onResume() {
        super.onResume()
        updateNfcStatus()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun updateNfcStatus() {
        val enabled = nfcAdapter != null && nfcAdapter!!.isEnabled
        binding.nfcStatus.text = if (enabled) getString(R.string.nfc_enabled) else getString(R.string.nfc_disabled)
        val writeEnabled = getSharedPreferences("settings", MODE_PRIVATE).getBoolean("write_enabled", false)
        binding.writeStatus.text = if (writeEnabled) getString(R.string.write_enabled) else getString(R.string.write_disabled)
    }

    private fun handleIntent(intent: Intent?) {
        if (intent?.action == NfcAdapter.ACTION_TAG_DISCOVERED ||
            intent?.action == NfcAdapter.ACTION_TECH_DISCOVERED ||
            intent?.action == NfcAdapter.ACTION_NDEF_DISCOVERED
        ) {
            val tag = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(NfcAdapter.EXTRA_TAG, Tag::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(NfcAdapter.EXTRA_TAG)
            }
            if (tag != null) {
                val dump = com.testbed.nfcrecipetag.core.nfc.readTagToDump(tag)
                if (dump != null) {
                    TagDetailActivity.start(this, dump)
                } else {
                    Toast.makeText(this, "Unsupported tag type", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
}
