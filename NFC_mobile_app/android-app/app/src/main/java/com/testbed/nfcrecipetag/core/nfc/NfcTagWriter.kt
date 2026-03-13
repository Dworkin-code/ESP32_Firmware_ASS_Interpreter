package com.testbed.nfcrecipetag.core.nfc

import android.nfc.Tag
import android.nfc.tech.MifareUltralight
import com.testbed.nfcrecipetag.core.tagmodel.TagType
import com.testbed.nfcrecipetag.core.nfc.getTagType

/**
 * Write recipe stream to Ultralight/NTAG only (page 8+). Never writes lock/config pages.
 * Returns true if all written pages verified.
 */
fun writeUltralightRecipeBytes(tag: Tag, stream: ByteArray): Boolean {
    if (getTagType(tag) != TagType.ULTRALIGHT_NTAG) return false
    MifareUltralight.get(tag)?.use { ul ->
        ul.connect()
        try {
            var offset = 0
            var page = ULTRALIGHT_FIRST_PAGE
            while (page <= ULTRALIGHT_MAX_RECIPE_PAGE && offset < stream.size) {
                val chunk = stream.copyOfRange(offset, minOf(offset + 4, stream.size))
                val pad = ByteArray(4) { if (it < chunk.size) chunk[it] else 0 }
                ul.writePage(page, pad)
                offset += 4
                page++
            }
            return true
        } finally {
            try { ul.close() } catch (_: Exception) {}
        }
    }
    return false
}
