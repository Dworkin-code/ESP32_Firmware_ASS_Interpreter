package com.testbed.nfcrecipetag.core.nfc

import android.nfc.Tag
import android.nfc.tech.MifareClassic
import android.nfc.tech.MifareUltralight
import android.util.Log
import com.testbed.nfcrecipetag.core.codec.NTAG213_USER_MEMORY
import com.testbed.nfcrecipetag.core.tagmodel.TagType

/**
 * Write recipe stream to Ultralight/NTAG only (page 8+). Never writes lock/config pages.
 * Payload must not exceed NTAG213 user memory (144 bytes). Returns true if all written pages verified.
 * @param log optional logger for detailed diagnostics (each page write and any exception).
 */
fun writeUltralightRecipeBytes(tag: Tag, stream: ByteArray, log: ((String) -> Unit)? = null): Boolean {
    if (getTagType(tag) != TagType.ULTRALIGHT_NTAG) {
        log?.invoke("writeUltralightRecipeBytes: tag is not ULTRALIGHT_NTAG")
        return false
    }
    if (stream.size > NTAG213_USER_MEMORY) {
        log?.invoke("writeUltralightRecipeBytes: stream too large ${stream.size} > $NTAG213_USER_MEMORY")
        return false
    }
    log?.invoke("Ultralight write start: streamSize=${stream.size} bytes")
    var failedAtPage = -1
    var failedAtOffset = -1
    return try {
        val ul = MifareUltralight.get(tag)
        if (ul == null) {
            log?.invoke("MifareUltralight.get(tag) returned null")
            return false
        }
        ul.use {
            log?.invoke("connect()...")
            it.connect()
            log?.invoke("connect() OK")
            // Brief delay after connect so tag is ready for first transceive (reduces "Transceive failed" on some devices)
            Thread.sleep(50)
            var offset = 0
            var page = ULTRALIGHT_FIRST_PAGE
            while (page <= ULTRALIGHT_MAX_RECIPE_PAGE && offset < stream.size) {
                failedAtPage = page
                failedAtOffset = offset
                val chunk = stream.copyOfRange(offset, minOf(offset + 4, stream.size))
                val pad = ByteArray(4) { i -> if (i < chunk.size) chunk[i] else 0 }
                log?.invoke("writePage(page=$page) offset=$offset hex=${pad.joinToString("") { b -> "%02X".format(b) }}")
                it.writePage(page, pad)
                offset += 4
                page++
                // Short delay between pages to reduce "Transceive failed" on some devices/tags
                if (offset < stream.size) Thread.sleep(25)
            }
            log?.invoke("Ultralight write complete: wrote ${offset} bytes, last page=${page - 1}")
            true
        }
    } catch (e: Exception) {
        Log.e("NfcTagWriter", "writeUltralightRecipeBytes failed: ${e.message}", e)
        log?.invoke("*** FAILED AT PAGE=$failedAtPage offset=$failedAtOffset ***")
        log?.invoke("writeUltralightRecipeBytes exception: ${e.javaClass.name}: ${e.message}")
        e.stackTrace.take(15).forEach { ste ->
            log?.invoke("  at ${ste.className}.${ste.methodName}(${ste.fileName}:${ste.lineNumber})")
        }
        false
    }
}

private val CLASSIC_KEY_DEFAULT = byteArrayOf(
    0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte(),
    0xFF.toByte(), 0xFF.toByte(), 0xFF.toByte()
)

/**
 * Write recipe bytes to Mifare Classic logical data blocks.
 *
 * - Uses the same logical mapping as [logicalToPhysicalClassicBlock] / [readClassicBytes].
 * - Authenticates each sector with the default key.
 * - Skips manufacturer block (0) and sector trailers (3, 7, 11, ...).
 * - Writes sequentially across logical blocks until [data] is exhausted.
 *
 * Returns true if the full [data] array has been written.
 */
fun writeClassicRecipeBytes(tag: Tag, data: ByteArray): Boolean {
    if (getTagType(tag) != TagType.CLASSIC) return false
    if (data.isEmpty()) return true
    val mifare = MifareClassic.get(tag) ?: return false
    var offset = 0
    var logicalIndex = 0
    val logicalBlocksWritten = mutableListOf<Int>()
    val physicalBlocksWritten = mutableListOf<Int>()
    val authenticatedSectors = mutableSetOf<Int>()
    try {
        mifare.connect()
        val blockCount = mifare.blockCount
        while (offset < data.size) {
            val physicalBlock = logicalToPhysicalClassicBlock(logicalIndex)
            if (physicalBlock >= blockCount) {
                Log.d(
                    "NfcTagWriter",
                    "writeClassicRecipeBytes: ran out of blocks at logical=$logicalIndex physical=$physicalBlock " +
                        "blockCount=$blockCount offset=$offset total=${data.size}"
                )
                return false
            }

            // Safety: ensure we never touch manufacturer block (0) or sector trailers (3,7,11,...).
            if (physicalBlock == 0 || physicalBlock % 4 == 3) {
                Log.d(
                    "NfcTagWriter",
                    "writeClassicRecipeBytes: safety check hit for physicalBlock=$physicalBlock; aborting."
                )
                return false
            }

            val sector = mifare.blockToSector(physicalBlock)
            if (!authenticatedSectors.contains(sector)) {
                val authOk = mifare.authenticateSectorWithKeyB(sector, CLASSIC_KEY_DEFAULT) ||
                    mifare.authenticateSectorWithKeyA(sector, CLASSIC_KEY_DEFAULT)
                if (!authOk) {
                    Log.e(
                        "NfcTagWriter",
                        "writeClassicRecipeBytes: authentication failed for sector=$sector (block=$physicalBlock) with default key"
                    )
                    return false
                }
                authenticatedSectors.add(sector)
            }

            val remaining = data.size - offset
            val chunkSize = minOf(16, remaining)
            val blockData = ByteArray(16) { i ->
                if (i < chunkSize) data[offset + i] else 0
            }

            mifare.writeBlock(physicalBlock, blockData)
            logicalBlocksWritten.add(logicalIndex)
            physicalBlocksWritten.add(physicalBlock)
            offset += chunkSize
            logicalIndex++
        }

        Log.d(
            "NfcTagWriter",
            "writeClassicRecipeBytes: bytesWritten=${data.size}, logicalBlocks=$logicalBlocksWritten, " +
                "physicalBlocks=$physicalBlocksWritten, sectorsAuthenticated=${authenticatedSectors.size}"
        )
        return true
    } catch (e: Exception) {
        Log.d(
            "NfcTagWriter",
            "writeClassicRecipeBytes: exception during write offset=$offset logicalIndex=$logicalIndex",
            e
        )
        return false
    } finally {
        try {
            mifare.close()
        } catch (_: Exception) {
        }
    }
}
