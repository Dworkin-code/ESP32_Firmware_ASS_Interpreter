package com.testbed.nfcrecipetag.core.backup

import android.content.Context
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.codec.decodeRecipe
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Saves raw dumps and optional decoded recipe JSON to app-specific storage.
 * Filenames include timestamp and UID for traceability.
 */
class BackupManager(private val context: Context) {

    private val backupDir: File
        get() = File(context.filesDir, "backups").also { if (!it.exists()) it.mkdirs() }

    fun saveBackup(dump: RawTagDump, decoded: DecodedRecipe?, prefix: String = "backup"): List<File> {
        val timestamp = SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.US).format(Date())
        val uidPart = dump.metadata.uidHex.replace(":", "-").take(20)
        val baseName = "${prefix}_${uidPart}_$timestamp"
        val files = mutableListOf<File>()
        val rawFile = File(backupDir, "$baseName.hex")
        rawFile.writeText(dump.bytes.joinToString(" ") { "%02X".format(it) })
        files.add(rawFile)
        val metaFile = File(backupDir, "${baseName}_meta.json")
        metaFile.writeText(metadataToJson(dump, decoded))
        files.add(metaFile)
        if (decoded != null) {
            val jsonFile = File(backupDir, "${baseName}_recipe.json")
            jsonFile.writeText(decodedRecipeToJson(decoded))
            files.add(jsonFile)
        }
        return files
    }

    fun listBackups(): List<BackupEntry> {
        return backupDir.listFiles()?.asSequence()
            ?.filter { it.name.endsWith("_meta.json") }
            ?.mapNotNull { file ->
                try {
                    val json = JSONObject(file.readText())
                    BackupEntry(
                        id = file.nameWithoutExtension.removeSuffix("_meta"),
                        timestamp = json.optString("timestamp", ""),
                        uid = json.optString("uid", ""),
                        tagType = json.optString("tagType", ""),
                        metaPath = file.absolutePath
                    )
                } catch (_: Exception) {
                    null
                }
            }
            ?.sortedByDescending { it.timestamp }
            ?.toList() ?: emptyList()
    }

    fun loadBackupRaw(metaPath: String): RawTagDump? {
        val metaFile = File(metaPath)
        if (!metaFile.exists()) return null
        val baseName = metaFile.nameWithoutExtension.removeSuffix("_meta")
        val hexFile = File(metaFile.parent, "$baseName.hex")
        if (!hexFile.exists()) return null
        val bytes = hexFile.readText().split(" ").filter { it.isNotBlank() }.map { it.toInt(16).toByte() }.toByteArray()
        val json = JSONObject(metaFile.readText())
        val uidHex = json.optString("uid", "")
        val uid = uidHex.split(":").map { it.toInt(16).toByte() }.toByteArray()
        val tagType = when (json.optString("tagType", "")) {
            "ULTRALIGHT_NTAG" -> com.testbed.nfcrecipetag.core.tagmodel.TagType.ULTRALIGHT_NTAG
            "CLASSIC" -> com.testbed.nfcrecipetag.core.tagmodel.TagType.CLASSIC
            else -> com.testbed.nfcrecipetag.core.tagmodel.TagType.UNKNOWN
        }
        val metadata = com.testbed.nfcrecipetag.core.tagmodel.TagMetadata(
            uid = uid,
            uidHex = uidHex,
            tagType = tagType,
            memorySizeBytes = json.optInt("memorySize", 0),
            techList = emptyList()
        )
        return RawTagDump(metadata = metadata, bytes = bytes)
    }

    private fun metadataToJson(dump: RawTagDump, decoded: DecodedRecipe?): String {
        val j = JSONObject()
        j.put("uid", dump.metadata.uidHex)
        j.put("tagType", dump.metadata.tagType.name)
        j.put("memorySize", dump.metadata.memorySizeBytes)
        j.put("timestamp", SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", Locale.US).format(Date()))
        j.put("integrityValid", decoded?.integrityValid ?: false)
        j.put("checksumValid", decoded?.checksumValid ?: false)
        return j.toString(2)
    }

    private fun decodedRecipeToJson(decoded: DecodedRecipe): String {
        val j = JSONObject()
        val h = decoded.header
        j.put("id", h.id)
        j.put("numOfDrinks", h.numOfDrinks)
        j.put("recipeSteps", h.recipeSteps)
        j.put("actualRecipeStep", h.actualRecipeStep)
        j.put("actualBudget", h.actualBudget)
        j.put("parameters", h.parameters)
        j.put("recipeDone", h.recipeDone)
        j.put("checksum", h.checksum)
        val steps = org.json.JSONArray()
        decoded.steps.forEach { s ->
            val step = JSONObject()
            step.put("id", s.id)
            step.put("nextId", s.nextId)
            step.put("typeOfProcess", s.typeOfProcess)
            step.put("parameterProcess1", s.parameterProcess1)
            step.put("parameterProcess2", s.parameterProcess2)
            step.put("priceForTransport", s.priceForTransport)
            step.put("transportCellId", s.transportCellId)
            step.put("transportCellReservationId", s.transportCellReservationId)
            step.put("priceForProcess", s.priceForProcess)
            step.put("processCellId", s.processCellId)
            step.put("processCellReservationId", s.processCellReservationId)
            step.put("timeOfProcess", s.timeOfProcess)
            step.put("timeOfTransport", s.timeOfTransport)
            step.put("needForTransport", s.needForTransport)
            step.put("isTransport", s.isTransport)
            step.put("isProcess", s.isProcess)
            step.put("isStepDone", s.isStepDone)
            steps.put(step)
        }
        j.put("steps", steps)
        return j.toString(2)
    }
}

data class BackupEntry(
    val id: String,
    val timestamp: String,
    val uid: String,
    val tagType: String,
    val metaPath: String
)
