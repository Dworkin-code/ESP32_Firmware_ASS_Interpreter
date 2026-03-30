package com.testbed.nfcrecipetag.core.tagmodel

import com.testbed.nfcrecipetag.core.codec.HEADER_SIZE
import com.testbed.nfcrecipetag.core.codec.NTAG213_USER_MEMORY
import com.testbed.nfcrecipetag.core.codec.STEP_SIZE

/**
 * Per-tag recipe storage capacity and write support information.
 */
data class TagCapacityInfo(
    val tagType: TagType,
    val usableRecipeBytes: Int,
    val maxRecipeSteps: Int,
    val writeSupported: Boolean,
    val explanation: String
)

/**
 * Resolve how many bytes are usable for the logical recipe payload on a given tag.
 *
 * - Ultralight/NTAG: use NTAG213 user memory constant (pages 8..39 → 144 bytes).
 * - Classic: use the bytes actually exposed by [TagMetadata.memorySizeBytes], which is
 *   populated from [RawTagDump.bytes] built via the Classic logical block mapping
 *   (data blocks only, sector trailers and manufacturer blocks excluded).
 * - Unknown: no usable bytes, write disabled.
 */
fun resolveTagCapacity(metadata: TagMetadata): TagCapacityInfo {
    val (usableBytes, writeSupported, explanation) = when (metadata.tagType) {
        TagType.ULTRALIGHT_NTAG -> {
            Triple(
                NTAG213_USER_MEMORY,
                true,
                "Ultralight/NTAG: assuming NTAG213 user memory $NTAG213_USER_MEMORY bytes " +
                    "(pages 8..39 used for recipe region; config/lock pages excluded)."
            )
        }
        TagType.CLASSIC -> {
            val classicBytes = metadata.memorySizeBytes.coerceAtLeast(0)
            Triple(
                classicBytes,
                true,
                "Mifare Classic: using $classicBytes bytes from logical data blocks " +
                    "(physical block 2 onwards, sector trailers and reserved blocks excluded)."
            )
        }
        TagType.UNKNOWN -> {
            Triple(
                0,
                false,
                "Unsupported or unknown tag type (UID length/tech list did not match Ultralight/NTAG or Classic)."
            )
        }
    }

    val maxSteps = if (usableBytes > HEADER_SIZE) {
        (usableBytes - HEADER_SIZE) / STEP_SIZE
    } else {
        0
    }

    return TagCapacityInfo(
        tagType = metadata.tagType,
        usableRecipeBytes = usableBytes,
        maxRecipeSteps = maxSteps.coerceAtLeast(0),
        writeSupported = writeSupported,
        explanation = explanation
    )
}

