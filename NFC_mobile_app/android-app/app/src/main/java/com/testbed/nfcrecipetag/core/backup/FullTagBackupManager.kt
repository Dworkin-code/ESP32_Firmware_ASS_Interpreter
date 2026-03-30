package com.testbed.nfcrecipetag.core.backup

import android.content.Context
import android.nfc.NdefMessage
import android.nfc.NdefRecord
import android.nfc.Tag
import android.nfc.tech.MifareClassic
import android.nfc.tech.MifareUltralight
import android.nfc.tech.Ndef
import com.testbed.nfcrecipetag.core.nfc.getTagType
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.TagType
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Builds and saves a full JSON backup of an NFC tag:
 * - UID, tech list, basic tag type
 * - NDEF presence + records (if any)
 * - Raw memory dump for MifareClassic / MifareUltralight where accessible
 *
 * The JSON layout matches the structure requested for reverse engineering.
 */
object FullTagBackupManager {

    fun saveFullJsonBackup(
        context: Context,
        tagName: String,
        tag: Tag?,
        dump: RawTagDump?
    ): File {
        val dir = File(context.filesDir, "nfc_tag_backups").also { if (!it.exists()) it.mkdirs() }
        val safeBaseName = sanitizeFileName(tagName.ifBlank { "tag_backup" })
        var target = File(dir, "$safeBaseName.json")
        if (target.exists()) {
            val ts = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            target = File(dir, "${safeBaseName}_$ts.json")
        }
        val json = buildBackupJson(tagName, tag, dump)
        target.writeText(json)
        return target
    }

    private fun sanitizeFileName(name: String): String {
        return name.replace(Regex("[^A-Za-z0-9._-]"), "_")
    }

    private fun buildBackupJson(
        tagName: String,
        tag: Tag?,
        dump: RawTagDump?
    ): String {
        val j = JSONObject()

        val uidBytes: ByteArray? = dump?.metadata?.uid ?: tag?.id
        val uidHex: String? = dump?.metadata?.uidHex ?: uidBytes?.joinToString(":") { b -> "%02X".format(b) }
        val techList: List<String> =
            dump?.metadata?.techList
                ?: (tag?.techList?.toList() ?: emptyList())
        val tagType: TagType =
            dump?.metadata?.tagType
                ?: (tag?.let { getTagType(it) } ?: TagType.UNKNOWN)

        j.put("tagName", tagName)
        j.put("uid", uidHex ?: JSONObject.NULL)
        j.put("uidLength", uidBytes?.size ?: JSONObject.NULL)
        j.put("techList", JSONArray(techList))
        j.put("tagType", tagType.name)

        j.put("ndef", buildNdefSection(tag))

        val rawSection = buildRawSection(tag, dump)
        j.put("rawDump", rawSection)

        return j.toString(2)
    }

    private fun buildNdefSection(tag: Tag?): JSONObject {
        val j = JSONObject()
        if (tag == null) {
            j.put("present", JSONObject.NULL)
            return j
        }
        var ndef: Ndef? = null
        var message: NdefMessage? = null
        try {
            ndef = Ndef.get(tag)
            if (ndef != null) {
                try {
                    ndef.connect()
                } catch (_: Exception) {
                }
                message = try {
                    ndef.cachedNdefMessage ?: ndef.ndefMessage
                } catch (_: Exception) {
                    null
                }
            }
        } catch (_: Exception) {
        } finally {
            try {
                ndef?.close()
            } catch (_: Exception) {
            }
        }

        if (message == null) {
            j.put("present", false)
            j.put("records", JSONArray())
            return j
        }

        j.put("present", true)
        val recordsArray = JSONArray()
        for (record in message.records) {
            recordsArray.put(buildNdefRecordJson(record))
        }
        j.put("records", recordsArray)
        return j
    }

    private fun buildNdefRecordJson(record: NdefRecord): JSONObject {
        val jr = JSONObject()
        jr.put("tnf", record.tnf.toInt())
        jr.put("typeHex", toHex(record.type))
        jr.put("idHex", toHex(record.id))
        jr.put("payloadHex", toHex(record.payload))
        jr.put("payloadAsciiPreview", asciiPreview(record.payload))
        return jr
    }

