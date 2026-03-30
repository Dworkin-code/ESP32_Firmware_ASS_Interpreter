package com.testbed.nfcrecipetag.ui

import android.app.AlertDialog
import android.content.Intent
import android.graphics.Typeface
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Bundle
import android.util.Log
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.backup.FullTagBackupManager
import com.testbed.nfcrecipetag.core.codec.HEADER_SIZE
import com.testbed.nfcrecipetag.core.codec.STEP_OFFSET_FLAGS
import com.testbed.nfcrecipetag.core.codec.STEP_SIZE
import com.testbed.nfcrecipetag.core.cellprofile.getActiveCellProfile
import com.testbed.nfcrecipetag.core.codec.decodeRecipe
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.databinding.ActivityTagDetailBinding

class TagDetailActivity : AppCompatActivity() {

    private lateinit var binding: ActivityTagDetailBinding
    private var dump: RawTagDump? = null
    private var decoded: DecodedRecipe? = null
    private var nfcAdapter: NfcAdapter? = null
    private var pendingBackupName: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityTagDetailBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        nfcAdapter = try {
            NfcAdapter.getDefaultAdapter(this)
        } catch (_: Exception) {
            null
        }
        dump = intent.getSerializableExtra(EXTRA_DUMP) as? RawTagDump
        if (dump == null) {
            finish()
            return
        }
        val source = intent.getStringExtra(EXTRA_SOURCE) ?: SOURCE_UNKNOWN

        val bytes = dump!!.bytes
        val metadata = dump!!.metadata
        binding.tagBasicInfo.text =
            "UID: ${metadata.uidHex}\nTYPE: ${metadata.tagType}\nSOURCE: $source"

        if (bytes.size >= HEADER_SIZE) {
            val id = bytes[0].toInt() and 0xFF
            val numOfDrinks =
                (bytes[1].toInt() and 0xFF) or ((bytes[2].toInt() and 0xFF) shl 8)
            val recipeSteps = bytes[3].toInt() and 0xFF
            val actualRecipeStep = bytes[4].toInt() and 0xFF
            val actualBudget =
                (bytes[5].toInt() and 0xFF) or ((bytes[6].toInt() and 0xFF) shl 8)
            val parameters = bytes[7].toInt() and 0xFF
            val rightNumber = bytes[8].toInt() and 0xFF
            val recipeDone = bytes[9].toInt() and 0xFF
            val checkSum =
                (bytes[10].toInt() and 0xFF) or ((bytes[11].toInt() and 0xFF) shl 8)

            val headerText = buildString {
                appendLine("ID               = $id")
                appendLine("NumOfDrinks      = $numOfDrinks")
                appendLine("RecipeSteps      = $recipeSteps")
                appendLine("ActualRecipeStep = $actualRecipeStep")
                appendLine("ActualBudget     = $actualBudget")
                appendLine("Parameters       = $parameters")
                appendLine("RightNumber      = $rightNumber")
                appendLine("RecipeDone       = $recipeDone")
                append("CheckSum         = $checkSum")
            }
            binding.tagHeaderValues.text = headerText
        } else {
            binding.tagHeaderValues.text = "(Tag header not available – less than 12 bytes read)"
        }

        decoded = decodeRecipe(bytes)
        buildStepLayout(bytes)
        binding.rawHex.setText(
            if (bytes.isEmpty()) "(no data read from tag — try holding longer or use Load test recipe then Write)"
            else bytes.joinToString(" ") { "%02X".format(it) }
        )

