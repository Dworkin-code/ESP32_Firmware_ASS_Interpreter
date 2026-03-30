package com.testbed.nfcrecipetag.ui

import android.app.PendingIntent
import android.content.Intent
import android.nfc.NfcAdapter
import android.nfc.Tag
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.testbed.nfcrecipetag.R
import com.testbed.nfcrecipetag.core.backup.BackupManager
import com.testbed.nfcrecipetag.core.debug.DebugLog
import com.testbed.nfcrecipetag.core.codec.encodeRecipe
import com.testbed.nfcrecipetag.core.codec.decodeRecipe
import com.testbed.nfcrecipetag.core.codec.encodeStep
import com.testbed.nfcrecipetag.core.codec.HEADER_SIZE
import com.testbed.nfcrecipetag.core.codec.STEP_SIZE
import com.testbed.nfcrecipetag.core.codec.computeStepChecksum
import com.testbed.nfcrecipetag.core.nfc.writeUltralightRecipeBytes
import com.testbed.nfcrecipetag.core.nfc.writeClassicRecipeBytes
import com.testbed.nfcrecipetag.core.nfc.readTagToDump
import com.testbed.nfcrecipetag.core.nfc.getTagType
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.TagType
import com.testbed.nfcrecipetag.core.tagmodel.resolveTagCapacity
import com.testbed.nfcrecipetag.databinding.ActivityWriteTagBinding

class WriteTagActivity : AppCompatActivity() {

    private lateinit var binding: ActivityWriteTagBinding
    private var dump: RawTagDump? = null
    private var originalDecoded: com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe? = null
    private var decoded: com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityWriteTagBinding.inflate(layoutInflater)
        setContentView(binding.root)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        dump = intent.getSerializableExtra(EXTRA_DUMP) as? RawTagDump
        originalDecoded = intent.getSerializableExtra(EXTRA_ORIGINAL_DECODED) as? com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
        decoded = intent.getSerializableExtra(EXTRA_DECODED) as? com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
        if (dump == null || decoded == null) {
            finish()
            return
        }
        val capacity = resolveTagCapacity(dump!!.metadata)
        val encodedRecipeSize = HEADER_SIZE + decoded!!.steps.size * STEP_SIZE
        val metadata = dump!!.metadata
        binding.writeSummary.text =
            "Recipe ID=${decoded!!.header.id} Steps=${decoded!!.header.recipeSteps}. Tag: ${metadata.uidHex} (${metadata.tagType})"
        binding.writeCapacity.text =
            "Tag capacity: ${capacity.usableRecipeBytes} bytes, max recipe steps: ${capacity.maxRecipeSteps}, write supported: ${if (capacity.writeSupported) "Yes" else "No"}"
        binding.writeCapacityWarning.text = if (encodedRecipeSize > capacity.usableRecipeBytes) {
            "WARNING: Current recipe requires $encodedRecipeSize bytes, which exceeds this tag capacity (${capacity.usableRecipeBytes} bytes)."
        } else {
            ""
        }

        val identityBlock = buildString {
            appendLine("LIVE TAG UID: ${metadata.uidHex}")
            appendLine("LIVE TAG UID LENGTH: ${metadata.uid.size} bytes")
            appendLine("LIVE TAG TYPE: ${metadata.tagType}")
            append("DATA SOURCE: edited recipe based on last loaded RawTagDump (no live NFC scan active on this screen)")
        }
        binding.writeIdentityDebug.text = identityBlock
        binding.writeIdentityWarning.text =
            "WARNING: This screen shows an edited recipe and cached tag metadata. The live tag will only be read during write/verify."

        Log.d(
            TAG,
            "WriteTagActivity identity (non-live screen):\n$identityBlock"
        )

