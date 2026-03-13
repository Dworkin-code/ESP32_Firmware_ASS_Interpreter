package com.testbed.nfcrecipetag.core.tagmodel

import java.io.Serializable

/**
 * Raw dump of tag user/data region as a linear byte array.
 * For Ultralight: bytes from page 8 onward (recipe region).
 * For Classic: bytes from logical data blocks (physical block 2, 3, 5, 6, ...).
 * See tag_format_spec.md.
 */
data class RawTagDump(
    val metadata: TagMetadata,
    val bytes: ByteArray
) : Serializable {
    override fun equals(other: Any?) =
        other is RawTagDump && metadata == other.metadata && bytes.contentEquals(other.bytes)
    override fun hashCode() = 31 * metadata.hashCode() + bytes.contentHashCode()
}