        binding.btnSaveBackup.setOnClickListener { promptBackupNameAndScan() }
        binding.btnEditRecipe.setOnClickListener {
            if (decoded != null) EditRecipeActivity.start(this, dump!!, decoded!!)
            else Toast.makeText(this, "No decoded recipe to edit", Toast.LENGTH_SHORT).show()
        }
        binding.btnWriteTag.setOnClickListener {
            val writeEnabled =
                getSharedPreferences("settings", MODE_PRIVATE).getBoolean("write_enabled", false)
            if (!writeEnabled) {
                Toast.makeText(this, R.string.write_disabled, Toast.LENGTH_LONG).show()
                return@setOnClickListener
            }
            if (decoded != null) WriteTagActivity.start(this, dump!!, null, decoded!!)
            else Toast.makeText(this, "Decode recipe first", Toast.LENGTH_SHORT).show()
        }
        binding.btnSet4Steps.setOnClickListener { setRecipeStepsTo4() }
    }

    private fun setRecipeStepsTo4() {
        val d = decoded ?: run {
            Toast.makeText(this, "No decoded recipe", Toast.LENGTH_SHORT).show()
            return
        }
        val profile = getActiveCellProfile()
        val currentSteps = d.steps
        val newSteps = (0 until 4).map { i ->
            if (i < currentSteps.size) currentSteps[i]
            else ProcessTypes.createStep(profile.defaultMaterial, profile.defaultSupportA, i, if (i < 3) i + 1 else i)
                .copy(parameterProcess2 = profile.defaultSupportB)
        }
        val newHeader = d.header.copy(
            recipeSteps = 4,
            actualRecipeStep = d.header.actualRecipeStep.coerceIn(0, 3)
        )
        decoded = d.copy(header = newHeader, steps = newSteps)
        // Refresh header display and step layout
        if (dump!!.bytes.size >= HEADER_SIZE) {
            val headerText = buildString {
                appendLine("ID               = ${newHeader.id}")
                appendLine("NumOfDrinks      = ${newHeader.numOfDrinks}")
                appendLine("RecipeSteps      = 4")
                appendLine("ActualRecipeStep = ${newHeader.actualRecipeStep}")
                appendLine("ActualBudget     = ${newHeader.actualBudget}")
                appendLine("Parameters       = ${newHeader.parameters}")
                appendLine("RightNumber      = ${newHeader.rightNumber}")
                appendLine("RecipeDone       = ${if (newHeader.recipeDone) 1 else 0}")
                append("CheckSum         = ${newHeader.checksum}")
            }
            binding.tagHeaderValues.text = headerText
        }
        buildStepLayout(dump!!.bytes)
        Toast.makeText(this, "RecipeSteps set to 4", Toast.LENGTH_SHORT).show()
    }

    private fun buildStepLayout(bytes: ByteArray) {
        val d = decoded ?: return
        val headerSteps = d.header.recipeSteps.coerceAtLeast(0)
        val availableForSteps = (bytes.size - HEADER_SIZE).coerceAtLeast(0)
        val maxStepsByLength = availableForSteps / STEP_SIZE
        val parsedSteps = d.steps.size
        val stepsToShow = minOf(headerSteps, maxStepsByLength, parsedSteps)

        Log.d(TAG, "StepLayout: headerSteps=$headerSteps parsedSteps=$parsedSteps renderedSteps=$stepsToShow")

        val container = binding.stepLayoutContainer
        container.removeAllViews()

        val debugHeader = TextView(this).apply {
            text = "RecipeSteps from header = $headerSteps\nParsed steps count      = $parsedSteps\nRendered steps count    = $stepsToShow"
            typeface = Typeface.MONOSPACE
            setPadding(16, 8, 16, 8)
        }
        container.addView(debugHeader)

        for (i in 0 until stepsToShow) {
            val offset = HEADER_SIZE + i * STEP_SIZE
            if (offset + STEP_SIZE > bytes.size) break
            val raw = bytes.copyOfRange(offset, offset + STEP_SIZE)
            val step = d.steps[i]
            val flagsByte = raw[STEP_OFFSET_FLAGS].toInt() and 0xFF
            val rawHex = raw.joinToString(" ") { String.format("%02X", it) }

            val blockText = buildString {
                appendLine("Step $i")
                appendLine("Offset in payload: $offset")
                appendLine("Raw bytes: $rawHex")
                appendLine()
                appendLine("ID                         = ${step.id}")
                appendLine("NextID                     = ${step.nextId}")
                appendLine("TypeOfProcess              = ${step.typeOfProcess}")
                appendLine("ParameterProcess1          = ${step.parameterProcess1}")
                appendLine("ParameterProcess2          = ${step.parameterProcess2}")
                appendLine("PriceForTransport          = ${step.priceForTransport}")
                appendLine("TransportCellID            = ${step.transportCellId}")
                appendLine("TransportCellReservationID = ${step.transportCellReservationId}")
                appendLine("PriceForProcess            = ${step.priceForProcess}")
                appendLine("ProcessCellID              = ${step.processCellId}")
                appendLine("ProcessCellReservationID   = ${step.processCellReservationId}")
                appendLine("TimeOfProcess              = ${step.timeOfProcess}")
                appendLine("TimeOfTransport            = ${step.timeOfTransport}")
                appendLine("Flags (byte)               = $flagsByte")
                appendLine("  NeedForTransport         = ${step.needForTransport}")
                appendLine("  IsTransport              = ${step.isTransport}")
                appendLine("  IsProcess                = ${step.isProcess}")
                append("  IsStepDone               = ${step.isStepDone}")
            }

            val tv = TextView(this).apply {
                text = blockText
                typeface = Typeface.MONOSPACE
                setPadding(16, 12, 16, 12)
            }
            container.addView(tv)
        }
    }

    private fun promptBackupNameAndScan() {
        if (dump == null) return
        val input = EditText(this)
        input.hint = "tag_AAS_1"
        AlertDialog.Builder(this)
            .setTitle("Backup NFC Tag")
            .setMessage("Enter a backup name, then hold the same tag to the phone to capture a full JSON backup.")
            .setView(input)
            .setPositiveButton("Start") { _, _ ->
                val name = input.text.toString().trim().ifEmpty { "tag_backup" }
                pendingBackupName = name
                startBackupReaderMode()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun startBackupReaderMode() {
        val adapter = nfcAdapter ?: run {
            Toast.makeText(this, "NFC adapter not available", Toast.LENGTH_SHORT).show()
            return
        }
        Toast.makeText(this, "Hold tag to phone to create backup…", Toast.LENGTH_LONG).show()
        adapter.enableReaderMode(
            this,
            { tag: Tag -> runOnUiThread { onBackupTagScanned(tag) } },
            NfcAdapter.FLAG_READER_NFC_A or NfcAdapter.FLAG_READER_NO_PLATFORM_SOUNDS,
            null
        )
    }

    private fun onBackupTagScanned(tag: Tag) {
        val name = pendingBackupName ?: "tag_backup"
        val currentDump = dump
        try {
            FullTagBackupManager.saveFullJsonBackup(this, name, tag, currentDump)
            Toast.makeText(this, "Backup saved", Toast.LENGTH_SHORT).show()
        } catch (_: Exception) {
            Toast.makeText(this, "Failed to save backup", Toast.LENGTH_LONG).show()
        } finally {
            try {
                nfcAdapter?.disableReaderMode(this)
            } catch (_: Exception) {
            }
            pendingBackupName = null
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }

    companion object {
        private const val TAG = "TagDetailActivity"
        private const val EXTRA_DUMP = "dump"
        private const val EXTRA_SOURCE = "source"

        private const val SOURCE_LIVE_SCAN = "live_scan"
        private const val SOURCE_BACKUP = "backup"
        private const val SOURCE_EXTERNAL_NFC = "external_nfc"
        private const val SOURCE_UNKNOWN = "unknown"

        private fun startInternal(activity: AppCompatActivity, dump: RawTagDump, source: String) {
            activity.startActivity(
                Intent(activity, TagDetailActivity::class.java)
                    .putExtra(EXTRA_DUMP, dump)
                    .putExtra(EXTRA_SOURCE, source)
            )
        }

        /** Default entry point kept for backwards compatibility (treated as live scan). */
        fun start(activity: AppCompatActivity, dump: RawTagDump) {
            startInternal(activity, dump, SOURCE_LIVE_SCAN)
        }

        /** Used when launched from dedicated scan flow (reader mode). */
        fun startFromScan(activity: AppCompatActivity, dump: RawTagDump) {
            startInternal(activity, dump, SOURCE_LIVE_SCAN)
        }

        /** Used when launched from MainActivity handling a foreground NFC intent. */
        fun startFromMainNfcIntent(activity: AppCompatActivity, dump: RawTagDump) {
            startInternal(activity, dump, SOURCE_EXTERNAL_NFC)
        }

        /** Used when viewing a stored JSON backup (non-live, cached data). */
        fun startFromBackup(activity: AppCompatActivity, dump: RawTagDump) {
            startInternal(activity, dump, SOURCE_BACKUP)
        }
    }
}
