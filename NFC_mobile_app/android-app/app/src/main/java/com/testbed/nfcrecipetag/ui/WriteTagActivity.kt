package com.testbed.nfcrecipetag.ui

import android.app.PendingIntent
import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.backup.BackupManager
import com.testbed.nfcrecipetag.core.codec.encodeRecipe
import com.testbed.nfcrecipetag.core.codec.decodeRecipe
import com.testbed.nfcrecipetag.core.nfc.writeUltralightRecipeBytes
import com.testbed.nfcrecipetag.core.nfc.readTagToDump
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.TagType
import com.testbed.nfcrecipetag.databinding.ActivityWriteTagBinding

class WriteTagActivity : AppCompatActivity() {

    private lateinit var binding: ActivityWriteTagBinding
    private var dump: RawTagDump? = null
    private var decoded: com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityWriteTagBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        dump = intent.getSerializableExtra(EXTRA_DUMP) as? RawTagDump
        decoded = intent.getSerializableExtra(EXTRA_DECODED) as? com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
        if (dump == null || decoded == null) {
            finish()
            return
        }
        binding.writeSummary.text = "Recipe ID=${decoded!!.header.id} Steps=${decoded!!.header.recipeSteps}. Tag: ${dump!!.metadata.uidHex}"
        binding.btnConfirmWrite.setOnClickListener { showConfirmDialog() }
    }

    private fun showConfirmDialog() {
        AlertDialog.Builder(this)
            .setTitle("Confirm write")
            .setMessage("This will overwrite the recipe data on the tag. A backup of the current tag will be saved first. Hold the tag to the phone when you tap OK.")
            .setPositiveButton("Write") { _, _ -> enableNfcAndWrite() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun enableNfcAndWrite() {
        val intent = Intent(this, javaClass).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        val pending = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        NfcAdapter.getDefaultAdapter(this)?.enableReaderMode(
            this,
            { tag: Tag -> runOnUiThread { performWrite(tag) } },
            NfcAdapter.FLAG_READER_NFC_A or NfcAdapter.FLAG_READER_NO_PLATFORM_SOUNDS,
            null
        )
        binding.writeResult.text = "Hold tag to phone now…"
    }

    private fun performWrite(tag: Tag) {
        if (dump == null || decoded == null) return
        if (com.testbed.nfcrecipetag.core.nfc.getTagType(tag) != TagType.ULTRALIGHT_NTAG) {
            binding.writeResult.text = "Only NTAG/Ultralight tags can be written."
            Toast.makeText(this, "Wrong tag type", Toast.LENGTH_SHORT).show()
            return
        }
        val backup = BackupManager(this)
        val currentDump = readTagToDump(tag)
        if (currentDump != null) backup.saveBackup(currentDump, decodeRecipe(currentDump.bytes), "pre_write")
        val stream = encodeRecipe(decoded!!.header, decoded!!.steps, decoded!!.unknownTail)
        val ok = writeUltralightRecipeBytes(tag, stream)
        if (ok) {
            val verify = readTagToDump(tag)
            if (verify == null) {
                binding.writeResult.text = "Write OK. Re-read failed – could not verify."
                Toast.makeText(this, "Written; re-read failed", Toast.LENGTH_SHORT).show()
            } else {
                val readBytes = verify.bytes
                val expectedLen = stream.size
                val bytesMatch = readBytes.size >= expectedLen && readBytes.take(expectedLen).toByteArray().contentEquals(stream)
                val decodedVerify = decodeRecipe(readBytes)
                val checksumOk = decodedVerify?.checksumValid == true
                val integrityOk = decodedVerify?.integrityValid == true
                val success = bytesMatch && checksumOk && integrityOk
                binding.writeResult.text = when {
                    success -> "Write OK. Verification passed (bytes match, checksum and integrity OK)."
                    bytesMatch -> "Write OK. Bytes match; checksum or integrity mismatch – check decode."
                    else -> "Write OK. Verification failed – written bytes differ from re-read."
                }
                Toast.makeText(this, if (success) "Write and verify OK" else "Written; verify failed", Toast.LENGTH_SHORT).show()
            }
        } else {
            binding.writeResult.text = "Write failed."
            Toast.makeText(this, "Write failed", Toast.LENGTH_SHORT).show()
        }
        NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this)
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }

    companion object {
        private const val EXTRA_DUMP = "dump"
        private const val EXTRA_DECODED = "decoded"
        fun start(activity: AppCompatActivity, dump: RawTagDump, decoded: com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe) {
            activity.startActivity(Intent(activity, WriteTagActivity::class.java)
                .putExtra(EXTRA_DUMP, dump)
                .putExtra(EXTRA_DECODED, decoded))
        }
    }
}