    private data class BlockDump(
        val index: Int,
        val data: ByteArray
    )

    private fun buildRawSection(tag: Tag?, dump: RawTagDump?): JSONObject {
        val j = JSONObject()

        val blocks = mutableListOf<BlockDump>()
        var allBytes: ByteArray? = null

        if (tag != null) {
            val classicBytes = safeReadClassic(tag, blocks)
            val ultralightBytes = safeReadUltralight(tag, blocks)
            allBytes = when {
                classicBytes != null -> classicBytes
                ultralightBytes != null -> ultralightBytes
                else -> null
            }
        }

        if (allBytes == null) {
            allBytes = dump?.bytes
        }

        // Per-block/page info
        val blocksArray = JSONArray()
        for (b in blocks) {
            val jb = JSONObject()
            jb.put("index", b.index)
            jb.put("hex", toHex(b.data, separator = " "))
            jb.put("ascii", asciiPreview(b.data))
            blocksArray.put(jb)
        }
        j.put("blocks", blocksArray)

        if (allBytes != null) {
            j.put("hex", toHex(allBytes, separator = " "))
            j.put("ascii", asciiPreview(allBytes))
        } else {
            j.put("hex", JSONObject.NULL)
            j.put("ascii", JSONObject.NULL)
        }

        return j
    }

    private fun safeReadClassic(tag: Tag, outBlocks: MutableList<BlockDump>): ByteArray? {
        val mifare = MifareClassic.get(tag) ?: return null
        val all = mutableListOf<Byte>()
        try {
            mifare.connect()
            for (sector in 0 until mifare.sectorCount) {
                val auth = try {
                    mifare.authenticateSectorWithKeyA(sector, MifareClassic.KEY_DEFAULT) ||
                        mifare.authenticateSectorWithKeyB(sector, MifareClassic.KEY_DEFAULT)
                } catch (_: Exception) {
                    false
                }
                if (!auth) continue
                val firstBlock = mifare.sectorToBlock(sector)
                val count = mifare.getBlockCountInSector(sector)
                for (offset in 0 until count) {
                    val blockIndex = firstBlock + offset
                    val data = try {
                        mifare.readBlock(blockIndex)
                    } catch (_: Exception) {
                        null
                    }
                    if (data != null) {
                        all.addAll(data.toList())
                        outBlocks.add(BlockDump(blockIndex, data))
                    }
                }
            }
        } catch (_: Exception) {
        } finally {
            try {
                mifare.close()
            } catch (_: Exception) {
            }
        }
        return if (all.isEmpty()) null else all.toByteArray()
    }

    private fun safeReadUltralight(tag: Tag, outBlocks: MutableList<BlockDump>): ByteArray? {
        val ul = MifareUltralight.get(tag) ?: return null
        val chunks = mutableListOf<ByteArray>()
        try {
            ul.connect()
            var page = 0
            while (true) {
                val data = try {
                    ul.readPages(page)
                } catch (_: Exception) {
                    null
                }
                if (data == null || data.isEmpty()) break
                val copy = if (data.size > 16) data.copyOf(16) else data
                chunks.add(copy)
                outBlocks.add(BlockDump(page, copy))
                page += 4
            }
        } catch (_: Exception) {
        } finally {
            try {
                ul.close()
            } catch (_: Exception) {
            }
        }
        if (chunks.isEmpty()) return null
        val flat = chunks.flatMap { it.toList() }
        return ByteArray(flat.size) { flat[it] }
    }

    private fun toHex(bytes: ByteArray?, separator: String = ""): String {
        if (bytes == null) return ""
        return bytes.joinToString(separator) { b -> "%02X".format(b) }
    }

    private fun asciiPreview(bytes: ByteArray?): String {
        if (bytes == null) return ""
        val sb = StringBuilder(bytes.size)
        for (b in bytes) {
            val c = b.toInt() and 0xFF
            sb.append(
                if (c in 0x20..0x7E) c.toChar() else '.'
            )
        }
        return sb.toString()
    }
}