        showWritePreview()
        binding.btnConfirmWrite.setOnClickListener { showConfirmDialog() }
    }

    /** Safe preview: old recipe (if any), new recipe, recalculated rightNumber and checksum. */
    private fun showWritePreview() {
        val toWrite = decoded!!
        val rightNumber = 255 - toWrite.header.id
        val stepBytes = toWrite.steps.flatMap { encodeStep(it).toList() }.toByteArray()
        val recalcChecksum = if (toWrite.steps.isEmpty()) 0 else computeStepChecksum(stepBytes)

        val oldBlock = originalDecoded?.let { old ->
            "OLD (from tag before edit):\n" +
                "  id=${old.header.id} recipeSteps=${old.header.recipeSteps} actualRecipeStep=${old.header.actualRecipeStep}\n" +
                "  rightNumber=${old.header.rightNumber} checksum=${old.header.checksum}\n" +
                "  integrityValid=${old.integrityValid} checksumValid=${old.checksumValid}\n"
        } ?: "OLD: (no original recipe passed)\n"

        val newBlock = "NEW (to write):\n" +
            "  id=${toWrite.header.id} recipeSteps=${toWrite.steps.size} actualRecipeStep=${toWrite.header.actualRecipeStep}\n" +
            "  actualBudget=${toWrite.header.actualBudget} recipeDone=${toWrite.header.recipeDone}\n"

        val recalcBlock = "RECALCULATED for write:\n" +
            "  rightNumber = 255 - id = $rightNumber\n" +
            "  checksum (over step bytes only) = $recalcChecksum\n"

        binding.writePreview.text = oldBlock + newBlock + recalcBlock
    }

    private fun showConfirmDialog() {
        AlertDialog.Builder(this)
            .setTitle("Write to tag")
            .setMessage("When you tap Write, the app will wait for you to hold the tag. You can put the tag on the back of the phone when you are ready – writing will start as soon as the tag is detected. A backup of the current tag will be saved first.")
            .setPositiveButton("Write") { _, _ -> enableNfcAndWrite() }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun enableNfcAndWrite() {
        // Run write on the reader callback thread (not UI thread). Some devices fail "Transceive failed"
        // when NFC I/O runs on the main thread; the callback thread is the intended context for tag I/O.
        NfcAdapter.getDefaultAdapter(this)?.enableReaderMode(
            this,
            { tag: Tag -> performWrite(tag) },
            NfcAdapter.FLAG_READER_NFC_A or NfcAdapter.FLAG_READER_NO_PLATFORM_SOUNDS,
            null
        )
        binding.writeResult.text = "Waiting for tag… Hold the tag to the back of the phone when you are ready to write."
    }

    private fun onUi(block: () -> Unit) {
        runOnUiThread(block)
    }

    private fun performWrite(tag: Tag) {
        try {
            if (dump == null || decoded == null) return
            val steps = decoded!!.steps
            val header = decoded!!.header
            val capacity = resolveTagCapacity(dump!!.metadata)
            val encodedRecipeSize = HEADER_SIZE + steps.size * STEP_SIZE
            val maxStepsAllowed = capacity.maxRecipeSteps
            val writeTagType = getTagType(tag)
            val uidHex = tag.id?.joinToString(":") { b -> "%02X".format(b) } ?: "null"
            val techList = tag.techList?.joinToString(", ") ?: "null"

            DebugLog.appendSection(this, "WRITE ATTEMPT")
            DebugLog.append(this, "UID=$uidHex tagType=$writeTagType techList=[$techList]")
            DebugLog.append(this, "capacity: usableRecipeBytes=${capacity.usableRecipeBytes} maxRecipeSteps=$maxStepsAllowed encodedSize=$encodedRecipeSize steps=${steps.size}")

            Log.d(
                TAG,
                "performWrite: uid=$uidHex scannedTagType=$writeTagType, storedMetadataType=${dump!!.metadata.tagType}, " +
                    "usableRecipeBytes=${capacity.usableRecipeBytes}, maxRecipeSteps=$maxStepsAllowed, " +
                    "encodedRecipeSize=$encodedRecipeSize bytes, header.RecipeSteps=${header.recipeSteps}, steps.size=${steps.size}"
            )

            // Header/steps validation
            val stepCount = steps.size
            if (header.recipeSteps != stepCount) {
                onUi {
                    binding.writeResult.text =
                        "Validation error: RecipeSteps (${header.recipeSteps}) must match the number of steps ($stepCount)."
                    Toast.makeText(this, "RecipeSteps must match step count", Toast.LENGTH_LONG).show()
                }
                DebugLog.appendSection(this, "VALIDATION FAILED")
                DebugLog.append(this, "RecipeSteps=${header.recipeSteps} != steps.size=$stepCount")
                onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                return
            }
            if (stepCount == 0) {
                onUi {
                    binding.writeResult.text = "Validation error: recipe has zero steps."
                    Toast.makeText(this, "Recipe must contain at least one step", Toast.LENGTH_LONG).show()
                }
                onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                return
            }
            if (header.actualRecipeStep !in 0 until stepCount) {
                onUi {
                    binding.writeResult.text =
                        "Validation error: ActualRecipeStep (${header.actualRecipeStep}) must be between 0 and ${stepCount - 1}."
                    Toast.makeText(this, "ActualRecipeStep out of range", Toast.LENGTH_LONG).show()
                }
                DebugLog.appendSection(this, "VALIDATION FAILED")
                DebugLog.append(this, "ActualRecipeStep=${header.actualRecipeStep} stepCount=$stepCount")
                onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                return
            }

            if (writeTagType != dump!!.metadata.tagType) {
                onUi {
                    binding.writeResult.text =
                        "Tag type mismatch between scanned tag ($writeTagType) and original dump (${dump!!.metadata.tagType}). Refusing to write."
                    Toast.makeText(this, "Tag type mismatch", Toast.LENGTH_LONG).show()
                }
                DebugLog.appendSection(this, "TAG TYPE MISMATCH")
                DebugLog.append(this, "scanned=$writeTagType stored=${dump!!.metadata.tagType} UID=$uidHex")
                Log.d(
                    TAG,
                    "performWrite: writeAllowed=false reason=TAG_TYPE_MISMATCH uid=$uidHex tagType=$writeTagType " +
                        "storedMetadataType=${dump!!.metadata.tagType} usableRecipeBytes=${capacity.usableRecipeBytes} " +
                        "encodedRecipeSize=$encodedRecipeSize stepCount=${steps.size}"
                )
                onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                return
            }

            // Capacity validation
            if (encodedRecipeSize > capacity.usableRecipeBytes || steps.size > maxStepsAllowed) {
                onUi {
                    binding.writeResult.text =
                        "This tag supports at most $maxStepsAllowed recipe steps (${capacity.usableRecipeBytes} bytes usable). " +
                            "Current recipe requires $encodedRecipeSize bytes and ${steps.size} step(s)."
                    Toast.makeText(this, "Recipe too large for this tag", Toast.LENGTH_LONG).show()
                }
                DebugLog.appendSection(this, "RECIPE TOO LARGE")
                DebugLog.append(this, "encodedRecipeSize=$encodedRecipeSize usableBytes=${capacity.usableRecipeBytes} steps=${steps.size} maxStepsAllowed=$maxStepsAllowed")
                Log.d(
                    TAG,
                    "performWrite: writeAllowed=false reason=RECIPE_TOO_LARGE uid=$uidHex tagType=$writeTagType " +
                        "usableRecipeBytes=${capacity.usableRecipeBytes} encodedRecipeSize=$encodedRecipeSize " +
                        "maxRecipeSteps=$maxStepsAllowed stepCount=${steps.size}"
                )
                onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                return
            }

            Log.d(
                TAG,
                "performWrite: validation passed; backup from in-memory dump then encode uid=$uidHex tagType=$writeTagType"
            )

            // Backup using in-memory dump (no NFC read here – avoids connection drop before write)
            try {
                val backup = BackupManager(this)
                backup.saveBackup(dump!!, originalDecoded, "pre_write")
            } catch (e: Exception) {
                Log.e(TAG, "performWrite: pre-write backup save failed uid=$uidHex (continuing)", e)
            }

            // Build final payload (header + steps), checksum computed inside encodeRecipe
            val unknownTailSize = decoded!!.unknownTail.size
            val stream = try {
                encodeRecipe(decoded!!.header, decoded!!.steps, ByteArray(0))
            } catch (e: Exception) {
                Log.e(TAG, "performWrite: encodeRecipe failed uid=$uidHex", e)
                onUi {
                    binding.writeResult.text = "Write failed: could not encode recipe (${e.message})."
                    Toast.makeText(this, "Write failed while encoding recipe", Toast.LENGTH_LONG).show()
                }
                DebugLog.appendSection(this, "ENCODE FAILED")
                DebugLog.append(this, "encodeRecipe failed uid=$uidHex", e)
                onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                return
            }

            Log.d(
                TAG,
                "performWrite: encodedRecipeSize=$encodedRecipeSize, unknownTailSize=$unknownTailSize, " +
                    "finalStreamSize=${stream.size}, uid=$uidHex tagType=$writeTagType"
            )

            // Low-level write with retry (NFC often fails with "Tag was lost" / "Transceive failed" if tag moves)
            val maxAttempts = 3
            var ok = false
            val nfcLogger: (String) -> Unit = { msg -> DebugLog.append(this, msg) }
            when (writeTagType) {
                TagType.ULTRALIGHT_NTAG -> {
                    DebugLog.append(this, "NFC write: starting Ultralight bytes=${stream.size} (up to $maxAttempts attempts)")
                    Log.d(TAG, "performWrite: starting Ultralight/NTAG write, bytes=${stream.size}, uid=$uidHex (up to $maxAttempts attempts)")
                    for (attempt in 1..maxAttempts) {
                        DebugLog.append(this, "--- Attempt $attempt/$maxAttempts ---")
                        ok = writeUltralightRecipeBytes(tag, stream, nfcLogger)
                        if (ok) {
                            DebugLog.append(this, "Attempt $attempt succeeded")
                            break
                        }
                        DebugLog.append(this, "Attempt $attempt failed, retrying in 300ms...")
                        Log.d(TAG, "performWrite: Ultralight attempt $attempt failed, retrying...")
                        if (attempt < maxAttempts) Thread.sleep(300)
                    }
                }
                TagType.CLASSIC -> {
                    Log.d(TAG, "performWrite: starting Classic write, bytes=${stream.size}, uid=$uidHex (up to $maxAttempts attempts)")
                    for (attempt in 1..maxAttempts) {
                        ok = writeClassicRecipeBytes(tag, stream)
                        if (ok) break
                        Log.d(TAG, "performWrite: Classic attempt $attempt failed, retrying...")
                    }
                }
                else -> {
                    onUi {
                        binding.writeResult.text =
                            "Write not supported for this tag type ($writeTagType)."
                        Toast.makeText(this, "Write not supported for this tag type", Toast.LENGTH_LONG).show()
                    }
                    Log.d(
                        TAG,
                        "performWrite: writeAllowed=false reason=UNSUPPORTED_TAG_TYPE uid=$uidHex tagType=$writeTagType " +
                            "usableRecipeBytes=${capacity.usableRecipeBytes} encodedRecipeSize=$encodedRecipeSize stepCount=${steps.size}"
                    )
                    onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
                    return
                }
            }

            if (ok) {
                val verify = readTagToDump(tag)
                if (verify == null) {
                    onUi {
                        binding.writeResult.text = "Write OK. Re-read failed – could not verify."
                        Toast.makeText(this, "Written; re-read failed", Toast.LENGTH_SHORT).show()
                    }
                    Log.d(
                        TAG,
                        "performWrite: writeCompleted=true verifyRead=false uid=$uidHex tagType=$writeTagType " +
                            "usableRecipeBytes=${capacity.usableRecipeBytes} encodedRecipeSize=$encodedRecipeSize stepCount=${steps.size}"
                    )
                } else {
                    val readBytes = verify.bytes
                    val expectedLen = stream.size
                    val bytesMatch = readBytes.size >= expectedLen &&
                        readBytes.take(expectedLen).toByteArray().contentEquals(stream)
                    val decodedVerify = decodeRecipe(readBytes)
                    val checksumOk = decodedVerify?.checksumValid == true
                    val integrityOk = decodedVerify?.integrityValid == true
                    val success = bytesMatch && checksumOk && integrityOk
                    Log.d(
                        TAG,
                        "performWrite: writeCompleted=true verifyRead=true success=$success uid=$uidHex " +
                            "bytesMatch=$bytesMatch checksumOk=$checksumOk integrityOk=$integrityOk " +
                            "tagType=$writeTagType usableRecipeBytes=${capacity.usableRecipeBytes} " +
                            "encodedRecipeSize=$encodedRecipeSize stepCount=${steps.size}"
                    )
                    onUi {
                        binding.writeResult.text = when {
                            success -> "Write OK. Verification passed (bytes match, checksum and integrity OK)."
                            bytesMatch -> "Write OK. Bytes match; checksum or integrity mismatch – check decode."
                            else -> "Write OK. Verification failed – written bytes differ from re-read."
                        }
                        Toast.makeText(this, if (success) "Write and verify OK" else "Written; verify failed", Toast.LENGTH_SHORT).show()
                    }
                    DebugLog.appendSection(this, "WRITE VERIFY")
                    DebugLog.append(this, "success=$success bytesMatch=$bytesMatch checksumOk=$checksumOk integrityOk=$integrityOk readBytes=${readBytes.size} expected=$expectedLen")
                }
            } else {
                val msg = "Write failed. Keep the tag still against the back of the phone and try again."
                onUi {
                    binding.writeResult.text = msg
                    Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
                }
                DebugLog.appendSection(this, "NFC WRITE FAILED AFTER RETRIES")
                DebugLog.append(this, "All $maxAttempts attempts failed. Check log above for last page/exception (tag lost or transceive failed).")
                Log.d(
                    TAG,
                    "performWrite: writeCompleted=false reason=LOW_LEVEL_WRITE_FAILURE uid=$uidHex tagType=$writeTagType " +
                        "usableRecipeBytes=${capacity.usableRecipeBytes} encodedRecipeSize=$encodedRecipeSize stepCount=${steps.size}"
                )
            }
        } catch (e: Exception) {
            Log.e(TAG, "performWrite: unexpected exception during write", e)
            val isTagLost = e.message?.contains("Tag was lost", ignoreCase = true) == true ||
                e.message?.contains("Transceive failed", ignoreCase = true) == true
            val msg = if (isTagLost) {
                "Write failed. Keep the tag still against the back of the phone and try again."
            } else {
                "Write failed: ${e.message ?: "unknown error"}."
            }
            onUi {
                binding.writeResult.text = msg
                Toast.makeText(this, if (isTagLost) msg else "Write failed", Toast.LENGTH_LONG).show()
            }
            DebugLog.appendSection(this, "WRITE FATAL EXCEPTION")
            DebugLog.append(this, "${e.javaClass.simpleName}: ${e.message}", e)
        } finally {
            onUi { NfcAdapter.getDefaultAdapter(this)?.disableReaderMode(this) }
        }
    }

    override fun onSupportNavigateUp(): Boolean {
        onBackPressedDispatcher.onBackPressed()
        return true
    }

    companion object {
        private const val TAG = "WriteTagActivity"
        private const val EXTRA_DUMP = "dump"
        private const val EXTRA_ORIGINAL_DECODED = "original_decoded"
        private const val EXTRA_DECODED = "decoded"
        fun start(
            activity: AppCompatActivity,
            dump: RawTagDump,
            originalDecoded: com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe?,
            decoded: com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
        ) {
            activity.startActivity(Intent(activity, WriteTagActivity::class.java)
                .putExtra(EXTRA_DUMP, dump)
                .putExtra(EXTRA_ORIGINAL_DECODED, originalDecoded)
                .putExtra(EXTRA_DECODED, decoded))
        }
    }
}
