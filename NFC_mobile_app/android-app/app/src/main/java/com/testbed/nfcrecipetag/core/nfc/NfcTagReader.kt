package com.testbed.nfcrecipetag.core.nfc

import android.nfc.NfcAdapter
import android.nfc.Tag
import android.nfc.tech.MifareClassic
import android.nfc.tech.MifareUltralight
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
 * User memory 144 bytes = 36 pages; pages 0-3 are header, 4-39 often user; we use 8..39.
 */
const val ULTRALIGHT_MAX_RECIPE_PAGE = 39

/**
 * Logical block 0 maps to physical block 2 on Classic (firmware OFFSETDATA_CLASSIC = 1 → first data block index 2).
 */
const val CLASSIC_FIRST_DATA_BLOCK = 2

/**
 * Map logical block index to physical block index (skip sector trailers: blocks 3, 7, 11, ...).
 */
fun logicalToPhysicalClassicBlock(logicalIndex: Int): Int {
    var physical = CLASSIC_FIRST_DATA_BLOCK
    var remaining = logicalIndex
    while (remaining > 0) {
        physical++
        if (physical % 4 != 3) remaining--
    }
    return physical
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

    val memorySize = when (type) {
        TagType.ULTRALIGHT_NTAG -> 144
        TagType.CLASSIC -> 1024
        TagType.UNKNOWN -> 0
    }
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

private fun readUltralightBytes(tag: Tag): ByteArray? {
    MifareUltralight.get(tag)?.use { ul ->
        try {
            ul.connect()
            val chunks = mutableListOf<ByteArray>()
            var page = ULTRALIGHT_FIRST_PAGE
            while (page <= ULTRALIGHT_MAX_RECIPE_PAGE) {
                try {
                    val data = ul.readPages(page)
                    if (data != null && data.size >= 16) {
                        chunks.add(data.copyOf(16))
                    } else break
                } catch (_: Exception) {
                    break
                }
                page += 4
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
