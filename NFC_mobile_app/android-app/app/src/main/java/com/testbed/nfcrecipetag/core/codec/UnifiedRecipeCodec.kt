package com.testbed.nfcrecipetag.core.codec

import com.testbed.nfcrecipetag.core.tagmodel.DecodedRecipe
import com.testbed.nfcrecipetag.core.tagmodel.RawTagDump
import com.testbed.nfcrecipetag.core.tagmodel.TagType
import com.testbed.nfcrecipetag.core.tagmodel.UnifiedRecipe

/**
 * Adapters between physical tag dumps and the unified recipe model.
 *
 * The underlying logical layout is always the same (TRecipeInfo + TRecipeStep[]),
 * only the mapping from NFC memory to the linear byte stream differs per tag type.
 * NfcTagReader already exposes that stream as RawTagDump.bytes, so these
 * helpers simply validate the tag type and decode using the existing codec.
 */

fun decodeUltralightToUnifiedRecipe(dump: RawTagDump): UnifiedRecipe? {
    if (dump.metadata.tagType != TagType.ULTRALIGHT_NTAG) return null
    val decoded: DecodedRecipe = decodeRecipe(dump.bytes) ?: return null
    return UnifiedRecipe(decoded)
}

fun decodeClassicToUnifiedRecipe(dump: RawTagDump): UnifiedRecipe? {
    if (dump.metadata.tagType != TagType.CLASSIC) return null
    val decoded: DecodedRecipe = decodeRecipe(dump.bytes) ?: return null
    return UnifiedRecipe(decoded)
}

/**
 * Encode a unified recipe back to the canonical firmware byte stream
 * (TRecipeInfo followed by TRecipeStep[]), including recomputed RightNumber
 * and CheckSum. Unknown tail bytes from the original dump are preserved.
 */
fun encodeUnifiedRecipeToCanonicalBytes(recipe: UnifiedRecipe): ByteArray {
    return encodeRecipe(recipe.header, recipe.steps, recipe.unknownTail)
}

