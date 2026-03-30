package com.testbed.nfcrecipetag.ui

import android.app.PendingIntent
import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.nfc.readTagToDump
import com.testbed.nfcrecipetag.databinding.ActivityScanBinding

class ScanActivity : AppCompatActivity() {

    private lateinit var binding: ActivityScanBinding
    private var nfcAdapter: NfcAdapter? = null
    private var pendingIntent: PendingIntent? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityScanBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        nfcAdapter = NfcAdapter.getDefaultAdapter(this)
        val intent = Intent(this, javaClass).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        enableReaderMode()
    }

    override fun onResume() {
        super.onResume()
        enableReaderMode()
    }

    override fun onPause() {
        super.onPause()
        nfcAdapter?.disableReaderMode(this)
    }

    private fun enableReaderMode() {
        nfcAdapter?.enableReaderMode(
            this,
            { tag: Tag ->
                runOnUiThread {
                    binding.scanStatus.text = "Tag detected, reading…"
                    val dump = readTagToDump(tag)
                    if (dump != null) {
                        binding.scanStatus.text = "OK: ${dump.metadata.uidHex}"
                        TagDetailActivity.startFromScan(this, dump)
                        finish()
                    } else {
                        binding.scanStatus.text = "Unsupported tag type"
                        Toast.makeText(this, "Unsupported tag", Toast.LENGTH_SHORT).show()
                    }
                }
            },
            NfcAdapter.FLAG_READER_NFC_A or NfcAdapter.FLAG_READER_NO_PLATFORM_SOUNDS,
            null
        )
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }
}
