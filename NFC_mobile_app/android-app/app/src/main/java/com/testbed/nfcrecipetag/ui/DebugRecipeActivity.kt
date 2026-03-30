package com.testbed.nfcrecipetag.ui

import android.content.Intent
import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.core.codec.HEADER_SIZE
import com.testbed.nfcrecipetag.core.codec.STEP_SIZE
import com.testbed.nfcrecipetag.core.codec.computeStepChecksum
import com.testbed.nfcrecipetag.core.codec.decodeStep
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.ProcessTypes
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.resolveTagCapacity
import com.testbed.nfcrecipetag.databinding.ActivityDebugRecipeBinding

/**
 * Debug screen: shows full decoded recipe (header, all steps, raw values),
 * checksum and integrity validity. Same information is logged to logcat
 * with tag [TAG] for verification without the UI.
 */
class DebugRecipeActivity : AppCompatActivity() {

    private lateinit var binding: ActivityDebugRecipeBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityDebugRecipeBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)

        val dump = intent.getSerializableExtra(EXTRA_DUMP) as? RawTagDump
        val decoded = intent.getSerializableExtra(EXTRA_DECODED) as? DecodedRecipe
        val source = intent.getStringExtra(EXTRA_SOURCE) ?: SOURCE_UNKNOWN

        if (decoded == null) {
            binding.debugContent.text = "No decoded recipe passed."
            return
        }

        val text = buildDebugText(dump, decoded, source)
        binding.debugContent.text = text
        Log.d(TAG, text)
    }

    private fun buildDebugText(dump: RawTagDump?, decoded: DecodedRecipe, source: String): String =
        buildDebugTextStatic(dump, decoded, source)

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }

    companion object {
        const val TAG = "RecipeDebug"
        private const val EXTRA_DUMP = "dump"
        private const val EXTRA_DECODED = "decoded"
        private const val EXTRA_SOURCE = "source"

        private const val SOURCE_LIVE_SCAN = "live_scan"
        private const val SOURCE_BACKUP = "backup"
        private const val SOURCE_EXTERNAL_NFC = "external_nfc"
        private const val SOURCE_UNKNOWN = "unknown"

        fun logRecipe(logTag: String, dump: RawTagDump?, decoded: DecodedRecipe, source: String = SOURCE_UNKNOWN) {
            Log.d(logTag, buildDebugTextStatic(dump, decoded, source))
        }

        fun start(activity: AppCompatActivity, dump: RawTagDump?, decoded: DecodedRecipe, source: String) {
            activity.startActivity(Intent(activity, DebugRecipeActivity::class.java).apply {
                putExtra(EXTRA_DUMP, dump)
                putExtra(EXTRA_DECODED, decoded)
                putExtra(EXTRA_SOURCE, source)
            })
        }

        /** Backwards-compatible entry point (source will be reported as unknown). */
        fun start(activity: AppCompatActivity, dump: RawTagDump?, decoded: DecodedRecipe) {
            start(activity, dump, decoded, SOURCE_UNKNOWN)
        }

        private fun buildDebugTextStatic(dump: RawTagDump?, decoded: DecodedRecipe, source: String): String {
            val rawBytes = dump?.bytes ?: decoded.rawBytes
            val h = decoded.header
            val sb = StringBuilder()

            val metadata = dump?.metadata
            val uidHex = metadata?.uidHex ?: "(no RawTagDump metadata; decoded bytes only)"
            val uidLength = metadata?.uid?.size ?: rawBytes.size
            val tagType = metadata?.tagType?.name ?: "(unknown; no RawTagDump metadata available)"
            val isLiveSource = source == SOURCE_LIVE_SCAN || source == SOURCE_EXTERNAL_NFC
            val dataSourceDescription = when (source) {
                SOURCE_LIVE_SCAN -> "RawTagDump from current NFC scan (ScanActivity/MainActivity)"
                SOURCE_EXTERNAL_NFC -> "RawTagDump from foreground NFC intent (MainActivity)"
                SOURCE_BACKUP -> "RawTagDump loaded from JSON backup (cached data)"
                else -> if (dump != null) {
                    "RawTagDump from unknown/cached source"
                } else {
                    "Decoded recipe only (no RawTagDump attached)"
                }
            }

            sb.appendLine("=== RECIPE IDENTITY ===")
            sb.appendLine("LIVE TAG UID: $uidHex")
            sb.appendLine("LIVE TAG UID LENGTH: $uidLength bytes")
            sb.appendLine("LIVE TAG TYPE: $tagType")
            sb.appendLine("DATA SOURCE: $dataSourceDescription")
            if (!isLiveSource) {
                sb.appendLine("WARNING: This debug output is based on non-live data (cached/backup/edited).")
            }
            sb.appendLine()

            sb.appendLine("=== RECIPE DEBUG ===")
            val capacity = dump?.let { resolveTagCapacity(it.metadata) }
            if (capacity != null) {
                sb.appendLine("Tag type: ${capacity.tagType}")
                sb.appendLine("Usable recipe bytes for this tag: ${capacity.usableRecipeBytes}")
                sb.appendLine("Max recipe steps for this tag: ${capacity.maxRecipeSteps}")
                sb.appendLine("Write supported for this tag: ${capacity.writeSupported}")
                sb.appendLine("Capacity resolver explanation: ${capacity.explanation}")
            } else {
                sb.appendLine("Tag type/capacity: (no RawTagDump metadata available)")
            }
            val totalRecipeSize = HEADER_SIZE + decoded.steps.size * STEP_SIZE
            sb.appendLine("Total encoded recipe size (header + steps): $totalRecipeSize bytes")
            capacity?.let {
                val requiredBytes = HEADER_SIZE + h.recipeSteps * STEP_SIZE
                val exceedsByHeader = requiredBytes > it.usableRecipeBytes
                val exceedsByActual = totalRecipeSize > it.usableRecipeBytes
                sb.appendLine("Capacity check against this tag: usable=${it.usableRecipeBytes} bytes, maxSteps=${it.maxRecipeSteps}")
                if (exceedsByHeader) {
                    sb.appendLine(
                        "WARNING: Header.RecipeSteps=${h.recipeSteps} implies $requiredBytes bytes " +
                            "which exceeds this tag capacity (${it.usableRecipeBytes} bytes)."
                    )
                }
                if (exceedsByActual) {
                    sb.appendLine(
                        "WARNING: Actual decoded recipe size ($totalRecipeSize bytes) exceeds this tag capacity " +
                            "(${it.usableRecipeBytes} bytes)."
                    )
                }
            }
            sb.appendLine()
            sb.appendLine("1. Raw dump length passed to decodeRecipe: ${rawBytes.size}")
            sb.appendLine("checksumValid=${decoded.checksumValid}")
            sb.appendLine("integrityValid=${decoded.integrityValid}")
            sb.appendLine()
            sb.appendLine("--- Header ---")
            sb.appendLine("id=${h.id} numOfDrinks=${h.numOfDrinks} recipeSteps=${h.recipeSteps}")
            sb.appendLine("actualRecipeStep=${h.actualRecipeStep}  (0-based index of next step to execute)")
            sb.appendLine("actualBudget=${h.actualBudget} parameters=${h.parameters}")
            sb.appendLine("rightNumber=${h.rightNumber}  (must be 255-id=${255 - h.id}) recipeDone=${h.recipeDone} checksum(stored)=${h.checksum}")
            sb.appendLine()

            val stepCount = decoded.steps.size
            sb.appendLine("--- Step byte offsets (HEADER_SIZE=$HEADER_SIZE STEP_SIZE=$STEP_SIZE) ---")
            for (i in 0 until stepCount) {
                val start = HEADER_SIZE + i * STEP_SIZE
                val end = start + STEP_SIZE - 1
                sb.appendLine("Step $i: start=$start end=$end (inclusive)")
                if (rawBytes.size > end) {
                    val slice = rawBytes.copyOfRange(start, end + 1)
                    sb.appendLine("  raw slice: ${slice.joinToString(" ") { "%02X".format(it) }}")
                } else {
                    sb.appendLine("  raw slice: (buffer too short: need $end, have ${rawBytes.size})")
                }
            }

            val checksumRangeStart = HEADER_SIZE
            val checksumRangeEnd = HEADER_SIZE + stepCount * STEP_SIZE
            val checksumByteCount = stepCount * STEP_SIZE
            val stepBytesForChecksum = if (rawBytes.size >= checksumRangeEnd) {
                rawBytes.copyOfRange(checksumRangeStart, checksumRangeEnd)
            } else {
                ByteArray(0)
            }
            val computedChecksum = if (stepBytesForChecksum.isNotEmpty()) computeStepChecksum(stepBytesForChecksum) else 0

            sb.appendLine()
            sb.appendLine("--- Checksum region (firmware: TRecipeStep[] only) ---")
            sb.appendLine("checksum range: bytes $checksumRangeStart .. ${checksumRangeEnd - 1} (inclusive)")
            sb.appendLine("checksum byte count: $checksumByteCount")
            sb.appendLine("stored checksum (header): ${h.checksum}")
            sb.appendLine("computed checksum: $computedChecksum")
            sb.appendLine("checksum bytes (hex): ${stepBytesForChecksum.take(64).joinToString(" ") { "%02X".format(it) }}${if (stepBytesForChecksum.size > 64) " ..." else ""}")

            if (stepCount >= 5 && rawBytes.size >= HEADER_SIZE + 5 * STEP_SIZE) {
                sb.appendLine()
                sb.appendLine("--- Step 4 decoded explicitly from raw bytes (offset ${HEADER_SIZE + 4 * STEP_SIZE}) ---")
                val step4Offset = HEADER_SIZE + 4 * STEP_SIZE
                val step4 = decodeStep(rawBytes, step4Offset)
                sb.appendLine("id=${step4.id} nextId=${step4.nextId} typeOfProcess=${step4.typeOfProcess} (${ProcessTypes.name(step4.typeOfProcess)}) param1=${step4.parameterProcess1} param2=${step4.parameterProcess2}")
                sb.appendLine("(Expected: id=4 nextId=4 type=0 ToStorageGlass param1=0 param2=0)")
            }

            val bdIndex = rawBytes.indexOfFirst { it == 0xBD.toByte() }
            if (bdIndex >= 0) {
                sb.appendLine()
                sb.appendLine("--- Byte 0xBD position ---")
                sb.appendLine("0xBD first occurrence at byte index: $bdIndex")
                when {
                    bdIndex < HEADER_SIZE -> sb.appendLine("  -> inside header (bytes 0..11)")
                    else -> {
                        val stepIndex = (bdIndex - HEADER_SIZE) / STEP_SIZE
                        val inStepOffset = (bdIndex - HEADER_SIZE) % STEP_SIZE
                        sb.appendLine("  -> step $stepIndex, offset within step: $inStepOffset")
                        when (inStepOffset) {
                            in 0..1 -> sb.appendLine("  -> id/nextId")
                            in 2..5 -> sb.appendLine("  -> type/param1/param2")
                            in 6..13 -> sb.appendLine("  -> transport/process IDs")
                            in 14..21 -> sb.appendLine("  -> TimeOfProcess")
                            in 22..29 -> sb.appendLine("  -> TimeOfTransport")
                            30 -> sb.appendLine("  -> flags byte")
                            else -> sb.appendLine("  -> (unknown)")
                        }
                    }
                }
            }

            sb.appendLine()
            sb.appendLine("--- Steps (decoded) ---")
            decoded.steps.forEachIndexed { index, s ->
                sb.appendLine("Step $index: id=${s.id} nextId=${s.nextId} typeOfProcess=${s.typeOfProcess} (${ProcessTypes.name(s.typeOfProcess)}) param1=${s.parameterProcess1} param2=${s.parameterProcess2}")
                sb.appendLine("  priceTransport=${s.priceForTransport} transportCellId=${s.transportCellId} transportCellReservationId=${s.transportCellReservationId}")
                sb.appendLine("  priceProcess=${s.priceForProcess} processCellId=${s.processCellId} processCellReservationId=${s.processCellReservationId}")
                sb.appendLine("  needForTransport=${s.needForTransport} isTransport=${s.isTransport} isProcess=${s.isProcess} isStepDone=${s.isStepDone}")
            }
            if (rawBytes.isNotEmpty()) {
                sb.appendLine()
                sb.appendLine("--- Raw bytes (hex) ---")
                sb.appendLine(rawBytes.joinToString(" ") { "%02X".format(it) })
            }
            sb.appendLine("=== END ===")
            return sb.toString()
        }
    }
}
