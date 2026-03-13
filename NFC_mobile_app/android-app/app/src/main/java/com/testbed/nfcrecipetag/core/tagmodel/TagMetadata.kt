package com.testbed.nfcrecipetag.core.tagmodel

import java.io.Serializable

/**
 * Metadata read from an NFC tag (UID, type, tech list, memory size).
 * See tag_format_spec.md for tag type detection rules.
 */
data class TagMetadata(
    val uid: ByteArray,
    val uidHex: String,
    val tagType: TagType,
    val memorySizeBytes: Int,
    val techList: List<String>,
    val atqa: Short = 0,
    val sak: Short = 0
) : Serializable {
    override fun equals(other: Any?) = other is TagMetadata && uid.contentEquals(other.uid)
    override fun hashCode() = uid.contentHashCode()
}

enum class TagType {
    ULTRALIGHT_NTAG,  // 7-byte UID
    CLASSIC,          // 4-byte UID
    UNKNOWN
}
