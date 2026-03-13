package com.testbed.nfcrecipetag.ui

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.codec.decodeHeader
import com.testbed.nfcrecipetag.core.codec.decodeRecipe
import com.testbed.nfcrecipetag.core.tagmodel.createTestRecipeShaker5
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.databinding.ActivityTagDetailBinding

class TagDetailActivity : AppCompatActivity() {

    private lateinit var binding: ActivityTagDetailBinding
    private var dump: RawTagDump? = null
    private var decoded: DecodedRecipe? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityTagDetailBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        dump = intent.getSerializableExtra(EXTRA_DUMP) as? RawTagDump
        if (dump == null) {
            finish()
            return
        }
        decoded = decodeRecipe(dump!!.bytes)
        binding.tagUid.text = dump!!.metadata.uidHex
        binding.tagType.text = dump!!.metadata.tagType.name
        binding.memorySize.text = getString(R.string.memory_size_fmt, dump!!.metadata.memorySizeBytes)
        binding.checksumStatus.text = when {
            decoded == null -> "—"
            decoded!!.checksumValid -> "OK"
            else -> "Invalid"
        }
        binding.integrityStatus.text = when {
            decoded == null -> "—"
            decoded!!.integrityValid -> "ID+RightNumber=255 OK"
            else -> "Invalid"
        }
        binding.rawHex.setText(
            if (dump!!.bytes.isEmpty()) "(no data read from tag — try holding longer or use Load test recipe then Write)"
            else dump!!.bytes.joinToString(" ") { "%02X".format(it) }
        )
        val header = try { decodeHeader(dump!!.bytes) } catch (_: Exception) { null }
        if (header != null) {
            binding.headerId.text = "ID: ${header.id}"
            binding.headerNumDrinks.text = "Number of drinks: ${header.numOfDrinks}"
            binding.headerSteps.text = "Recipe steps: ${header.recipeSteps}"
            binding.headerBudget.text = "Budget: ${header.actualBudget}"
        } else {
            binding.headerId.text = "Header not recognized"
            binding.headerNumDrinks.text = ""
            binding.headerSteps.text = ""
            binding.headerBudget.text = ""
        }
        binding.recyclerSteps.layoutManager = LinearLayoutManager(this)
        binding.recyclerSteps.adapter = StepsAdapter(decoded?.steps ?: emptyList())
        binding.btnSaveBackup.setOnClickListener { saveBackup() }
        binding.btnEditRecipe.setOnClickListener {
            if (decoded != null) EditRecipeActivity.start(this, dump!!, decoded!!)
            else Toast.makeText(this, "No decoded recipe to edit", Toast.LENGTH_SHORT).show()
        }
        binding.btnWriteTag.setOnClickListener {
            val writeEnabled = getSharedPreferences("settings", MODE_PRIVATE).getBoolean("write_enabled", false)
            if (!writeEnabled) {
                Toast.makeText(this, R.string.write_disabled, Toast.LENGTH_LONG).show()
                return@setOnClickListener
            }
            if (decoded != null) WriteTagActivity.start(this, dump!!, decoded!!)
            else Toast.makeText(this, "Decode recipe first", Toast.LENGTH_SHORT).show()
        }
        binding.btnLoadTestRecipe.setOnClickListener {
            decoded = createTestRecipeShaker5(decoded?.unknownTail ?: ByteArray(0))
            refreshDecodedUi()
            Toast.makeText(this, "Test recipe loaded: Shaker 5 s", Toast.LENGTH_SHORT).show()
        }
    }

    private fun refreshDecodedUi() {
        val d = decoded ?: return
        binding.headerId.text = "ID: ${d.header.id}"
        binding.headerNumDrinks.text = "Number of drinks: ${d.header.numOfDrinks}"
        binding.headerSteps.text = "Recipe steps: ${d.header.recipeSteps}"
        binding.headerBudget.text = "Budget: ${d.header.actualBudget}"
        binding.checksumStatus.text = if (d.checksumValid) "OK" else "Invalid"
        binding.integrityStatus.text = if (d.integrityValid) "ID+RightNumber=255 OK" else "Invalid"
        binding.recyclerSteps.adapter = StepsAdapter(d.steps)
    }

    private fun saveBackup() {
        if (dump == null) return
        val backup = com.testbed.nfcrecipetag.core.backup.BackupManager(this)
        backup.saveBackup(dump!!, decoded, "backup")
        Toast.makeText(this, "Backup saved", Toast.LENGTH_SHORT).show()
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }

    companion object {
        private const val EXTRA_DUMP = "dump"
        fun start(activity: AppCompatActivity, dump: RawTagDump) {
            activity.startActivity(Intent(activity, TagDetailActivity::class.java).putExtra(EXTRA_DUMP, dump))
        }
    }
}
