package com.testbed.nfcrecipetag.core.nfc

import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.tech.MifareClassic
import android.nfc.tech.MifareUltralight
import com.testbed.nfcrecipetag.core.codec.HEADER_SIZE
import com.testbed.nfcrecipetag.core.codec.NTAG213_USER_MEMORY
import com.testbed.nfcrecipetag.core.codec.STEP_SIZE
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.TagMetadata
import com.testbed.nfcrecipetag.core.tagmodel.TagType

/**
 * First user data page on Ultralight (firmware OFFSETDATA_ULTRALIGHT = 8).
 * See tag_format_spec.md.
 */
const val ULTRALIGHT_FIRST_PAGE = 8

/**
 * For NTAG213, do not write beyond this page (config/lock area follows).
 * User memory; pages 8..39 are safe for recipe write.
 */
const val ULTRALIGHT_MAX_RECIPE_PAGE = 39

/**
 * Maximum page we will read when doing two-phase recipe read (read header, then extend to full recipe).
 * Allows reading beyond 39 so that requiredBytes = HEADER_SIZE + recipeSteps * STEP_SIZE can be satisfied.
 */
const val ULTRALIGHT_MAX_READ_PAGE = 55

private const val CLASSIC_OFFSETDATA = 1

/**
 * Logical block 0 maps to physical block 1 on Classic (firmware OFFSETDATA_CLASSIC = 1).
 * This mirrors the ESP32 NFC_GetMifareClassicIndex mapping so that byte 0 of the
 * linear payload corresponds to the first byte of TRecipeInfo.
 */
const val CLASSIC_FIRST_DATA_BLOCK = 1

/**
 * Map logical block index to physical block index (skip sector trailers: blocks 3, 7, 11, ...).
 */
fun logicalToPhysicalClassicBlock(logicalIndex: Int): Int {
    var number = 1 + CLASSIC_OFFSETDATA
    repeat(logicalIndex) {
        number++
        if (number % 4 == 0) {
            number++
        }
    }
    return number - 1
}

/**
 * Detect tag type from UID length and tech list (firmware: 4 = Classic, 7 = Ultralight).
 */
fun getTagType(tag: Tag): TagType {
    val uidLen = tag.id?.size ?: 0
    return when {
        uidLen == 7 -> TagType.ULTRALIGHT_NTAG
        uidLen == 4 -> TagType.CLASSIC
        else -> TagType.UNKNOWN
    }
}

/**
 * Read raw recipe region from tag into RawTagDump.
 * Ultralight: pages 8..max (4 bytes per page).
 * Classic: logical data blocks from block 2, skipping trailers.
 */
fun readTagToDump(tag: Tag): RawTagDump? {
    val uid = tag.id
    val uidHex = uid.joinToString(":") { b -> "%02X".format(b) }
    val techList = tag.techList?.toList() ?: emptyList()
    val atqa = tag.getOrDefault("atqa", 0.toShort())
    val sak = tag.getOrDefault("sak", 0.toShort())
    val type = getTagType(tag)

    val bytes = when (type) {
        TagType.ULTRALIGHT_NTAG -> readUltralightBytes(tag)
        TagType.CLASSIC -> readClassicBytes(tag)
        TagType.UNKNOWN -> return null
    } ?: return null

    val memorySize = bytes.size
    val metadata = TagMetadata(
        uid = uid,
        uidHex = uidHex,
        tagType = type,
        memorySizeBytes = memorySize,
        techList = techList,
        atqa = atqa,
        sak = sak
    )
    return RawTagDump(metadata = metadata, bytes = bytes)
}

private fun Tag.getOrDefault(key: String, default: Short): Short {
    return try {
        val field = javaClass.getDeclaredField(key)
        field.isAccessible = true
        (field.get(this) as? Short) ?: default
    } catch (_: Exception) {
        default
    }
}

/**
 * Two-phase read: first read header, parse recipeSteps, then read until
 * currentSize >= requiredBytes. requiredBytes is capped at NTAG213_USER_MEMORY (144);
 * we do not read beyond the NTAG213 usable region.
 */
private fun readUltralightBytes(tag: Tag): ByteArray? {
    MifareUltralight.get(tag)?.use { ul ->
        try {
            ul.connect()
            val chunks = mutableListOf<ByteArray>()
            var page = ULTRALIGHT_FIRST_PAGE
            val bytesPerChunk = 16
            var requiredBytes = HEADER_SIZE

            while (page <= ULTRALIGHT_MAX_READ_PAGE) {
                try {
                    val data = ul.readPages(page) ?: break
                    if (data.isEmpty()) break

                    if (chunks.isEmpty()) {
                        val recipeSteps = (data.getOrNull(3)?.toInt() ?: 0) and 0xFF
                        val requested = HEADER_SIZE + recipeSteps * STEP_SIZE
                        requiredBytes = minOf(requested, NTAG213_USER_MEMORY)
                    }

                    val currentSize = chunks.sumOf { it.size }
                    if (currentSize >= NTAG213_USER_MEMORY) break
                    val remaining = (requiredBytes - currentSize).coerceAtLeast(1)
                    val maxTake = minOf(NTAG213_USER_MEMORY - currentSize, remaining, data.size, bytesPerChunk)
                    if (maxTake <= 0) break
                    chunks.add(data.copyOfRange(0, maxTake))
                    val newSize = chunks.sumOf { it.size }
                    if (newSize >= requiredBytes || newSize >= NTAG213_USER_MEMORY) break
                    page += 4
                } catch (_: Exception) {
                    break
                }
            }
            val list = chunks.flatMap { it.toList() }
            return ByteArray(list.size) { list[it] }
        } finally {
            try { ul.close() } catch (_: Exception) {}
        }
    }
    return null
}

private val CLASSIC_KEY_B = byteArrayOf(0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte())

private fun readClassicBytes(tag: Tag): ByteArray? {
    val mifare = MifareClassic.get(tag) ?: return null
    try {
        mifare.connect()
        val out = mutableListOf<Byte>()
        var logicalIndex = 0
        val maxLogicalBlocks = 48
        while (logicalIndex < maxLogicalBlocks) {
            val physicalBlock = logicalToPhysicalClassicBlock(logicalIndex)
            if (physicalBlock >= mifare.blockCount) break
            val sector = mifare.blockToSector(physicalBlock)
            try {
                if (!mifare.authenticateSectorWithKeyB(sector, CLASSIC_KEY_B)) break
                val block = mifare.readBlock(physicalBlock)
                if (block != null) block.forEach { out.add(it) }
                else break
            } catch (_: Exception) {
                break
            }
            logicalIndex++
        }
        return out.toByteArray()
    } finally {
        try { mifare.close() } catch (_: Exception) {}
    }
}
